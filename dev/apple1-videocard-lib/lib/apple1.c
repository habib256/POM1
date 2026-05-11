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

void apple1_input_line(unsigned char *buffer, unsigned char max) {
    unsigned char x = 0;
    for (;;) {
        unsigned char c = apple1_getkey();
        buffer[x] = c;
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
            if (x < max) {
                woz_putc(c);
                ++x;
            }
        }
    }
    buffer[x] = 0;
}

void apple1_input_line_prompt(unsigned char *buffer, unsigned char max) {
    unsigned char x = 0;
    woz_putc((unsigned char)INPUT_LINE_PROMPT_CHAR);
    for (;;) {
        unsigned char c = apple1_getkey();
        buffer[x] = c;
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
            if (x < max) {
                woz_putc(c);
                ++x;
            }
        }
    }
    buffer[x] = 0;
}

void apple1_eprom_init(void) {
    /* cc65 copydata handles .data — reserved for API compatibility. */
}
