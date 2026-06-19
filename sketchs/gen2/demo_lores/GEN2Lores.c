/*
 * GEN2Lores.c — demonstration of the LORES mode (40x48, 16 colours) on Uncle
 *               Bernie's GEN2 card, written in C with the gen2c runtime.
 *
 *   GEN2 LORES demo / VERHILLE Arnaud 2026
 *
 * Unlike HIRES (colour = NTSC artefact of the bit pattern), LORES has a
 * REAL per-block colour: a 40x48 grid of 7px*4px blocks, 16 colours. This
 * demo:
 *   1. shows the 16 palette colours as vertical bars,
 *   2. draws a white frame around the screen,
 *   3. draws a rainbow diagonal,
 * then keeps the image on screen.
 *
 *   Build : make    -> "software/Graphic HGR/GEN2Lores.bin" (+ .txt)
 *   Run   : build/POM1 --preset 11 \
 *               --load 6000:"software/Graphic HGR/GEN2Lores.bin" --run 6000
 *           or: DevBench -> POM1 Bench -> new C sketch / GEN2 HGR,
 *                paste this file (the gen2c runtime handles LORES and HIRES).
 *
 * We DRAW FIRST: drawing writes the $0400 text page (RAM), which PERSISTS.
 * Then we loop re-asserting LORES mode — it is instantaneous and it handles
 * the DEFERRED plug of the card by DevBench (~15 frames): as soon as the
 * card is there, the mode takes effect and the already-drawn image appears.
 * No long wait loop BEFORE drawing (otherwise the screen stays black too long).
 */
#include "gen2.h"

void main(void)
{
    unsigned char x, y, c;

    /* --- Mode + draw, right away (the drawing persists in RAM $0400). --- */
    gen2_lores_init();
    gen2_lores_clear(GEN2_LO_BLACK);

    /* 1. The 16 colours as full-height vertical bars, 2 columns each
     *    (16*2 = 32 columns, centred: start column 4). */
    for (c = 0; c < 16u; ++c) {
        x = (unsigned char)(4u + c * 2u);
        gen2_lores_vlin(x,                       0u, 47u, c);
        gen2_lores_vlin((unsigned char)(x + 1u), 0u, 47u, c);
    }

    /* 2. White frame all around the screen (HLIN/VLIN, bounds inclusive). */
    gen2_lores_hlin(0u, 39u, 0u,  GEN2_LO_WHITE);
    gen2_lores_hlin(0u, 39u, 47u, GEN2_LO_WHITE);
    gen2_lores_vlin(0u,  0u, 47u, GEN2_LO_WHITE);
    gen2_lores_vlin(39u, 0u, 47u, GEN2_LO_WHITE);

    /* 3. Rainbow diagonal via individual blocks (setblock). */
    for (y = 2u; y < 46u; ++y) {
        x = (unsigned char)(2u + y);            /* x = 4..47 -> clip to 39 */
        gen2_lores_setblock(x, y, (unsigned char)(y & 0x0Fu));
    }

    /* Hold the image: re-assert LORES mode in a loop. Instantaneous
     * (4 soft-switch reads) and it covers DevBench's deferred plug —
     * the image itself is already drawn. */
    for (;;) { gen2_lores_init(); }
}
