/*
 * P-LAB TMS9918 (Apple-1) — cc65 C runtime, POM1 CodeTank port
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: derived from Antonino "Nino" Porcino's apple1-videocard-lib
 *   (https://github.com/nippur72/apple1-videocard-lib).
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 *
 * sprite_shadow.c — see sprite_shadow.h.
 */
#include "sprite_shadow.h"

unsigned char tms_sprite_shadow[TMS_SHADOW_BYTES];

void tms_shadow_init(void) {
    unsigned char i;
    /* Wipe all 128 bytes, then park every entry off-screen: Y=$D0 at slot 0
     * (chain terminator) and Y=$D1 for slots 1..31. The old zero-fill left
     * slots 1-31 at Y=0: invisible (colour 0) but LIVE — they'd count toward
     * the 4-per-line scan limit on lines 1-8 and pollute 5S the moment a
     * program moves the terminator past them without rewriting every slot
     * (same rationale as the $D0+$D1 gold standard in screen*_prepare). */
    for (i = 0; i < TMS_SHADOW_BYTES; ++i) {
        tms_sprite_shadow[i] = 0;
    }
    for (i = 1; i < TMS_SHADOW_SPRITES; ++i) {
        tms_sprite_shadow[(unsigned)i * SIZEOF_SPRITE] = 0xD1U;
    }
    tms_sprite_shadow[0] = SPRITE_OFF_MARKER;
}

/* $D0 dodge shared by set/move: y = -48 casts to exactly $D0 — the SAT
 * chain TERMINATOR. A sprite animated off the top edge would silently
 * blank every higher-numbered sprite while it holds that value. $D1 is
 * one line lower and equally off-screen. */
static unsigned char shadow_safe_y(unsigned char y) {
    return (y == 0xD0U) ? 0xD1U : y;
}

void tms_shadow_set(unsigned char sprite_num, const tms_sprite *s) {
    unsigned char *p = &tms_sprite_shadow[(unsigned)sprite_num * SIZEOF_SPRITE];
    p[0] = shadow_safe_y((unsigned char)s->y);
    p[1] = s->x;
    p[2] = s->name;
    /* Colour + deliberate EARLY_CLOCK only (best-practices §3): stray bits
     * 4-6 are undefined on silicon; an accidental bit 7 = 32 px left shift. */
    p[3] = (unsigned char)(s->color & 0x8FU);
}

void tms_shadow_move(unsigned char sprite_num, signed char y, unsigned char x) {
    unsigned char *p = &tms_sprite_shadow[(unsigned)sprite_num * SIZEOF_SPRITE];
    p[0] = shadow_safe_y((unsigned char)y);
    p[1] = x;
}

void tms_shadow_clear(unsigned char sprite_num) {
    unsigned char *p = &tms_sprite_shadow[(unsigned)sprite_num * SIZEOF_SPRITE];
    p[0] = SPRITE_OFF_MARKER;
    p[1] = 0;
    p[2] = 0;
    p[3] = 0;
}

void tms_shadow_set_terminator(unsigned char first_inactive) {
    unsigned char i = first_inactive;
    if (i >= TMS_SHADOW_SPRITES) {
        return;
    }
    /* First inactive Y = 0xD0 stops the SAT scan. Wipe trailing entries
     * (debug-friendly; not required by hardware). */
    tms_sprite_shadow[(unsigned)i * SIZEOF_SPRITE] = SPRITE_OFF_MARKER;
    ++i;
    for (; i < TMS_SHADOW_SPRITES; ++i) {
        unsigned char *p = &tms_sprite_shadow[(unsigned)i * SIZEOF_SPRITE];
        p[0] = SPRITE_OFF_MARKER;
        p[1] = 0;
        p[2] = 0;
        p[3] = 0;
    }
}
