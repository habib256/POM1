/*
 * gfx_rect.c — card-neutral rectangle outline. See gfx.h.
 *
 * Four spans, interior untouched. Corners normalised so x0<=x1, y0<=y1.
 * Split from gfx_draw.c so a rect-only program doesn't drag in gfx_plot
 * (used by line/circle/ellipse) — every span routes through the card's
 * fast hline/vline.
 */
#include "gfx.h"

void gfx_rect(unsigned x0, unsigned char y0, unsigned x1, unsigned char y1)
{
    if (x1 < x0) { unsigned t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { unsigned char t = y0; y0 = y1; y1 = t; }
    gfx_hline(x0, x1, y0);
    gfx_hline(x0, x1, y1);
    gfx_vline(x0, y0, y1);
    gfx_vline(x1, y0, y1);
}
