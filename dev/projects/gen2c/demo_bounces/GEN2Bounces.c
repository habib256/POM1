/*
 * GEN2Vectors.c — vector showcase + HUD numbers + XOR sprites from gen2c, in
 *                 double buffering with INCREMENTAL rendering and FAST blit.
 *
 *   GEN2 Vectors/HUD demo / VERHILLE Arnaud 2026
 *
 * FOUR balls (disc sprites) — one big 48x48 and three small 16x16 —
 * bounce inside a frame AND collide pairwise (every pair); bounce counter
 * in HUD, dense caption in 8x8 font. Combines:
 *   E  gen2_hgr_rect / gen2_hgr_line                     (vector decor)
 *   B  gen2_hgr_blit7(..., GEN2_XOR)                     (fast XOR sprites)
 *   D  gen2_hgr_putu_field + gen2_hgr_puts8              (HUD number + 8x8 text)
 *   C  gen2_set_draw_page / gen2_show_page               (double buffering)
 *
 * SPEED — three cumulative levers:
 *   1. The decor is NOT redrawn (drawn once per page).
 *   2. Each ball is erased by XOR (re-blit at the same spot -> background
 *      restored, no box to scrub).
 *   3. Balls are blitted via gen2_hgr_blit7: sprites pre-packed at
 *      7px/byte -> we XOR whole BYTES (~7x fewer writes than pixel-by-pixel
 *      blit). Trade-off: x aligned on 7px (horizontal step of 7).
 *
 * Collision: for each pair, centres within (Ri+Rj) AND closing in
 * (dx.dvx + dy.dvy < 0) -> swap velocities. All balls move at the same
 * speed, so the swap preserves byte alignment.
 *
 *   Build : make    -> "software/Graphic HGR/GEN2Vectors.bin" (+ .txt)
 *   Run   : build/POM1 --preset 11 \
 *               --load 6000:"software/Graphic HGR/GEN2Vectors.bin" --run 6000
 */
#include "gen2.h"

/* Small ball: filled disc 16x16, 7px/byte, 3 bytes/row. */
static const unsigned char kBall7[48] = {
    0x00,0x00,0x00, 0x70,0x1F,0x00, 0x78,0x3F,0x00, 0x7C,0x7F,0x00,
    0x7E,0x7F,0x01, 0x7E,0x7F,0x01, 0x7E,0x7F,0x01, 0x7E,0x7F,0x01,
    0x7E,0x7F,0x01, 0x7E,0x7F,0x01, 0x7E,0x7F,0x01, 0x7E,0x7F,0x01,
    0x7C,0x7F,0x00, 0x78,0x3F,0x00, 0x70,0x1F,0x00, 0x00,0x00,0x00,
};
/* Big ball: filled disc 48x48 (3x), 7px/byte, 7 bytes/row. */
static const unsigned char kBig7[336] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x78,0x7F,0x07,0x00,0x00,
    0x00,0x00,0x7E,0x7F,0x1F,0x00,0x00, 0x00,0x40,0x7F,0x7F,0x7F,0x00,0x00,
    0x00,0x70,0x7F,0x7F,0x7F,0x03,0x00, 0x00,0x78,0x7F,0x7F,0x7F,0x07,0x00,
    0x00,0x7E,0x7F,0x7F,0x7F,0x1F,0x00, 0x00,0x7F,0x7F,0x7F,0x7F,0x3F,0x00,
    0x40,0x7F,0x7F,0x7F,0x7F,0x7F,0x00, 0x40,0x7F,0x7F,0x7F,0x7F,0x7F,0x00,
    0x60,0x7F,0x7F,0x7F,0x7F,0x7F,0x01, 0x70,0x7F,0x7F,0x7F,0x7F,0x7F,0x03,
    0x70,0x7F,0x7F,0x7F,0x7F,0x7F,0x03, 0x78,0x7F,0x7F,0x7F,0x7F,0x7F,0x07,
    0x78,0x7F,0x7F,0x7F,0x7F,0x7F,0x07, 0x7C,0x7F,0x7F,0x7F,0x7F,0x7F,0x0F,
    0x7C,0x7F,0x7F,0x7F,0x7F,0x7F,0x0F, 0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F,
    0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F, 0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F,
    0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F, 0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F,
    0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F, 0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F,
    0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F, 0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F,
    0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F, 0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F,
    0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F, 0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F,
    0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F, 0x7C,0x7F,0x7F,0x7F,0x7F,0x7F,0x0F,
    0x7C,0x7F,0x7F,0x7F,0x7F,0x7F,0x0F, 0x78,0x7F,0x7F,0x7F,0x7F,0x7F,0x07,
    0x78,0x7F,0x7F,0x7F,0x7F,0x7F,0x07, 0x70,0x7F,0x7F,0x7F,0x7F,0x7F,0x03,
    0x70,0x7F,0x7F,0x7F,0x7F,0x7F,0x03, 0x60,0x7F,0x7F,0x7F,0x7F,0x7F,0x01,
    0x40,0x7F,0x7F,0x7F,0x7F,0x7F,0x00, 0x40,0x7F,0x7F,0x7F,0x7F,0x7F,0x00,
    0x00,0x7F,0x7F,0x7F,0x7F,0x3F,0x00, 0x00,0x7E,0x7F,0x7F,0x7F,0x1F,0x00,
    0x00,0x78,0x7F,0x7F,0x7F,0x07,0x00, 0x00,0x70,0x7F,0x7F,0x7F,0x03,0x00,
    0x00,0x40,0x7F,0x7F,0x7F,0x00,0x00, 0x00,0x00,0x7E,0x7F,0x1F,0x00,0x00,
    0x00,0x00,0x78,0x7F,0x07,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};
