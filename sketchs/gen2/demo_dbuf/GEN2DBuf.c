/*
 * GEN2DBuf.c — demonstration of the DOUBLE BUFFERING (PAGE2) of Uncle Bernie's
 *              GEN2 card, in C with the gen2c runtime.
 *
 *   GEN2 Double Buffer demo / VERHILLE Arnaud 2026
 *
 * The card has TWO HIRES framebuffers: page 1 ($2000) and page 2 ($4000).
 * Instead of drawing onto the displayed page (which shows the half-drawn
 * image — flicker / tearing), we draw the next frame into the HIDDEN page,
 * then flip the display to it. The viewer only ever sees complete images:
 * smooth full-screen animation.
 *
 *   gen2_set_draw_page(p) — picks where primitives write (1 or 2)
 *   gen2_show_page()      — displays the current draw page ($C254/$C255)
 *
 * This demo bounces a 16x16 block all over the screen. The static decor is
 * drawn ONCE on both pages; then each frame touches only the hidden page's old
 * and new block positions with XOR:
 *   1. XOR the previous block off the hidden page,
 *   2. XOR the block at its new position,
 *   3. flip it on screen in one go.
 *
 *   Build : make    -> "software/Graphic HGR/GEN2DBuf.bin" (+ .txt)
 *   Run   : build/POM1 --preset 11 \
 *               --load 6000:"software/Graphic HGR/GEN2DBuf.bin" --run 6000
 *
 * The mode (graphics/hires/full) is re-asserted every frame: instantaneous
 * and it covers DevBench's DEFERRED plug of the card (~15 frames).
 * gen2_show_page() flips the page switch; no need for gen2_hgr_init in the
 * loop (it would force display of page 1 and break the flip).
 */
#include "gen2.h"

#define BALL  16u                 /* 16x16 block                             */
#define STEP   4u                 /* pixels per frame                        */
#define XMAX  (280u - BALL)       /* max x position (right edge)             */
#define YMAX  (192u - BALL)       /* max y position (bottom edge)            */

static const unsigned char kBallXor[32] = {
    0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF,
    0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF,
    0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF,
    0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF,
};

static void xor_ball(unsigned x, unsigned char y)
{
    gen2_hgr_blit(x, y, BALL, BALL, kBallXor, GEN2_XOR);
}

static void draw_static_page(unsigned char page, unsigned x, unsigned char y)
{
    gen2_set_draw_page(page);
    gen2_hgr_clear(0u);
    gen2_hgr_puts(8u, 4u, "XOR DBUF");
    xor_ball(x, y);
}

void main(void)
{
    unsigned char draw = 2u;      /* start by filling the hidden page        */
    unsigned      bx = 40u;       /* block position (x fits in 16 bits)      */
    unsigned char by = 30u;
    unsigned      oldx[3];        /* last block position stored per page     */
    unsigned char oldy[3];
    unsigned char xpos = 1u;      /* direction: 1 = towards +, 0 = towards - */
    unsigned char ypos = 1u;

    gen2_hgr_init();              /* HIRES, full screen, show page 1         */
    oldx[1] = oldx[2] = bx;
    oldy[1] = oldy[2] = by;
    draw_static_page(1u, bx, by);
    draw_static_page(2u, bx, by);
    gen2_set_draw_page(1u);
    gen2_show_page();

    for (;;) {
        /* Mode re-asserted every frame (robust to DevBench deferred plug). */
        gen2_graphics();
        gen2_hires();
        gen2_full();

        /* Touch ONLY what changed on the hidden page. That page still contains
         * the block position from the last time it was displayed. */
        gen2_set_draw_page(draw);
        xor_ball(oldx[draw], oldy[draw]);       /* erase old via XOR        */
        xor_ball(bx, by);                       /* draw new via XOR         */
        oldx[draw] = bx;
        oldy[draw] = by;

        /* Flip it on screen. The hidden page is now fully drawn, but the
         * $C254/$C255 switch takes effect mid-scan if we flip while the beam is
         * still in the visible area -> the top of the screen shows the new page
         * and the bottom the old one (a brief ghost of the previous position).
         * Wait for V-blank first so the flip lands between frames: the whole
         * next frame shows the freshly drawn page, genuinely tear-free. Also
         * paces the loop to one frame per refresh. */
        gen2_wait_vbl();
        gen2_show_page();

        /* The next frame will go into the OTHER page. */
        draw = (draw == 1u) ? 2u : 1u;

        /* Advance + bounce on the four edges. */
        if (xpos) { bx += STEP; if (bx >= XMAX) { bx = XMAX; xpos = 0u; } }
        else      { if (bx < STEP) { bx = 0u; xpos = 1u; } else bx -= STEP; }
        if (ypos) { by += STEP; if (by >= YMAX) { by = (unsigned char)YMAX; ypos = 0u; } }
        else      { if (by < STEP) { by = 0u; ypos = 1u; } else by -= STEP; }
    }
}

