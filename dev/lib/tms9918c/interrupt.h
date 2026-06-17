/*
 * apple1-videocard-lib — POM1 CodeTank cc65 port
 * Derived from nippur72/apple1-videocard-lib (Antonino "Nino" Porcino).
 *   https://github.com/nippur72/apple1-videocard-lib
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 */
#ifndef INTERRUPT_H
#define INTERRUPT_H

#include "utils.h"

/* KickC IRQ wiring is not ported — counters kept for API compatibility. */
extern volatile unsigned char irq_ticks;
extern volatile unsigned char irq_seconds;
extern volatile unsigned char irq_minutes;
extern volatile unsigned char irq_hours;
extern volatile unsigned char irq_trigger;

void install_interrupt(unsigned addr);
void wait_interrupt(void);

#endif
