#include "usb.h"
#include "pcb.h"

USBClass usb;

#define BUFFER_SIZE 64
#define TIMEOUT 200
#define SYNC_FRAME_SIZE 8

// Incoming message types
#define MSG_REQUEST 0xAA

// Outgoing message types
#define MSG_NUM_BLOCKS 0x11
#define MSG_CONTACT_DATA 0x22

#define NUM_IDS 16
#define ID_SIZE 8
#define BLOCK_SIZE (1 + 4 + ID_SIZE * NUM_IDS)

#define REQUEST_TYPE_CONTACT_DATA 0x1

void (*getContacts)(uint8_t[MAX_CONTACTS][ID_SIZE], unsigned*) = nullptr;
uint8_t IDs[MAX_CONTACTS][ID_SIZE];
unsigned numContacts = 0;

uint8_t syncFrame[SYNC_FRAME_SIZE] = {97, 98, 99, 100, 101, 102, 103, 104};

uint8_t buffer[BUFFER_SIZE];
uint8_t pos = 0;

uint32_t lastMsgTime = 0;
uint8_t *USBID = nullptr;

void USBClass::setup(uint8_t ID[ID_SIZE], void (*getContacts_)(uint8_t[MAX_CONTACTS][ID_SIZE], unsigned*)) {
  USBID = ID;
  getContacts = getContacts_;
}

void send(uint8_t msg[], unsigned len) {
  uint8_t checksum = 0;
  for (unsigned i = 0; i < len; i++)
    checksum += msg[i];

  Srl.write(msg, len);
  Srl.write(checksum);
  Srl.write(syncFrame, SYNC_FRAME_SIZE);
}

unsigned getNumBlocks() {
  return (numContacts + NUM_IDS - 1) / NUM_IDS;
}

void sendNumBlocksBlock() {
  uint8_t data[BLOCK_SIZE];
  uint32_t numBlocks = getNumBlocks();
  data[0] = MSG_NUM_BLOCKS;
  memcpy(&data[1], &numBlocks, 4);
  memcpy(&data[1 + 4], USBID, ID_SIZE);
  send(data, BLOCK_SIZE);
}

void sendBlock(uint32_t i) {
  uint8_t data[BLOCK_SIZE];

  data[0] = MSG_CONTACT_DATA;
  memcpy(&data[1], &i, 4);

  for (unsigned j = 0; j < NUM_IDS; j++) {
    unsigned contactNum = (i - 1) * NUM_IDS + j;
    if (contactNum >= numContacts) {
      memset(&data[1 + 4 + j * ID_SIZE], 0, ID_SIZE);
    } else {
      memcpy(&data[1 + 4 + j * ID_SIZE], IDs[contactNum], ID_SIZE);
    }
  }

  send(data, BLOCK_SIZE);
}

void sendContactData(uint32_t requestedBlocks[], unsigned len) {
  if (len == 0)
    return;

  getContacts(IDs, &numContacts);

  uint32_t firstBlock;
  memcpy(&firstBlock, &requestedBlocks[0], 4);
  if (len == 1 && firstBlock == 0) {
    unsigned numBlocks = getNumBlocks();
    sendNumBlocksBlock();
    for (unsigned i = 1; i <= numBlocks; i++) {
      sendBlock(i);
    }
  } else {
    for (unsigned i = 0; i < len; i++) {
      uint32_t curBlock;
      memcpy(&curBlock, &requestedBlocks[i], 4);
      sendBlock(curBlock);
    }
  }
}

void parse(uint32_t len) {
  // Verify checksum
  uint8_t computedChecksum = 0;
  for (unsigned i = 0; i < len - 1; i++)
    computedChecksum += buffer[i];
  if (buffer[len - 1] != computedChecksum)
    return;

  uint8_t msgType = buffer[0];
  if (msgType != MSG_REQUEST)
    return;

  uint8_t requestType = buffer[1];
  if (requestType != REQUEST_TYPE_CONTACT_DATA)
    return;

  sendContactData((uint32_t*)(buffer + 2), (len - 3) / 4);
}

int findSyncFrame() {
  for (int i = 0; i <= pos - SYNC_FRAME_SIZE; i++) {
    bool matched = true;
    for (int j = 0; j < SYNC_FRAME_SIZE; j++) {
      if (buffer[i + j] != syncFrame[j]) {
        matched = false;
        break;
      }
    }
    if (matched)
      return i;
  }
  return -1;
}

void USBClass::loop() {
  uint32_t curTime = millis();
  if (curTime - lastMsgTime > TIMEOUT) {
    lastMsgTime = curTime;
    pos = 0;
  }

  if (Srl.available()) {
    buffer[pos++] = Srl.read();

    int syncFrameStart = findSyncFrame();
    if (syncFrameStart >= 0) {
      parse(syncFrameStart);
    }
  }
}
