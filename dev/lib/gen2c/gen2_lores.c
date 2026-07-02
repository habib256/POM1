/* gen2_lores.c — 40x48 blocks of 16 real colours.
 *
 * LORES lives in the TEXT page ($0400), Apple II row interleave, two stacked
 * blocks per byte (low nibble = upper / even block-row, high = lower / odd).
 * The grid is tiny (40x48 = 1920 blocks) and the addressing is pure shifts +
 * masks (no x/7 per-pixel division as HIRES needs), so plain C is fast enough
 * — no asm fast path. A 24-entry text-row base table is built once on the
 * first lores call. The table state (gen2_lo_rowlo/hi, gen2_lo_base,
 * gen2_lo_ready) lives in gen2_init.c so a HIRES-only program does not pull
 * this file into the link. */

#include "gen2.h"
#include "gen2_internal.h"

static void gen2_lores_build(void)
{
    unsigned r, a;
    if (gen2_lo_ready) return;
    if (!gen2_lo_base) gen2_lo_base = 0x04u;   /* BSS default: page 1 */
    for (r = 0; r < 24u; ++r) {
        /* base + $80*(r&7) + $28*(r>>3); r>>3 is only 0,1,2 so add directly. */
        a = ((unsigned)gen2_lo_base << 8) + ((unsigned)(r & 7u) << 7);
        if ((r >> 3) == 1u)      a += 0x28u;
        else if ((r >> 3) == 2u) a += 0x50u;
        gen2_lo_rowlo[r] = (unsigned char)(a & 0xFFu);
        gen2_lo_rowhi[r] = (unsigned char)(a >> 8);
    }
    gen2_lo_ready = 1;
}

/* Byte holding block (x, block-row y) — caller guarantees x<40, y<48. */
static unsigned char *gen2_lores_cell(unsigned char x, unsigned char y)
{
    unsigned char r = y >> 1;        /* text row 0..23 */
    return (unsigned char *)(((unsigned)gen2_lo_rowhi[r] << 8)
                             | gen2_lo_rowlo[r]) + x;
}

void gen2_lores_clear(unsigned char color)
{
    /* Both nibbles = colour, then fill the 1 KB draw page (4 contiguous pages
     * from gen2_lo_base; the 64 unused screen-hole bytes per region are harmless
     * card DRAM) a page at a time with an 8-bit index. Four base pointers keep
     * the inner store a simple (ptr),Y — page 1 ($0400) or page 2 ($0800). */
    unsigned char v = (unsigned char)((color & 0x0Fu) | (color << 4));
    unsigned base;
    unsigned char *p0, *p1, *p2, *p3;
    unsigned char i = 0;
    if (!gen2_lo_base) gen2_lo_base = 0x04u;   /* BSS default: page 1 */
    base = (unsigned)gen2_lo_base << 8;
    p0 = (unsigned char *)(base);
    p1 = (unsigned char *)(base + 0x100u);
    p2 = (unsigned char *)(base + 0x200u);
    p3 = (unsigned char *)(base + 0x300u);
    do {
        p0[i] = v;
        p1[i] = v;
        p2[i] = v;
        p3[i] = v;
    } while (++i != 0u);
}

void gen2_lores_setblock(unsigned char x, unsigned char y, unsigned char color)
{
    unsigned char *p;
    if (x >= 40u || y >= 48u) return;
    gen2_lores_build();
    p = gen2_lores_cell(x, y);
    color &= 0x0Fu;
    if (y & 1u) *p = (unsigned char)((*p & 0x0Fu) | (color << 4));  /* lower block */
    else        *p = (unsigned char)((*p & 0xF0u) | color);         /* upper block */
}

unsigned char gen2_lores_getblock(unsigned char x, unsigned char y)
{
    unsigned char v;
    if (x >= 40u || y >= 48u) return 0u;
    gen2_lores_build();
    v = *gen2_lores_cell(x, y);
    return (unsigned char)((y & 1u) ? (v >> 4) : (v & 0x0Fu));
}

void gen2_lores_hlin(unsigned char x0, unsigned char x1, unsigned char y,
                     unsigned char color)
{
    unsigned char *base, keep, val, x;
    if (y >= 48u || x0 >= 40u) return;
    if (x1 >= 40u) x1 = 39u;
    if (x0 > x1) return;
    gen2_lores_build();
    base = gen2_lores_cell(0u, y);              /* row base; cell adds x below */
    color &= 0x0Fu;
    if (y & 1u) { keep = 0x0Fu; val = (unsigned char)(color << 4); }
    else        { keep = 0xF0u; val = color; }
    for (x = x0; x <= x1; ++x)
        base[x] = (unsigned char)((base[x] & keep) | val);
}

void gen2_lores_vlin(unsigned char x, unsigned char y0, unsigned char y1,
                     unsigned char color)
{
    unsigned char y;
    if (x >= 40u || y0 >= 48u) return;
    if (y1 >= 48u) y1 = 47u;
    if (y0 > y1) return;
    gen2_lores_build();
    color &= 0x0Fu;
    for (y = y0; y <= y1; ++y) {
        unsigned char *p = gen2_lores_cell(x, y);
        if (y & 1u) *p = (unsigned char)((*p & 0x0Fu) | (color << 4));
        else        *p = (unsigned char)((*p & 0xF0u) | color);
    }
}

void gen2_lores_fill_rect(unsigned char x, unsigned char y,
                          unsigned char w, unsigned char h, unsigned char color)
{
    unsigned xr, yb, yy;
    if (w == 0u || h == 0u || x >= 40u || y >= 48u) return;
    xr = (unsigned)x + w; if (xr > 40u) xr = 40u;   /* one past right column   */
    yb = (unsigned)y + h; if (yb > 48u) yb = 48u;   /* one past bottom block-row */
    for (yy = y; yy < yb; ++yy)
        gen2_lores_hlin(x, (unsigned char)(xr - 1u), (unsigned char)yy, color);
}
