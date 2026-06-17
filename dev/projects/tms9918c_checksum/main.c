/*
 * checksum — port de demos/checksum (upstream), sans stdlib.
 * Somme des octets entre deux adresses hex (Wozmon).
 */
#include "apple1.h"
#include "utils.h"

static unsigned char *const keybuf = (unsigned char *)(unsigned)INBUFFER;

static unsigned int hex_to_word(const unsigned char *str) {
    unsigned int tmpword = 0;
    unsigned char c;
    unsigned char i;

    for (i = 0; (c = str[i]) != 0; ++i) {
        tmpword = tmpword << 4U;
        if (c >= (unsigned char)'0' && c <= (unsigned char)'9') {
            tmpword += (unsigned int)(c - (unsigned char)'0');
        } else if (c >= (unsigned char)'A' && c <= (unsigned char)'F') {
            tmpword += (unsigned int)(c - (unsigned char)'A') + 10U;
        } else {
            return 0;
        }
        if (i >= 4U) {
            return 0;
        }
    }
    if (i == 0U) {
        return 0;
    }
    return tmpword;
}

void main(void) {
    unsigned int start_address;
    unsigned int end_address;
    unsigned int check_sum;
    unsigned int t;

    woz_puts((const unsigned char *)"\r\r*** CHECKSUM ***\r");

    for (;;) {
        woz_puts((const unsigned char *)"\rSTART ADDRESS ");
        apple1_input_line_prompt(keybuf, 4);
        start_address = hex_to_word(keybuf);

        woz_puts((const unsigned char *)"\rEND   ADDRESS ");
        apple1_input_line_prompt(keybuf, 4);
        end_address = hex_to_word(keybuf);

        check_sum = 0;
        for (t = start_address;; ++t) {
            check_sum += (unsigned int)PEEK(t);
            if (t == end_address) {
                break;
            }
        }

        woz_puts((const unsigned char *)"\r\r");
        woz_print_hexword((word)start_address);
        woz_putc((unsigned char)'-');
        woz_print_hexword((word)end_address);
        woz_puts((const unsigned char *)" => ");
        woz_print_hexword((word)check_sum);
        woz_puts((const unsigned char *)" CHECKSUM\r\r");
    }
}
