/*
 * P-LAB TMS9918 (Apple-1) — cc65 C runtime, POM1 CodeTank port
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: derived from Antonino "Nino" Porcino's apple1-videocard-lib
 *   (https://github.com/nippur72/apple1-videocard-lib).
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 *
 * screen2_init.c — bitmap-mode table init + linear name table (~6 KB VRAM
 * fill). Split from screen2.c so a program that only calls `screen2_plot`
 * (via direct VRAM access from elsewhere) doesn't have to pay for this init.
 */
#include "screen2.h"
#include "sprites.h"

const unsigned char SCREEN2_TABLE[8] = {
    0x02U, 0xC0U, 0x0EU, 0xFFU, 0x03U, 0x76U, 0x03U, 0x25U
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
