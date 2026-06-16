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

/* gen2_hgr_plot / gen2_hgr_unplot are defined further down, after the asm
 * fast-path declarations — they now route through gen2_plot_asm/unplot_asm
 * (col7/mask7 LUTs, no per-pixel division). */

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

/* --- Fast assembly glyph blitter (gen2_blit.s) -----------------------------
 * The text path used to compute px/7 and px%7 (cc65 software division — no
 * hardware DIV on the 6502) for every plotted pixel, which made one line of
 * text cost millions of cycles. The work now happens in hand-written 6502:
 * gen2_blit_glyph() draws one 8x8 glyph pixel-doubled to 16x16, advancing the
 * column/mask incrementally (no per-pixel division). Scanline base addresses
 * come from the gen2_rowlo/gen2_rowhi tables, built once below.
 *
 * gen2_g_* are the parameter block; they live in the zero page (defined in
 * gen2_blit.s) so the asm can use (gen2_g_glyph),Y directly. */
unsigned char gen2_rowlo[192];          /* low  byte of each scanline's base   */
unsigned char gen2_rowhi[192];          /* high byte of each scanline's base   */
unsigned char gen2_col7[280];           /* x / 7  (byte column) for x=0..279   */
unsigned char gen2_mask7[280];          /* 1 << (x % 7) (bit mask) for x=0..279 */
extern const unsigned char *gen2_g_glyph;
extern unsigned char gen2_g_col;
extern unsigned char gen2_g_mask;
extern unsigned char gen2_g_y;
extern void gen2_blit_glyph(void);      /* asm: draws gen2_g_glyph at (col,mask,y) */
/* gen2_hgr_fill_rect parameter block (also in the zero page, gen2_blit.s). */
extern unsigned char gen2_f_y0, gen2_f_rows, gen2_f_col0, gen2_f_cols, gen2_f_val;
extern void gen2_fill_rect_asm(void);   /* asm: byte-fills the rectangle above  */
/* gen2_hgr_plot/unplot parameter block (zero page, gen2_blit.s). */
extern unsigned gen2_p_x;               /* pixel x (0..279)                     */
extern unsigned char gen2_p_y;          /* pixel y (0..191)                     */
extern void gen2_plot_asm(void);        /* asm: OR  the pixel (col7/mask7 LUTs)  */
extern void gen2_unplot_asm(void);      /* asm: AND off the pixel                */
/* gen2_hgr_fill_pixrect/clear_pixrect parameter block (zero page, gen2_blit.s).
 * The asm derives byte columns + edge masks from x/xr (col7/mask7 LUTs); the C
 * side only clips the geometry and picks the mode. */
extern unsigned gen2_r_x, gen2_r_xr;    /* left / right pixel x (0..279)         */
extern unsigned char gen2_r_y0, gen2_r_rows;
extern unsigned char gen2_r_mode;       /* 1 = fill white, 0 = clear             */
extern void gen2_pixrect_asm(void);     /* asm: derive cols/masks + (byte&keep)|set */
/* gen2_colorize parameter block (zero page, gen2_blit.s) — see gen2_hgr_puts_color. */
extern unsigned char gen2_z_col0, gen2_z_ncols, gen2_z_y0, gen2_z_rows;
extern unsigned char gen2_z_ce, gen2_z_co, gen2_z_hi;
extern void gen2_colorize_asm(void);    /* asm: (byte & carrier[col&1]) | hibit   */
/* The parameter blocks live in the zero page (gen2_blit.s) — tell cc65 so it
 * emits zp stores instead of absolute (smaller/faster, and no ld65 warning). */
#pragma zpsym("gen2_g_glyph")
#pragma zpsym("gen2_g_col")
#pragma zpsym("gen2_g_mask")
#pragma zpsym("gen2_g_y")
#pragma zpsym("gen2_f_y0")
#pragma zpsym("gen2_f_rows")
#pragma zpsym("gen2_f_col0")
#pragma zpsym("gen2_f_cols")
#pragma zpsym("gen2_f_val")
#pragma zpsym("gen2_p_x")
#pragma zpsym("gen2_p_y")
#pragma zpsym("gen2_r_x")
#pragma zpsym("gen2_r_xr")
#pragma zpsym("gen2_r_y0")
#pragma zpsym("gen2_r_rows")
#pragma zpsym("gen2_r_mode")
#pragma zpsym("gen2_z_col0")
#pragma zpsym("gen2_z_ncols")
#pragma zpsym("gen2_z_y0")
#pragma zpsym("gen2_z_rows")
#pragma zpsym("gen2_z_ce")
#pragma zpsym("gen2_z_co")
#pragma zpsym("gen2_z_hi")

