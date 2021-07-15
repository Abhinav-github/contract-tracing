#include "anchor.h"
#include "pcb.h"

Anchor anchor;

void (*Anchor::onRange)(uint8_t id[ID_SIZE], float *range);
uint8_t Anchor::rangeAvailable = 0;
uint8_t Anchor::lastID[ID_SIZE];
float Anchor::lastRange;

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

// message flow state
volatile byte expectedMsgId = POLL;
// message sent/received state
volatile boolean sentAck = false;
volatile boolean receivedAck = false;
// protocol error state
boolean protocolFailed = false;
// timestamps to remember
DW1000Time timePollSent;
DW1000Time timePollReceived;
DW1000Time timePollAckSent;
DW1000Time timePollAckReceived;
DW1000Time timeRangeSent;
DW1000Time timeRangeReceived;
// last computed range/time
DW1000Time timeComputedRange;
// data buffer
#define LEN_DATA 24
byte data[LEN_DATA];
// watchdog and reset period
uint32_t lastActivity;
uint32_t resetPeriod = 250;
// reply times (same on both sides for symm. ranging)
uint16_t replyDelayTimeUS = 3000;

uint8_t *AnchorID = nullptr;

void noteActivity();
void resetInactive();
void handleSent();
void handleReceived();
void transmitPollAck();
void transmitRangeReport(float curRange);
void transmitRangeFailed();
void receiver();
void computeRangeAsymmetric();

void Anchor::setup(uint8_t ID[ID_SIZE]) {
    AnchorID = ID;

    // initialize the driver
    DW1000.begin(PIN_IRQ, PIN_RST);
    DW1000.select(PIN_SS);
    // Srl.println(F("DW1000 initialized ..."));
    // general configuration
    DW1000.newConfiguration();
    DW1000.setDefaults();
    DW1000.setDeviceAddress(1);
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
    // anchor starts in receiving mode, awaiting a ranging poll message
    receiver();
    noteActivity();
}

void noteActivity() {
    // update activity timestamp, so that we do not reach "resetPeriod"
    lastActivity = millis();
}

void resetInactive() {
    // anchor listens for POLL
    expectedMsgId = POLL;
    receiver();
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

void transmitPollAck() {
    DW1000.newTransmit();
    DW1000.setDefaults();
    data[0] = POLL_ACK;
    // delay the same amount as ranging tag
    DW1000Time deltaTime = DW1000Time(replyDelayTimeUS, DW1000Time::MICROSECONDS);
    DW1000.setDelay(deltaTime);
    DW1000.setData(data, LEN_DATA);
    DW1000.startTransmit();
}

void transmitRangeReport(float curRange) {
    DW1000.newTransmit();
    DW1000.setDefaults();
    data[0] = RANGE_REPORT;
    // write final ranging result
    memcpy(data + 1, &curRange, 4);
    memcpy(data + 16, AnchorID, ID_SIZE);
    DW1000.setData(data, LEN_DATA);
    DW1000.startTransmit();
}

void transmitRangeFailed() {
    DW1000.newTransmit();
    DW1000.setDefaults();
    data[0] = RANGE_FAILED;
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

void computeRangeAsymmetric() {
    // asymmetric two-way ranging (more computation intense, less error prone)
    DW1000Time round1 = (timePollAckReceived - timePollSent).wrap();
    DW1000Time reply1 = (timePollAckSent - timePollReceived).wrap();
    DW1000Time round2 = (timeRangeReceived - timePollAckSent).wrap();
    DW1000Time reply2 = (timeRangeSent - timePollAckReceived).wrap();
    DW1000Time tof = (round1 * round2 - reply1 * reply2) / (round1 + round2 + reply1 + reply2);
    // set tof timestamp
    timeComputedRange.setTimestamp(tof);
}

void Anchor::loop() {
    int32_t curMillis = millis();
    if (!sentAck && !receivedAck) {
        // check if inactive
        if (curMillis - lastActivity > resetPeriod) {
            resetInactive();
        }
        return;
    }
    // continue on any success confirmation
    if (sentAck) {
        sentAck = false;
        byte msgId = data[0];
        if (msgId == POLL_ACK) {
            DW1000.getTransmitTimestamp(timePollAckSent);
            noteActivity();
        }
    }
    if (receivedAck) {
        receivedAck = false;
        // get message and parse
        DW1000.getData(data, LEN_DATA);
        byte msgId = data[0];
        if (msgId != expectedMsgId) {
            // unexpected message, start over again (except if already POLL)
            protocolFailed = true;
        }
        if (msgId == POLL) {
            // on POLL we (re-)start, so no protocol failure
            protocolFailed = false;
            DW1000.getReceiveTimestamp(timePollReceived);
            expectedMsgId = RANGE;
            transmitPollAck();
            noteActivity();
        }
        else if (msgId == RANGE) {
            DW1000.getReceiveTimestamp(timeRangeReceived);
            expectedMsgId = POLL;
            if (!protocolFailed) {
                timePollSent.setTimestamp(data + 1);
                timePollAckReceived.setTimestamp(data + 6);
                timeRangeSent.setTimestamp(data + 11);
                // (re-)compute range as two-way ranging is done
                computeRangeAsymmetric(); // CHOSEN RANGING ALGORITHM
                float distance = timeComputedRange.getAsMeters();

                rangeAvailable = 1;
                memcpy(lastID, data + 16, ID_SIZE);
                memcpy(&lastRange, &distance, 4);

                transmitRangeReport(distance);
                // Srl.print("Range: "); Srl.print(distance); Srl.print(" m");
                // Srl.print("\t RX power: "); Srl.print(DW1000.getReceivePower()); Srl.print(" dBm");
            }
            else {
                transmitRangeFailed();
            }

            noteActivity();
        }
    }
}
