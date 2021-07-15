#pragma once

#include "contacts.h"

#include <SPI.h>

class USBClass {
public:
	static void setup(uint8_t ID[ID_SIZE], void (*getContacts)(uint8_t[MAX_CONTACTS][ID_SIZE], unsigned*));
	static void loop();
};

extern USBClass usb;
