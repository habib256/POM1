/*
 * gfx_backend_gen2_rect.c — gfx_filled_rect + gfx_clear forwarders for the
 * GEN2 HGR backend.
 *
 * Split from gfx_backend_gen2.c so a GEN2 program that only draws
 * lines/circles/ellipses never references gen2_hgr_fill_pixrect or
 * gen2_hgr_clear, keeping ld65 dead-strip symmetric with the TMS side.
 */
#include "gfx.h"
#define GEN2_NO_APPLE1
#include "gen2.h"          /* -I dev/lib/gen2c */

void gfx_filled_rect(unsigned x0, unsigned char y0,
                     unsigned x1, unsigned char y1)
{
    unsigned char w, h;
    if (x1 < x0) { unsigned t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { unsigned char t = y0; y0 = y1; y1 = t; }
    w = (unsigned char)(x1 - x0 + 1u);
    h = (unsigned char)(y1 - y0 + 1u);
    gen2_hgr_fill_pixrect(x0, y0, w, h);
}

void gfx_clear(unsigned char color) { gen2_hgr_clear(color); }
