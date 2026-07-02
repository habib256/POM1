/* main.c -- GEN2 HGR masked sprite ENGINE demo (save-under over decor).
 *
 * Four SCROLL-O-SPRITES creatures (sprites.txt -> sprites_msk.h, baked by
 * tools/build_preshift_sprites.py --masked --halo 1) bounce OVER a busy,
 * multi-coloured background -- coloured bands, rectangles and text. That
 * background is the whole point: an XOR sprite (demo_preshift) would toggle
 * the decor to garbage wherever it overlaps; the masked engine draws OPAQUE
 * sprites (dst = (dst & mask) | data, mask bit 7 = 1 so the background's
 * palette bit survives) and puts every covered byte back EXACTLY from its
 * save-under buffer each frame.
 *
 * Starts DOUBLE-BUFFERED (tear-free by construction: restore+draw happen on
 * the hidden page, only the $C254/$C255 flip sits in V-blank). SPACE toggles
 * to SINGLE-buffer mode (restore+draw raced inside the ~4200-cycle V-blank
 * window -- with four 16x16 creatures it overflows and visibly tears near the
 * top of the frame, but never corrupts the background: that contrast is the
 * demo). Any other key exits to the Woz Monitor.
 *
 * Build:  make            -> ./main.bin  (load in POM1 GEN2 preset, run 6000R)
 * Test:   make selftest   -> ./main_test.bin (phased draw/restore round-trip
 *         driven by tools/test_sprengine_gen2.py -- see that harness). */

#include "gen2.h"
#include "sprites_msk.h"   /* generated: gen2_mspr_t wolf, dragon, cyclops, blob */

#define NSPR    4
#define SPR_W   16
#define SPR_H   16
#define X_MIN   0
#define X_MAX   (280 - SPR_W)     /* 264 */
#define Y_MIN   26                /* below the title band     */
#define Y_MAX   (158 - SPR_H)     /* above the bottom band    */

static const gen2_mspr_t *shapes[NSPR] = { &wolf, &dragon, &cyclops, &blob };

/* Deliberately NON-uniform decor: coloured bands + rects + text everywhere the
 * sprites fly. fill/colorize widths are unsigned char, so full-width bands go
 * in two halves (cf. demo_preshift). Drawn per page (double buffering). */
static void draw_background(unsigned char dbuf)
{
    gen2_hgr_clear(0);

    /* title band (blue) + bottom band (green) */
    gen2_hgr_fill_pixrect(0, 0, 140, 24);
    gen2_hgr_fill_pixrect(140, 0, 140, 24);
    gen2_hgr_colorize(0, 0, 140, 24, GEN2_BLUE);
    gen2_hgr_colorize(140, 0, 140, 24, GEN2_BLUE);
    gen2_hgr_fill_pixrect(0, 160, 140, 32);
    gen2_hgr_fill_pixrect(140, 160, 140, 32);
    gen2_hgr_colorize(0, 160, 140, 32, GEN2_GREEN);
    gen2_hgr_colorize(140, 160, 140, 32, GEN2_GREEN);

    /* mid-field decor the sprites pass over */
    gen2_hgr_fill_pixrect(14, 60, 56, 40);
    gen2_hgr_colorize(14, 60, 56, 40, GEN2_VIOLET);
    gen2_hgr_fill_pixrect(112, 84, 56, 48);
    gen2_hgr_colorize(112, 84, 56, 48, GEN2_ORANGE);
    gen2_hgr_fill_pixrect(210, 52, 56, 40);         /* white block */
    gen2_hgr_puts8(84, 40, "MASKED SAVE-UNDER");
    gen2_hgr_puts8(84, 140, "DECOR SURVIVES");

    gen2_hgr_puts(4, 4, "SPRITE ENGINE");
    gen2_hgr_puts8(4, 166, dbuf ? "MODE: DOUBLE BUFFER (TEAR-FREE)"
                                : "MODE: SINGLE BUFFER (VBL RACE)");
    gen2_hgr_puts8(4, 178, "SPACE=MODE  OTHER=EXIT");
}

#ifndef SPRENGINE_SELFTEST

