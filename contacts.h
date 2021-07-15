#pragma once

#include <stdint.h>

#define ID_SIZE 8
#define MAX_CONTACTS 2

class Contacts {
public:
	static void onRange(uint8_t id[ID_SIZE], float *range);
	static void getContacts(uint8_t IDs[MAX_CONTACTS][ID_SIZE], unsigned *numContacts);
};

extern Contacts contacts;
