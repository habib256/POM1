/*
 * apple1-videocard-lib — POM1 CodeTank cc65 port
 * Derived from nippur72/apple1-videocard-lib (Antonino "Nino" Porcino).
 *   https://github.com/nippur72/apple1-videocard-lib
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 *
 * random.{c,h} — LFSR pseudo-random generators (deterministic, no /dev/urandom).
 *
 *   rand8  — 8-bit LFSR (period 255, x^8 + x^6 + x^5 + x^4 + 1).
 *   rand16 — 16-bit Galois LFSR (period 65535, polynomial 0xB400).
 *
 * Seeding: srand8 / srand16. The state is shared library-global; if you need
 * decorrelated streams, save/restore _rand8_state / _rand16_state yourself.
 */
#ifndef RANDOM_H
#define RANDOM_H

#include "utils.h"

/* Visible state — exposed for save/restore + tests. Don't write 0 to either:
 * an LFSR locks at zero. tms_shadow_init / rand16 calls treat 0 as "use the
 * default seed" anyway, but be explicit if you care. */
extern unsigned char rand8_state;
extern unsigned int  rand16_state;

void srand8(unsigned char seed);
void srand16(unsigned int seed);

unsigned char rand8(void);
unsigned int  rand16(void);

/* Uniform byte in [0..limit-1] (rejection-sampled, unbiased; no runtime
 * modulo). limit < 2 returns 0. */
unsigned char rand8_below(unsigned char limit);

#endif
