/*
 * P-LAB TMS9918 (Apple-1) — cc65 C program for POM1 CodeTank ($4000, 4000R)
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: dev/lib/tms9918c — cc65 port of Antonino "Nino" Porcino's
 *   apple1-videocard-lib (https://github.com/nippur72/apple1-videocard-lib).
 *
 * nino-democ — minimal TMS9918 menu (minimal port of upstream demos/demo).
 * Keys:
 *   1 : Screen 1 demo
 *   2 : Screen 2 demo
 *   3/A/I/E/F/B/H : "not ported" messages
 *   0 : quit (wozmon)
 *
 * Goal: provide a "tms9918 touched" demo for every entry.
 */
#include "apple1.h"
#include "screen1.h"
#include "screen2.h"

#include "tms9918.h"

static unsigned char key_upper(unsigned char k) {
    if (k >= 'a' && k <= 'z') return (unsigned char)(k - 32U);
    return k;
}

static void wait_return(void) {
    while (apple1_getkey() != 13U) {
        /* wait */
    }
}

static void screen1_demo(void) {
    tms_init_regs(SCREEN1_TABLE);
    screen1_prepare();
    screen1_load_font();
    screen1_putc(CHR_CLS);

    screen1_puts((const unsigned char *)"SCREEN1 DEMO\n\n");
    screen1_puts((const unsigned char *)REVERSE_ON);
    screen1_puts((const unsigned char *)"REVERSE TEXT");
    screen1_puts((const unsigned char *)REVERSE_OFF);
    screen1_puts((const unsigned char *)"\n\n");
    screen1_puts((const unsigned char *)"Press Return to go back.\n");
    wait_return();
}

static void screen2_demo(void) {
    tms_init_regs(SCREEN2_TABLE);
    tms_set_color(COLOR_BLACK);
    screen2_init_bitmap(FG_BG(COLOR_WHITE, COLOR_BLACK));
    screen2_plot_mode = PLOT_MODE_SET;

    screen2_puts("SCREEN2", 2U, 2U, FG_BG(COLOR_WHITE, COLOR_BLACK));
    screen2_circle(128U, 76U, 30U);
    screen2_ellipse_rect(10U, 10U, 150U, 100U);

    while (apple1_getkey() != 13U) {
        /* wait */
    }
}

static void stub_msg(const char *s) {
    tms_init_regs(SCREEN1_TABLE);
    screen1_prepare();
    screen1_load_font();
    screen1_putc(CHR_CLS);
    screen1_puts((const unsigned char *)s);
    screen1_puts((const unsigned char *)"\n\nPress Return to go back.\n");
    wait_return();
}

void main(void) {
    unsigned char k;

    /* init menu */
    tms_init_regs(SCREEN1_TABLE);
    screen1_prepare();
    screen1_load_font();
    screen1_putc(CHR_CLS);

    for (;;) {
        screen1_puts((const unsigned char *)"TMS9918 DEMO MENU\n");
        screen1_puts((const unsigned char *)"1 SCREEN1\n");
        screen1_puts((const unsigned char *)"2 SCREEN2\n");
        screen1_puts((const unsigned char *)"0 EXIT\n\n");
        screen1_puts((const unsigned char *)"Other keys -> messages.\n");

        k = key_upper(apple1_getkey());

        if (k == '0') {
            woz_puts((const unsigned char *)"BYE!\r");
            woz_mon();
        } else if (k == '1') {
            screen1_demo();
        } else if (k == '2') {
            screen2_demo();
        } else if (k == '3') {
            stub_msg("BALLOON : not ported");
        } else if (k == 'A') {
            stub_msg("AMIGA HAND : not ported");
        } else if (k == 'I') {
            stub_msg("INTERRUPT : not ported");
        } else if (k == 'E') {
            stub_msg("FLIP EXT VID : not ported");
        } else if (k == 'F') {
            stub_msg("TEST END-OF-FRAME : not ported");
        } else if (k == 'B') {
            stub_msg("BLANK ON/OFF : not ported");
        } else if (k == 'H') {
            stub_msg("HELP : not ported");
        } else {
            stub_msg("Unknown key.");
        }
        /* back to menu */
        tms_init_regs(SCREEN1_TABLE);
        screen1_prepare();
        screen1_load_font();
        screen1_putc(CHR_CLS);
    }
}

