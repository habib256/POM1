/*
 * apple1-videocard-lib — POM1 CodeTank cc65 port
 * Derived from nippur72/apple1-videocard-lib (Antonino "Nino" Porcino).
 *   https://github.com/nippur72/apple1-videocard-lib
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 *
 * screen_ext.c — additions to screen1 / screen2 declared in their headers but
 * kept here so existing demos that only link screen1.c / screen2.c don't drag
 * in tms_fast.s symbols they never use. Opt in by adding screen_ext.c +
 * tms_fast.s to your Makefile SOURCES.
 */
#include "screen1.h"
#include "screen2.h"
#include "tms9918.h"

/* From tms_fast.s. */
extern void tms_fill_vram(unsigned addr, unsigned char val, unsigned count);

/* ------------------------------------------------------------------ */
/* screen1                                                            */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* screen2                                                            */
/* ------------------------------------------------------------------ */

void screen2_clear(void) {
    /* Pattern table covers the full 256x192 bitmap (3 banks of 2048 bytes). */
    tms_fill_vram(TMS_PATTERN_TABLE, 0x00U, 6144U);
}

void screen2_filled_rect(unsigned char x0, unsigned char y0,
                         unsigned char x1, unsigned char y1) {
    unsigned char y;
    unsigned char yhi = (unsigned char)(y0 < y1 ? y1 : y0);
    unsigned char ylo = (unsigned char)(y0 < y1 ? y0 : y1);
    /* Scanline loop. screen2_line obeys current screen2_plot_mode. */
    for (y = ylo; ; ++y) {
        screen2_line(x0, y, x1, y);
        if (y == yhi) {
            break;
        }
    }
}
