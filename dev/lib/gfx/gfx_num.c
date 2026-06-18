/*
 * gfx_num.c — card-neutral integer -> ASCII string builders. See gfx.h.
 *
 * One implementation of the binary->decimal / binary->hex conversion that was
 * previously written THREE times:
 *   - dev/lib/gen2c/gen2_blit.s  gen2_utoa   (asm, decimal)
 *   - dev/lib/gen2c/gen2_text.c  gen2_hgr_putx (hex builder, inline)
 *   - dev/lib/tms9918c/printlib.c  pl_print_dec_* / pl_print_hex_*
 *
 * These build a NUL-terminated string; drawing it is each card's job (the
 * text-positioning model differs — see gfx.h / README). Logic ported from
 * printlib.c (decimal) and gen2_hgr_putx (hex), so output is byte-identical.
 *
 * cc65 note: the / and % by the constant 10 below compile to cc65's runtime
 * 16-bit divide. That is fine for the general path; GEN2's hot HUD primitive
 * (gen2_hgr_putu_field) deliberately keeps its hand-written asm gen2_utoa, which
 * avoids the soft-divide — do NOT route that one through here (see README).
 */
#include "gfx.h"

unsigned char gfx_utoa(char *buf, unsigned value)
{
    /* Build digits in reverse into a scratch, then copy out in order — same
     * shape as printlib pl_print_dec_u16 (max 65535 = 5 digits). */
    unsigned char digits[5];
    unsigned char n = 0u;
    unsigned char i;

    if (value == 0u) {
        buf[0] = '0';
        buf[1] = 0;
        return 1u;
    }
    while (value != 0u && n < 5u) {
        digits[n++] = (unsigned char)('0' + (value % 10u));
        value /= 10u;
    }
    for (i = 0u; i < n; ++i)
        buf[i] = (char)digits[n - 1u - i];
    buf[n] = 0;
    return n;
}

unsigned char gfx_itoa(char *buf, int value)
{
    unsigned mag;
    if (value < 0) {
        buf[0] = '-';
        mag = (unsigned)(-value);            /* -32768 -> 32768 wraps correctly */
        return (unsigned char)(1u + gfx_utoa(buf + 1, mag));
    }
    return gfx_utoa(buf, (unsigned)value);
}

unsigned char gfx_hexstr(char *buf, unsigned value)
{
    static const char hexd[16] = {
        '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'
    };
    unsigned char i = 0u, started = 0u, nyb;
    signed char sh;
    for (sh = 12; sh >= 0; sh = (signed char)(sh - 4)) {
        nyb = (unsigned char)((value >> sh) & 0x0Fu);
        if (nyb != 0u || started || sh == 0) { buf[i++] = hexd[nyb]; started = 1u; }
    }
    buf[i] = 0;
    return i;
}
