/* gen2.c — minimal C runtime for Uncle Bernie's GEN2 HGR. See gen2.h. */
#include "gen2.h"

void gen2_hgr_init(void)
{
    gen2_graphics();   /* TEXT off  */
    gen2_hires();      /* RES=HIRES */
    gen2_page1();      /* page 1    */
    gen2_full();       /* no MIXED  */
}

void gen2_hgr_clear(unsigned char fill)
{
    /* Page-at-a-time fill: the inner byte index wraps at 256, so cc65 emits a
     * tight STA (ptr),Y / INY / BNE loop (~8 KB in well under a frame) instead
     * of the slow 16-bit pointer-increment runtime helper. */
    unsigned char *p = GEN2_HGR1;
    unsigned char hi = 0x20;
    do {
        unsigned char i = 0;
        do { p[i] = fill; } while (++i != 0);
        p += 256;
    } while (++hi != 0x40);
}

/* Apple II interleaved HIRES scanline base:
   $2000 + (y&7)*$400 + ((y>>3)&7)*$80 + (y>>6)*40 */
unsigned char *gen2_hgr_row(unsigned char y)
{
    return (unsigned char *)(0x2000u
        + ((unsigned)(y & 7) << 10)
        + ((unsigned)((y >> 3) & 7) << 7)
        + (unsigned)(y >> 6) * 40u);
}

void gen2_hgr_plot(unsigned x, unsigned char y)
{
    if (x > 279 || y > 191) return;
    gen2_hgr_row(y)[x / 7u] |= (unsigned char)(1u << (x % 7u));   /* bit 7 = 0 */
}

void gen2_hgr_unplot(unsigned x, unsigned char y)
{
    if (x > 279 || y > 191) return;
    gen2_hgr_row(y)[x / 7u] &= (unsigned char)~(1u << (x % 7u));
}

/* Double-sampled HST0 (the burst notch can zero one of two close reads). */
static unsigned char gen2_blank(void)
{
    return (unsigned char)((GEN2_SS[7] | GEN2_SS[7]) & GEN2_HST0);
}

void gen2_wait_vbl(void)
{
    unsigned n;
    while (gen2_blank()) { }            /* wait for the live raster */
    for (;;) {
        while (!gen2_blank()) { }        /* wait for a blanking edge */
        for (n = 0; n < 400u && gen2_blank(); ++n) { }
        if (n >= 400u) return;          /* sustained blank => V-blank */
        /* short blank => it was just an H-blank; find the next edge */
    }
}

/* --- Beautiful Boot 8x8 font, ASCII 0x20-0x7F (96 glyphs).
 * gen2_bbfont.inc is GENERATED from the single source dev/lib/hgr/bbfont_cp437.inc
 * by tools/emit_bbfont.py (re-run after editing the font) — so the C copy can't
 * drift from the asm table. bit 0 = leftmost pixel, rows top->bottom; values stay
 * <= 0x7F so each glyph fits HGR's 7 px/byte. */
static const unsigned char kBBFontAscii[96u * 8u] = {
#include "gen2_bbfont.inc"
};

/* Draw an ASCII string at (x, y) in HIRES page 1, white and artifact-free:
 * every set pixel is doubled horizontally (>=2px run -> white, no NTSC colour
 * fringe) and each glyph row is drawn on two scanlines, giving 16x16 cells on
 * an 18px pitch. Non-printable chars render as a space. Clipping is done here:
 * gen2_hgr_row does NOT bounds-check, so rows past line 191 are dropped and
 * pixel pairs past the 280px (40-byte) scanline are skipped rather than spilling
 * into adjacent memory. */
void gen2_hgr_puts(unsigned x, unsigned char y, const char *s)
{
    unsigned char c, row, bit, bits;
    const unsigned char *g;
    unsigned cx = x;
    while ((c = (unsigned char)*s++) != 0) {
        if (c < 0x20 || c > 0x7F) c = 0x20;
        g = kBBFontAscii + (unsigned)(c - 0x20) * 8u;
        for (row = 0; row < 8; ++row) {
            /* Resolve both doubled scanlines' bases ONCE per glyph row, then OR
             * the pixel pairs straight in (gen2_hgr_plot would recompute the
             * base on every one of the 4 writes per set pixel). Clip vertically:
             * a base is only taken for a line that is actually on-screen. */
            unsigned char yy  = (unsigned char)(y + (unsigned char)(row << 1));
            unsigned char *r0 = (yy <= 191u) ? gen2_hgr_row(yy) : (unsigned char *)0;
            unsigned char *r1 = (yy <  191u) ? gen2_hgr_row((unsigned char)(yy + 1))
                                             : (unsigned char *)0;
            unsigned px = cx;
            if (!r0 && !r1) continue;          /* whole doubled row off-screen */
            bits = g[row];
            for (bit = 0; bit < 8; ++bit) {
                if ((bits & 1u) && px + 1u <= 279u) {   /* clip to the scanline */
                    unsigned char c0 = (unsigned char)(px / 7u);
                    unsigned char m0 = (unsigned char)(1u << (px % 7u));
                    unsigned char c1 = (unsigned char)((px + 1u) / 7u);
                    unsigned char m1 = (unsigned char)(1u << ((px + 1u) % 7u));
                    if (r0) { r0[c0] |= m0;  r0[c1] |= m1; }
                    if (r1) { r1[c0] |= m0;  r1[c1] |= m1; }
                }
                bits >>= 1;
                px += 2u;
            }
        }
        cx += 18u;   /* 16px glyph + 2px gap */
    }
}

/* Unsigned decimal at (x, y) via gen2_hgr_puts. Builds the digits right-to-left
 * into a 6-byte buffer (max 65535 = 5 digits + NUL), no leading zeros. */
void gen2_hgr_putu(unsigned x, unsigned char y, unsigned value)
{
    char buf[6];
    unsigned char i = 6;
    buf[--i] = 0;                    /* buf[5] = NUL terminator */
    do {
        buf[--i] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    gen2_hgr_puts(x, y, buf + i);
}
