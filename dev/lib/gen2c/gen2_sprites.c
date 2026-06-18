/* gen2_sprites.c — 1bpp sprite blit (gen2_hgr_blit, gen2_hgr_blit7).
 *
 * Named *_sprites instead of *_blit to avoid the gen2_blit.o collision with
 * gen2_blit.s (the asm fast paths). The blit kernels live in the asm file
 * (gen2_blit_run / gen2_blit7_run); this file is just the C clipping wrapper. */

#include "gen2.h"
#include "gen2_internal.h"

/* Blit a 1-bit-per-pixel sprite (MSB-first, see gen2.h) at pixel (x, y) in one of
 * three modes. The asm walks pixel by pixel (HIRES is 7px/byte, so a byte-shift
 * blit is impossible) and never touches the palette bit. Clips to the screen here
 * (drop rows past 191, columns past 279) so the asm needs no per-pixel bound
 * check; off-screen LEFT (x would be negative) is not supported — keep x >= 0. */
void gen2_hgr_blit(unsigned x, unsigned char y, unsigned char w, unsigned char h,
                   const unsigned char *bitmap, unsigned char mode)
{
    /* NO stack locals: store straight to the zero-page params, and run the clips
     * on gen2_b_w / gen2_b_h IN zp. cc65 -Oirs aggressively reuses a stack local's
     * slot as scratch while evaluating a 16-bit sub-expression (x+w>280), which
     * silently clobbered a local computed earlier (it ate the stride). zp params
     * are not scratch targets, so this side-steps the whole class of bug. stride
     * is derived from the FULL w (source row length); gen2_b_w is the clipped
     * pixel count to draw. */
    gen2_build_tables();
    if (y > 191u || w == 0u || h == 0u || x > 279u) return;

    gen2_b_col    = (unsigned char)(x / 7u);
    gen2_b_mask   = (unsigned char)(1u << (x % 7u));
    /* stride = ceil(w/8), done with 8-bit ops only. The obvious (w+7)/8 makes
     * cc65 do a 16-bit divide whose high byte it leaves as garbage ($01), so
     * (8+7)=$010F /8 came out 33 — the (unsigned char) cast truncates too late. */
    gen2_b_stride = (unsigned char)((w >> 3) + ((w & 7u) != 0u));/* full source row */
    gen2_b_y      = y;
    gen2_b_mode   = mode;
    gen2_b_src    = bitmap;
    gen2_b_w      = w;
    gen2_b_h      = h;
    if ((unsigned)y + h > 192u) gen2_b_h = (unsigned char)(192u - y);  /* bottom clip */
    if ((unsigned)x + w > 280u) gen2_b_w = (unsigned char)(280u - x);  /* right clip  */
    gen2_blit_run();
}

/* Fast BYTE-ALIGNED blit of a sprite pre-packed in the framebuffer's 7px/byte
 * layout (each byte = 7 horizontal pixels, bit 0 = leftmost, bit 7 = 0). Because
 * source and destination share that layout, whole bytes are SET/CLEAR/XOR'd
 * straight in — ~7x fewer memory ops than gen2_hgr_blit for a solid sprite. The
 * trade-off: the sprite snaps to a 7px column grid (x is floored to x/7), so
 * move it in steps of 7 to stay put under an XOR erase. `wbytes` = source bytes
 * per row (ceil(width/7)); `mode` is GEN2_SET/CLEAR/XOR. Clipped to the
 * right/bottom edges. Build the packed bitmap with bit k of byte j = pixel (j*7+k). */
void gen2_hgr_blit7(unsigned x, unsigned char y, unsigned char wbytes,
                    unsigned char h, const unsigned char *src, unsigned char mode)
{
    unsigned char col;
    gen2_build_tables();
    if (wbytes == 0u || h == 0u || y > 191u) return;
    col = (unsigned char)(x / 7u);
    if (col >= 40u) return;
    gen2_b_col    = col;
    gen2_b_stride = wbytes;                                  /* full source row     */
    if ((unsigned)col + wbytes > 40u) wbytes = (unsigned char)(40u - col);/* R clip */
    gen2_b_w      = wbytes;
    gen2_b_y      = y;
    gen2_b_h      = ((unsigned)y + h > 192u) ? (unsigned char)(192u - y) : h;
    gen2_b_mode   = mode;
    gen2_b_src    = src;
    gen2_blit7_run();
}
