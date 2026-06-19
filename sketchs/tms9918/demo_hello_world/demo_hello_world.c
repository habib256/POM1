/*
 * P-LAB TMS9918 (Apple-1) — cc65 C program for POM1 CodeTank ($4000, 4000R)
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: dev/lib/tms9918c — cc65 port of Antonino "Nino" Porcino's
 *   apple1-videocard-lib (https://github.com/nippur72/apple1-videocard-lib).
 *
 * hello_world — minimal port of demos/hello-world (upstream).
 * Wozmon only (no TMS). CodeTank @ $4000, Wozmon 4000R.
 */
#include "apple1.h"

void main(void) {
    woz_puts((const unsigned char *)"\r\rHELLO WORLD (cc65)\r\r");
    woz_mon();
}
