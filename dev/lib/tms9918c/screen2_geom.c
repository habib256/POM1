/*
 * apple1-videocard-lib — POM1 CodeTank cc65 port
 * Derived from nippur72/apple1-videocard-lib (Antonino "Nino" Porcino).
 *   https://github.com/nippur72/apple1-videocard-lib
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 *
 * screen2_geom.c — bitmap-mode vector wrappers forwarding to dev/lib/gfx
 * (factoring axis 1). gfx_plot resolves via gfx_backend_tms.c straight back
 * to screen2_plot, so the current plot mode (SET/RESET/INVERT) is honoured.
 *
 * The ellipse cos/sin LUTs + abs helper that used to live next to plot are
 * now in gfx_draw.c. Link gfx-tms.lib in the project Makefile.
 *
 * Split from screen2.c so a `screen2_plot`-only program doesn't drag in the
 * line/circle/ellipse/rect wrappers + the gfx archive's symbol references.
 */
#include "screen2.h"
#include "gfx.h"

void screen2_line(unsigned char x0, unsigned char y0, unsigned char x1, unsigned char y1) {
    gfx_line(x0, y0, x1, y1);
}

void screen2_circle(unsigned char xm, unsigned char ym, unsigned char r) {
    gfx_circle(xm, ym, r);
}

void screen2_ellipse_rect(unsigned char x0, unsigned char y0, unsigned char x1, unsigned char y1) {
    gfx_ellipse(x0, y0, x1, y1);
}

/* Rectangle OUTLINE through opposite corners — capability gained from the
 * shared layer (screen2 previously had only the slow screen2_filled_rect). */
void screen2_rect(unsigned char x0, unsigned char y0, unsigned char x1, unsigned char y1) {
    gfx_rect(x0, y0, x1, y1);
}
