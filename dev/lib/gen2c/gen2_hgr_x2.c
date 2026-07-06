/* gen2_hgr_x2.c — mono ×1 sprite → ×2 single-colour inflate (gen2_hgr_inflate_x2).
 *
 * On HIRES a sprite has no CHOSEN colour: at ×1 its colour is a pure NTSC artifact
 * (Woz's trick), parity-dependent and uncontrollable — but compact, the mono bytes
 * ARE the sprite. To get a deliberate colour you DOUBLE the sprite: each source
 * pixel becomes a 2×2 block spanning one full NTSC colour clock, lighting the even
 * OR odd dot of the pair (+ the palette high bit for the blue/orange group) instead
 * of both, so the doubled sprite reads as that one hue instead of solid white.
 *
 * This is the 6502/cc65 twin of the editor's HgrSpriteBlit::magnifyColor2x — the
 * SAME transform, so a sprite authored ×2 in the POM1 HGR Sprite editor and one
 * inflated here from its mono ×1 master come out byte-identical (pinned host-side
 * by hgr_inflate_x2_smoke). Keep the two in lock-step.
 *
 * Recommended use is INFLATE-ONCE: store the compact mono ×1 (e.g. 48 B for a
 * 16×16), inflate to its ×2 form (192 B) once at init into your own buffer, then
 * blit that buffer every frame with gen2_hgr_blit7 / gen2_hgr_sprite. See gen2.h
 * for the ×2 parity contract (an even byte column keeps the hue). */

#include "gen2.h"

/* WHY THE #pragma (do not remove). cc65 2.18 `-Oirs` (the standard gen2c flag)
 * MISCOMPILES this function: whatever the loop shape, the optimizer decides the
 * inner pixel-setting body is dead and drops it — it computes the source-bit
 * test and then just does sx++, emitting ZERO stores into `out`, so every x2
 * sprite comes out blank ON-TARGET. The host build optimises differently and is
 * fine, so the host pin hgr_inflate_x2_smoke never caught it; the on-target pin
 * gen2_hgr_inflate_x2_target_smoke runs the real 6502 code and would. Confirmed
 * juillet 2026 by reading the -Oirs asm (both the original continue+inner-loop
 * form and a flat if/if rewrite lose the stores). Turning the optimiser OFF for
 * just this one function restores correct naive codegen; it is called once at
 * init (inflate-once), so the lost optimisation costs nothing. The loop below is
 * also written in the flattest style (no `continue`, no inner loop, pointer-
 * walked rows) so it stays correct even if the pragma is ever dropped. */
#pragma optimize (push, off)
void gen2_hgr_inflate_x2(const unsigned char *mono, unsigned char wbytes,
                         unsigned char h, unsigned char color, unsigned char *out)
{
    unsigned char dW  = (unsigned char)(wbytes << 1);       /* doubled byte width  */
    unsigned char wpx = (unsigned char)(wbytes * 7u);       /* wbytes<=36 -> 8-bit */
    unsigned char litLeft, litRight, palbit;
    unsigned char sy, sx, dc, byte, bits;
    unsigned char *top, *bot;                               /* the two dbl rows    */
    const unsigned char *m;
    unsigned n, i;

    /* Decode the single hue into (which dot of the pair, palette bit) — the exact
     * table magnifyColor2x uses. Black lights neither -> all-zero output (for a
     * black silhouette inflate GEN2_X2_WHITE and blit GEN2_CLEAR). */
    litLeft  = (unsigned char)(color == GEN2_X2_VIOLET || color == GEN2_X2_BLUE   || color == GEN2_X2_WHITE);
    litRight = (unsigned char)(color == GEN2_X2_GREEN  || color == GEN2_X2_ORANGE || color == GEN2_X2_WHITE);
    palbit   = (color == GEN2_X2_BLUE || color == GEN2_X2_ORANGE) ? 0x80u : 0x00u;

    n = (unsigned)dW * (unsigned)(h << 1);
    for (i = 0; i < n; ++i) out[i] = 0u;

    m   = mono;
    top = out;                                              /* top doubled row     */
    for (sy = 0; sy < h; ++sy) {
        bot = top + dW;                                     /* bottom doubled row  */
        for (sx = 0; sx < wpx; ++sx) {
            if (m[sx / 7u] & (unsigned char)(1u << (sx % 7u))) {
                if (litLeft) {                              /* left dot: dc = sx*2 */
                    dc   = (unsigned char)(sx << 1);
                    byte = (unsigned char)(dc / 7u);
                    bits = (unsigned char)((1u << (dc % 7u)) | palbit);
                    top[byte] |= bits;
                    bot[byte] |= bits;
                }
                if (litRight) {                             /* right dot: dc = sx*2+1 */
                    dc   = (unsigned char)((sx << 1) + 1u);
                    byte = (unsigned char)(dc / 7u);
                    bits = (unsigned char)((1u << (dc % 7u)) | palbit);
                    top[byte] |= bits;
                    bot[byte] |= bits;
                }
            }
        }
        m   += wbytes;
        top += (unsigned char)(dW << 1);                    /* skip both dbl rows  */
    }
}
#pragma optimize (pop)
