/* gen2_text.c — text + decimal/hex/signed number rendering.
 *
 * Routes through the asm gen2_puts_run (16x16 doubled glyphs) or
 * gen2_puts_run8 (native 8x8). Both consume the same Beautiful Boot font
 * (kBBFontAscii below) and the same gen2_t_* zero-page parameter block. */

#include "gen2.h"
#include "gen2_internal.h"
#include "gfx.h"   /* gfx_hexstr for the uppercase / no-leading-zero hex string */

/* --- Beautiful Boot 8x8 font, ASCII 0x20-0x7F (96 glyphs).
 * gen2_bbfont.inc is GENERATED from the single source dev/lib/hgr/bbfont_cp437.inc
 * by tools/build_shared_font.py (re-run after editing the font) — so the C copy
 * can't drift from the asm table, and the TMS9918 side gets the same font from the
 * same master (bbfont_tms.inc). bit 0 = leftmost pixel, rows top->bottom; values
 * stay <= 0x7F so each glyph fits HGR's 7 px/byte. */
static const unsigned char kBBFontAscii[96u * 8u] = {
#include "gen2_bbfont.inc"
};

/* See note in gen2_rect.c: gen2_set_carrier is duplicated as static so a text-
 * only program does not pull gen2_rect.c, and a graphics-only program does not
 * pull gen2_text.c. */
static unsigned char gen2_set_carrier(unsigned char color)
{
    switch (color) {
        case GEN2_GREEN:  gen2_z_ce = 0x2Au; gen2_z_co = 0x55u; gen2_z_hi = 0x00u; break;
        case GEN2_ORANGE: gen2_z_ce = 0x2Au; gen2_z_co = 0x55u; gen2_z_hi = 0x80u; break;
        case GEN2_BLUE:   gen2_z_ce = 0x55u; gen2_z_co = 0x2Au; gen2_z_hi = 0x80u; break;
        case GEN2_VIOLET: gen2_z_ce = 0x55u; gen2_z_co = 0x2Au; gen2_z_hi = 0x00u; break;
        default: return 0u;
    }
    return 1u;
}

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

/* Draw an ASCII string at (x, y) in HIRES page 1, white and artifact-free:
 * every set pixel is doubled horizontally (>=2px run -> white, no NTSC colour
 * fringe) and each glyph row is drawn on two scanlines, giving 16x16 cells on
 * an 18px pitch. Non-printable chars render as a space. */
void gen2_hgr_puts(unsigned x, unsigned char y, const char *s)
{
    gen2_puts_common(x, y, s, 0u);
}

/* Draw a string in an NTSC artifact COLOUR (GEN2 HIRES has no per-pixel colour):
 *   GEN2_VIOLET / GEN2_GREEN / GEN2_ORANGE / GEN2_BLUE  (see gen2.h).
 * ONE pass: gen2_blit_glyph_color draws every glyph ALREADY tinted — it ORs only
 * the carrier bit for each pixel's column parity (+ the palette high bit), so the
 * artifact colour is produced as the glyph is laid down. */
void gen2_hgr_puts_color(unsigned x, unsigned char y, const char *s, unsigned char color)
{
    if (!gen2_set_carrier(color)) {         /* white / unknown -> plain white text */
        gen2_puts_common(x, y, s, 0u);
        return;
    }
    gen2_puts_common(x, y, s, 1u);          /* one tinted pass per glyph */
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
 * stale digits AND the wipe can't bite into an adjacent label. A value too wide
 * for the field overflows to the right (keep width >= the max digit count).
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
    /* Erase the field box: (width-1) full pitches + one 16px cell, 16px tall. */
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
    char buf[5];
    gfx_hexstr(buf, value);
    gen2_hgr_puts(x, y, buf);
}

/* Draw a string at the font's NATIVE 8x8 size (no pixel doubling) — half the
 * height/width of gen2_hgr_puts, so ~3-4x more text per line and faster to draw. */
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
