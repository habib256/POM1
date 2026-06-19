/* main.c — smallest TMS9918 C program with sprites.
 *
 * Sets up Graphics I (text mode + sprites), uploads one 8x8 sprite pattern,
 * places it on screen via the SHADOW workflow (no per-frame tearing), and
 * waits for a key. About 18 lines of "real" code.
 *
 * COPY THIS FOLDER to start a TMS9918 sprite project. Build with `make`, then
 * load the .bin in POM1 preset 9 (TMS9918 CodeTank) and run with 4000R.
 *
 * Why this is small:
 *   - The Makefile only links the families we call (CORE + SCREEN1 + SPRITES
 *     + APPLE1). Vsync / printlib / random / interrupt families are skipped.
 *   - The shadow API (tms_shadow_*) is the no-tearing way to handle sprites:
 *     mutate a 128-byte RAM mirror, then push the whole SAT in one burst at
 *     VBlank. NEVER call tms_set_sprite() per frame — that writes mid-frame
 *     and tears visibly. */

#include "tms9918c.h"

/* 8x8 sprite pattern — a filled square outline. Stored MSB-first, one byte per
 * row. Real sprites should live in flash/ROM; this is fine for the demo. */
static const unsigned char sprite_pattern[8] = {
    0xFFu, 0x81u, 0x81u, 0x81u,
    0x81u, 0x81u, 0x81u, 0xFFu,
};

void main(void)
{
    tms_sprite s;
    /* Mode init: Graphics I + cyan background, font in VRAM. */
    tms_init_regs(SCREEN1_TABLE);
    tms_set_color(COLOR_CYAN);
    screen1_prepare();
    screen1_load_font();
    screen1_puts((const unsigned char *)"SPRITES via SHADOW (no tearing)");

    /* Upload our sprite pattern to VRAM (pattern 0). */
    tms_set_vram_write_addr(TMS_SPRITE_PATTERNS);
    tms_copy_to_vram(sprite_pattern, sizeof sprite_pattern, TMS_SPRITE_PATTERNS);

    /* Shadow workflow: init RAM mirror, place one sprite, flush in one burst. */
    tms_shadow_init();
    s.y = 80; s.x = 120; s.name = 0; s.color = COLOR_WHITE;
    tms_shadow_set(0, &s);
    tms_shadow_set_terminator(1);
    tms_shadow_flush();

    (void)apple1_getkey();         /* wait for a keypress */
    woz_mon();                     /* return to Wozmon */
}