#define NB    4                       /* 1 big + 3 small                       */

#define FL    4                       /* frame                                 */
#define FT    4
#define FR  275
#define FB  150
#define HSTEP 7                       /* horizontal step = 1 byte (aligned)    */
#define VSTEP 7
#define HUDX 150u
#define HUDY 162u
#define HUDW   5u

/* Per ball: bitmap 7px/byte, bytes/row, side (px), radius (=side/2). */
static const unsigned char *const spr[NB] = { kBig7, kBall7, kBall7, kBall7 };
static const unsigned char       wb[NB]   = { 7u, 3u, 3u, 3u };
static const unsigned char       sz[NB]   = { 48u, 16u, 16u, 16u };
static const int                 rad[NB]  = { 24, 8, 8, 8 };

static void draw_static(void)
{
    gen2_hgr_rect(FL, FT, FR, FB);
    gen2_hgr_line(FL, 156u, FR, 156u);
    gen2_hgr_puts(FL, HUDY, "BOUNCES");
    gen2_hgr_puts8(FL, 182u, "4 XOR BALLS - DBL BUFFER - FAST 7PX BLIT");
}

static void ball(unsigned char b, unsigned x, unsigned char y)   /* fast XOR-blit  */
{
    gen2_hgr_blit7(x, y, wb[b], sz[b], spr[b], GEN2_XOR);
}

void main(void)
{
    unsigned char page = 1u, pidx, huddirty = 2u, i, j;
    int  x[NB]  = { 119, 21, 238, 35 };       /* top-left corner (x mult. of 7)  */
    int  y[NB]  = {  49, 21,  28, 119 };
    int  vx[NB] = { HSTEP,  HSTEP, -HSTEP,  HSTEP };
    int  vy[NB] = { VSTEP,  HSTEP,  VSTEP, -VSTEP };
    int  ox[NB][2], oy[NB][2];
    int  bxmax, bymax, dx, dy, adx, ady, thr;
    unsigned bounces = 0u;

    gen2_hgr_init();

    gen2_set_draw_page(1u); gen2_hgr_clear(0u); draw_static();
    for (i = 0u; i < NB; ++i) ball(i, (unsigned)x[i], (unsigned char)y[i]);
    gen2_set_draw_page(2u); gen2_hgr_clear(0u); draw_static();
    for (i = 0u; i < NB; ++i) ball(i, (unsigned)x[i], (unsigned char)y[i]);
    for (i = 0u; i < NB; ++i) {
        ox[i][0] = ox[i][1] = x[i];
        oy[i][0] = oy[i][1] = y[i];
    }

    for (;;) {
        gen2_graphics(); gen2_hires(); gen2_full();
        pidx = (unsigned char)(page - 1u);
        gen2_set_draw_page(page);

        for (i = 0u; i < NB; ++i) {
            ball(i, (unsigned)ox[i][pidx], (unsigned char)oy[i][pidx]);  /* XOR erase */
            ball(i, (unsigned)x[i], (unsigned char)y[i]);                /* XOR draw  */
            ox[i][pidx] = x[i];
            oy[i][pidx] = y[i];
        }

        if (huddirty) { gen2_hgr_putu_field(HUDX, HUDY, bounces, HUDW); --huddirty; }

        gen2_show_page();
        page = (page == 1u) ? 2u : 1u;

        /* Walls: bounds aligned on 7 in x (byte alignment preserved). */
        for (i = 0u; i < NB; ++i) {
            bxmax = ((FR - 2 - (int)sz[i]) / 7) * 7;
            bymax = FB - 2 - (int)sz[i];
            x[i] += vx[i];
            if (x[i] <= 7)          { x[i] = 7;     vx[i] = -vx[i]; ++bounces; huddirty = 2u; }
            else if (x[i] >= bxmax) { x[i] = bxmax; vx[i] = -vx[i]; ++bounces; huddirty = 2u; }
            y[i] += vy[i];
            if (y[i] <= FT + 2)     { y[i] = FT + 2; vy[i] = -vy[i]; ++bounces; huddirty = 2u; }
            else if (y[i] >= bymax) { y[i] = bymax;  vy[i] = -vy[i]; ++bounces; huddirty = 2u; }
        }

        /* Collision: all pairs (i<j). */
        for (i = 0u; i < NB; ++i) {
            for (j = (unsigned char)(i + 1u); j < NB; ++j) {
                dx  = (x[i] + rad[i]) - (x[j] + rad[j]);
                dy  = (y[i] + rad[i]) - (y[j] + rad[j]);
                adx = (dx < 0) ? -dx : dx;
                ady = (dy < 0) ? -dy : dy;
                thr = rad[i] + rad[j];
                if (adx < thr && ady < thr && (adx * adx + ady * ady) < thr * thr) {
                    if (dx * (vx[i] - vx[j]) + dy * (vy[i] - vy[j]) < 0) {  /* closing  */
                        int t;
                        t = vx[i]; vx[i] = vx[j]; vx[j] = t;
                        t = vy[i]; vy[i] = vy[j]; vy[j] = t;
                        ++bounces; huddirty = 2u;
                    }
                }
            }
        }
    }
}
