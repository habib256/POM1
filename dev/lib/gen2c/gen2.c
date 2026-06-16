/* gen2.c — minimal C runtime for Uncle Bernie's GEN2 HGR. See gen2.h. */
#include "gen2.h"

/* Sink for the individual soft-switch macros. A soft-switch READ is the toggle;
 * its value is meaningless. cc65 -Oirs drops a volatile read cast to void, so
 * the macros store the read into this sink — a store cc65 cannot prove dead, so
 * the (volatile) load that feeds it is always emitted. The whole-mode init
 * routines gen2_hgr_init / gen2_lores_init live in asm (gen2_blit.s) for the
 * same reason; see the note there and gen2.h. */
volatile unsigned char gen2_ss_sink;

/* gen2_hgr_init / gen2_lores_init are now in asm (gen2_blit.s) — a void-cast
 * volatile read of a soft switch is silently elided by cc65, so the old C
 * versions compiled to an empty rts and never actually set the mode. */

/* gen2_hgr_clear(fill) is implemented in asm (gen2_blit.s _gen2_hgr_clear): a
 * tight 32-page STA (ptr),Y / INY / BNE fill of $2000-$3FFF, fill byte in A. */

/* HIRES draw-page high-byte base: $20 = page 1 ($2000), $40 = page 2 ($4000).
 * GLOBAL (not static) on purpose: the asm _gen2_hgr_clear imports _gen2_hgr_base
 * so it erases whichever page is currently being drawn. Set by
 * gen2_set_draw_page; defaults to page 1. */
unsigned char gen2_hgr_base = 0x20u;

/* Apple II interleaved HIRES scanline base, in the CURRENT draw page:
   (gen2_hgr_base<<8) + (y&7)*$400 + ((y>>3)&7)*$80 + (y>>6)*40
   gen2_hgr_base=$20 gives the classic $2000 page-1 layout; $40 is page 2. */
