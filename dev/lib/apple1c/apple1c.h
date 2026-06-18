/* apple1c.h — umbrella header for Apple-1 C base (dev/lib/apple1c).
 *
 * One include for a text-mode Apple-1 program:
 *     #include "apple1c.h"
 *
 * It currently aliases apple1io.h (which carries woz_puts / woz_putc /
 * apple1_getkey / the beginner-friendly puts_apple1 wrappers). Future
 * additions to the Apple-1 base (e.g. tape, RTC, microSD shell helpers) get
 * included here so consumers only ever name one header.
 *
 * Pure preprocessor — no code is generated, no symbol is added. Costs zero
 * bytes in the linked binary unless you actually call something from inside.
 *
 * NOTE: the upstream nippur72 TMS9918 lib (dev/lib/tms9918c/) ships its OWN
 * `apple1.h` with a similar but slightly different API. To avoid header-name
 * collisions when both libs are visible to a project, this umbrella is named
 * `apple1c.h` — its content matches dev/lib/apple1c/ which is what gen2c and
 * pure-text C programs pull. */

#ifndef APPLE1C_H
#define APPLE1C_H

#include "apple1io.h"

#endif /* APPLE1C_H */
