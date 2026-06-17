/*
 * apple1-videocard-lib — POM1 CodeTank cc65 port
 * Derived from nippur72/apple1-videocard-lib (Antonino "Nino" Porcino).
 *   https://github.com/nippur72/apple1-videocard-lib
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 *
 * sprite_shadow.c — see sprite_shadow.h.
 */
#include "sprite_shadow.h"

unsigned char tms_sprite_shadow[TMS_SHADOW_BYTES];

void tms_shadow_init(void) {
    unsigned char i;
    /* Wipe all 128 bytes, then set the first Y to 0xD0 so the VDP scan
     * terminates immediately on flush. Per TI spec only entry 0 needs the
     * terminator, but wiping the tail keeps debug dumps readable. */
    for (i = 0; i < TMS_SHADOW_BYTES; ++i) {
        tms_sprite_shadow[i] = 0;
    }
    tms_sprite_shadow[0] = SPRITE_OFF_MARKER;
}

void tms_shadow_set(unsigned char sprite_num, const tms_sprite *s) {
    unsigned char *p = &tms_sprite_shadow[(unsigned)sprite_num * SIZEOF_SPRITE];
    p[0] = (unsigned char)s->y;
    p[1] = s->x;
    p[2] = s->name;
    p[3] = s->color;
}

void tms_shadow_move(unsigned char sprite_num, signed char y, unsigned char x) {
    unsigned char *p = &tms_sprite_shadow[(unsigned)sprite_num * SIZEOF_SPRITE];
    p[0] = (unsigned char)y;
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
