/*
 * P-LAB TMS9918 (Apple-1) — cc65 C program for POM1 CodeTank ($4000, 4000R)
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: dev/lib/tms9918c — cc65 port of Antonino "Nino" Porcino's
 *   apple1-videocard-lib (https://github.com/nippur72/apple1-videocard-lib).
 *
 * HELLO WORLD — minimal Screen 1 text demo.
 * DevBench target: P-LAB TMS9918 CodeTank ROM (C), entry $4000.
 */

#include "tms9918.h"
#include "screen1.h"

void main(void) {
    tms_init_regs(SCREEN1_TABLE);
    tms_set_color(COLOR_CYAN);
    screen1_prepare();
    screen1_load_font();
    screen1_puts((const unsigned char *)"HELLO WORLD (C / TMS9918)\nPOM1 Bench");

    for (;;) {
        /* idle */
    }
}

