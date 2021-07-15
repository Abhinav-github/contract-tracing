#pragma once

#include "contacts.h"

#include <SPI.h>
#include <DW1000.h>

class Anchor {
public:
	void setup(uint8_t ID[ID_SIZE]);
	void loop();
	void checkRange() {
		if (rangeAvailable) {
			rangeAvailable = 0;
			onRange(lastID, &lastRange);
		}
	}

	void attachOnRange(void (*onRange_)(uint8_t id[ID_SIZE], float *range)) {
		onRange = onRange_;
	}

private:
	static void (*onRange)(uint8_t id[ID_SIZE], float *range);

	static uint8_t rangeAvailable;
	static uint8_t lastID[ID_SIZE];
	static float lastRange;
};

extern Anchor anchor;