/* Build the lookup tables once (all the asm fast paths read them): the
 * non-linear Apple II HIRES scanline bases (gen2_rowlo/gen2_rowhi) and the
 * x->(byte column, bit mask) tables that replace per-pixel division. Built
 * division-free: walk x and carry the column/bit, wrapping every 7 pixels. */
static unsigned char gen2_tables_ready = 0;
static void gen2_build_tables(void)
{
    unsigned y, x;
    unsigned char col, bit;
    if (gen2_tables_ready) return;
    for (y = 0; y < 192u; ++y) {
        unsigned a = (unsigned)gen2_hgr_row((unsigned char)y);
        gen2_rowlo[y] = (unsigned char)(a & 0xFFu);
        gen2_rowhi[y] = (unsigned char)(a >> 8);
    }
    col = 0; bit = 0;
    for (x = 0; x < 280u; ++x) {
        gen2_col7[x]  = col;
        gen2_mask7[x] = (unsigned char)(1u << bit);
        if (++bit == 7u) { bit = 0; ++col; }
    }
    gen2_tables_ready = 1;
}

/* Fast rectangular byte-fill (assembly inner loop). See gen2.h. Clips the
 * rectangle to the screen, then hands the on-screen part to gen2_fill_rect_asm. */
void gen2_hgr_fill_rect(unsigned char y0, unsigned char rows,
                        unsigned char col0, unsigned char ncols,
                        unsigned char val)
{
    gen2_build_tables();
    if (rows == 0u || ncols == 0u || y0 > 191u || col0 >= 40u) return;
    if ((unsigned)y0   + rows  > 192u) rows  = (unsigned char)(192u - y0);
    if ((unsigned)col0 + ncols >  40u) ncols = (unsigned char)(40u  - col0);
    gen2_f_y0   = y0;
    gen2_f_rows = rows;
    gen2_f_col0 = col0;
    gen2_f_cols = ncols;
    gen2_f_val  = val;
    gen2_fill_rect_asm();
}

/* Set / clear one white pixel via the assembly fast path (col7/mask7 LUTs +
 * the scanline-base table — NO per-pixel division). x:0..279, y:0..191.
 * Drop-in replacements for the old per-pixel-division versions; this is what
 * makes per-pixel games (Snake's 6x6 blocks, etc.) usable on the GEN2. */
void gen2_hgr_plot(unsigned x, unsigned char y)
{
    if (x > 279u || y > 191u) return;
    gen2_build_tables();
    gen2_p_x = x;
    gen2_p_y = y;
    gen2_plot_asm();
}

void gen2_hgr_unplot(unsigned x, unsigned char y)
{
    if (x > 279u || y > 191u) return;
    gen2_build_tables();
    gen2_p_x = x;
    gen2_p_y = y;
    gen2_unplot_asm();
}

/* Shared core for fill_pixrect (set=1, white) and clear_pixrect (set=0, erase).
 * The C side only clips the rectangle to the screen and picks the mode; the asm
 * derives the byte columns and edge masks (col7/mask7 LUTs) and does the fill. */
static void gen2_pixrect(unsigned x, unsigned char y,
                         unsigned char w, unsigned char h, unsigned char set)
{
    unsigned xr;
    gen2_build_tables();
    if (w == 0u || h == 0u || x > 279u || y > 191u) return;
    xr = x + (unsigned)w - 1u;                 /* rightmost pixel               */
    if (xr > 279u) xr = 279u;                  /* clip right edge               */
    if ((unsigned)y + h > 192u) h = (unsigned char)(192u - y);   /* clip bottom */
    gen2_r_x    = x;
    gen2_r_xr   = xr;
    gen2_r_y0   = y;
    gen2_r_rows = h;
    gen2_r_mode = set;
    gen2_pixrect_asm();
}

void gen2_hgr_fill_pixrect(unsigned x, unsigned char y, unsigned char w, unsigned char h)
{
    gen2_pixrect(x, y, w, h, 1u);
}

