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
 * This demo bounces a 16x16 block all over the screen. The loop:
 *   1. draws the entire frame (clear + title + block) into the hidden page,
 *   2. flips it on screen in one go,
 *   3. swaps the draw page for the next frame.
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

void main(void)
{
    unsigned char draw = 2u;      /* start by filling the hidden page        */
    unsigned      bx = 40u;       /* block position (x fits in 16 bits)      */
    unsigned char by = 30u;
    unsigned char xpos = 1u;      /* direction: 1 = towards +, 0 = towards - */
    unsigned char ypos = 1u;

    gen2_hgr_init();              /* HIRES, full screen, show page 1         */

    for (;;) {
        /* Mode re-asserted every frame (robust to DevBench deferred plug). */
        gen2_graphics();
        gen2_hires();
        gen2_full();

        /* 1. Draw the ENTIRE frame into the hidden page. */
        gen2_set_draw_page(draw);
        gen2_hgr_clear(0u);
        gen2_hgr_puts(8u, 4u, "DOUBLE BUFFER");
        gen2_hgr_fill_pixrect(bx, by, BALL, BALL);

        /* 2. Flip it on screen (tear-free flip). */
        gen2_show_page();

        /* 3. The next frame will go into the OTHER page. */
        draw = (draw == 1u) ? 2u : 1u;

        /* Advance + bounce on the four edges. */
        if (xpos) { bx += STEP; if (bx >= XMAX) { bx = XMAX; xpos = 0u; } }
        else      { if (bx < STEP) { bx = 0u; xpos = 1u; } else bx -= STEP; }
        if (ypos) { by += STEP; if (by >= YMAX) { by = (unsigned char)YMAX; ypos = 0u; } }
        else      { if (by < STEP) { by = 0u; ypos = 1u; } else by -= STEP; }
    }
}
