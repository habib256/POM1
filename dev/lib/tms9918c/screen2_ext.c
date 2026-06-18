/*
 * apple1-videocard-lib — POM1 CodeTank cc65 port
 * Derived from nippur72/apple1-videocard-lib (Antonino "Nino" Porcino).
 *   https://github.com/nippur72/apple1-videocard-lib
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 *
 * screen2_ext.c — screen-2 (bitmap) extensions: VRAM clear + byte-aligned
 * filled rectangle (the analogue of GEN2's fill_pixrect). Split out of
 * screen_ext.c so a text-only program never drags the bitmap helpers in.
 */
#include "screen2.h"
#include "tms9918.h"

extern void tms_fill_vram(unsigned addr, unsigned char val, unsigned count);

void screen2_clear(void) {
    /* Pattern table covers the full 256x192 bitmap (3 banks of 2048 bytes). */
    tms_fill_vram(TMS_PATTERN_TABLE, 0x00U, 6144U);
}

/* Read-modify-write one VRAM byte, changing only the bits in `mask` per the
 * current screen2_plot_mode (SET → |mask, RESET → &~mask, INVERT → ^mask). */
static void s2_rmw(unsigned addr, unsigned char mask) {
    unsigned char d;
    tms_set_vram_read_addr(addr);
    d = TMS_READ_DATA_PORT;
    switch (screen2_plot_mode) {
        case PLOT_MODE_RESET:  d = (unsigned char)(d & (unsigned char)~mask); break;
        case PLOT_MODE_INVERT: d = (unsigned char)(d ^ mask); break;
        default:               d = (unsigned char)(d | mask); break;   /* SET */
    }
    tms_set_vram_write_addr(addr);
    TMS_WRITE_DATA_PORT(d);
}

/* Write a FULL byte (all 8 px). SET/RESET need NO read — that's the speedup
 * over a per-pixel plot loop; INVERT still has to read (XOR). */
static void s2_full_byte(unsigned addr) {
    if (screen2_plot_mode == PLOT_MODE_INVERT) {
        s2_rmw(addr, 0xFFU);
    } else {
        tms_set_vram_write_addr(addr);
        TMS_WRITE_DATA_PORT(screen2_plot_mode == PLOT_MODE_RESET ? 0x00U : 0xFFU);
    }
}

/* Fast filled rectangle for the TMS9918 bitmap — the byte-aligned analogue of
 * GEN2's fill_pixrect. Each scanline is a LEFT partial byte / run of FULL bytes
 * / RIGHT partial byte, so the interior costs one VRAM write per 8 px instead of
 * the old per-pixel read-modify-write (a scanline loop of screen2_line). Same
 * pixels, same plot_mode semantics — just ~8-14x fewer VRAM port operations.
 *
 * Pattern-table byte for (x,y): (y&0xF8)*32 + (x&0xF8) + (y&7), MSB = leftmost
 * pixel (matches screen2_plot). cell column = x>>3, byte addr = base + cx*8. */
void screen2_filled_rect(unsigned char x0, unsigned char y0,
                         unsigned char x1, unsigned char y1) {
    unsigned char y, cx, cx0, cx1, b0, b1;
    unsigned base;

    if (x1 < x0) { unsigned char t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { unsigned char t = y0; y0 = y1; y1 = t; }

    cx0 = (unsigned char)(x0 >> 3);  b0 = (unsigned char)(x0 & 7U);
    cx1 = (unsigned char)(x1 >> 3);  b1 = (unsigned char)(x1 & 7U);

    for (y = y0; ; ++y) {
        base = (unsigned)(y & 0xF8U) * 32U + (unsigned)(y & 7U);
        if (cx0 == cx1) {
            /* Whole span in one byte: columns b0..b1. */
            s2_rmw(base + (unsigned)cx0 * 8U,
                   (unsigned char)((0xFFU >> b0) & (0xFFU << (7U - b1))));
        } else {
            s2_rmw(base + (unsigned)cx0 * 8U, (unsigned char)(0xFFU >> b0)); /* cols b0..7 */
            for (cx = (unsigned char)(cx0 + 1U); cx < cx1; ++cx)
                s2_full_byte(base + (unsigned)cx * 8U);                      /* cols 0..7 */
            s2_rmw(base + (unsigned)cx1 * 8U,
                   (unsigned char)((0xFFU << (7U - b1)) & 0xFFU));           /* cols 0..b1 */
        }
        if (y == y1) break;
    }
}
