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

/* R1 = $80: 16K, display OFF. The display is enabled by
 * screen2_init_bitmap() once the tables + SAT park are valid — enabling it
 * here (the old $C0) flashed ~0.4 s of power-on VRAM garbage on real DRAM
 * (best-practices §1 blank-first order). */
const unsigned char SCREEN2_TABLE[8] = {
    0x02U, 0x80U, 0x0EU, 0xFFU, 0x03U, 0x76U, 0x03U, 0x25U
};

void screen2_init_bitmap(unsigned char color) {
    unsigned i;

    /* Blank UNCONDITIONALLY. tms_regs_latch is BSS — on a warm start
     * (4000R re-run, CodeTank game switch) the mirror can read "blanked"
     * while the REAL chip still has the display ON from the previous
     * program; trusting the mirror ran this ~12.5 KB init hot and dropped
     * most bytes on real silicon. (mirror & $3F) | $80 keeps the known
     * mode bits, forces 16K, clears the display bit. While blanked, bursts
     * are legal without per-byte pacing. The R1 write clobbers the VRAM
     * address counter (real silicon), so each upload re-primes its address. */
    tms_write_reg(1, (unsigned char)((tms_regs_latch[1] & 0x3FU) | 0x80U));

    /* Park the SAT: $D0 terminator + 127×$D1 prefill (a lone $D0 leaves SAT
     * noise armed on real silicon — TMS9918-SPRITE_INIT.md §4.2/§11). */
    tms_set_vram_write_addr(TMS_SPRITE_ATTRS);
    TMS_WRITE_DATA_PORT(SPRITE_OFF_MARKER);
    for (i = 127U; i != 0U; --i) {
        TMS_WRITE_DATA_PORT(0xD1U);
    }

    tms_set_vram_write_addr(TMS_PATTERN_TABLE);
    for (i = 768U * 8U; i != 0U; --i) {
        TMS_WRITE_DATA_PORT(0);
    }

    tms_set_vram_write_addr(TMS_COLOR_TABLE);
    for (i = 768U * 8U; i != 0U; --i) {
        TMS_WRITE_DATA_PORT(color);
    }

    tms_set_vram_write_addr(TMS_NAME_TABLE);
    for (i = 0U; i < SCREEN2_SIZE; ++i) {
        TMS_WRITE_DATA_PORT((unsigned char)(i & 0xFFU));
    }

    /* Contract (juillet 2026): init_bitmap LEAVES THE DISPLAY ON — the
     * tables + SAT park are valid now; SCREEN2_TABLE ships the display bit
     * clear so this is the first enable the viewer ever sees. */
    tms_set_blank(1);
}
