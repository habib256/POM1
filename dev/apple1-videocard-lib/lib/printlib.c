/*
 * apple1-videocard-lib — POM1 CodeTank cc65 port
 * Derived from nippur72/apple1-videocard-lib (Antonino "Nino" Porcino).
 *   https://github.com/nippur72/apple1-videocard-lib
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 *
 * printlib.c — see printlib.h.
 */
#include "printlib.h"
#include "apple1.h"
#include "screen1.h"
#include "gfx.h"   /* shared int->ASCII builders (dev/lib/gfx); link gfx-tms.lib */

/* These now build the digits with the card-neutral gfx_utoa / gfx_hexstr (the
 * de-dup: gfx_num.c is the single implementation, replacing the per-card copies)
 * and emit the result through the caller's pl_putc_fn. Output matches printlib's
 * documented contract — "no leading zeros, no width control" (printlib.h) — so
 * hex is now minimal-width, like the decimal path always was. */
static void pl_emit(pl_putc_fn putc, const char *s) {
    while (*s != 0) {
        putc((unsigned char)*s++);
    }
}

void pl_print_dec_u8(pl_putc_fn putc, unsigned char v) {
    char buf[6];
    gfx_utoa(buf, v);
    pl_emit(putc, buf);
}

void pl_print_dec_u16(pl_putc_fn putc, unsigned int v) {
    char buf[6];
    gfx_utoa(buf, v);
    pl_emit(putc, buf);
}

void pl_print_hex_u8(pl_putc_fn putc, unsigned char v) {
    char buf[5];
    gfx_hexstr(buf, v);
    pl_emit(putc, buf);
}

void pl_print_hex_u16(pl_putc_fn putc, unsigned int v) {
    char buf[5];
    gfx_hexstr(buf, v);
    pl_emit(putc, buf);
}

/* --- Wozmon wrappers --- */
void woz_print_dec_u8 (unsigned char v) { pl_print_dec_u8 (woz_putc, v); }
void woz_print_dec_u16(unsigned int  v) { pl_print_dec_u16(woz_putc, v); }
void woz_print_hex_u16(unsigned int  v) { pl_print_hex_u16(woz_putc, v); }

/* --- screen1 wrappers --- */
void screen1_print_dec_u8 (unsigned char v) { pl_print_dec_u8 (screen1_putc, v); }
void screen1_print_dec_u16(unsigned int  v) { pl_print_dec_u16(screen1_putc, v); }
void screen1_print_hex_u16(unsigned int  v) { pl_print_hex_u16(screen1_putc, v); }
