/*
 * P-LAB TMS9918 (Apple-1) — cc65 C runtime, POM1 CodeTank port
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: derived from Antonino "Nino" Porcino's apple1-videocard-lib
 *   (https://github.com/nippur72/apple1-videocard-lib).
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 *
 * screen2_pixel.c — bitmap-mode plot + point primitives + plot-mode state.
 * Split from screen2.c so a text-only bitmap program (puts) doesn't drag in
 * the bit-mask table or the read-modify-write path.
 */
#include "screen2.h"

/* BSS (crt0 has no copydata — a DATA initializer here was never applied).
 * PLOT_MODE_SET is defined as 0 precisely so the zerobss-cleared BSS IS the
 * documented default on every entry. */
unsigned char screen2_plot_mode;

static const unsigned char pow2_table_reversed[8] = {
    128U, 64U, 32U, 16U, 8U, 4U, 2U, 1U
};

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
