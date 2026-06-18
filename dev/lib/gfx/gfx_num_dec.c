/*
 * gfx_num_dec.c — unsigned/signed decimal -> ASCII (gfx_utoa, gfx_itoa).
 * See gfx.h.
 *
 * Split from gfx_num.c so a HEX-only program (e.g. a HUD that only shows
 * `$04AC` style addresses via gfx_hexstr) skips this TU and the cc65
 * runtime 16-bit soft-divide (`udiv16`) it pulls via `value / 10u` /
 * `value % 10u` below. That divide alone is ~250 bytes ROM on cc65.
 *
 * GEN2's hot HUD primitive (gen2_hgr_putu_field) deliberately keeps its
 * hand-written asm gen2_utoa, which avoids the soft-divide — do NOT route
 * that one through here (see README).
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
