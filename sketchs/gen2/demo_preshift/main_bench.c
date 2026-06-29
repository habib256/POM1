/* main_bench.c -- SELF-CONTAINED pre-shifted sprite demo for DevBench.
 *
 * DevBench compiles ONE buffer, so it can't resolve the multi-file demo's
 * `#include "sprites_ps.h"`. This single-file version inlines a sprite bank's
 * 7-phase data (normally emitted by tools/build_preshift_sprites.py or the HGR
 * Paint "Sprite Bank" export), so you can just open it in DevBench and Run.
 *
 * gen2_hgr_sprite_xor() (dev/lib/gen2c/gen2_preshift.c + gen2_blit.s) is the
 * all-asm XOR fast path of the pre-shift engine, linked by the GEN2 HGR C Bench
 * target. A ball BOUNCES 1px at a time in both axes, SINGLE-BUFFER + V-blank sync
 * the Buzzard-Bait way -- flicker-free because the asm draw is fast enough to fit
 * the erase+redraw inside V-blank. XOR keeps the green floor intact. */

#include "gen2.h"

/* --- inlined 7-phase pre-shifted bank (9x9 ball, stride 3, 189 B) ----------- */
static const unsigned char ball_ps[189] = {
    0x10, 0x00, 0x00, 0x7C, 0x00, 0x00, 0x7E, 0x01, 0x00, 0x7E, 0x01, 0x00,
    0x7F, 0x03, 0x00, 0x7E, 0x01, 0x00, 0x7E, 0x01, 0x00, 0x7C, 0x00, 0x00,
    0x10, 0x00, 0x00, 0x20, 0x00, 0x00, 0x78, 0x01, 0x00, 0x7C, 0x03, 0x00,
    0x7C, 0x03, 0x00, 0x7E, 0x07, 0x00, 0x7C, 0x03, 0x00, 0x7C, 0x03, 0x00,
    0x78, 0x01, 0x00, 0x20, 0x00, 0x00, 0x40, 0x00, 0x00, 0x70, 0x03, 0x00,
    0x78, 0x07, 0x00, 0x78, 0x07, 0x00, 0x7C, 0x0F, 0x00, 0x78, 0x07, 0x00,
    0x78, 0x07, 0x00, 0x70, 0x03, 0x00, 0x40, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x60, 0x07, 0x00, 0x70, 0x0F, 0x00, 0x70, 0x0F, 0x00, 0x78, 0x1F, 0x00,
    0x70, 0x0F, 0x00, 0x70, 0x0F, 0x00, 0x60, 0x07, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x02, 0x00, 0x40, 0x0F, 0x00, 0x60, 0x1F, 0x00, 0x60, 0x1F, 0x00,
    0x70, 0x3F, 0x00, 0x60, 0x1F, 0x00, 0x60, 0x1F, 0x00, 0x40, 0x0F, 0x00,
    0x00, 0x02, 0x00, 0x00, 0x04, 0x00, 0x00, 0x1F, 0x00, 0x40, 0x3F, 0x00,
    0x40, 0x3F, 0x00, 0x60, 0x7F, 0x00, 0x40, 0x3F, 0x00, 0x40, 0x3F, 0x00,
    0x00, 0x1F, 0x00, 0x00, 0x04, 0x00, 0x00, 0x08, 0x00, 0x00, 0x3E, 0x00,
    0x00, 0x7F, 0x00, 0x00, 0x7F, 0x00, 0x40, 0x7F, 0x01, 0x00, 0x7F, 0x00,
    0x00, 0x7F, 0x00, 0x00, 0x3E, 0x00, 0x00, 0x08, 0x00,
};
static const gen2_sprite_t ball = { ball_ps, 3, 9 };

/* Ball is 9x9. Bounce area: full width, top of screen down to the floor. */
#define BALL_W   9
#define BALL_H   9
#define X_MAX   (280 - BALL_W)   /* 271 */
#define Y_BOT   (150 - BALL_H)   /* 141: rests on the floor */

void main(void)
{
    int x = 20, y = 16;          /* current ball position (top-left)       */
    int vx = 2,  vy = 1;         /* velocity (px/frame), different periods */
    int px = 20, py = 16;        /* where it was drawn last frame          */

    gen2_hgr_init();
    gen2_hgr_clear(0);

    /* green floor, drawn ONCE -- XOR is non-destructive, so it survives.
     * (fill width is an unsigned char <= 255 -> two bands for the full 280px.) */
    gen2_hgr_fill_pixrect(0,   150, 140, 4);
    gen2_hgr_fill_pixrect(140, 150, 140, 4);
    gen2_hgr_colorize(0,   150, 140, 4, GEN2_GREEN);
    gen2_hgr_colorize(140, 150, 140, 4, GEN2_GREEN);

    gen2_hgr_sprite_xor((unsigned)px, (unsigned char)py, &ball);   /* first draw */

    /* Single-buffer + V-blank sync, the Buzzard-Bait way: once per frame, in
     * V-blank, erase the ball at its old spot, step it, redraw. Flicker-free
     * because gen2_hgr_sprite_xor is the all-asm XOR fast path -- the erase+redraw
     * pair fits inside V-blank instead of racing the beam. */
    while (!apple1_iskeypressed()) {
        gen2_wait_vbl();
        gen2_hgr_sprite_xor((unsigned)px, (unsigned char)py, &ball);  /* erase old */

        x += vx;
        if (x <= 0)     { x = 0;     vx = -vx; }
        if (x >= X_MAX) { x = X_MAX; vx = -vx; }
        y += vy;
        if (y <= 0)     { y = 0;     vy = -vy; }
        if (y >= Y_BOT) { y = Y_BOT; vy = -vy; }

        gen2_hgr_sprite_xor((unsigned)x, (unsigned char)y, &ball);    /* draw new */
        px = x; py = y;
    }

    (void)apple1_getkey();
    woz_mon();
}
