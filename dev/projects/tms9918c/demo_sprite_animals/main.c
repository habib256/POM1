/*
 * sprite_animals — TMS9918 sprites (CodeTank preset 9, Wozmon 4000R).
 *
 * Quatre silhouettes Fauna fixes (chien, poulpe, chauve-souris, lion),
 * 16×16 sans grossissement (1 pixel écran = 1 pixel sprite).
 * dev/lib/tms9918/sprites_fauna.asm — SCROLL-O-SPRITES « Fauna », CC-BY Quale.
 */
#include "tms9918.h"
#include "screen2.h"
#include "sprites.h"

#define FAUNA_PAT_BYTES 32U

extern const unsigned char fauna_dog_pat[];
extern const unsigned char fauna_octopus_pat[];
extern const unsigned char fauna_bat_pat[];
extern const unsigned char fauna_lion_pat[];

typedef const unsigned char *fauna_ptr_t;

static void upload_fauna_slot(unsigned char slot, fauna_ptr_t pat) {
    unsigned dest = TMS_SPRITE_PATTERNS + (unsigned)slot * FAUNA_PAT_BYTES;
    tms_copy_to_vram(pat, FAUNA_PAT_BYTES, dest);
}

/* sin(2*pi*i/32)*12 */
static const signed char sin32[32] = {
    0, 2, 5, 7, 8, 10, 11, 12, 12, 12, 11, 10, 8, 7, 5, 2,
    0, -2, -5, -7, -8, -10, -11, -12, -12, -12, -11, -10, -8, -7, -5, -2
};

static unsigned char clamp_x(signed int x) {
    if (x < 8) {
        return 8U;
    }
    if (x > 232) {
        return 232U;
    }
    return (unsigned char)x;
}

static unsigned char clamp_y(signed int y) {
    if (y < 24) {
        return 24U;
    }
    if (y > 160) {
        return 160U;
    }
    return (unsigned char)y;
}

void main(void) {
    tms_sprite s[4];
    unsigned int t = 0U;
    unsigned char phase;
    unsigned char i;
    signed int px, py;
    signed char dx, dy;

    tms_init_regs(SCREEN2_TABLE);
    tms_set_color(FG_BG(COLOR_DARK_GREEN, COLOR_BLACK));
    screen2_init_bitmap(FG_BG(COLOR_DARK_GREEN, COLOR_BLACK));

    screen2_puts((const char *)"Fauna  dog oct bat lion", 1U, 1U, FG_BG(COLOR_WHITE, COLOR_DARK_GREEN));

    /* 16×16 natif : pas de grossissement (REG1 bit MAG = 0). */
    tms_set_sprite_double_size(1U);
    tms_set_sprite_magnification(0U);
    tms_clear_collisions();

    upload_fauna_slot(0U, fauna_dog_pat);
    upload_fauna_slot(1U, fauna_octopus_pat);
    upload_fauna_slot(2U, fauna_bat_pat);
    upload_fauna_slot(3U, fauna_lion_pat);

    s[0].name = 0U;
    s[0].color = (unsigned char)(COLOR_WHITE & 15U);
    s[1].name = 4U;
    s[1].color = (unsigned char)(COLOR_LIGHT_BLUE & 15U);
    s[2].name = 8U;
    s[2].color = (unsigned char)(COLOR_GREY & 15U);
    s[3].name = 12U;
    s[3].color = (unsigned char)(COLOR_DARK_YELLOW & 15U);

    for (i = 0U; i < 4U; ++i) {
        s[i].y = (signed char)(36 + (signed char)(i * 26));
        s[i].x = (unsigned char)(32 + i * 52U);
        tms_set_sprite(i, &s[i]);
    }
    tms_set_total_sprites(4U);

    for (;;) {
        phase = (unsigned char)(t >> 2);

        dx = sin32[phase & 31U];
        dy = sin32[(unsigned char)(phase + 8U) & 31U];
        px = 88 + (signed int)dx;
        py = 44 + (signed int)dy;
        s[0].x = clamp_x(px);
        s[0].y = (signed char)clamp_y(py);

        dx = sin32[(unsigned char)((phase * 2U) & 31U)];
        dy = sin32[(unsigned char)(phase + 17U) & 31U];
        px = 132 + (signed int)dx;
        py = 76 + ((signed int)dy * 3) / 5;
        s[1].x = clamp_x(px);
        s[1].y = (signed char)clamp_y(py);

        dx = sin32[(unsigned char)((phase * 3U) & 31U)];
        dy = sin32[(unsigned char)((phase * 3U + 11U) & 31U)];
        px = 116 + ((signed int)dx * 2) / 3;
        py = 100 + ((signed int)dy * 2) / 3;
        s[2].x = clamp_x(px);
        s[2].y = (signed char)clamp_y(py);

        dx = sin32[(unsigned char)(phase + 3U) & 31U];
        px = 48 + (signed int)dx + (signed int)((t >> 4) & 31U);
        py = 124 + ((signed int)sin32[(unsigned char)((phase * 5U) & 31U)] * 2) / 7;
        s[3].x = clamp_x(px);
        s[3].y = (signed char)clamp_y(py);

        for (i = 0U; i < 4U; ++i) {
            tms_set_sprite(i, &s[i]);
        }

        ++t;
        tms_wait_end_of_frame();
    }
}
