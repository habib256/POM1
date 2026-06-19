/*
 * P-LAB TMS9918 (Apple-1) — cc65 C runtime, POM1 CodeTank port
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: derived from Antonino "Nino" Porcino's apple1-videocard-lib
 *   (https://github.com/nippur72/apple1-videocard-lib).
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 */
#include "interrupt.h"

void install_interrupt(unsigned addr) {
    (void)addr;
    /* Not implemented on POM1 Apple-1 + CodeTank (no vectored TMS /INT path in lib). */
}

void wait_interrupt(void) {
    /* Stub — polling hook reserved. */
}
