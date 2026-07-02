/*
 * P-LAB TMS9918 (Apple-1) — cc65 C runtime, POM1 CodeTank port
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: derived from Antonino "Nino" Porcino's apple1-videocard-lib
 *   (https://github.com/nippur72/apple1-videocard-lib).
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 *
 * random.c — see random.h.
 */
#include "random.h"

/* BSS, deliberately NOT initialized: the cc65 `-t none` crt0 has NO
 * copydata, so DATA-segment initializers are NEVER copied to RAM (load =
 * ROM, run = RAM) — the old `= 0xAC` initializers silently left the LFSRs
 * holding power-on garbage, and a garbage value of 0 ZERO-LOCKS both LFSRs
 * (state 0 shifts to 0 forever). BSS *is* zeroed by crt0's zerobss on every
 * entry, so the states start at a known 0 and rand8()/rand16() auto-seed
 * from it below. Call srand8()/srand16() for a specific sequence. */
unsigned char rand8_state;
unsigned int  rand16_state;

void srand8(unsigned char seed) {
    rand8_state = seed ? seed : 1U;
}

void srand16(unsigned int seed) {
    rand16_state = seed ? seed : 1U;
}

/* 8-bit LFSR: taps at bits 7,5,4,3 (x^8 + x^6 + x^5 + x^4 + 1). */
unsigned char rand8(void) {
    unsigned char s   = rand8_state;
    unsigned char lsb;
    if (s == 0U) {
        s = 0xACU;   /* auto-seed: zero state would lock the LFSR at 0 */
    }
    lsb = (unsigned char)(s & 1U);
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
    unsigned char lsb;
    if (s == 0U) {
        s = 0xACE1U; /* auto-seed: zero state would lock the LFSR at 0 */
    }
    lsb = (unsigned char)(s & 1U);
    s >>= 1;
    if (lsb) {
        s ^= 0xB400U;
    }
    rand16_state = s;
    return s;
}

unsigned char rand8_below(unsigned char limit) {
    unsigned char mask, r;
    if (limit < 2U) {
        return 0U;
    }
    /* Rejection-sample against the smallest (2^k - 1) >= limit-1, using only
     * shift / AND / compare. This avoids cc65's runtime modulo helper (the
     * kind of hidden code-size cost the Apple-1 budget rule rules out) and is
     * unbiased; expected iterations < 2 for the limit<=32 range these games
     * use. */
    mask = (unsigned char)(limit - 1U);
    mask |= (unsigned char)(mask >> 1);
    mask |= (unsigned char)(mask >> 2);
    mask |= (unsigned char)(mask >> 4);
    do {
        r = (unsigned char)(rand8() & mask);
    } while (r >= limit);
    return r;
}