unsigned char *gen2_hgr_row(unsigned char y)
{
    return (unsigned char *)(((unsigned)gen2_hgr_base << 8)
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
extern void gen2_blit_glyph_color(void);/* asm: same, but tinted (reads gen2_z_ce/co/hi) */
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
/* gen2_puts string-renderer parameter block (zero page, gen2_blit.s). */
extern unsigned char gen2_t_col, gen2_t_bit, gen2_t_n, gen2_t_color;
extern const unsigned char *gen2_t_s;   /* string pointer                          */
extern const unsigned char *gen2_t_font;/* glyph table base (kBBFontAscii)         */
extern void gen2_puts_run(void);        /* asm: draw the whole string in one loop  */
extern void gen2_puts_run8(void);       /* asm: same, NATIVE 8x8 (no pixel doubling)*/
/* gen2_utoa (16-bit unsigned -> decimal ASCII) parameter block. */
extern unsigned char gen2_u_lo, gen2_u_hi;
extern char *gen2_u_ptr;
extern void gen2_utoa(void);            /* asm: bin->dec, no cc65 software /10      */
/* gen2_hgr_blit (1bpp MSB-first sprite, SET/CLEAR/XOR) parameter block. */
extern unsigned char gen2_b_col, gen2_b_mask, gen2_b_w, gen2_b_h;
extern unsigned char gen2_b_stride, gen2_b_y, gen2_b_mode;
extern const unsigned char *gen2_b_src;
extern void gen2_blit_run(void);        /* asm: pixel-walk blit, mode-aware         */
extern void gen2_blit7_run(void);       /* asm: byte-aligned 7px/byte blit (fast)   */
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
#pragma zpsym("gen2_t_col")
#pragma zpsym("gen2_t_bit")
#pragma zpsym("gen2_t_n")
#pragma zpsym("gen2_t_color")
#pragma zpsym("gen2_t_s")
#pragma zpsym("gen2_t_font")
#pragma zpsym("gen2_u_lo")
#pragma zpsym("gen2_u_hi")
#pragma zpsym("gen2_u_ptr")
#pragma zpsym("gen2_b_col")
#pragma zpsym("gen2_b_mask")
#pragma zpsym("gen2_b_w")
#pragma zpsym("gen2_b_h")
#pragma zpsym("gen2_b_stride")
#pragma zpsym("gen2_b_y")
#pragma zpsym("gen2_b_mode")
#pragma zpsym("gen2_b_src")

/* Build the lookup tables once (all the asm fast paths read them): the
 * non-linear Apple II HIRES scanline bases (gen2_rowlo/gen2_rowhi) and the
 * x->(byte column, bit mask) tables that replace per-pixel division. Built
 * division-free: walk x and carry the column/bit, wrapping every 7 pixels. */
/* (Re)derive the HIRES scanline-base tables for the current gen2_hgr_base. The
 * low bytes are page-independent; the high bytes carry the page. Cheap enough to
 * redo on a page flip — gen2_set_draw_page calls this when the draw page moves. */
static void gen2_fill_rows(void)
{
    unsigned y;
    for (y = 0; y < 192u; ++y) {
        unsigned a = (unsigned)gen2_hgr_row((unsigned char)y);
        gen2_rowlo[y] = (unsigned char)(a & 0xFFu);
        gen2_rowhi[y] = (unsigned char)(a >> 8);
    }
}

static unsigned char gen2_tables_ready = 0;
static void gen2_build_tables(void)
{
    unsigned x;
    unsigned char col, bit;
    if (gen2_tables_ready) return;
    gen2_fill_rows();
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
/* Shared body for gen2_hgr_puts (white) and gen2_hgr_puts_color (tinted): one
 * division sets the pen's byte column / bit, then the WHOLE string is rendered by
 * a single asm loop (gen2_blit.s _gen2_puts_run) — no per-character C overhead.
 * colorMode 0 = white blitter, 1 = colour blitter (carrier already in gen2_z_*).
 * Clipping matches the old per-char test (cx+15 <= 279): the leftmost 16px cell
 * fits only if x <= 264, and (264-x)/18 + 1 cells then fit — precomputed here so
 * the asm loop needs no 16-bit clip. The blitter still drops rows past 191 and
 * skips pixel pairs past the 280px scanline. */
static void gen2_puts_common(unsigned x, unsigned char y, const char *s,
                             unsigned char colorMode)
{
    gen2_build_tables();
    if (y > 176u || x > 264u) return;        /* cell needs y+15<=191 and a fitting x */
    gen2_g_y     = y;
    gen2_t_col   = (unsigned char)(x / 7u);
    gen2_t_bit   = (unsigned char)(x % 7u);
    gen2_t_n     = (unsigned char)((264u - x) / 18u + 1u);  /* glyph cells that fit */
    gen2_t_s     = (const unsigned char *)s;
    gen2_t_font  = kBBFontAscii;
    gen2_t_color = colorMode;
    gen2_puts_run();
}

void gen2_hgr_puts(unsigned x, unsigned char y, const char *s)
{
    gen2_puts_common(x, y, s, 0u);
}

/* Map a GEN2_* colour constant onto the colorize carrier parameter block
 * (carrier bytes + palette high bit, verified against the GEN2 renderer):
 *   violet even=$55 odd=$2A hi=0 ; green even=$2A odd=$55 hi=0 ;
 *   orange = green|hi ; blue = violet|hi.
 * Returns 1 for a known artifact colour, 0 for white/unknown (caller then leaves
 * the pixels white). Shared by gen2_hgr_puts_color (text) and gen2_hgr_colorize
 * (graphics) so both stay in lock-step. */
static unsigned char gen2_set_carrier(unsigned char color)
{
    switch (color) {
        case GEN2_GREEN:  gen2_z_ce = 0x2Au; gen2_z_co = 0x55u; gen2_z_hi = 0x00u; break;
        case GEN2_ORANGE: gen2_z_ce = 0x2Au; gen2_z_co = 0x55u; gen2_z_hi = 0x80u; break;
        case GEN2_BLUE:   gen2_z_ce = 0x55u; gen2_z_co = 0x2Au; gen2_z_hi = 0x80u; break;
        case GEN2_VIOLET: gen2_z_ce = 0x55u; gen2_z_co = 0x2Au; gen2_z_hi = 0x00u; break;
        default: return 0u;                 /* unknown / white -> leave as drawn */
    }
    return 1u;
}

/* Draw a string in an NTSC artifact COLOUR (GEN2 HIRES has no per-pixel colour):
 *   GEN2_VIOLET / GEN2_GREEN / GEN2_ORANGE / GEN2_BLUE  (see gen2.h).
 * ONE pass: gen2_blit_glyph_color draws every glyph ALREADY tinted — it ORs only
 * the carrier bit for each pixel's column parity (+ the palette high bit), so the
 * artifact colour is produced as the glyph is laid down. No white-then-recolorize
 * round trip, and — unlike the old box colorize — nothing outside the glyph is
 * touched, so coloured labels may sit right next to other content without bleeding
 * a tint over it. (Layout/clipping identical to gen2_hgr_puts; an unknown colour
 * falls back to plain white.) */
void gen2_hgr_puts_color(unsigned x, unsigned char y, const char *s, unsigned char color)
{
    if (!gen2_set_carrier(color)) {         /* white / unknown -> plain white text */
        gen2_puts_common(x, y, s, 0u);
        return;
    }
    gen2_puts_common(x, y, s, 1u);          /* one tinted pass per glyph */
}

/* Tint an arbitrary PIXEL rectangle to one of the four artifact colours — the
 * graphics analogue of gen2_hgr_puts_color (for food / sprites / bars, not text).
 * Draw the shape WHITE first, then call this to recolour it. x,w,y,h are pixels;
 * the covering byte-column box [x..x+w) is tinted (HIRES colour is byte-granular,
 * so keep ~1 empty cell between differently-coloured shapes). Black pixels in the
 * box stay black (only the palette bit flips), so an isolated shape tints clean. */
void gen2_hgr_colorize(unsigned x, unsigned char y, unsigned char w,
                       unsigned char h, unsigned char color)
{
    unsigned right;

    if (w == 0u || h == 0u || y > 191u) return;
    if (!gen2_set_carrier(color)) return;
    if ((unsigned)y + h > 192u) h = (unsigned char)(192u - y);

    right = x + (unsigned)w - 1u;
    if (right > 279u) right = 279u;
    gen2_z_col0  = (unsigned char)(x / 7u);
    gen2_z_ncols = (unsigned char)(right / 7u - x / 7u + 1u);
    gen2_z_y0    = y;
    gen2_z_rows  = h;
    gen2_colorize_asm();
}

/* Unsigned decimal at (x, y) via gen2_hgr_puts. Builds the digits right-to-left
 * into a 6-byte buffer (max 65535 = 5 digits + NUL), no leading zeros. */
void gen2_hgr_putu(unsigned x, unsigned char y, unsigned value)
{
    char buf[6];                     /* up to 5 digits + NUL */
    gen2_u_lo  = (unsigned char)(value & 0xFFu);
    gen2_u_hi  = (unsigned char)(value >> 8);
    gen2_u_ptr = buf;
    gen2_utoa();                     /* asm bin->dec (no cc65 software /10 + %10) */
    gen2_hgr_puts(x, y, buf);
}

/* strlen for the small NUL-terminated number buffers below. */
static unsigned char gen2_slen(const char *s)
{
    unsigned char n = 0u;
    while (s[n]) ++n;
    return n;
}

/* Fixed-width, right-aligned unsigned decimal — the flicker-free HUD primitive.
 * The field is `width` glyph cells (18px pitch) wide; this ERASES exactly that
 * box first, then draws the digits flush-right inside it. Because the erase is
 * bounded to the field the function owns, a shrinking value (123 -> 9) leaves no
 * stale digits AND the wipe can't bite into an adjacent label — the failure mode
 * of a hand-rolled clear_pixrect + redraw (the HGR SNAKE score bug). A value too
 * wide for the field overflows to the right (keep width >= the max digit count).
 * width is clamped to 1..14 (a 14-cell field already fills the 280px line). */
void gen2_hgr_putu_field(unsigned x, unsigned char y, unsigned value,
                         unsigned char width)
{
    char buf[6];
    unsigned char len;
    unsigned dx;
    if (width == 0u) return;
    if (width > 14u) width = 14u;
    gen2_u_lo  = (unsigned char)(value & 0xFFu);
    gen2_u_hi  = (unsigned char)(value >> 8);
    gen2_u_ptr = buf;
    gen2_utoa();
    len = gen2_slen(buf);
    /* Erase the field box: (width-1) full pitches + one 16px cell, 16px tall.
     * Page-aware (clear_pixrect routes through the rebuilt scanline tables). */
    gen2_hgr_clear_pixrect(x, y,
                           (unsigned char)((unsigned)(width - 1u) * 18u + 16u), 16u);
    /* Flush-right: blank the leading (width-len) cells; overflow if len>=width. */
    dx = (len < width) ? (x + (unsigned)(width - len) * 18u) : x;
    gen2_hgr_puts(dx, y, buf);
}

/* Signed decimal at (x, y): a leading '-' then the magnitude (OR-drawn like
 * gen2_hgr_putu — transparent, no field erase). -32768 prints correctly. */
void gen2_hgr_puti(unsigned x, unsigned char y, int value)
{
    char buf[7];                     /* '-' + up to 5 digits + NUL */
    char *dst = buf;
    unsigned mag;
    if (value < 0) { buf[0] = '-'; dst = buf + 1; mag = (unsigned)(-value); }
    else           { mag = (unsigned)value; }
    gen2_u_lo  = (unsigned char)(mag & 0xFFu);
    gen2_u_hi  = (unsigned char)(mag >> 8);
    gen2_u_ptr = dst;
    gen2_utoa();
    gen2_hgr_puts(x, y, buf);
}

/* Unsigned hexadecimal at (x, y), uppercase, no leading zeros (1-4 digits).
 * OR-drawn like gen2_hgr_putu. Handy for addresses / bit masks in a HUD. */
void gen2_hgr_putx(unsigned x, unsigned char y, unsigned value)
{
    static const char hexd[16] = {
        '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'
    };
    char buf[5];
    unsigned char i = 0u, started = 0u, nyb;
    signed char sh;
    for (sh = 12; sh >= 0; sh = (signed char)(sh - 4)) {
        nyb = (unsigned char)((value >> sh) & 0x0Fu);
        if (nyb != 0u || started || sh == 0) { buf[i++] = hexd[nyb]; started = 1u; }
    }
    buf[i] = 0;
    gen2_hgr_puts(x, y, buf);
}

/* Draw a string at the font's NATIVE 8x8 size (no pixel doubling) — half the
 * height/width of gen2_hgr_puts, so ~3-4x more text per line and faster to draw.
 * Same Beautiful Boot font; each glyph is a 7px cell on an 8px pitch (the 8th
 * column is the inter-char gap). Renders white into HIRES page 1. Clipping: the
 * cell is 8px tall (y<=184) and the leftmost glyph spans 7px (x<=273). */
void gen2_hgr_puts8(unsigned x, unsigned char y, const char *s)
{
    gen2_build_tables();
    if (y > 184u || x > 273u) return;
    gen2_g_y    = y;
    gen2_t_col  = (unsigned char)(x / 7u);
    gen2_t_bit  = (unsigned char)(x % 7u);
    gen2_t_n    = (unsigned char)((273u - x) / 8u + 1u);  /* 8x8 cells that fit */
    gen2_t_s    = (const unsigned char *)s;
    gen2_t_font = kBBFontAscii;
    gen2_puts_run8();
}

/* Unsigned decimal at the native 8x8 size (the small-font twin of gen2_hgr_putu).
 * Handy for dense HUDs where the 16x16 digits are too big. */
void gen2_hgr_putu8(unsigned x, unsigned char y, unsigned value)
{
    char buf[6];
    gen2_u_lo  = (unsigned char)(value & 0xFFu);
    gen2_u_hi  = (unsigned char)(value >> 8);
    gen2_u_ptr = buf;
    gen2_utoa();
    gen2_hgr_puts8(x, y, buf);
}

/* ===========================================================================
 * Vector primitives — line / box / circle (white, HIRES)
 * ===========================================================================
 * Built on the asm fast paths: hline/vline route through gen2_hgr_fill_pixrect
 * (a 1px-thin pixel rectangle — one tight STA run per scanline), and line/circle
 * walk gen2_hgr_plot (the col7/mask7 LUT plot, no per-pixel division). All take
 * inclusive pixel endpoints (x:0..279, y:0..191), matching the LORES hlin/vlin
 * convention; everything is clipped to the screen. */

/* Horizontal run from (x0..x1) inclusive on scanline y. Splits into <=255px
 * chunks because gen2_hgr_fill_pixrect's width is a single byte (a full 280px
 * line would otherwise wrap). */
void gen2_hgr_hline(unsigned x0, unsigned x1, unsigned char y)
{
    unsigned len;
    if (x1 < x0) { unsigned t = x0; x0 = x1; x1 = t; }
    if (x0 > 279u || y > 191u) return;
    if (x1 > 279u) x1 = 279u;
    len = x1 - x0 + 1u;                          /* 1..280 */
    while (len) {
        unsigned char chunk = (len > 255u) ? 255u : (unsigned char)len;
        gen2_hgr_fill_pixrect(x0, y, chunk, 1u);
        x0  += chunk;
        len -= chunk;
    }
}

/* Vertical run from (y0..y1) inclusive at column x. */
void gen2_hgr_vline(unsigned x, unsigned char y0, unsigned char y1)
{
    if (y1 < y0) { unsigned char t = y0; y0 = y1; y1 = t; }
    if (x > 279u || y0 > 191u) return;
    if (y1 > 191u) y1 = 191u;
    gen2_hgr_fill_pixrect(x, y0, 1u, (unsigned char)(y1 - y0 + 1u));
}

/* Bresenham line between two endpoints (both drawn). Pure horizontal / vertical
 * lines shortcut to the hline/vline fast paths; the diagonal case walks plot.
 * Stays within the endpoints' bounding box, so no point ever leaves the screen. */
void gen2_hgr_line(unsigned x0, unsigned char y0, unsigned x1, unsigned char y1)
{
    int x, y, xe, ye, dx, dy, sx, sy, err, e2;
    if (y0 == y1) { gen2_hgr_hline(x0, x1, y0); return; }
    if (x0 == x1) { gen2_hgr_vline(x0, y0, y1); return; }
    x = (int)x0; y = (int)y0; xe = (int)x1; ye = (int)y1;
    dx = xe - x; if (dx < 0) { dx = -dx; sx = -1; } else sx = 1;
    dy = ye - y; if (dy < 0) { dy = -dy; sy = -1; } else sy = 1;
    err = dx - dy;
    for (;;) {
        gen2_hgr_plot((unsigned)x, (unsigned char)y);
        if (x == xe && y == ye) break;
        e2 = err + err;                          /* 2*err */
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 <  dx) { err += dx; y += sy; }
    }
}

/* Rectangle OUTLINE through opposite corners (inclusive). The interior is left
 * untouched — fill with gen2_hgr_fill_pixrect if you want it solid. */
void gen2_hgr_rect(unsigned x0, unsigned char y0, unsigned x1, unsigned char y1)
{
    if (x1 < x0) { unsigned t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { unsigned char t = y0; y0 = y1; y1 = t; }
    gen2_hgr_hline(x0, x1, y0);
    gen2_hgr_hline(x0, x1, y1);
    gen2_hgr_vline(x0, y0, y1);
    gen2_hgr_vline(x1, y0, y1);
}

/* Plot (x, y) only if on-screen — int coords so a centre±radius that runs off an
 * edge is dropped, not wrapped (a uchar cast of a negative y would alias into a
 * valid row). Used by the circle's 8-way symmetry. */
static void gen2_hgr_plot_clip(int x, int y)
{
    if (x >= 0 && x <= 279 && y >= 0 && y <= 191)
        gen2_hgr_plot((unsigned)x, (unsigned char)y);
}

/* Midpoint (Bresenham) circle outline, centre (xc, yc), radius r. The 8 octant-
 * symmetric points are plotted per step; parts off-screen are clipped. */
void gen2_hgr_circle(unsigned xc, unsigned char yc, unsigned char r)
{
    int cx = (int)xc, cy = (int)yc;
    int x = 0, y = (int)r;
    int d = 1 - (int)r;
    while (x <= y) {
        gen2_hgr_plot_clip(cx + x, cy + y);
        gen2_hgr_plot_clip(cx - x, cy + y);
        gen2_hgr_plot_clip(cx + x, cy - y);
        gen2_hgr_plot_clip(cx - x, cy - y);
        gen2_hgr_plot_clip(cx + y, cy + x);
        gen2_hgr_plot_clip(cx - y, cy + x);
        gen2_hgr_plot_clip(cx + y, cy - x);
        gen2_hgr_plot_clip(cx - y, cy - x);
        if (d < 0) { d += (x << 1) + 3; }
        else       { d += ((x - y) << 1) + 5; --y; }
        ++x;
    }
}

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
 * straight in — ~7x fewer memory ops than gen2_hgr_blit for a solid sprite, the
 * way to move big shapes (a 48px ball) at speed. The trade-off: the sprite snaps
 * to a 7px column grid (x is floored to x/7), so move it in steps of 7 to stay
 * put under an XOR erase. `wbytes` = source bytes per row (ceil(width/7)); `mode`
 * is GEN2_SET/CLEAR/XOR. Clipped to the right/bottom edges. Build the packed
 * bitmap with bit k of byte j = pixel (j*7+k). */
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

/* ===========================================================================
 * LORES — 40×48 blocks of 16 real colours. See gen2.h for the model.
 * ===========================================================================
 * LORES lives in the TEXT page ($0400), Apple II row interleave, two stacked
 * blocks per byte (low nibble = upper / even block-row, high = lower / odd).
 * The grid is tiny (40×48 = 1920 blocks), and the addressing is pure shifts +
 * masks (no x/7 per-pixel division as HIRES needs), so plain C is fast enough —
 * no asm fast path. A 24-entry text-row base table built once turns every
 * block access into a table lookup + add. gen2_lores_init() is in asm
 * (gen2_blit.s) — see the note at the top of this file. */

/* Base address of each of the 24 text rows in page 1 (NOT exported — no asm
 * reads these, unlike the HIRES gen2_rowlo/hi). Built on first LORES call. */
static unsigned char gen2_lo_rowlo[24];
static unsigned char gen2_lo_rowhi[24];
static unsigned char gen2_lo_ready = 0;
/* LORES/TEXT draw-page high-byte base: $04 = page 1 ($0400), $08 = page 2
 * ($0800). Set by gen2_set_draw_page; defaults to page 1. */
static unsigned char gen2_lo_base = 0x04u;

static void gen2_lores_build(void)
{
    unsigned r, a;
    if (gen2_lo_ready) return;
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
    unsigned base = (unsigned)gen2_lo_base << 8;
    unsigned char *p0 = (unsigned char *)(base);
    unsigned char *p1 = (unsigned char *)(base + 0x100u);
    unsigned char *p2 = (unsigned char *)(base + 0x200u);
    unsigned char *p3 = (unsigned char *)(base + 0x300u);
    unsigned char i = 0;
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

/* ===========================================================================
 * Double buffering — draw page vs display page (PAGE2)
 * ===========================================================================
 * The card has two framebuffers: page 1 (HIRES $2000 / LORES $0400) and page 2
 * (HIRES $4000 / LORES $0800). gen2_set_draw_page picks where the drawing
 * primitives WRITE; gen2_show_page flips the card to DISPLAY the current draw
 * page (the $C254/$C255 soft switch). Tear-free animation = always draw into the
 * hidden page, then show it:
 *     unsigned char draw = 2;
 *     for (;;) {
 *         gen2_set_draw_page(draw);
 *         ... render the whole frame into the hidden page ...
 *         gen2_show_page();                  // the freshly drawn page goes live
 *         draw = (draw == 1) ? 2 : 1;
 *     }
 * Both pages share the same mode — pick it once (gen2_hgr_init / gen2_lores_init,
 * which also display PAGE1). All primitives (HIRES + LORES) follow the draw page,
 * because they read the gen2_rowlo/hi (HIRES) and gen2_lo_rowlo/hi (LORES)
 * scanline tables this re-derives, and the asm gen2_hgr_clear reads gen2_hgr_base
 * directly. The per-pixel hot paths are untouched — the page only costs a table
 * refresh at the flip, never a cycle per pixel. */
static unsigned char gen2_draw_page = 1u;

void gen2_set_draw_page(unsigned char page)
{
    unsigned char newh, newl, i;
    page = (page == 2u) ? 2u : 1u;
    newh = (page == 2u) ? 0x40u : 0x20u;            /* HIRES $4000 / $2000 */
    newl = (page == 2u) ? 0x08u : 0x04u;            /* LORES $0800 / $0400 */
    /* Ensure the page-1 tables exist (first call builds col7/mask7 + both row
     * tables; later calls are guarded no-ops). */
    gen2_build_tables();
    gen2_lores_build();
    gen2_draw_page = page;
    /* Flip pages by SHIFTING the existing scanline-base HIGH bytes by the page
     * delta — the low bytes are page-independent, so this is a few hundred byte
     * adds instead of recomputing every base from the interleave formula
     * (multiplies and all). Cheap enough to call every frame; a no-op when the
     * page is unchanged. Wrap-around makes the -$20/-$04 case work in uchar. */
    if (newh != gen2_hgr_base) {
        unsigned char dh = (unsigned char)(newh - gen2_hgr_base);
        for (i = 0u; i < 192u; ++i)
            gen2_rowhi[i] = (unsigned char)(gen2_rowhi[i] + dh);
        gen2_hgr_base = newh;
    }
    if (newl != gen2_lo_base) {
        unsigned char dl = (unsigned char)(newl - gen2_lo_base);
        for (i = 0u; i < 24u; ++i)
            gen2_lo_rowhi[i] = (unsigned char)(gen2_lo_rowhi[i] + dl);
        gen2_lo_base = newl;
    }
}

void gen2_show_page(void)
{
    if (gen2_draw_page == 2u) gen2_page2();
    else                      gen2_page1();
}