void gen2_hgr_clear_pixrect(unsigned x, unsigned char y, unsigned char w, unsigned char h)
{
    gen2_pixrect(x, y, w, h, 0u);
}

/* Draw an ASCII string at (x, y) in HIRES page 1, white and artifact-free:
 * every set pixel is doubled horizontally (>=2px run -> white, no NTSC colour
 * fringe) and each glyph row is drawn on two scanlines, giving 16x16 cells on
 * an 18px pitch. Non-printable chars render as a space. Clipping is done here:
 * gen2_hgr_row does NOT bounds-check, so rows past line 191 are dropped and
 * pixel pairs past the 280px (40-byte) scanline are skipped rather than spilling
 * into adjacent memory. */
void gen2_hgr_puts(unsigned x, unsigned char y, const char *s)
{
    unsigned char c, col, bit;
    unsigned cx;

    gen2_build_tables();
    if (y > 176u) return;            /* a 16px-tall cell needs y+15 <= 191    */

    /* One division here establishes the pen's byte column / bit within the
     * scanline; the rest is division-free. The asm blitter walks each glyph,
     * and we step the pen by 18px (16px cell + 2px gap) WITHOUT dividing:
     * 18 = 2*7 + 4, so col += 2, bit += 4, normalising the bit once. */
    cx  = x;
    col = (unsigned char)(x / 7u);
    bit = (unsigned char)(x % 7u);
    gen2_g_y = y;

    while ((c = (unsigned char)*s++) != 0) {
        if (cx + 15u > 279u) break;          /* clip: the whole cell must fit  */
        if (c < 0x20 || c > 0x7F) c = 0x20;
        gen2_g_glyph = kBBFontAscii + (unsigned)(c - 0x20) * 8u;
        gen2_g_col   = col;
        gen2_g_mask  = (unsigned char)(1u << bit);
        gen2_blit_glyph();                   /* hand-written 6502 inner loop   */
        cx += 18u;
        bit += 4u; col += 2u;
        if (bit >= 7u) { bit -= 7u; ++col; }
    }
}

/* Draw a string in an NTSC artifact COLOUR (GEN2 HIRES has no per-pixel colour).
 * The glyph is drawn WHITE first, then gen2_colorize_asm masks each byte to the
 * colour's carrier (and sets the palette high bit for orange/blue):
 *   GEN2_VIOLET / GEN2_GREEN / GEN2_ORANGE / GEN2_BLUE  (see gen2.h).
 * Caveat: colour halves the horizontal carrier, so coloured text looks slightly
 * finer than white; and the box is tinted as a block, so keep coloured labels
 * clear of other content (don't overlap the white score, etc.). */
void gen2_hgr_puts_color(unsigned x, unsigned char y, const char *s, unsigned char color)
{
    unsigned char len = 0;
    unsigned right;
    const char *p = s;

    gen2_hgr_puts(x, y, s);                 /* 1) draw the glyphs white */
    if (y > 176u) return;
    while (*p++) ++len;
    if (len == 0u) return;

    /* Carrier + high bit per colour (verified against the GEN2 renderer):
     * violet even=$55 odd=$2A hi=0 ; green even=$2A odd=$55 hi=0 ;
     * orange = green|hi ; blue = violet|hi. */
    switch (color) {
        case GEN2_GREEN:  gen2_z_ce = 0x2Au; gen2_z_co = 0x55u; gen2_z_hi = 0x00u; break;
        case GEN2_ORANGE: gen2_z_ce = 0x2Au; gen2_z_co = 0x55u; gen2_z_hi = 0x80u; break;
        case GEN2_BLUE:   gen2_z_ce = 0x55u; gen2_z_co = 0x2Au; gen2_z_hi = 0x80u; break;
        case GEN2_VIOLET: gen2_z_ce = 0x55u; gen2_z_co = 0x2Au; gen2_z_hi = 0x00u; break;
        default: return;                    /* unknown / white -> leave as drawn */
    }

    /* Byte-column box covering the text (last glyph spans to x+(len-1)*18+15). */
    right = x + (unsigned)(len - 1u) * 18u + 15u;
    if (right > 279u) right = 279u;
    gen2_z_col0  = (unsigned char)(x / 7u);
    gen2_z_ncols = (unsigned char)(right / 7u - x / 7u + 1u);
    gen2_z_y0    = y;
    gen2_z_rows  = 16u;                      /* glyph cell height */
    gen2_colorize_asm();
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
