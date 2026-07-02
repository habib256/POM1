/*
 * P-LAB TMS9918 (Apple-1) — cc65 C runtime, POM1 CodeTank port
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: derived from Antonino "Nino" Porcino's apple1-videocard-lib
 *   (https://github.com/nippur72/apple1-videocard-lib).
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 */
#include "sprites.h"

void tms_set_total_sprites(unsigned char num) {
    unsigned addr;
    if (num >= 32U) {
        /* All 32 sprites active = no terminator needed (legal); writing one
         * would land PAST the SAT at $3B80 — silent VRAM corruption. */
        return;
    }
    addr = TMS_SPRITE_ATTRS + (unsigned)num * SIZEOF_SPRITE;
    tms_set_vram_write_addr(addr);
    TMS_WRITE_DATA_PORT(SPRITE_OFF_MARKER);
}

void tms_set_sprite(unsigned char sprite_num, const tms_sprite *s) {
    unsigned addr = TMS_SPRITE_ATTRS + (unsigned)sprite_num * SIZEOF_SPRITE;
    unsigned char y = (unsigned char)s->y;
    if (y == 0xD0U) {
        /* y = -48 casts to exactly $D0 — the SAT chain TERMINATOR. A sprite
         * animated off the top edge silently blanks every higher-numbered
         * sprite while it holds that value (miserable to debug on real
         * hardware). $D1 is one line lower and equally off-screen. */
        y = 0xD1U;
    }
    tms_set_vram_write_addr(addr);
    TMS_WRITE_DATA_PORT(y);
    TMS_IO_DELAY();
    TMS_WRITE_DATA_PORT(s->x);
    TMS_IO_DELAY();
    TMS_WRITE_DATA_PORT(s->name);
    TMS_IO_DELAY();
    /* Mask to colour + deliberate EARLY_CLOCK only: stray bits 4-6 are
     * undefined on silicon, and an accidental bit 7 shifts the sprite
     * 32 px left (best-practices §3 defensive mask). */
    TMS_WRITE_DATA_PORT((unsigned char)(s->color & 0x8FU));
    TMS_IO_DELAY();
}

void tms_set_sprite_double_size(unsigned char size) {
    unsigned char regval = (unsigned char)(tms_regs_latch[1] & (unsigned char)~REG1_SIZE_MASK);
    if (size) {
        regval = (unsigned char)(regval | REG1_SIZE_MASK);
    }
    tms_write_reg(1, regval);
}

void tms_set_sprite_magnification(unsigned char m) {
    unsigned char regval = (unsigned char)(tms_regs_latch[1] & (unsigned char)~REG1_MAG_MASK);
    if (m) {
        regval = (unsigned char)(regval | REG1_MAG_MASK);
    }
    tms_write_reg(1, regval);
}

void tms_clear_collisions(void) {
    /* Reading the status port clears the collision / 5th-sprite / frame flags.
     * Store the result into tms_io_sink rather than casting to void: cc65 -Oirs
     * ELIDES a volatile read whose value is discarded by (void), which silently
     * turned this into a no-op (same trap fixed in gen2c via gen2_ss_sink). */
    tms_io_sink = tms_read_status();
}
