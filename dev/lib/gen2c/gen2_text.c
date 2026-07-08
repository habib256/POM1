/* gen2_text.c — the glyph CORE: string rendering only.
 *
 * Routes through the asm gen2_puts_run (16x16 doubled glyphs) or
 * gen2_puts_run8 (native 8x8). Both consume the same Beautiful Boot font
 * (kBBFontAscii below) and the same gen2_t_* zero-page parameter block.
 *
 * The number formatters (putu / putu_field / puti / putx / putu8) live in
 * gen2_text_num.c — split out so a string-only program does not pay for them
 * NOR for what they pull (gen2_rect via putu_field's erase, gfx_num_hex via
 * putx, cc65 soft multiply). This file must never call into gen2_text_num.c;
 * the dependency direction is numbers -> glyph core only. */

#include "gen2.h"
#include "gen2_internal.h"

/* --- Beautiful Boot 8x8 font, ASCII 0x20-0x7F (96 glyphs).
 * gen2_bbfont.inc is GENERATED from the single source dev/lib/gen2/bbfont_cp437.inc
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
