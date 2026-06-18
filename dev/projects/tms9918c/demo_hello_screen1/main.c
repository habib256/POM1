/*
 * hello_screen1 — minimal CodeTank + TMS9918 demo (preset 9).
 * Build: make   → software/Apple-1_TMS_CC65/hello_screen1.{bin,txt}
 * Run:   Wozmon 4000R
 */
#include "tms9918.h"
#include "screen1.h"

void main(void) {
    tms_init_regs(SCREEN1_TABLE);
    tms_set_color(COLOR_CYAN);
    screen1_prepare();
    screen1_load_font();
    screen1_puts((const unsigned char *)"apple1-videocard-lib (cc65)\nCodeTank+TMS");
    for (;;) {
        /* idle */
    }
}
