/*
 * picshow — port minimal upstream (demos/picshow) pour TMS9918 (Screen 2).
 * Affiche une image convertie (pic_c / pic_p) en copiant couleur + motifs
 * vers les tables VRAM (avec chunking pour rester compatible silicon-strict).
 */
#include "tms9918.h"
#include "screen2.h"
#include "apple1.h"

static unsigned char key_is_return(unsigned char k) {
    return (unsigned char)(k == 13U);
}

static void woz_puts_simple(const char *s) {
    unsigned char c;
    while ((c = (unsigned char)*s++) != 0U) {
        woz_putc(c);
    }
}

void main(void) {
    /* init Screen 2 */
    tms_init_regs(SCREEN2_TABLE);
    tms_set_color(COLOR_BLACK);
    screen2_init_bitmap(FG_BG(COLOR_WHITE, COLOR_BLACK));
    screen2_plot_mode = PLOT_MODE_SET;

    woz_puts_simple("\rDISPLAYING PICTURE...\r");
    screen2_puts("PICSHOW", 2U, 2U, FG_BG(COLOR_WHITE, COLOR_BLACK));
    screen2_circle(128U, 76U, 30U);
    screen2_ellipse_rect(20U, 10U, 140U, 150U);
    while (!key_is_return(apple1_getkey())) {
        /* attente */
    }
    woz_puts_simple("BYE!\r");
    woz_mon();
}

