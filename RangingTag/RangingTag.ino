#include "contacts.h"
#include "tag.h"
#include "pcb.h"
#include "usb.h"

uint8_t ID[ID_SIZE] = {3, 2, 3, 4, 5, 6, 7, 8};

void setup() {
    Srl.begin(115200);
    tag.setup(ID);
    tag.attachOnRange(contacts.onRange);
    usb.setup(ID, contacts.getContacts);
}

void loop() {
    tag.loop();
    tag.checkRange();

    usb.loop();
}
