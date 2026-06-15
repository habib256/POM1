/*
 * apple1io.c — small C layer over the apple1io.s Wozmon shim. See apple1io.h.
 *
 * woz_putc / woz_print_hex / woz_mon / apple1_getkey live in apple1io_asm.s
 * (they call the WOZ Monitor ROM directly); everything here is plain C on top.
 */
#include "apple1io.h"

void woz_puts(const unsigned char *s) {
    unsigned char c;
    while ((c = *s++) != 0) {
        woz_putc(c);
    }
}

void woz_print_hexword(unsigned w) {
    /* cc65 is little-endian: low byte first. Print high nibble-pair then low. */
    woz_print_hex(*((unsigned char *)&w + 1));
    woz_print_hex(*(unsigned char *)&w);
}

unsigned char apple1_iskeypressed(void) {
    return (unsigned char)(*(volatile unsigned char *)KBD_CTRL & 0x80U);
}

unsigned char apple1_readkey(void) {
    if ((*(volatile unsigned char *)KBD_CTRL & 0x80U) == 0) {
        return 0;
    }
    return (unsigned char)(*(volatile unsigned char *)KBD_DATA & 0x7FU);
}
