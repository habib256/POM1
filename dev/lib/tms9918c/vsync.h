/*
 * P-LAB TMS9918 (Apple-1) — cc65 C runtime, POM1 CodeTank port
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: derived from Antonino "Nino" Porcino's apple1-videocard-lib
 *   (https://github.com/nippur72/apple1-videocard-lib).
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 *
 * vsync.{c,h} — frame counter, polled-VBLANK style.
 *
 * The P-LAB card wires TMS /INT → 6502 /IRQ (trace verified on real hardware
 * by Parmigiani), but the canonical, simplest way to count frames is still to
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
