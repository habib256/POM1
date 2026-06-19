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

unsigned int vsync_frames = 0;

void vsync_reset(void) {
    vsync_frames = 0;
}

void vsync_wait(void) {
    tms_wait_end_of_frame();
    ++vsync_frames;
}

void vsync_wait_n(unsigned char n) {
    while (n != 0) {
        tms_wait_end_of_frame();
        ++vsync_frames;
        --n;
    }
}
