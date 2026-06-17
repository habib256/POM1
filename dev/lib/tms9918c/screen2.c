/*
 * apple1-videocard-lib — POM1 CodeTank cc65 port
 * Derived from nippur72/apple1-videocard-lib (Antonino "Nino" Porcino).
 *   https://github.com/nippur72/apple1-videocard-lib
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 */
#include "screen2.h"
#include "c64font.h"
#include "sprites.h"
#include "gfx.h"   /* card-neutral geometry (dev/lib/gfx); link gfx-tms.lib */

const unsigned char SCREEN2_TABLE[8] = {
    0x02U, 0xC0U, 0x0EU, 0xFFU, 0x03U, 0x76U, 0x03U, 0x25U
};

unsigned char tms_global_mulf_initialized = 0;
unsigned char screen2_plot_mode = PLOT_MODE_SET;

static const unsigned char pow2_table_reversed[8] = {
    128U, 64U, 32U, 16U, 8U, 4U, 2U, 1U
};

void screen2_init_bitmap(unsigned char color) {
    unsigned i;

    /* Disable sprites (avoid linking sprites.c when not needed). */
    tms_set_vram_write_addr(TMS_SPRITE_ATTRS);
    TMS_WRITE_DATA_PORT(SPRITE_OFF_MARKER);

    tms_set_vram_write_addr(TMS_PATTERN_TABLE);
    for (i = 768U * 8U; i != 0U; --i) {
        TMS_WRITE_DATA_PORT(0);
        TMS_IO_DELAY();
    }

    tms_set_vram_write_addr(TMS_COLOR_TABLE);
    for (i = 768U * 8U; i != 0U; --i) {
        TMS_WRITE_DATA_PORT(color);
        TMS_IO_DELAY();
    }

    tms_set_vram_write_addr(TMS_NAME_TABLE);
    for (i = 0U; i < SCREEN2_SIZE; ++i) {
        TMS_WRITE_DATA_PORT((unsigned char)(i & 0xFFU));
        TMS_IO_DELAY();
    }
}

void screen2_putc(unsigned char ch, unsigned char x, unsigned char y, unsigned char col) {
    unsigned addr;
    unsigned char i;

    if (ch < 32U) {
        return;
    }
    addr = (unsigned)x * 8U + (unsigned)y * 256U;
    tms_set_vram_write_addr(TMS_PATTERN_TABLE + addr);
    {
        const unsigned char *glyph = FONT + (unsigned)(ch - 32U) * 8U;
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

void screen2_plot(unsigned char x, unsigned char y) {
    unsigned paddr = TMS_PATTERN_TABLE
        + (unsigned)(x & 0xF8U)
        + (unsigned)(y & 0xF8U) * 32U
        + (unsigned)(y & 7U);
    unsigned char data;
    unsigned char mask = pow2_table_reversed[x & 7U];

    tms_set_vram_read_addr(paddr);
    data = TMS_READ_DATA_PORT;
    tms_set_vram_write_addr(paddr);
    switch (screen2_plot_mode) {
        case PLOT_MODE_RESET:
            data = (unsigned char)(data & (unsigned char)~mask);
            break;
        case PLOT_MODE_SET:
            data = (unsigned char)(data | mask);
            break;
        default:
            data = (unsigned char)(data ^ mask);
            break;
    }
    TMS_WRITE_DATA_PORT(data);
}

unsigned char screen2_point(unsigned char x, unsigned char y) {
    unsigned paddr = TMS_PATTERN_TABLE
        + (unsigned)(x & 0xF8U)
        + (unsigned)(y & 0xF8U) * 32U
        + (unsigned)(y & 7U);
    unsigned char data;
    unsigned char mask = pow2_table_reversed[x & 7U];
    tms_set_vram_read_addr(paddr);
    data = TMS_READ_DATA_PORT;
    return (unsigned char)((data & mask) != 0 ? 1 : 0);
}

/* Vector primitives now forward to the card-neutral gfx layer (dev/lib/gfx,
 * factoring axis 1). gfx_plot resolves (via the TMS backend, gfx_backend_tms.c)
 * straight back to screen2_plot, so the current plot mode (SET/RESET/INVERT) is
 * honoured. The ellipse's cos/sin tables + math_abs that lived here are now in
 * gfx_draw.c — removed from this file (the dedup). The public names below stay
 * as thin wrappers so existing demos compile unchanged. Link gfx-tms.lib. */
void screen2_line(unsigned char x0, unsigned char y0, unsigned char x1, unsigned char y1) {
    gfx_line(x0, y0, x1, y1);
}

void screen2_circle(unsigned char xm, unsigned char ym, unsigned char r) {
    gfx_circle(xm, ym, r);
}

void screen2_ellipse_rect(unsigned char x0, unsigned char y0, unsigned char x1, unsigned char y1) {
    gfx_ellipse(x0, y0, x1, y1);
}

/* Rectangle OUTLINE through opposite corners — capability gained from the shared
 * layer (screen2 previously had only the slow screen2_filled_rect). */
void screen2_rect(unsigned char x0, unsigned char y0, unsigned char x1, unsigned char y1) {
    gfx_rect(x0, y0, x1, y1);
}
