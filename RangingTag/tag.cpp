#include "tag.h"
#include "pcb.h"

Tag tag;

void (*Tag::onRange)(uint8_t id[ID_SIZE], float *range);
uint8_t Tag::rangeAvailable = 0;
uint8_t Tag::lastID[ID_SIZE];
float Tag::lastRange;

// connection pins
#ifdef PCB
    #define PIN_RST 2 // reset pin
    #define PIN_IRQ 3 // irq pin
    #define PIN_SS 4 // spi select pin
#else
    #define PIN_RST 9 // reset pin
    #define PIN_IRQ 2 // irq pin
    #define PIN_SS SS // spi select pin
#endif

// messages used in the ranging protocol
// TODO replace by enum
#define POLL 0
#define POLL_ACK 1
#define RANGE 2
#define RANGE_REPORT 3
#define RANGE_FAILED 255

#define RESET_PERIOD 500
// reply times (same on both sides for symm. ranging)
#define REPLY_DELAY_TIME_US 3000

// message flow state
volatile byte expectedMsgId = POLL_ACK;
// message sent/received state
volatile boolean sentAck = false;
volatile boolean receivedAck = false;
// timestamps to remember
DW1000Time timePollSent;
DW1000Time timePollAckReceived;
DW1000Time timeRangeSent;
// data buffer
#define LEN_DATA 24
byte data[LEN_DATA];
// watchdog
uint32_t lastActivity;

uint8_t *TagID = nullptr;

void noteActivity();
void resetInactive();
void handleSent();
void handleReceived();
void transmitPoll();
void transmitRange();
void receiver();

void Tag::setup(uint8_t ID[ID_SIZE]) {
    TagID = ID;

    // initialize the driver
    DW1000.begin(PIN_IRQ, PIN_RST);
    DW1000.select(PIN_SS);
    // Srl.println("DW1000 initialized ...");
    // general configuration
    DW1000.newConfiguration();
    DW1000.setDefaults();
    DW1000.setDeviceAddress(2);
    DW1000.setNetworkId(10);
    DW1000.enableMode(DW1000.MODE_LONGDATA_RANGE_LOWPOWER);
    DW1000.commitConfiguration();
    // Srl.println(F("Committed configuration ..."));
    // DEBUG chip info and registers pretty printed
    char msg[128];
    DW1000.getPrintableDeviceIdentifier(msg);
    // Srl.print("Device ID: "); Srl.println(msg);
    DW1000.getPrintableExtendedUniqueIdentifier(msg);
    // Srl.print("Unique ID: "); Srl.println(msg);
    DW1000.getPrintableNetworkIdAndShortAddress(msg);
    // Srl.print("Network ID & Device Address: "); Srl.println(msg);
    DW1000.getPrintableDeviceMode(msg);
    // Srl.print("Device mode: "); Srl.println(msg);
    // attach callback for (successfully) sent and received messages
    DW1000.attachSentHandler(handleSent);
    DW1000.attachReceivedHandler(handleReceived);
    // anchor starts by transmitting a POLL message
    receiver();
    transmitPoll();
    noteActivity();
}

void noteActivity() {
    // update activity timestamp, so that we do not reach "RESET_PERIOD"
    lastActivity = millis();
}

void resetInactive() {
    // tag sends POLL and listens for POLL_ACK
    expectedMsgId = POLL_ACK;
    transmitPoll();
    noteActivity();
}

void handleSent() {
    // status change on sent success
    sentAck = true;
}

void handleReceived() {
    // status change on received success
    receivedAck = true;
}

void transmitPoll() {
    DW1000.newTransmit();
    DW1000.setDefaults();
    data[0] = POLL;
    DW1000.setData(data, LEN_DATA);
    DW1000.startTransmit();
}

void transmitRange() {
    DW1000.newTransmit();
    DW1000.setDefaults();
    data[0] = RANGE;
    // delay sending the message and remember expected future sent timestamp
    DW1000Time deltaTime = DW1000Time(REPLY_DELAY_TIME_US, DW1000Time::MICROSECONDS);
    timeRangeSent = DW1000.setDelay(deltaTime);
    timePollSent.getTimestamp(data + 1);
    timePollAckReceived.getTimestamp(data + 6);
    timeRangeSent.getTimestamp(data + 11);
    memcpy(data + 16, TagID, ID_SIZE);
    DW1000.setData(data, LEN_DATA);
    DW1000.startTransmit();
}

void receiver() {
    DW1000.newReceive();
    DW1000.setDefaults();
    // so we don't need to restart the receiver manually
    DW1000.receivePermanently(true);
    DW1000.startReceive();
}

void Tag::loop() {
    if (!sentAck && !receivedAck) {
        // check if inactive
        if (millis() - lastActivity > RESET_PERIOD) {
            resetInactive();
        }
        return;
    }
    // continue on any success confirmation
    if (sentAck) {
        sentAck = false;
        byte msgId = data[0];
        if (msgId == POLL) {
            DW1000.getTransmitTimestamp(timePollSent);
            //Srl.print("Sent POLL @ "); Srl.println(timePollSent.getAsFloat());
        } else if (msgId == RANGE) {
            DW1000.getTransmitTimestamp(timeRangeSent);
            noteActivity();
        }
    }
    if (receivedAck) {
        receivedAck = false;
        // get message and parse
        DW1000.getData(data, LEN_DATA);
        byte msgId = data[0];
        if (msgId != expectedMsgId) {
            // unexpected message, start over again
            expectedMsgId = POLL_ACK;
            transmitPoll();
            return;
        }
        if (msgId == POLL_ACK) {
            DW1000.getReceiveTimestamp(timePollAckReceived);
            expectedMsgId = RANGE_REPORT;
            transmitRange();
            noteActivity();
        } else if (msgId == RANGE_REPORT) {
            expectedMsgId = POLL_ACK;
            rangeAvailable = 1;
            memcpy(lastID, data + 16, ID_SIZE);
            memcpy(&lastRange, data + 1, 4);
            transmitPoll();
            noteActivity();
        } else if (msgId == RANGE_FAILED) {
            expectedMsgId = POLL_ACK;
            transmitPoll();
            noteActivity();
        }
    }
}
