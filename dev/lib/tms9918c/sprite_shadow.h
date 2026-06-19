/*
 * P-LAB TMS9918 (Apple-1) — cc65 C runtime, POM1 CodeTank port
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: derived from Antonino "Nino" Porcino's apple1-videocard-lib
 *   (https://github.com/nippur72/apple1-videocard-lib).
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 *
 * sprite_shadow.{c,h} — 128-byte RAM-side Sprite Attribute Table (SAT).
 *
 * Pattern recommended by sketchs/doc/TMS9918-SPRITE_INIT.md (§3.2, §6) and by the
 * MSX / SMS toolchains Nino draws from: maintain a host-RAM copy of the
 * 32×4 SAT, mutate it freely (no VRAM lock contention), then push the
 * whole 128 bytes to VRAM $3B00 inside VBLANK.
 *
 * Single-flush guarantees:
 *   - No mid-frame tearing.
 *   - The 0xD0 terminator can be moved without writing transient bad
 *     values to VRAM.
 */
#ifndef SPRITE_SHADOW_H
#define SPRITE_SHADOW_H

#include "utils.h"
#include "sprites.h"

#define TMS_SHADOW_BYTES   128U
#define TMS_SHADOW_SPRITES 32U

/* RAM-side mirror of $3B00..$3B7F. */
extern unsigned char tms_sprite_shadow[TMS_SHADOW_BYTES];

/* Fill shadow with 0xD0 terminator at slot 0 (all sprites off) and clears
 * the rest. Idempotent — safe to call before each level/screen. */
void tms_shadow_init(void);

/* Write one entry into the shadow (does NOT touch VRAM). */
void tms_shadow_set(unsigned char sprite_num, const tms_sprite *s);

/* Write only the y/x coords of one shadow entry (cheaper). */
void tms_shadow_move(unsigned char sprite_num, signed char y, unsigned char x);

/* Mark sprite as inactive in the shadow (Y = 0xD0 entry, EC bit off). */
void tms_shadow_clear(unsigned char sprite_num);

/* Set the terminator: every entry from `first_inactive` onward gets Y=0xD0
 * (only the first matters per TMS hardware, but we wipe the tail to keep
 * the shadow grep-friendly). */
void tms_shadow_set_terminator(unsigned char first_inactive);

/* Blit the full 128-byte shadow to VRAM $3B00. Defined in tms_fast.s. */
void tms_shadow_flush(void);

#endif
