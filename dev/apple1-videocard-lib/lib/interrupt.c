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
