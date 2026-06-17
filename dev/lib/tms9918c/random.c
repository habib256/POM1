/*
 * apple1-videocard-lib — POM1 CodeTank cc65 port
 * Derived from nippur72/apple1-videocard-lib (Antonino "Nino" Porcino).
 *   https://github.com/nippur72/apple1-videocard-lib
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 *
 * random.c — see random.h.
 */
#include "random.h"

unsigned char rand8_state  = 0xACU;
unsigned int  rand16_state = 0xACE1U;

void srand8(unsigned char seed) {
    rand8_state = seed ? seed : 1U;
}

void srand16(unsigned int seed) {
    rand16_state = seed ? seed : 1U;
}

/* 8-bit LFSR: taps at bits 7,5,4,3 (x^8 + x^6 + x^5 + x^4 + 1). */
unsigned char rand8(void) {
    unsigned char s   = rand8_state;
    unsigned char lsb = (unsigned char)(s & 1U);
    s = (unsigned char)(s >> 1);
    if (lsb) {
        s = (unsigned char)(s ^ 0xB8U);
    }
    rand8_state = s;
    return s;
}

/* 16-bit Galois LFSR, polynomial 0xB400 (maximal period 65535). */
unsigned int rand16(void) {
    unsigned int s = rand16_state;
    unsigned char lsb = (unsigned char)(s & 1U);
    s >>= 1;
    if (lsb) {
        s ^= 0xB400U;
    }
    rand16_state = s;
    return s;
}

unsigned char rand8_below(unsigned char limit) {
    if (limit < 2U) {
        return 0U;
    }
    /* Wider modulo by intent — accept bias rather than rejection-sample, since
     * games here use limit in 2..32 where bias < 5%. */
    return (unsigned char)(rand8() % limit);
}
