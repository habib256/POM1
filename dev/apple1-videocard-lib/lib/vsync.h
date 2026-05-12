/*
 * apple1-videocard-lib — POM1 CodeTank cc65 port
 * Derived from nippur72/apple1-videocard-lib (Antonino "Nino" Porcino).
 *   https://github.com/nippur72/apple1-videocard-lib
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 *
 * vsync.{c,h} — frame counter, polled-VBLANK style.
 *
 * Absent IRQ wiring on P-LAB stock cards (TMS /INT floats — see CLAUDE memory
 * "P-LAB TMS9918 /INT not wired"), the canonical way to count frames is to
 * poll $CC01 bit 7. vsync_wait() does exactly that and bumps `vsync_frames`,
 * giving a cheap monotonic time base (~60 Hz NTSC / ~50 Hz PAL).
 */
#ifndef VSYNC_H
#define VSYNC_H

#include "utils.h"

extern unsigned int vsync_frames;

/* Reset the counter to 0. */
void vsync_reset(void);

/* Block until the next end-of-frame, then increment vsync_frames. */
void vsync_wait(void);

/* Wait n frames. Counter advances by n. */
void vsync_wait_n(unsigned char n);

#endif
