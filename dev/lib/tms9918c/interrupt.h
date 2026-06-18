/*
 * apple1-videocard-lib — POM1 CodeTank cc65 port
 * Derived from nippur72/apple1-videocard-lib (Antonino "Nino" Porcino).
 *   https://github.com/nippur72/apple1-videocard-lib
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 */
#ifndef INTERRUPT_H
#define INTERRUPT_H

#include "utils.h"

/* KickC IRQ wiring is not ported. The upstream irq_ticks/seconds/minutes/
 * hours/trigger counters were dropped: nothing on POM1 drives them, so they
 * were pure dead RAM (5 bytes) in every program that linked this stub. */
void install_interrupt(unsigned addr);
void wait_interrupt(void);

#endif
