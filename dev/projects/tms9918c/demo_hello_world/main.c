/*
 * hello_world — port minimal de demos/hello-world (upstream).
 * Wozmon uniquement (pas de TMS). CodeTank @ $4000, Wozmon 4000R.
 */
#include "apple1.h"

void main(void) {
    woz_puts((const unsigned char *)"\r\rHELLO WORLD (cc65)\r\r");
    woz_mon();
}
