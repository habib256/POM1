/*
 * gfx_line.c — card-neutral Bresenham line. See gfx.h.
 *
 * Split from gfx_draw.c so a program that draws lines + rectangles only doesn't
 * drag in the circle (Bresenham circle) and ellipse (128-byte cos/sin LUTs +
 * soft-multiply) code that ld65 can't strip per-function.
 *
 * Straight runs shortcut to the card's fast span (gfx_hline/gfx_vline); the
 * diagonal case walks gfx_plot. Endpoints assumed on-screen (matches the old
 * gen2_hgr_line contract); the H/V shortcuts still clip via the span calls.
 */
#include "gfx.h"

void gfx_line(unsigned x0, unsigned char y0, unsigned x1, unsigned char y1)
{
    int x, y, xe, ye, dx, dy, sx, sy, err, e2;
    if (y0 == y1) { gfx_hline(x0, x1, y0); return; }
    if (x0 == x1) { gfx_vline(x0, y0, y1); return; }
    x = (int)x0; y = (int)y0; xe = (int)x1; ye = (int)y1;
    dx = xe - x; if (dx < 0) { dx = -dx; sx = -1; } else sx = 1;
    dy = ye - y; if (dy < 0) { dy = -dy; sy = -1; } else sy = 1;
    err = dx - dy;
    for (;;) {
        gfx_plot((unsigned)x, (unsigned char)y);
        if (x == xe && y == ye) break;
        e2 = err + err;                          /* 2*err */
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 <  dx) { err += dx; y += sy; }
    }
}
