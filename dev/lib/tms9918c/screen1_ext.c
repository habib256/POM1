/*
 * P-LAB TMS9918 (Apple-1) — cc65 C runtime, POM1 CodeTank port
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: derived from Antonino "Nino" Porcino's apple1-videocard-lib
 *   (https://github.com/nippur72/apple1-videocard-lib).
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 *
 * screen1_ext.c — screen-1 (text mode) extensions: random-access character
 * placement + colour-attribute fill. Split out of screen_ext.c so a bitmap-only
 * program never drags the screen-1 helpers in (and vice versa).
 */
#include "screen1.h"
#include "tms9918.h"

extern void tms_fill_vram(unsigned addr, unsigned char val, unsigned count);

void screen1_putcharxy(unsigned char x, unsigned char y, unsigned char c) {
    unsigned addr = TMS_NAME_TABLE + (unsigned)y * 32U + (unsigned)x;
    unsigned char ch = c;
    if (tms_reverse) {
        ch = (unsigned char)(ch | 128U);
    }
    tms_set_vram_write_addr(addr);
    TMS_WRITE_DATA_PORT(ch);
}

void screen1_fill_color_attr(unsigned char fg_bg) {
    /* Screen 1 has one color byte per 8-glyph group → 32 bytes total. */
    tms_fill_vram(TMS_COLOR_TABLE, fg_bg, 32U);
}
