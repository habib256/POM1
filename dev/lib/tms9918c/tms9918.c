/*
 * P-LAB TMS9918 (Apple-1) — cc65 C runtime, POM1 CodeTank port
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: derived from Antonino "Nino" Porcino's apple1-videocard-lib
 *   (https://github.com/nippur72/apple1-videocard-lib).
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 */
#include "tms9918.h"

volatile unsigned char *const VDP_DATA = (volatile unsigned char *)0xCC00u;
volatile unsigned char *const VDP_REG  = (volatile unsigned char *)0xCC01u;

/* Sink for observed status-port reads (TMS_IO_DELAY / tms_clear_collisions).
 * See utils.h for the cc65 -Oirs volatile-read-elision rationale. */
volatile unsigned char tms_io_sink;

unsigned char tms_regs_latch[8];
unsigned char tms_cursor_x;
unsigned char tms_cursor_y;
unsigned char tms_reverse;

void tms_set_vram_write_addr(unsigned addr) {
    TMS_WRITE_CTRL_PORT(LOBYTE(addr));
    TMS_WRITE_CTRL_PORT((unsigned char)((HIBYTE(addr) & HIADDRESS_MASK) | WRITE_TO_VRAM));
}

void tms_set_vram_read_addr(unsigned addr) {
    TMS_WRITE_CTRL_PORT(LOBYTE(addr));
    TMS_WRITE_CTRL_PORT((unsigned char)((HIBYTE(addr) & HIADDRESS_MASK) | READ_FROM_VRAM));
}

void tms_write_reg(unsigned char regnum, unsigned char val) {
    TMS_WRITE_CTRL_PORT(val);
    TMS_WRITE_CTRL_PORT((unsigned char)((regnum & REGNUM_MASK) | WRITE_TO_REG));
    tms_regs_latch[regnum & 7u] = val;
}

void tms_set_color(unsigned char col) {
    tms_write_reg(7, col);
}

void tms_init_regs(const unsigned char *table) {
    unsigned char i;
    for (i = 0; i < 8; ++i) {
        tms_write_reg(i, table[i]);
    }
}

void tms_set_interrupt_bit(unsigned char val) {
    unsigned char regvalue = (unsigned char)(tms_regs_latch[1] & (unsigned char)~REG1_IE_MASK);
    if (val) {
        regvalue = (unsigned char)(regvalue | REG1_IE_MASK);
    }
    tms_write_reg(1, regvalue);
}

void tms_set_blank(unsigned char val) {
    unsigned char regvalue = (unsigned char)(tms_regs_latch[1] & (unsigned char)~REG1_BLANK_MASK);
    if (val) {
        regvalue = (unsigned char)(regvalue | REG1_BLANK_MASK);
    }
    tms_write_reg(1, regvalue);
}

void tms_set_external_video(unsigned char val) {
    unsigned char regvalue = (unsigned char)(tms_regs_latch[0] & (unsigned char)~REG0_EXTVID_MASK);
    if (val) {
        regvalue = (unsigned char)(regvalue | REG0_EXTVID_MASK);
    }
    tms_write_reg(0, regvalue);
}

unsigned char tms_wait_end_of_frame(void) {
    /* Drain-then-poll (the asm WAIT_VBLANK idiom). Without the drain, an F
     * flag latched any time since the last status read returned IMMEDIATELY —
     * the caller was at a random raster position, not in VBlank, and every
     * hard-VBlank contract downstream (tms_shadow_flush's ~15c/byte cadence,
     * the scroll burst chunks) silently ran mid-frame on real silicon.
     * A status read latch-clears F+5S+C atomically (TI datasheet §2.2), so
     * the drain byte is also the only place a collision from the waited-out
     * frame can be observed: the return value merges C/5S seen on either
     * read with the fresh F — snapshot it once per frame and test the copy. */
    unsigned char acc = (unsigned char)(tms_read_status() & 0x60U); /* drain */
    unsigned char s;
    do {
        s = tms_read_status();
        acc |= (unsigned char)(s & 0x60U);  /* EVERY poll read latch-clears
                                             * C/5S — a collision landing
                                             * mid-wait would otherwise be
                                             * eaten by a discarded read */
    } while (!FRAME_BIT(s));
    return (unsigned char)(s | acc);
}

void tms_copy_to_vram(const unsigned char *source, unsigned size, unsigned dest) {
    unsigned i;
    tms_set_vram_write_addr(dest);
    for (i = size; i != 0; --i) {
        TMS_WRITE_DATA_PORT(*source++);
    }
}
