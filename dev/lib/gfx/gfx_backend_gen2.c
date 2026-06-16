/*
 * gfx_backend_gen2.c — GEN2 HGR backend for the card-neutral gfx layer.
 *
 * Fills in the gfx.h backend contract by forwarding to the existing GEN2 fast
 * paths. Link this (NOT gfx_backend_tms.c) into a GEN2 program; the linker
 * resolves gfx_plot/hline/vline to these direct wrappers — no indirection.
 *
 * After this layer is wired in (see README), gen2_hgr_line / gen2_hgr_rect /
 * gen2_hgr_circle in gen2.c become one-line wrappers around gfx_line / gfx_rect
 * / gfx_circle, and gen2 ADDS gfx_ellipse for free. The card keeps its own fast
 * span primitives (gen2_hgr_hline/vline route through fill_pixrect) — those ARE
 * the backend, so the geometry never loses the STA-run fast path.
 *
 * Build: compiled in a GEN2 project with -I dev/lib/gen2c (for gen2.h) and
 *        -I dev/lib/gfx (for gfx.h).
 */
#include "gfx.h"
#include "gen2.h"          /* -I dev/lib/gen2c */

const unsigned      gfx_width  = 280u;
const unsigned char gfx_height = 192u;

void gfx_plot(unsigned x, unsigned char y)                 { gen2_hgr_plot(x, y); }
void gfx_hline(unsigned x0, unsigned x1, unsigned char y)  { gen2_hgr_hline(x0, x1, y); }
void gfx_vline(unsigned x, unsigned char y0, unsigned char y1) { gen2_hgr_vline(x, y0, y1); }
