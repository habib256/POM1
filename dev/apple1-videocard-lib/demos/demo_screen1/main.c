/*
 * demo_screen1 — port de demos/demo/demo_screen1.h (upstream).
 * Texte screen1 + reverse + charset + sprites + ligne saisie.
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
        TMS_WRITE_DATA_PORT((unsigned char)(40U + (i & 7U) * 24U));
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
        TMS_WRITE_DATA_PORT(i);
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

    woz_mon();
}
