/*
 * P-LAB TMS9918 (Apple-1) — cc65 C runtime, POM1 CodeTank port
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: derived from Antonino "Nino" Porcino's apple1-videocard-lib
 *   (https://github.com/nippur72/apple1-videocard-lib).
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 */
#ifndef APPLE1_H
#define APPLE1_H

#include "utils.h"

/* woz_mon() jumps $FF1A — the Wozmon PROMPT entry (prints "\" + CR), the house
 * rule shared with dev/lib/apple1 (apple1.inc), dev/lib/apple1c and
 * dev/lib/gen2c: the user at the keyboard needs the "\" to know the monitor is
 * back. (Historically this runtime jumped $FF1F, the SILENT post-prompt warm
 * restart, which looks like a hang; unified June 2026. WOZMON below now agrees
 * with apple1c/apple1io.h — the two headers used to #define WOZMON with
 * different values.) woz_mon_silent() keeps $FF1F for callers that just
 * printed their own status line and genuinely want no prompt. */
#define WOZMON        0xFF1AU
#define WOZMON_SILENT 0xFF1FU
#define ECHO      0xFFEFU
#define PRBYTE    0xFFDCU
#define KEY_DATA  0xD010U
#define KEY_CTRL  0xD011U
#define TERM_DATA 0xD012U
#define TERM_CTRL 0xD013U
#define INBUFFER  0x0200U
#define INBUFSIZE 0x80U

void woz_print_hex(unsigned char c);
void woz_print_hexword(word w);
void woz_putc(unsigned char c);
void woz_puts(const unsigned char *s);
void woz_mon(void);         /* return to the WOZ Monitor "\" prompt ($FF1A) */
void woz_mon_silent(void);  /* silent warm restart ($FF1F) — no "\" printed */

unsigned char apple1_iskeypressed(void);
unsigned char apple1_getkey(void);
unsigned char apple1_readkey(void);
void apple1_input_line(unsigned char *buffer, unsigned char max);
void apple1_input_line_prompt(unsigned char *buffer, unsigned char max);

#ifndef INPUT_LINE_PROMPT_CHAR
#define INPUT_LINE_PROMPT_CHAR '>'
#endif

#endif
