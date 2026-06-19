/*
 * P-LAB TMS9918 (Apple-1) — cc65 C runtime, POM1 CodeTank port
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: derived from Antonino "Nino" Porcino's apple1-videocard-lib
 *   (https://github.com/nippur72/apple1-videocard-lib).
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 *
 * screen1_input.c — interactive line input on the screen-1 cursor.
 *
 * Split out of screen1.c so a demo without a text-input prompt doesn't pay
 * the cost of `apple1_getkey` + the input-echo dance. ld65 dead-strips at
 * .o granularity, so leaving `screen1_strinput` here means a program that
 * doesn't call it skips the whole TU.
 */
#include "screen1.h"
#include "apple1.h"

void screen1_strinput(unsigned char *buffer, unsigned char max_length) {
    unsigned char pos = 0;
    screen1_putc(CHR_REVSPACE);
    screen1_putc(CHR_BACKSPACE);
    for (;;) {
        unsigned char key = apple1_getkey();
        if (key == CHR_RETURN) {
            buffer[pos] = 0;
            screen1_putc(CHR_SPACE);
            screen1_putc(CHR_BACKSPACE);
            return;
        } else if (key == CHR_BACKSPACE) {
            if (pos != 0) {
                --pos;
                screen1_putc(CHR_BACKSPACE);
                screen1_putc(CHR_REVSPACE);
                screen1_putc(CHR_SPACE);
                screen1_putc(CHR_BACKSPACE);
                screen1_putc(CHR_BACKSPACE);
            }
        } else if (key >= 32U && key <= 128U) {
            if (pos < max_length) {
                buffer[pos++] = key;
                screen1_putc(key);
                screen1_putc(CHR_REVSPACE);
                screen1_putc(CHR_BACKSPACE);
            }
        }
    }
}
