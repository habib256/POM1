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

void gen2_hgr_inflate_x2(const unsigned char *mono, unsigned char wbytes,
                         unsigned char h, unsigned char color, unsigned char *out)
{
    const unsigned char *monoRow;
    unsigned char dW  = (unsigned char)(wbytes << 1);       /* doubled byte width  */
    unsigned char dH  = (unsigned char)(h << 1);            /* doubled row count   */
    unsigned char wpx = (unsigned char)(wbytes * 7u);       /* wbytes<=36 -> 8-bit */
    unsigned char litLeft, litRight, p1;
    unsigned char sy, sx, k, dc, byte, bit;
    unsigned      rowbase, n, i;

    /* Decode the single hue into (which dot of the pair, palette bit) — the exact
     * table magnifyColor2x uses. Black lights neither -> all-zero output (for a
     * black silhouette inflate GEN2_X2_WHITE and blit GEN2_CLEAR). */
    litLeft  = (unsigned char)(color == GEN2_X2_VIOLET || color == GEN2_X2_BLUE   || color == GEN2_X2_WHITE);
    litRight = (unsigned char)(color == GEN2_X2_GREEN  || color == GEN2_X2_ORANGE || color == GEN2_X2_WHITE);
    p1       = (unsigned char)(color == GEN2_X2_BLUE   || color == GEN2_X2_ORANGE);

    n = (unsigned)dW * dH;
    for (i = 0; i < n; ++i) out[i] = 0u;

    for (sy = 0; sy < h; ++sy) {
        monoRow = mono + (unsigned)sy * wbytes;             /* 16-bit ptr, once/row */
        rowbase = (unsigned)(sy + sy) * dW;                 /* top doubled-row base */
        for (sx = 0; sx < wpx; ++sx) {
            if ((monoRow[sx / 7u] & (unsigned char)(1u << (sx % 7u))) == 0u) continue;
            for (k = 0; k < 2u; ++k) {
                if ((k == 0u && !litLeft) || (k == 1u && !litRight)) continue;
                dc   = (unsigned char)((sx << 1) + k);      /* doubled column       */
                byte = (unsigned char)(dc / 7u);
                bit  = (unsigned char)(dc % 7u);
                /* Both doubled rows: top = rowbase, bottom = rowbase + dW. */
                out[rowbase + byte]      |= (unsigned char)(1u << bit);
                out[rowbase + dW + byte] |= (unsigned char)(1u << bit);
                if (p1) {
                    out[rowbase + byte]      |= 0x80u;
                    out[rowbase + dW + byte] |= 0x80u;
                }
            }
        }
    }
}
