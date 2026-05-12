/*
 * apple1-videocard-lib — POM1 CodeTank cc65 port
 * Derived from nippur72/apple1-videocard-lib (Antonino "Nino" Porcino).
 *   https://github.com/nippur72/apple1-videocard-lib
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 */
#include "screen2.h"
#include "c64font.h"
#include "sprites.h"

const unsigned char SCREEN2_TABLE[8] = {
    0x02U, 0xC0U, 0x0EU, 0xFFU, 0x03U, 0x76U, 0x03U, 0x25U
};

unsigned char tms_global_mulf_initialized = 0;
unsigned char screen2_plot_mode = PLOT_MODE_SET;

static const unsigned char pow2_table_reversed[8] = {
    128U, 64U, 32U, 16U, 8U, 4U, 2U, 1U
};

static signed int math_abs(signed int x) {
    return x < 0 ? -x : x;
}

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

void screen2_line(unsigned char _x0, unsigned char _y0, unsigned char _x1, unsigned char _y1) {
    signed int x0 = (signed int)_x0;
    signed int x1 = (signed int)_x1;
    signed int y0 = (signed int)_y0;
    signed int y1 = (signed int)_y1;
    signed int dx = math_abs(x1 - x0);
    signed int dy = -math_abs(y1 - y0);
    signed int err = dx + dy;
    unsigned char ix = (unsigned char)(x0 < x1 ? 1 : 0);
    unsigned char iy = (unsigned char)(y0 < y1 ? 1 : 0);
    signed int e2;

    for (;;) {
        screen2_plot((unsigned char)x0, (unsigned char)y0);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        e2 = err + err;
        if (e2 >= dy) {
            err += dy;
            if (ix) {
                ++x0;
            } else {
                --x0;
            }
        }
        if (e2 <= dx) {
            err += dx;
            if (iy) {
                ++y0;
            } else {
                --y0;
            }
        }
    }
}

void screen2_circle(unsigned char _xm, unsigned char _ym, unsigned char _r) {
    signed int xm = (signed int)_xm;
    signed int ym = (signed int)_ym;
    signed int r = (signed int)_r;
    signed int x = -r;
    signed int y = 0;
    signed int err = 2 - 2 * r;
    signed int r2;

    do {
        screen2_plot((unsigned char)(xm - x), (unsigned char)(ym + y));
        screen2_plot((unsigned char)(xm - y), (unsigned char)(ym - x));
        screen2_plot((unsigned char)(xm + x), (unsigned char)(ym - y));
        screen2_plot((unsigned char)(xm + y), (unsigned char)(ym + x));
        r2 = err;
        if (r2 <= y) {
            err += ++y * 2 + 1;
        }
        if (r2 > x || err > y) {
            err += ++x * 2 + 1;
        }
    } while (x < 0);
}

/* cos/sin * 64 for parametric angle i/64 * 2pi (same scale as upstream ellipse.s). */
static const signed char kEllipseCos64[64] = {
    64, 64, 63, 61, 59, 56, 53, 49, 45, 41, 36, 30, 24, 19, 12, 6,
    0, -6, -12, -19, -24, -30, -36, -41, -45, -49, -53, -56, -59, -61, -63, -64,
    -64, -64, -63, -61, -59, -56, -53, -49, -45, -41, -36, -30, -24, -19, -12, -6,
    0, 6, 12, 19, 24, 30, 36, 41, 45, 49, 53, 56, 59, 61, 63, 64
};
static const signed char kEllipseSin64[64] = {
    0, 6, 12, 19, 24, 30, 36, 41, 45, 49, 53, 56, 59, 61, 63, 64,
    64, 64, 63, 61, 59, 56, 53, 49, 45, 41, 36, 30, 24, 19, 12, 6,
    0, -6, -12, -19, -24, -30, -36, -41, -45, -49, -53, -56, -59, -61, -63, -64,
    -64, -64, -63, -61, -59, -56, -53, -49, -45, -41, -36, -30, -24, -19, -12, -6
};

static unsigned char screen2_clamp_x(signed int v) {
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return (unsigned char)v;
}

static unsigned char screen2_clamp_y(signed int v) {
    if (v < 0) {
        return 0;
    }
    if (v > 191) {
        return 191;
    }
    return (unsigned char)v;
}

void screen2_ellipse_rect(unsigned char x0, unsigned char y0, unsigned char x1, unsigned char y1) {
    signed int xc = (((signed int)x0 + (signed int)x1) >> 1);
    signed int yc = (((signed int)y0 + (signed int)y1) >> 1);
    signed int rx = math_abs((signed int)x1 - (signed int)x0) >> 1;
    signed int ry = math_abs((signed int)y1 - (signed int)y0) >> 1;
    unsigned char i;

    if (rx < 1) {
        rx = 1;
    }
    if (ry < 1) {
        ry = 1;
    }

    for (i = 0; i < 64U; ++i) {
        unsigned char j = (unsigned char)((i + 1U) & 63U);
        signed int x0s = xc + ((signed int)kEllipseCos64[i] * rx) / 64;
        signed int y0s = yc + ((signed int)kEllipseSin64[i] * ry) / 64;
        signed int x1s = xc + ((signed int)kEllipseCos64[j] * rx) / 64;
        signed int y1s = yc + ((signed int)kEllipseSin64[j] * ry) / 64;
        screen2_line(screen2_clamp_x(x0s), screen2_clamp_y(y0s),
                     screen2_clamp_x(x1s), screen2_clamp_y(y1s));
    }
}
