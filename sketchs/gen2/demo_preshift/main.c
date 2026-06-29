/* main.c -- GEN2 HGR pre-shifted sprite engine demo (the "Buzzard Bait" method).
 *
 * Shows the pre-shifted sprite engine: each sprite is baked into 7 pre-shifted
 * phases offline (sprites.txt -> sprites_ps.h, built by
 * tools/build_preshift_sprites.py), so a 1px-precise blit is just "pick the phase
 * for x%7, byte-blit at x/7" -- no per-pixel shifting.
 *
 * SINGLE-BUFFER + V-blank sync, exactly how Buzzard Bait does it -- and it is
 * flicker-free because the ball uses gen2_hgr_sprite_xor, the ALL-ASM XOR fast
 * path (no cc65 wrapper: x/7, x%7, phase offset and edge clip are done in asm).
 * That keeps the erase+redraw pair inside V-blank, so the beam never catches a
 * mid-update (the general gen2_hgr_sprite C path was ~3-4x heavier and spilled
 * past V-blank into the top scanlines). POM1's GEN2 renderer is also beam-
 * accurate (latches per scanline, frozen per frame at the rollover).
 * XOR is non-destructive, so the green floor and the ship survive.
 *
 * Build:  make            -> ./main.bin  (load in POM1 GEN2 preset, run 6000R) */

#include "gen2.h"
#include "sprites_ps.h"        /* generated banks: gen2_sprite_t ball, ship */

/* Ball is 11x11 (sprites.txt). Play area: full width, from just under the ship
 * down to the top of the floor, so it bounces ON the floor. */
#define BALL_W   11
#define BALL_H   11
#define X_MAX   (280 - BALL_W)   /* 269 */
#define Y_TOP    32              /* just below the ship            */
#define Y_BOT   (150 - BALL_H)   /* 139: rests on top of the floor */

void main(void)
{
    int x = 24, y = 40;          /* current ball position (top-left)       */
    int vx = 2,  vy = 1;         /* velocity (px/frame), different periods */
    int px = 24, py = 40;        /* where it was drawn last frame          */

    gen2_hgr_init();                                   /* graphics + hires + page1 */
    gen2_hgr_clear(0);                                 /* black */

    /* green floor (two bands, since fill width is an unsigned char <= 255) */
    gen2_hgr_fill_pixrect(0,   150, 140, 4);
    gen2_hgr_fill_pixrect(140, 150, 140, 4);
    gen2_hgr_colorize(0,   150, 140, 4, GEN2_GREEN);
    gen2_hgr_colorize(140, 150, 140, 4, GEN2_GREEN);

    gen2_hgr_sprite(120, 20, &ship, GEN2_SET);         /* static opaque ship (SET) */
    gen2_hgr_sprite_xor((unsigned)px, (unsigned char)py, &ball);   /* first draw */

    while (!apple1_iskeypressed()) {
        gen2_wait_vbl();                               /* 60fps pace + draw in V-blank */
        gen2_hgr_sprite_xor((unsigned)px, (unsigned char)py, &ball); /* erase old */

        x += vx;                                       /* step + bounce off the walls */
        if (x <= 0)     { x = 0;     vx = -vx; }
        if (x >= X_MAX) { x = X_MAX; vx = -vx; }
        y += vy;
        if (y <= Y_TOP) { y = Y_TOP; vy = -vy; }
        if (y >= Y_BOT) { y = Y_BOT; vy = -vy; }

        gen2_hgr_sprite_xor((unsigned)x, (unsigned char)y, &ball);   /* draw new */
        px = x; py = y;
    }

    (void)apple1_getkey();
    woz_mon();
}
