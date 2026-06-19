/*
 * P-LAB TMS9918 (Apple-1) — cc65 C runtime, POM1 CodeTank port
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: derived from Antonino "Nino" Porcino's apple1-videocard-lib
 *   (https://github.com/nippur72/apple1-videocard-lib).
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 */
#ifndef C64FONT_H
#define C64FONT_H

/* C64-style 8x8 font: 96 glyphs (ASCII 32..127), 768 bytes linear. */
extern const unsigned char tms_c64font[768];

#endif
