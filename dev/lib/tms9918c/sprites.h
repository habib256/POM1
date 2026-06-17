/*
 * apple1-videocard-lib — POM1 CodeTank cc65 port
 * Derived from nippur72/apple1-videocard-lib (Antonino "Nino" Porcino).
 *   https://github.com/nippur72/apple1-videocard-lib
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 */
#ifndef SPRITES_H
#define SPRITES_H

#include "utils.h"
#include "tms9918.h"

typedef struct {
    signed char y;
    unsigned char x;
    unsigned char name;
    unsigned char color;
} tms_sprite;

#define SIZEOF_SPRITE      4U
#define SPRITE_OFF_MARKER  0xD0U
#define EARLY_CLOCK        128U

void tms_set_total_sprites(unsigned char num);
void tms_set_sprite(unsigned char sprite_num, const tms_sprite *s);
void tms_set_sprite_double_size(unsigned char size);
void tms_set_sprite_magnification(unsigned char m);
void tms_clear_collisions(void);

#endif
