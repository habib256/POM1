/*
 * Frogger mini — Apple-1 + CodeTank + TMS9918 (cc65, preset 7, 4000R)
 * Grenouille en sprite matériel (SPGT $1800), berges herbe sur les bords,
 * eau animée (2 motifs). A/D/W, Espace/Entrée pour rejouer.
 */
#include "tms9918.h"
#include "screen1.h"
#include "apple1.h"
#include "sprites.h"

#define LANE_FIRST  9U
#define LANE_COUNT  6U
#define LANE_WIDTH  32U
#define GOAL_ROW    2U
#define START_ROW   22U
#define START_COL   15U

#define PAT_GRASS    1U
#define PAT_WATER_A  8U
#define PAT_WATER_B  9U
#define PAT_LILY    16U
#define PAT_CAR     24U
#define PAT_EMPTY   32U

#define SPR_FROG_PAT  0U
#define SPR_FROG_COL  3U   /* COLOR_LIGHT_GREEN — pixels 1 du masque */

static unsigned char lane[LANE_COUNT][LANE_WIDTH];
static unsigned char lane_dir[LANE_COUNT];
static unsigned char frog_x;
static unsigned char frog_y;
static unsigned char tick;
static unsigned char game_over;

static const unsigned char tile_grass[8] = {
    0x44U, 0x28U, 0x10U, 0x28U, 0x44U, 0x00U, 0x12U, 0x24U
};
static const unsigned char tile_water_a[8] = {
    0x24U, 0x49U, 0x12U, 0x24U, 0x49U, 0x12U, 0x24U, 0x49U
};
static const unsigned char tile_water_b[8] = {
    0x12U, 0x24U, 0x49U, 0x92U, 0x24U, 0x49U, 0x92U, 0x24U
};
static const unsigned char tile_lily[8] = {
    0x18U, 0x3CU, 0x7EU, 0x7EU, 0x3CU, 0x18U, 0x00U, 0x00U
};
static const unsigned char tile_car[8] = {
    0x3CU, 0x5AU, 0x7EU, 0x5EU, 0x7EU, 0x5AU, 0x3CU, 0x18U
};
static const unsigned char tile_frog_mask[8] = {
    0x18U, 0x3CU, 0x7EU, 0xDBU, 0xFFU, 0x7EU, 0x24U, 0x42U
};

static void delay(unsigned n) {
    unsigned i;
    for (i = 0; i < n; ++i) {
        /* attente */
    }
}

static void upload_pattern(unsigned char index, const unsigned char *gfx) {
    unsigned addr = TMS_PATTERN_TABLE + (unsigned)index * 8U;
    unsigned char i;
    tms_set_vram_write_addr(addr);
    for (i = 0; i < 8U; ++i) {
        TMS_WRITE_DATA_PORT(gfx[i]);
    }
}

static void upload_game_tiles(void) {
    upload_pattern(PAT_GRASS, tile_grass);
    upload_pattern(PAT_WATER_A, tile_water_a);
    upload_pattern(PAT_WATER_B, tile_water_b);
    upload_pattern(PAT_LILY, tile_lily);
    upload_pattern(PAT_CAR, tile_car);
}

static void upload_frog_sprite_pattern(void) {
    tms_copy_to_vram(tile_frog_mask, 8U, TMS_SPRITE_PATTERNS + (unsigned)SPR_FROG_PAT * 8U);
}

static void setup_game_colors(void) {
    unsigned char buf[32];
    unsigned char i;

    for (i = 0; i < 32U; ++i) {
        buf[i] = FG_BG(COLOR_BLACK, COLOR_WHITE);
    }
    buf[0U]  = FG_BG(COLOR_LIGHT_GREEN, COLOR_DARK_GREEN);
    buf[1U]  = FG_BG(COLOR_LIGHT_BLUE, COLOR_DARK_BLUE);
    buf[2U]  = FG_BG(COLOR_LIGHT_YELLOW, COLOR_DARK_GREEN);
    buf[3U]  = FG_BG(COLOR_WHITE, COLOR_MEDIUM_RED);
    buf[4U]  = FG_BG(COLOR_GREY, COLOR_DARK_BLUE);

    tms_set_vram_write_addr(TMS_COLOR_TABLE);
    for (i = 0; i < 32U; ++i) {
        TMS_WRITE_DATA_PORT(buf[i]);
    }
}

static unsigned char water_phase(unsigned char col, unsigned char row) {
    unsigned char ph = (unsigned char)(((unsigned)tick >> 1) ^ col ^ row);
    return (ph & 1U) ? PAT_WATER_A : PAT_WATER_B;
}

static void vram_put_row(unsigned char row, const unsigned char *src) {
    unsigned addr = TMS_NAME_TABLE + (unsigned)row * LANE_WIDTH;
    unsigned char i;
    tms_set_vram_write_addr(addr);
    for (i = 0; i < LANE_WIDTH; ++i) {
        TMS_WRITE_DATA_PORT(src[i]);
    }
}

static void vram_fill_row(unsigned char row, unsigned char pat) {
    unsigned char buf[LANE_WIDTH];
    unsigned char c;
    for (c = 0; c < LANE_WIDTH; ++c) {
        buf[c] = pat;
    }
    vram_put_row(row, buf);
}

static void draw_water_row(unsigned char row) {
    unsigned char buf[LANE_WIDTH];
    unsigned char c;
    for (c = 0; c < LANE_WIDTH; ++c) {
        if (c == 0U || c == (unsigned char)(LANE_WIDTH - 1U)) {
            buf[c] = PAT_GRASS;
        } else {
            buf[c] = water_phase(c, row);
        }
    }
    vram_put_row(row, buf);
}

