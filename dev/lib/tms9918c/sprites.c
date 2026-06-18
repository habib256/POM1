/*
 * apple1-videocard-lib — POM1 CodeTank cc65 port
 * Derived from nippur72/apple1-videocard-lib (Antonino "Nino" Porcino).
 *   https://github.com/nippur72/apple1-videocard-lib
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 */
#include "sprites.h"

void tms_set_total_sprites(unsigned char num) {
    unsigned addr = TMS_SPRITE_ATTRS + (unsigned)num * SIZEOF_SPRITE;
    tms_set_vram_write_addr(addr);
    TMS_WRITE_DATA_PORT(SPRITE_OFF_MARKER);
}

void tms_set_sprite(unsigned char sprite_num, const tms_sprite *s) {
    unsigned addr = TMS_SPRITE_ATTRS + (unsigned)sprite_num * SIZEOF_SPRITE;
    tms_set_vram_write_addr(addr);
    TMS_WRITE_DATA_PORT((unsigned char)s->y);
    TMS_IO_DELAY();
    TMS_WRITE_DATA_PORT(s->x);
    TMS_IO_DELAY();
    TMS_WRITE_DATA_PORT(s->name);
    TMS_IO_DELAY();
    TMS_WRITE_DATA_PORT(s->color);
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
