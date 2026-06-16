/*
 * gfx_backend_tms.c — TMS9918 (Graphics II bitmap) backend for the gfx layer.
 *
 * Fills in the gfx.h backend contract by forwarding to apple1-videocard-lib's
 * screen2 bitmap primitives. Link this (NOT gfx_backend_gen2.c) into a TMS9918
 * bitmap program.
 *
 * The TMS9918 bitmap screen is 256x192 and has NO native span primitive, so
 * gfx_hline / gfx_vline are plot loops here (screen2.c never had hline/vline —
 * screen2_filled_rect was a slow scanline-loop of screen2_line). gfx_plot
 * honours the current screen2_plot_mode (SET / RESET / INVERT).
 *
 * After wiring (see README), screen2_line / screen2_circle / screen2_ellipse_rect
 * become wrappers around gfx_line / gfx_circle / gfx_ellipse, and the TMS bitmap
 * mode GAINS gfx_rect (outline) for free.
 *
 * Build: compiled in a TMS9918 project with -I dev/apple1-videocard-lib/lib
 *        (for screen2.h) and -I dev/lib/gfx (for gfx.h).
 */
#include "gfx.h"
#include "screen2.h"       /* -I dev/apple1-videocard-lib/lib */

const unsigned      gfx_width  = 256u;
const unsigned char gfx_height = 192u;

void gfx_plot(unsigned x, unsigned char y)
{
    screen2_plot((unsigned char)x, y);
}

/* Inclusive horizontal run. x0/x1 are 0..255; the `break after plot` form
 * terminates cleanly even when x1 == 255 (a `++xx` past 255 would wrap). */
void gfx_hline(unsigned x0, unsigned x1, unsigned char y)
{
    unsigned char xx;
    if (x1 < x0) { unsigned t = x0; x0 = x1; x1 = t; }
    xx = (unsigned char)x0;
    for (;;) {
        screen2_plot(xx, y);
        if (xx == (unsigned char)x1) break;
        ++xx;
    }
}

/* Inclusive vertical run. */
void gfx_vline(unsigned x, unsigned char y0, unsigned char y1)
{
    unsigned char xx = (unsigned char)x;
    unsigned char yy;
    if (y1 < y0) { unsigned char t = y0; y0 = y1; y1 = t; }
    yy = y0;
    for (;;) {
        screen2_plot(xx, yy);
        if (yy == y1) break;
        ++yy;
    }
}
