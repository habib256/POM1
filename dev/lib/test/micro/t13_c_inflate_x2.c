/* ===========================================================================
 * t13_c_inflate_x2.c — micro-test: gen2c gen2_hgr_inflate_x2 ON-TARGET
 * ===========================================================================
 * GUARDS the cc65 `-Oirs` miscompile of gen2_hgr_inflate_x2 (dev/lib/gen2c/
 * gen2_hgr_x2.c). The optimiser used to drop the function's whole pixel-setting
 * body, so every x2 colour sprite inflated to ALL ZERO on the 6502 — while the
 * host build (and thus hgr_inflate_x2_smoke) stayed correct. gen2_hgr_x2.c now
 * guards the function with `#pragma optimize (off)`; this pin runs the REAL
 * compiled 6502 code and catches a regression the host pin can't see.
 *
 * Inflates the 16x16 "dog" Fauna master to x2 WHITE (6 bytes x 32 rows) and
 * stamps a mailbox: a known lit row + the 16-bit sum of all 192 output bytes.
 * A miscompiled inflate zeroes the output -> sum 0x0000, lit row 00 -> mismatch.
 * Expected values computed by sketchs/gen2/demo_sprite_animals/gen_x2.py.
 *
 * The pragma-off inflate is slow naive codegen (~2-4M instr for a 16x16), hence
 * the fat STEPS budget; headless still runs it in well under a second.
 *
 * POM1-LIB-MICRO-TEST
 * MODE: gen2
 * LIBS: gen2c/gen2_hgr_x2.c apple1c/apple1io.c apple1c/apple1io_asm.s
 * CFG: ../cc65/apple1_gen2_c.cfg
 * PRESET: 11
 * LOAD: 6000
 * RUN: 6000
 * STEPS: 6000000
 * EXPECT: 1000 A5 40 07 70 01 5A 1A
 * ===========================================================================
 * Mailbox at $1000 (idle user RAM below the HIRES framebuffers; GEN2 C code +
 * stack live $6000-$BEFF, framebuffers $2000-$5FFF, so $1000 is untouched):
 *   +0 magic $A5   +1..4 out[48..51] = 40 07 70 01 (the first lit doubled row)
 *   +5 sum_lo $5A  +6 sum_hi $1A     (sum of all 192 bytes = 0x1A5A)
 */

#include "gen2.h"

#define MB ((volatile unsigned char *)0x1000)

/* 16x16 "dog" mono master, 7px/byte — dev/lib/gen2/sprites/sprites_fauna_hgr.asm */
static const unsigned char dog[48] = {
    0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
    0x18,0x0C,0x00, 0x18,0x0C,0x00, 0x70,0x03,0x00, 0x6A,0x06,0x00,
    0x7C,0x0F,0x01, 0x00,0x4E,0x00, 0x60,0x5F,0x00, 0x70,0x5F,0x00,
    0x68,0x5F,0x00, 0x60,0x5F,0x00, 0x60,0x5F,0x00, 0x50,0x37,0x00,
};

static unsigned char out[192];

void main(void) {
    unsigned i;
    unsigned sum = 0u;

    gen2_hgr_inflate_x2(dog, 3u, 16u, GEN2_X2_WHITE, out);

    for (i = 0u; i < 192u; ++i) sum += out[i];

    MB[1] = out[48];
    MB[2] = out[49];
    MB[3] = out[50];
    MB[4] = out[51];
    MB[5] = (unsigned char)(sum & 0xFFu);
    MB[6] = (unsigned char)(sum >> 8);
    MB[0] = 0xA5u;                       /* magic LAST */

    for (;;) { /* spin — harness snapshots the mailbox */ }
}
