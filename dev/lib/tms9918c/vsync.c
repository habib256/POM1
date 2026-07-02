/*
 * P-LAB TMS9918 (Apple-1) — cc65 C runtime, POM1 CodeTank port
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: derived from Antonino "Nino" Porcino's apple1-videocard-lib
 *   (https://github.com/nippur72/apple1-videocard-lib).
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 *
 * vsync.c — see vsync.h.
 */
#include "vsync.h"
#include "tms9918.h"

/* BSS (crt0 has no copydata — `= 0` in DATA was never applied; BSS-zero
 * re-zeroes the counter on every 4000R entry, which is what we want). */
unsigned int vsync_frames;

void vsync_reset(void) {
    vsync_frames = 0;
}

unsigned char vsync_wait(void) {
    /* Propagates tms_wait_end_of_frame()'s status snapshot (fresh F +
     * C/5S collected across the drain and terminal reads) so callers can
     * test COLLISION_BIT/FIVESPR_BIT without a second, self-defeating
     * status read (reading clears all three flags atomically). */
    unsigned char s = tms_wait_end_of_frame();
    ++vsync_frames;
    return s;
}

void vsync_wait_n(unsigned char n) {
    while (n != 0) {
        tms_wait_end_of_frame();
        ++vsync_frames;
        --n;
    }
}
