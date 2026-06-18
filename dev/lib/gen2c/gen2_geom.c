/* gen2_geom.c — vector primitives (hline, vline, line, rect, circle, ellipse).
 *
 * line/rect/circle/ellipse forward to the card-neutral gfx layer
 * (dev/lib/gfx); the backend (gfx_backend_gen2.c, linked via gfx-gen2.lib)
 * resolves gfx_plot/gfx_hline/gfx_vline back to our own primitives. hline +
 * vline live here (not in gen2_rect.c) so a pixel-rect-only program does not
 * pull the chunked fill_pixrect run loop, and a line-drawing program does
 * not pull rect colorize. */

#include "gen2.h"
#include "gen2_internal.h"
#include "gfx.h"

/* Horizontal run from (x0..x1) inclusive on scanline y. Splits into <=255px
 * chunks because gen2_hgr_fill_pixrect's width is a single byte. */
void gen2_hgr_hline(unsigned x0, unsigned x1, unsigned char y)
{
    unsigned len;
    if (x1 < x0) { unsigned t = x0; x0 = x1; x1 = t; }
    if (x0 > 279u || y > 191u) return;
    if (x1 > 279u) x1 = 279u;
    len = x1 - x0 + 1u;                          /* 1..280 */
    while (len) {
        unsigned char chunk = (len > 255u) ? 255u : (unsigned char)len;
        gen2_hgr_fill_pixrect(x0, y, chunk, 1u);
        x0  += chunk;
        len -= chunk;
    }
}

/* Vertical run from (y0..y1) inclusive at column x. */
void gen2_hgr_vline(unsigned x, unsigned char y0, unsigned char y1)
{
    if (y1 < y0) { unsigned char t = y0; y0 = y1; y1 = t; }
    if (x > 279u || y0 > 191u) return;
    if (y1 > 191u) y1 = 191u;
    gen2_hgr_fill_pixrect(x, y0, 1u, (unsigned char)(y1 - y0 + 1u));
}

/* Vector primitives forward to the card-neutral gfx layer. gfx_line / gfx_rect
 * were ported VERBATIM from the bodies that lived in gen2.c, and gfx_plot /
 * gfx_hline / gfx_vline resolve (via the GEN2 backend, gfx_backend_gen2.c)
 * straight back to gen2_hgr_plot / hline / vline — pixel-identical output.
 * gfx_circle clips to [0,280) x [0,192). */
void gen2_hgr_line(unsigned x0, unsigned char y0, unsigned x1, unsigned char y1)
{
    gfx_line(x0, y0, x1, y1);
}

/* Rectangle OUTLINE through opposite corners (inclusive). The interior is left
 * untouched — fill with gen2_hgr_fill_pixrect if you want it solid. */
void gen2_hgr_rect(unsigned x0, unsigned char y0, unsigned x1, unsigned char y1)
{
    gfx_rect(x0, y0, x1, y1);
}

/* Midpoint circle outline, centre (xc, yc), radius r; off-screen points clipped. */
void gen2_hgr_circle(unsigned xc, unsigned char yc, unsigned char r)
{
    gfx_circle(xc, yc, r);
}

/* Ellipse inscribed in the (x0,y0)-(x1,y1) box — 64-segment polyline. */
void gen2_hgr_ellipse(unsigned x0, unsigned char y0, unsigned x1, unsigned char y1)
{
    gfx_ellipse(x0, y0, x1, y1);
}
