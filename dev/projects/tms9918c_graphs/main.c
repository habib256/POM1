/*
 * graphs — port de demos/graphs (upstream) : bitmap mode 2 + cercle + ellipse.
 */
#include "tms9918.h"
#include "screen2.h"

void main(void) {
    unsigned char text_color = FG_BG(COLOR_WHITE, COLOR_BLACK);

    tms_init_regs(SCREEN2_TABLE);
    tms_set_color(COLOR_BLACK);
    screen2_init_bitmap(text_color);
    screen2_plot_mode = PLOT_MODE_SET;

    screen2_circle(128U, 76U, 30U);
    screen2_ellipse_rect(10U, 10U, 150U, 100U);

    for (;;) {
        /* idle */
    }
}