/* Redraw the decor on both pages + (re)start the engine in `dbuf` mode.
 * gen2_spr_init forgets the under-buffers, so it must only run over freshly
 * clean backgrounds -- which this guarantees. */
static void start_mode(unsigned char dbuf)
{
    gen2_set_draw_page(1u);
    draw_background(dbuf);
    gen2_set_draw_page(2u);
    draw_background(dbuf);
    gen2_spr_init(dbuf);
}

void main(void)
{
    static unsigned      sx[NSPR] = {  10u, 200u, 120u,  60u };
    static unsigned char sy[NSPR] = {  30u,  50u, 100u, 130u };
    static int           vx[NSPR] = {   2,   -3,    1,   -2 };
    static int           vy[NSPR] = {   1,    1,   -2,   -1 };
    unsigned char dbuf = 1u;
    unsigned char i, k;

    gen2_hgr_init();                     /* graphics + hires + page 1 */
    start_mode(dbuf);
    for (i = 0u; i < NSPR; ++i) {
        gen2_spr_define(i, shapes[i]);
        gen2_spr_move(i, sx[i], sy[i]);
    }

    for (;;) {
        k = apple1_readkey();
        if (k == ' ') {                  /* toggle: clean pages + re-init */
            dbuf = (unsigned char)!dbuf;
            start_mode(dbuf);
            for (i = 0u; i < NSPR; ++i) gen2_spr_define(i, shapes[i]);
        } else if (k != 0u) {
            break;
        }

        for (i = 0u; i < NSPR; ++i) {    /* step + bounce off the field edges */
            int x = (int)sx[i] + vx[i];
            int y = (int)sy[i] + vy[i];
            if (x <= X_MIN) { x = X_MIN; vx[i] = -vx[i]; }
            if (x >= X_MAX) { x = X_MAX; vx[i] = -vx[i]; }
            if (y <= Y_MIN) { y = Y_MIN; vy[i] = -vy[i]; }
            if (y >= Y_MAX) { y = Y_MAX; vy[i] = -vy[i]; }
            sx[i] = (unsigned)x;
            sy[i] = (unsigned char)y;
            gen2_spr_move(i, sx[i], sy[i]);
        }
        gen2_spr_update();               /* restore + draw (+ flip if dbuf) */
    }

    woz_mon();
}

#else /* SPRENGINE_SELFTEST ------------------------------------------------- */

/* Phased self-test driven by tools/test_sprengine_gen2.py. Single-buffer on
 * page 1 so --dump-gen2-frame always captures the page being drawn. Three
 * WIDE, deterministic windows (frame-counted via gen2_wait_vbl, 17030
 * cycles/frame) the harness samples with --dump-after-cycles:
 *
 *   window A: background only                (dump0 -> baseline hash)
 *   window B: 3 sprites drawn over the decor (dump1 -> must differ)
 *   window C: sprites hidden + restored      (dump2 -> must equal dump0:
 *             the masked draw + save-under/restore round-trip is byte-exact)
 */
static void idle_frames(unsigned n)
{
    while (n--) gen2_wait_vbl();
}

void main(void)
{
    unsigned char i;

    gen2_hgr_init();
    draw_background(0u);                 /* page 1 only */
    gen2_spr_init(0u);                   /* single buffer, page 1 */
    for (i = 0u; i < 3u; ++i) {
        gen2_spr_define(i, shapes[i]);
    }
    /* park the sprites ON the decor (violet/orange rects + text) so a mask or
     * restore bug lands on non-black bytes and changes the hash */
    gen2_spr_move(0u, 20u, 70u);
    gen2_spr_move(1u, 120u, 90u);
    gen2_spr_move(2u, 215u, 56u);

    idle_frames(300u);                   /* window A: ~5.1M cycles           */
    gen2_spr_update();                   /* draw all three (save-under)      */
    idle_frames(400u);                   /* window B: ~6.8M cycles           */
    for (i = 0u; i < 3u; ++i) gen2_spr_hide(i);
    gen2_spr_update();                   /* restore -> background again      */
    for (;;) { }                         /* window C: forever                */
}

#endif /* SPRENGINE_SELFTEST */
