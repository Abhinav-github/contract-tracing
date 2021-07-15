#pragma once

// #define PCB 1
#ifdef PCB
	#define Srl SerialUSB
#else
	#define Srl Serial
#endif