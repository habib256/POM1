/*
 * P-LAB TMS9918 (Apple-1) — cc65 C runtime, POM1 CodeTank port
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: derived from Antonino "Nino" Porcino's apple1-videocard-lib
 *   (https://github.com/nippur72/apple1-videocard-lib).
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 *
 * screen2_text.c — bitmap-mode 8x8 text glyph blit (putc/puts). Split from
 * screen2.c so a vector-only program (plot + line) doesn't drag in FONT[].
 */
#include "screen2.h"
#include "c64font.h"

void screen2_putc(unsigned char ch, unsigned char x, unsigned char y, unsigned char col) {
    unsigned addr;
    unsigned char i;

    if (ch < 32U) {
        return;
    }
    addr = (unsigned)x * 8U + (unsigned)y * 256U;
    tms_set_vram_write_addr(TMS_PATTERN_TABLE + addr);
    {
        const unsigned char *glyph = tms_c64font + (unsigned)(ch - 32U) * 8U;
        for (i = 0; i < 8U; ++i) {
            TMS_WRITE_DATA_PORT(glyph[i]);
            TMS_IO_DELAY();
        }
    }
    tms_set_vram_write_addr(TMS_COLOR_TABLE + addr);
    for (i = 0; i < 8U; ++i) {
        TMS_WRITE_DATA_PORT(col);
        TMS_IO_DELAY();
    }
}

void screen2_puts(const char *s, unsigned char x, unsigned char y, unsigned char col) {
    unsigned char c;
    unsigned char cx = x;
    while ((c = (unsigned char)*s++) != 0) {
        screen2_putc(c, cx++, y, col);
    }
}
