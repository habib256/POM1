/* gen2_text_num.c — decimal/hex/signed NUMBER rendering on top of the glyph
 * core (gen2_text.c).
 *
 * Split out of gen2_text.c (juillet 2026, "familles grasses" pass) so a
 * string-only program stops paying for the formatters and — crucially — for
 * what they PULL: gen2_hgr_putu_field's erase drags gen2_rect.o (~700 B),
 * gen2_hgr_putx drags gfx_num_hex.o, and the field math drags cc65's soft
 * multiply. Measured on the text-only smoke sketch: ~1 KB back. The archive
 * link (rt.lib) makes the split effective automatically; Makefile consumers
 * keep listing GEN2C_TEXT_SRCS, which now names both files (same bytes as
 * before for a direct-object link — no regression, no gain).
 *
 * Everything here funnels through gen2_hgr_puts / gen2_hgr_puts8, so this
 * module pulling gen2_text.o is the intended dependency direction; the glyph
 * core must never call back into this file. */

#include "gen2.h"
#include "gen2_internal.h"
#include "gfx.h"   /* gfx_hexstr for the uppercase / no-leading-zero hex string */

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
