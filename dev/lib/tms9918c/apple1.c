/*
 * P-LAB TMS9918 (Apple-1) — cc65 C runtime, POM1 CodeTank port
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: derived from Antonino "Nino" Porcino's apple1-videocard-lib
 *   (https://github.com/nippur72/apple1-videocard-lib).
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 *
 * Almost identical to dev/lib/apple1c/apple1io.c. The intentional differences
 * (preserved to keep upstream attribution + the CodeTank's preferred Wozmon
 * warm-restart):
 *   - WOZMON entry: $FF1F here (silent post-prompt) vs $FF1A in apple1c
 *     (prints "\" + CR before line editor). A future merge would parameterise
 *     this via a single WOZMON_ENTRY macro.
 *   - PIA defines: KEY_DATA/KEY_CTRL here vs KBD_DATA/KBD_CTRL in apple1c.
 *   - Accessor idiom: PEEK(addr) macro here vs *(volatile unsigned char *)addr
 *     in apple1c (functionally equivalent under cc65).
 *   - Extra helpers: apple1_input_line / apple1_input_line_prompt are
 *     tms9918c-only (Nino's line-editor heritage).
 */
#include "apple1.h"

void woz_print_hexword(word w) {
    woz_print_hex(*((unsigned char *)&w + 1));
    woz_print_hex(*(unsigned char *)&w);
}

void woz_puts(const unsigned char *s) {
    unsigned char c;
    while ((c = *s++) != 0) {
        woz_putc(c);
    }
}

unsigned char apple1_iskeypressed(void) {
    return (unsigned char)(PEEK(KEY_CTRL) & 0x80U);
}

unsigned char apple1_readkey(void) {
    if ((PEEK(KEY_CTRL) & 0x80U) == 0) {
        return 0;
    }
    return (unsigned char)(PEEK(KEY_DATA) & 0x7FU);
}

/* `max` is the total buffer size INCLUDING the NUL terminator: at most max-1
 * characters are stored and buffer[0..max-1] is the only range touched, so
 * apple1_input_line(buf, sizeof buf) is safe. (The previous version wrote
 * buffer[x] unconditionally before the bounds check, so a buffer sized exactly
 * `max` took a 1-byte out-of-bounds write.) */
void apple1_input_line(unsigned char *buffer, unsigned char max) {
    unsigned char x = 0;
    if (max == 0) return;
    for (;;) {
        unsigned char c = apple1_getkey();
        if (c == 13) {
            break;
        } else if (c == 27) {
            x = 0;
            break;
        } else if (c == 8 || c == '_') {
            if (x != 0) {
                woz_putc('_');
                --x;
            }
        } else {
            if (x + 1u < max) {
                buffer[x] = c;
                woz_putc(c);
                ++x;
            }
        }
    }
    buffer[x] = 0;
}

/* `max` is the total buffer size INCLUDING the NUL terminator (see
 * apple1_input_line above). */
void apple1_input_line_prompt(unsigned char *buffer, unsigned char max) {
    unsigned char x = 0;
    if (max == 0) return;
    woz_putc((unsigned char)INPUT_LINE_PROMPT_CHAR);
    for (;;) {
        unsigned char c = apple1_getkey();
        if (c == 13) {
            break;
        } else if (c == 27) {
            x = 0;
            break;
        } else if (c == 8 || c == '_') {
            if (x != 0) {
                --x;
                buffer[x] = 0;
                woz_putc('\r');
                woz_putc((unsigned char)INPUT_LINE_PROMPT_CHAR);
                woz_puts(buffer);
            }
        } else {
            if (x + 1u < max) {
                buffer[x] = c;
                woz_putc(c);
                ++x;
            }
        }
    }
    buffer[x] = 0;
}
