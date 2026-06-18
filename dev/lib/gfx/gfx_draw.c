/*
 * gfx_draw.c — card-neutral vector geometry. See gfx.h.
 *
 * Ported verbatim (sign conventions preserved) from the two card libraries it
 * replaces, so behaviour is identical to what each card shipped:
 *   - line / rect / circle  <- dev/lib/gen2c/gen2_geom.c (gen2_hgr_line/rect/circle)
 *   - ellipse               <- dev/lib/tms9918c/screen2.c
 * The ONLY change is that the per-pixel store and the screen extent are now
 * backend symbols (gfx_plot / gfx_hline / gfx_vline / gfx_width / gfx_height)
 * resolved at link time to whichever card the program links against.
 */
#include "gfx.h"

/* ---- Bresenham line ------------------------------------------------------
 * Straight runs shortcut to the card's fast span (gfx_hline/gfx_vline); the
 * diagonal case walks gfx_plot. Endpoints assumed on-screen (matches the old
 * gen2_hgr_line contract); the H/V shortcuts still clip via the span calls. */
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

/* ---- Rectangle outline ---------------------------------------------------
 * Four spans; interior untouched. Corners normalised so x0<=x1, y0<=y1. */
void gfx_rect(unsigned x0, unsigned char y0, unsigned x1, unsigned char y1)
{
    if (x1 < x0) { unsigned t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { unsigned char t = y0; y0 = y1; y1 = t; }
    gfx_hline(x0, x1, y0);
    gfx_hline(x0, x1, y1);
    gfx_vline(x0, y0, y1);
    gfx_vline(x1, y0, y1);
}

/* Plot only if on-screen — int coords so a centre±radius that runs off an edge
 * is dropped, not wrapped (a uchar/unsigned cast of a negative coord would alias
 * back into a valid pixel). Reads the backend's screen extent. */
static void gfx_plot_clip(int x, int y)
{
    if (x >= 0 && x < (int)gfx_width && y >= 0 && y < (int)gfx_height)
        gfx_plot((unsigned)x, (unsigned char)y);
}

/* ---- Midpoint circle outline --------------------------------------------- */
void gfx_circle(unsigned xc, unsigned char yc, unsigned char r)
{
    int cx = (int)xc, cy = (int)yc;
    int x = 0, y = (int)r;
    int d = 1 - (int)r;
    while (x <= y) {
        gfx_plot_clip(cx + x, cy + y);
        gfx_plot_clip(cx - x, cy + y);
        gfx_plot_clip(cx + x, cy - y);
        gfx_plot_clip(cx - x, cy - y);
        gfx_plot_clip(cx + y, cy + x);
        gfx_plot_clip(cx - y, cy + x);
        gfx_plot_clip(cx + y, cy - x);
        gfx_plot_clip(cx - y, cy - x);
        if (d < 0) { d += (x << 1) + 3; }
        else       { d += ((x - y) << 1) + 5; --y; }
        ++x;
    }
}

/* ---- Ellipse (64-segment polyline) ---------------------------------------
 * cos/sin * 64 for parametric angle i/64 * 2pi (upstream ellipse.s scale). */
static const signed char kEllipseCos64[64] = {
    64, 64, 63, 61, 59, 56, 53, 49, 45, 41, 36, 30, 24, 19, 12, 6,
    0, -6, -12, -19, -24, -30, -36, -41, -45, -49, -53, -56, -59, -61, -63, -64,
    -64, -64, -63, -61, -59, -56, -53, -49, -45, -41, -36, -30, -24, -19, -12, -6,
    0, 6, 12, 19, 24, 30, 36, 41, 45, 49, 53, 56, 59, 61, 63, 64
};
static const signed char kEllipseSin64[64] = {
    0, 6, 12, 19, 24, 30, 36, 41, 45, 49, 53, 56, 59, 61, 63, 64,
    64, 64, 63, 61, 59, 56, 53, 49, 45, 41, 36, 30, 24, 19, 12, 6,
    0, -6, -12, -19, -24, -30, -36, -41, -45, -49, -53, -56, -59, -61, -63, -64,
    -64, -64, -63, -61, -59, -56, -53, -49, -45, -41, -36, -30, -24, -19, -12, -6
};

static unsigned gfx_clamp_x(signed int v)
{
    if (v < 0) return 0;
    if (v >= (int)gfx_width) return (unsigned)(gfx_width - 1u);
    return (unsigned)v;
}
static unsigned char gfx_clamp_y(signed int v)
{
    if (v < 0) return 0;
    if (v >= (int)gfx_height) return (unsigned char)(gfx_height - 1u);
    return (unsigned char)v;
}

void gfx_ellipse(unsigned x0, unsigned char y0, unsigned x1, unsigned char y1)
{
    signed int xc = (((signed int)x0 + (signed int)x1) >> 1);
    signed int yc = (((signed int)y0 + (signed int)y1) >> 1);
    signed int rx = (signed int)((x1 > x0 ? x1 - x0 : x0 - x1)) >> 1;
    signed int ry = (signed int)((y1 > y0 ? y1 - y0 : y0 - y1)) >> 1;
    unsigned char i;

    if (rx < 1) rx = 1;
    if (ry < 1) ry = 1;

    for (i = 0; i < 64U; ++i) {
        unsigned char j = (unsigned char)((i + 1U) & 63U);
        signed int ax = xc + ((signed int)kEllipseCos64[i] * rx) / 64;
        signed int ay = yc + ((signed int)kEllipseSin64[i] * ry) / 64;
        signed int bx = xc + ((signed int)kEllipseCos64[j] * rx) / 64;
        signed int by = yc + ((signed int)kEllipseSin64[j] * ry) / 64;
        gfx_line(gfx_clamp_x(ax), gfx_clamp_y(ay),
                 gfx_clamp_x(bx), gfx_clamp_y(by));
    }
}
