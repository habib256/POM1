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

void pl_print_dec_u8(pl_putc_fn putc, unsigned char v) {
    unsigned char h = (unsigned char)(v / 100U);
    unsigned char r = (unsigned char)(v - h * 100U);
    unsigned char t = (unsigned char)(r / 10U);
    unsigned char u = (unsigned char)(r - t * 10U);
    unsigned char emit = 0;
    if (h != 0) {
        putc((unsigned char)('0' + h));
        emit = 1;
    }
    if (emit || t != 0) {
        putc((unsigned char)('0' + t));
    }
    putc((unsigned char)('0' + u));
}

void pl_print_dec_u16(pl_putc_fn putc, unsigned int v) {
    /* 65535 = 5 digits max. Build digits in reverse, then emit. */
    unsigned char digits[5];
    unsigned char n = 0;
    if (v == 0) {
        putc('0');
        return;
    }
    while (v != 0 && n < 5) {
        digits[n++] = (unsigned char)('0' + (v % 10U));
        v /= 10U;
    }
    while (n != 0) {
        putc(digits[--n]);
    }
}

static unsigned char nibble_to_hex(unsigned char n) {
    n &= 0x0FU;
    return (unsigned char)(n < 10U ? '0' + n : 'A' + (n - 10U));
}

void pl_print_hex_u8(pl_putc_fn putc, unsigned char v) {
    putc(nibble_to_hex((unsigned char)(v >> 4)));
    putc(nibble_to_hex(v));
}

void pl_print_hex_u16(pl_putc_fn putc, unsigned int v) {
    pl_print_hex_u8(putc, (unsigned char)(v >> 8));
    pl_print_hex_u8(putc, (unsigned char)v);
}

/* --- Wozmon wrappers --- */
void woz_print_dec_u8 (unsigned char v) { pl_print_dec_u8 (woz_putc, v); }
void woz_print_dec_u16(unsigned int  v) { pl_print_dec_u16(woz_putc, v); }
void woz_print_hex_u16(unsigned int  v) { pl_print_hex_u16(woz_putc, v); }

/* --- screen1 wrappers --- */
void screen1_print_dec_u8 (unsigned char v) { pl_print_dec_u8 (screen1_putc, v); }
void screen1_print_dec_u16(unsigned int  v) { pl_print_dec_u16(screen1_putc, v); }
void screen1_print_hex_u16(unsigned int  v) { pl_print_hex_u16(screen1_putc, v); }
