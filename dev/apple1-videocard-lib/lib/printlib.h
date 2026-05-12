/*
 * apple1-videocard-lib — POM1 CodeTank cc65 port
 * Derived from nippur72/apple1-videocard-lib (Antonino "Nino" Porcino).
 *   https://github.com/nippur72/apple1-videocard-lib
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 *
 * printlib.{c,h} — small decimal / hex print helpers.
 *
 * Every helper takes a `putc` function pointer so the same code works against
 * Wozmon ECHO, screen1, screen2, or a custom backend. Convenience wrappers
 * for woz_putc and screen1_putc are provided in printlib.c.
 *
 * No leading zeros, no width control: keep it tiny. Use printf-equivalents
 * from cc65 stdio only if you really need format strings.
 */
#ifndef PRINTLIB_H
#define PRINTLIB_H

#include "utils.h"

typedef void (*pl_putc_fn)(unsigned char c);

void pl_print_dec_u8 (pl_putc_fn putc, unsigned char  v);
void pl_print_dec_u16(pl_putc_fn putc, unsigned int   v);
void pl_print_hex_u8 (pl_putc_fn putc, unsigned char  v);
void pl_print_hex_u16(pl_putc_fn putc, unsigned int   v);

/* Wozmon-bound convenience wrappers. */
void woz_print_dec_u8 (unsigned char v);
void woz_print_dec_u16(unsigned int  v);
void woz_print_hex_u16(unsigned int  v);

/* screen1-bound (requires linking screen1.c). */
void screen1_print_dec_u8 (unsigned char v);
void screen1_print_dec_u16(unsigned int  v);
void screen1_print_hex_u16(unsigned int  v);

#endif
