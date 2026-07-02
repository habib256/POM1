/*
 * P-LAB TMS9918 (Apple-1) — cc65 C program for POM1 CodeTank ($4000, 4000R)
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: dev/lib/tms9918c — cc65 port of Antonino "Nino" Porcino's
 *   apple1-videocard-lib (https://github.com/nippur72/apple1-videocard-lib).
 *
 * demo_screen1 — port of demos/demo/demo_screen1.h (upstream).
 * screen1 text + reverse + charset + sprites + input line.
 */
#include "tms9918.h"
#include "screen1.h"
#include "sprites.h"
#include "apple1.h"

static void screen1_square_sprites(void) {
    unsigned char i;

    tms_set_vram_write_addr(TMS_SPRITE_PATTERNS);
    for (i = 0; i < 8U; ++i) {
        TMS_WRITE_DATA_PORT(255U);
        TMS_IO_DELAY();
    }

    tms_set_vram_write_addr(TMS_SPRITE_ATTRS);
    for (i = 0; i < 32U; ++i) {
        /* Row pitch 20 px starting at Y=24: rows 24..164, all fully on the
         * 192-line screen. The old pitch (40 + (i&7)*24) put row 7 at
         * Y=208 = $D0 — the SAT chain TERMINATOR — so the hardware stopped
         * scanning at sprite 7 and sprites 7..31 never displayed (real
         * silicon and POM1 agree). Never place a sprite at Y=$D0. */
        TMS_WRITE_DATA_PORT((unsigned char)(24U + (i & 7U) * 20U));
        TMS_IO_DELAY();
        TMS_IO_DELAY();
        TMS_IO_DELAY();
        TMS_IO_DELAY();
        TMS_WRITE_DATA_PORT((unsigned char)(30U + (i >> 3) * 16U));
        TMS_IO_DELAY();
        TMS_IO_DELAY();
        TMS_IO_DELAY();
        TMS_IO_DELAY();
        TMS_WRITE_DATA_PORT(0);
        TMS_IO_DELAY();
        TMS_IO_DELAY();
        TMS_IO_DELAY();
        TMS_IO_DELAY();
        /* Colour 1..15, never 0: colour 0 is TRANSPARENT (sprite 0 was
         * invisible with the old `i` value), and masking to 4 bits keeps
         * bit 7 (Early Clock) clear for i >= 16. */
        TMS_WRITE_DATA_PORT((unsigned char)(1U + (i % 15U)));
        TMS_IO_DELAY();
        TMS_IO_DELAY();
        TMS_IO_DELAY();
        TMS_IO_DELAY();
    }
}

static unsigned char buf_len(const unsigned char *s) {
    unsigned char n = 0;
    while (s[n] != 0) {
        ++n;
    }
    return n;
}

void main(void) {
    static unsigned char buffer[32];

    tms_init_regs(SCREEN1_TABLE);
    screen1_prepare();
    screen1_load_font();

    screen1_putc(CHR_CLS);
    screen1_puts((const unsigned char *)"*** P-LAB  VIDEO CARD ***\n"
                                         "16K VRAM BYTES FREE\n\n"
                                         "READY.\n\n\n");

    screen1_puts((const unsigned char *)"what about ");
    screen1_puts(REVERSE_ON);
    screen1_puts((const unsigned char *)" REVERSE text ");
    screen1_puts(REVERSE_OFF);
    screen1_puts((const unsigned char *)" ?\n\n\n\n");

    {
        unsigned int i;
        for (i = 32U; i < 128U; ++i) {
            screen1_putc((unsigned char)i);
        }
        screen1_puts((const unsigned char *)"\n\n");
        screen1_puts(REVERSE_ON);
        for (i = 32U; i < 128U; ++i) {
            screen1_putc((unsigned char)i);
        }
        screen1_puts(REVERSE_OFF);
    }

    screen1_square_sprites();

    for (;;) {
        screen1_puts((const unsigned char *)"\n\nWRITE HERE: >");
        screen1_strinput(buffer, 16);
        if (buf_len(buffer) == 0) {
            break;
        }
        screen1_puts((const unsigned char *)"\n\n\nyou wrote: '");
        screen1_puts(buffer);
        screen1_puts((const unsigned char *)"'");
    }

    /* Blessed exit: park the SAT (32 sprites are live, no terminator) and
     * blank the display so whatever runs next starts from a quiet chip. */
    tms_set_total_sprites(0);
    tms_write_reg(1, 0x80U);
    woz_mon();
}
