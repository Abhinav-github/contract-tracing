#include "contacts.h"
#include "pcb.h"

#include <SPI.h>

#define REPETITION 3
#define CONTACT_DISTANCE 10.0
#define RESET_TIME 500

#define FEET_PER_METER 3.28084

// Values that go in lastResults
#define UNKNOWN 0
#define NO_CONTACT 1
#define CONTACT 2

struct Contact {
    uint32_t lastActive = 0;
    uint8_t lastResults[REPETITION];
    uint8_t confirmed = 0;
    uint8_t id[ID_SIZE];
};

Contact contactsList[MAX_CONTACTS];

bool slotOpen(unsigned i, uint32_t curTime) {
   return curTime - contactsList[i].lastActive > RESET_TIME && !contactsList[i].confirmed;
}

void Contacts::onRange(uint8_t id[ID_SIZE], float *range) {
    // Srl.print("ID: "); Srl.print(id[0]); Srl.print(" Range: "); Srl.println(*range);
	float rangeFeet = ((*range * FEET_PER_METER) - 0.295) / 1.11;

    uint32_t curTime = millis();
    int openSlot = -1;
    int matchingSlot = -1;
    for (unsigned i = 0; i < MAX_CONTACTS; i++) {
        if (slotOpen(i, curTime)) {
            openSlot = i;
        } else if (memcmp(contactsList[i].id, id, ID_SIZE) == 0) {
            matchingSlot = i;
            break;
        }
    }

    if (openSlot < 0 && matchingSlot < 0)
        return;

    unsigned idx;
    if (matchingSlot >= 0) {
        idx = matchingSlot;
    } else {
        idx = openSlot;
        for (unsigned j = 0; j < REPETITION; j++)
            contactsList[idx].lastResults[j] = UNKNOWN;
    }

    uint8_t result = (rangeFeet < CONTACT_DISTANCE) ? CONTACT : NO_CONTACT;

    // Update the last active time
    contactsList[idx].lastActive = curTime;
    for (unsigned i = REPETITION - 1; i > 0; i--) {
        contactsList[idx].lastResults[i] = contactsList[idx].lastResults[i - 1];
    }
    contactsList[idx].lastResults[0] = result;

    // Copy in the ID of this range (which has no effect if it was a matching slot)
    for (unsigned i = 0; i < ID_SIZE; i++) {
        contactsList[idx].id[i] = id[i];
    }

    // Check if the contact is now confirmed
    contactsList[idx].confirmed = 1;
    for (unsigned i = 0; i < REPETITION; i++) {
        if (contactsList[idx].lastResults[i] != CONTACT) {
            contactsList[idx].confirmed = 0;
        }
    }

    if (contactsList[idx].confirmed) {
        // Srl.print("Confirmed contact with "); Srl.println(contactsList[idx].id[0]);
    }
}

void Contacts::getContacts(uint8_t IDs[MAX_CONTACTS][ID_SIZE], unsigned *numContacts) {
	unsigned cur = 0;
	for (unsigned i = 0; i < MAX_CONTACTS; i++) {
		if (contactsList[i].confirmed) {
			memcpy(IDs[cur], contactsList[i].id, ID_SIZE);
			cur++;
		}
	}

	*numContacts = cur;
}
