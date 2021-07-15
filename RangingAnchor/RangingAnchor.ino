#include "contacts.h"
#include "anchor.h"
#include "pcb.h"
#include "usb.h"

uint8_t ID[ID_SIZE] = {1, 2, 3, 4, 5, 6, 7, 8};

void setup() {
    Srl.begin(115200);
    anchor.setup(ID);
    anchor.attachOnRange(contacts.onRange);
    usb.setup(ID, contacts.getContacts);
}

void loop() {
    anchor.loop();
    anchor.checkRange();

    usb.loop();
}
