/* HELLO WORLD in C for the P-LAB TMS9918.
   DevBench target: P-LAB TMS9918 CodeTank ROM (C), entry $4000. */

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

