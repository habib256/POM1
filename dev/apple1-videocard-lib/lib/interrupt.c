/*
 * apple1-videocard-lib — POM1 CodeTank cc65 port
 * Derived from nippur72/apple1-videocard-lib (Antonino "Nino" Porcino).
 *   https://github.com/nippur72/apple1-videocard-lib
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 */
#include "interrupt.h"

volatile unsigned char irq_ticks;
volatile unsigned char irq_seconds;
volatile unsigned char irq_minutes;
volatile unsigned char irq_hours;
volatile unsigned char irq_trigger;

void install_interrupt(unsigned addr) {
    (void)addr;
    /* Not implemented on POM1 Apple-1 + CodeTank (no vectored TMS /INT path in lib). */
}

void wait_interrupt(void) {
    /* Stub — polling hook reserved. */
}
