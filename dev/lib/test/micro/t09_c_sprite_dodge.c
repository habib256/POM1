/* ===========================================================================
 * t09_c_sprite_dodge.c — micro-test: tms9918c tms_set_sprite / SAT semantics
 * ===========================================================================
 * GUARDS: the C-side SAT-terminator dodge (twin of t05's asm dodge):
 *   tms_set_sprite() with y = -48 casts to exactly $D0 — the SAT chain
 *   TERMINATOR — and must be nudged to $D1 or a sprite animated off the top
 *   edge silently blanks every higher-numbered sprite. Also pins:
 *     - the colour defensive mask (color & $8F: EARLY_CLOCK allowed, stray
 *       bits 4-6 stripped — best-practices §3),
 *     - tms_set_total_sprites(2) planting $D0 at SAT[2].Y,
 *     - the tms_set_vram_read_addr / TMS_READ_DATA_PORT read-back path,
 *   and proves the cl65 + codetank_c.cfg harness path end to end (CodeTank
 *   ROM at $4000, C stack at $1000, BSS in the $E000 bank).
 *
 * POM1-LIB-MICRO-TEST
 * MODE: codetank
 * LIBS: tms9918c/tms9918.c tms9918c/tms_fast.s tms9918c/sprites.c tms9918c/sprite_shadow.c
 * CFG: tms9918c/cc65/codetank_c.cfg
 * PRESET: 1
 * STEPS: 100000
 * EXPECT: EF00 A5 D1 64 08 8F 32 28 04 03 D0
 * ===========================================================================
 * Mailbox at $EF00 (RAM_HI bank — the asm mailbox window $0F00 holds the
 * cc65 C stack under codetank_c.cfg, so the C tests use their own window):
 *   +0 magic  +1..4 SAT[5] = D1 64 08 8F   +5..8 SAT[0] = 32 28 04 03
 *   +9 SAT[2].Y = D0 (tms_set_total_sprites terminator)
 */

#include "tms9918.h"
#include "sprites.h"

#define MB ((volatile unsigned char *)0xEF00)

static void read_sat(unsigned char entry, volatile unsigned char *dst,
                     unsigned char n) {
    unsigned char i;
    tms_set_vram_read_addr(TMS_SPRITE_ATTRS + (unsigned)entry * SIZEOF_SPRITE);
    for (i = 0; i < n; ++i) {
        dst[i] = TMS_READ_DATA_PORT;   /* strict mode: paced by C overhead */
        TMS_IO_DELAY();
        TMS_IO_DELAY();
    }
}

void main(void) {
    tms_sprite s;

    /* Case 1: y = -48 -> raw $D0, must be dodged to $D1. Colour $FF must be
     * masked to $8F (EARLY_CLOCK kept, undefined bits 4-6 stripped). */
    s.y = -48;
    s.x = 100;
    s.name = 8;
    s.color = 0xFF;
    tms_set_sprite(5, &s);

    /* Case 2: ordinary sprite, nothing rewritten. */
    s.y = 50;
    s.x = 40;
    s.name = 4;
    s.color = 0x03;
    tms_set_sprite(0, &s);

    /* Terminator after 2 active sprites -> SAT[2].Y = $D0. */
    tms_set_total_sprites(2);

    read_sat(5, MB + 1, 4);
    read_sat(0, MB + 5, 4);
    read_sat(2, MB + 9, 1);

    MB[0] = 0xA5;                /* magic LAST */
    for (;;) { /* spin — harness snapshots the mailbox */ }
}
