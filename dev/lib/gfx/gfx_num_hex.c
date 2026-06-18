/*
 * gfx_num_hex.c — unsigned hex -> ASCII (gfx_hexstr). See gfx.h.
 *
 * Split from gfx_num.c so a HEX-only program skips gfx_num_dec.c and the
 * cc65 16-bit soft-divide it pulls. Hex itself uses only shifts + masks.
 *
 * Output: uppercase, no leading zeros, 1..4 digits, NUL-terminated.
 */
#include "gfx.h"

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
