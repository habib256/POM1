/*
 * P-LAB TMS9918 (Apple-1) — gfx layer backend (Graphics II bitmap)
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: forwards to screen2 via Antonino "Nino" Porcino's tms9918c runtime
 *   (apple1-videocard-lib, https://github.com/nippur72/apple1-videocard-lib).
 *
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
 * Build: compiled in a TMS9918 project with -I dev/lib/tms9918c
 *        (for screen2.h) and -I dev/lib/gfx (for gfx.h).
 */
#include "gfx.h"
#include "screen2.h"       /* -I dev/lib/tms9918c */

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

/* gfx_filled_rect + gfx_clear forward to screen2_ext / tms_fast (screen2_clear,
 * screen2_filled_rect) — those live in their own TUs so a program that never
 * calls them shouldn't drag them in. Park the forwarders in
 * gfx_backend_tms_rect.c to honour that dead-strip path. */