static void init_lanes(void) {
    unsigned char r, c, pat;

    for (r = 0; r < LANE_COUNT; ++r) {
        lane_dir[r] = (unsigned char)(r & 1U);
        pat = (unsigned char)(3U + (r % 5U));
        for (c = 0; c < LANE_WIDTH; ++c) {
            if ((c % pat) == 0U) {
                lane[r][c] = PAT_CAR;
            } else {
                lane[r][c] = PAT_EMPTY;
            }
        }
    }
}

static void scroll_lane(unsigned char r) {
    unsigned char c, t;
    if (lane_dir[r]) {
        t = lane[r][LANE_WIDTH - 1U];
        for (c = (unsigned char)(LANE_WIDTH - 1U); c > 0U; --c) {
            lane[r][c] = lane[r][(unsigned char)(c - 1U)];
        }
        lane[r][0] = t;
    } else {
        t = lane[r][0];
        for (c = 0; c < (unsigned char)(LANE_WIDTH - 1U); ++c) {
            lane[r][c] = lane[r][(unsigned char)(c + 1U)];
        }
        lane[r][LANE_WIDTH - 1U] = t;
    }
}

static void draw_goal_row(void) {
    unsigned char buf[LANE_WIDTH];
    unsigned char c;
    for (c = 0; c < LANE_WIDTH; ++c) {
        if (c == 0U || c == (unsigned char)(LANE_WIDTH - 1U)) {
            buf[c] = PAT_GRASS;
        } else if ((c & 3U) == 0U) {
            buf[c] = PAT_LILY;
        } else {
            buf[c] = water_phase(c, GOAL_ROW);
        }
    }
    vram_put_row(GOAL_ROW, buf);
}

static void draw_lanes(void) {
    unsigned char r;
    for (r = 0; r < LANE_COUNT; ++r) {
        vram_put_row((unsigned char)(LANE_FIRST + r), lane[r]);
    }
}

static void sync_frog_sprite(void) {
    unsigned char ypix;
    unsigned char xpix;
    unsigned addr;

    ypix = (unsigned char)(frog_y * 8U + 1U);
    xpix = (unsigned char)(frog_x * 8U);

    addr = TMS_SPRITE_ATTRS;
    tms_set_vram_write_addr(addr);
    TMS_WRITE_DATA_PORT(ypix);
    TMS_IO_DELAY();
    TMS_WRITE_DATA_PORT(xpix);
    TMS_IO_DELAY();
    TMS_WRITE_DATA_PORT(SPR_FROG_PAT);
    TMS_IO_DELAY();
    TMS_WRITE_DATA_PORT(SPR_FROG_COL);
}

static void frog_sprite_off(void) {
    tms_set_total_sprites(0);
}

static unsigned char hazard_at_frog(void) {
    if (frog_y < LANE_FIRST || frog_y >= (unsigned char)(LANE_FIRST + LANE_COUNT)) {
        return 0;
    }
    return lane[frog_y - LANE_FIRST][frog_x] == PAT_CAR;
}

static void redraw_world(void) {
    unsigned char y;

    draw_water_row(0U);
    draw_water_row(1U);
    draw_goal_row();
    for (y = 3U; y <= 8U; ++y) {
        draw_water_row(y);
    }
    draw_lanes();
    for (y = (unsigned char)(LANE_FIRST + LANE_COUNT); y <= 21U; ++y) {
        vram_fill_row(y, PAT_GRASS);
    }
}

static void hud(const unsigned char *msg) {
    screen1_locate(0, 23);
    screen1_puts(msg);
}

static void reset_game(void) {
    frog_x = START_COL;
    frog_y = START_ROW;
    tick = 0;
    game_over = 0;
    init_lanes();
    screen1_cls();
    redraw_world();
    hud((const unsigned char *)"FROGGER A/D W 4000R");
    sync_frog_sprite();
    tms_set_total_sprites(1);
}

void main(void) {
    unsigned char k, r;

    tms_init_regs(SCREEN1_TABLE);
    tms_set_color(COLOR_CYAN);
    screen1_prepare();
    screen1_load_font();
    upload_game_tiles();
    upload_frog_sprite_pattern();
    setup_game_colors();

    reset_game();

    for (;;) {
        if (game_over) {
            k = apple1_readkey();
            if (k == ' ' || k == 13) {
                reset_game();
            }
            delay(250U);
            continue;
        }

        k = apple1_readkey();
        if (k == 'A' && frog_x > 0) {
            --frog_x;
        } else if (k == 'D' && frog_x < (unsigned char)(LANE_WIDTH - 1U)) {
            ++frog_x;
        } else if (k == 'W' && frog_y > 0) {
            --frog_y;
        }

        if (frog_y == GOAL_ROW) {
            game_over = 1;
            redraw_world();
            frog_sprite_off();
            hud((const unsigned char *)"GAGNE! ESPACE ");
            continue;
        }

        if (hazard_at_frog()) {
            game_over = 1;
            redraw_world();
            frog_sprite_off();
            hud((const unsigned char *)"PERDU ESPACE ");
            continue;
        }

        ++tick;
        /* Défilement chaque frame (avant : 1 pas / 4 frames + pause énorme). */
        for (r = 0; r < LANE_COUNT; ++r) {
            scroll_lane(r);
        }

        redraw_world();
        sync_frog_sprite();
        delay(350U);
    }
}
