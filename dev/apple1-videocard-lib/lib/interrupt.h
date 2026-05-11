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
