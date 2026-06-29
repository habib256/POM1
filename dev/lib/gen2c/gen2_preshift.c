/* gen2_preshift.c — Buzzard-Bait-style pre-shifted sprite blit (gen2_hgr_sprite).
 *
 * The "missing middle" of the GEN2 blit family: gen2_hgr_blit is 1px-precise
 * but slow (per-pixel mask walk); gen2_hgr_blit7 is byte-fast but snaps x to a
 * 7px grid. This one is BOTH fast AND 1px-precise, by trading memory: the sprite
 * is pre-shifted into 7 phase copies OFFLINE (build_preshift_sprites.py), and at
 * runtime we just pick the phase for x%7 and reuse the proven byte-aligned blit
 * kernel (gen2_blit7_run) at column x/7. No per-pixel shifting at all.
 *
 * This is a pure C clipping/selection wrapper around the asm kernels in
 * gen2_blit.s. Speed matters here: a single-buffer XOR program must fit its
 * erase+redraw inside V-blank (~4550 cyc) or the beam catches it mid-update
 * (flicker), so this path is tuned the Buzzard-Bait way:
 *   - x/7 and x%7 come from the gen2_col7[] / gen2_phase7[] TABLES, not cc65's
 *     16-bit software divide (~150-300 cyc each — the dominant per-call cost);
 *   - GEN2_XOR (the hot animation path) uses gen2_preshift_xor_run, a dedicated
 *     XOR loop with no per-byte mode dispatch (~24 vs ~34 cyc/byte).
 * See gen2.h for the data ABI and DISASSEMBLY.md (S3) for the method. */

#include "gen2.h"
#include "gen2_internal.h"

void gen2_hgr_sprite(unsigned x, unsigned char y,
                     const gen2_sprite_t *spr, unsigned char mode)
{
    /* No stack locals for the clip math: write straight to the gen2_b_* zero-page
     * block (cf. the gen2_hgr_blit note — cc65 -Oirs reuses stack-local slots as
     * 16-bit scratch and silently clobbers them). phase_off is the only 16-bit
     * temp; it is consumed immediately. */
    unsigned char col, phase, stride, h;
    unsigned      phase_off;

    gen2_build_tables();
    stride = spr->stride;
    h      = spr->h;
    if (h == 0u || stride == 0u || y > 191u || x > 279u) return;

    /* Table lookups, NOT x/7 + x%7: cc65 has no hardware divide, so each of those
     * is a ~150-300 cyc runtime call. x <= 279 is guaranteed above, so the tables
     * (built by gen2_build_tables) are always in range; col is therefore <= 39. */
    col   = gen2_col7[x];
    phase = gen2_phase7[x];                     /* sub-byte phase 0..6           */

    /* Phase block p starts at p*(h*stride) bytes into the bank (see gen2.h ABI). */
    phase_off     = (unsigned)phase * ((unsigned)h * stride);
    gen2_b_src    = spr->bits + phase_off;
    gen2_b_col    = col;
    gen2_b_stride = stride;                     /* full source row length        */
    /* bytes actually drawn this row, right-clipped to the 40-byte page */
    gen2_b_w      = ((unsigned)col + stride > 40u)
                        ? (unsigned char)(40u - col)
                        : stride;
    gen2_b_y      = y;
    gen2_b_h      = ((unsigned)y + h > 192u)    /* bottom clip                   */
                        ? (unsigned char)(192u - y)
                        : h;
    if (mode == GEN2_XOR) {
        gen2_preshift_xor_run();                /* hot path: no per-byte mode test */
    } else {
        gen2_b_mode = mode;
        gen2_blit7_run();
    }
}

/* XOR-only fast path: a THIN C entry that just stows the args in zero page and
 * jumps to the all-asm worker (gen2_xs_run in gen2_blit.s). All the per-call cost
 * the cc65 wrapper above pays -- the x/7 + x%7 (here table lookups, but still),
 * the phase*h*stride multiply, the 16-bit edge-clip ternaries -- moves into asm,
 * so an erase+redraw pair fits in V-blank and single-buffer animation is
 * flicker-free. Caller guarantees x <= 279 / y <= 191 (the asm still edge-clips). */
void gen2_hgr_sprite_xor(unsigned x, unsigned char y, const gen2_sprite_t *spr)
{
    gen2_build_tables();
    if (spr->stride == 0u || spr->h == 0u || y > 191u || x > 279u) return;
    gen2_xs_x   = x;
    gen2_xs_y   = y;
    gen2_xs_spr = spr;
    gen2_xs_run();
}
