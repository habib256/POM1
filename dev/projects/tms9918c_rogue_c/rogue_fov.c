#include "rogue.h"

static unsigned char fov_radius;

static void vis_mark_cell(unsigned char cx, unsigned char cy) {
    unsigned char bi = (unsigned char)((unsigned)cy * 2U + (cx >> 3));
    unsigned char mask = (unsigned char)(1U << (cx & 7U));
    vis_buffer[bi] |= mask;
}

static unsigned char chebyshev(unsigned char ax, unsigned char ay, unsigned char bx, unsigned char by) {
    unsigned char dx = (ax >= bx) ? (unsigned char)(ax - bx) : (unsigned char)(bx - ax);
    unsigned char dy = (ay >= by) ? (unsigned char)(ay - by) : (unsigned char)(by - ay);
    return (unsigned char)((dx > dy) ? dx : dy);
}

static void strip_invisible_pit_reveals(void) {
    unsigned char idx;
    for (idx = 0U; idx < 160U; ++idx) {
        unsigned char c = (unsigned char)(idx & 15U);
        unsigned char r = (unsigned char)(idx >> 4);
        unsigned char nib = rogue_map_get(c, r);
        if ((nib & TILE_REVEAL_BIT) == 0U) {
            continue;
        }
        if ((vis_buffer[(unsigned)(idx >> 3)] & (unsigned char)(1U << (idx & 7U))) != 0U) {
            continue;
        }
        nib = (unsigned char)(nib & (unsigned char)~TILE_REVEAL_BIT);
        rogue_map_set(c, r, nib);
    }
}

void rogue_clear_vis(void) {
    unsigned char i;
    for (i = 0U; i < ROGUE_VIS_BYTES; ++i) {
        vis_buffer[i] = 0U;
    }
}

void rogue_compute_fov(void) {
    unsigned char r;
    unsigned char c;
    fov_radius = (torch_timer != 0U) ? 7U : 3U;
    rogue_clear_vis();
    vis_mark_cell(player_col, player_row);
    for (r = 0U; r < ROGUE_LOGICAL_ROWS; ++r) {
        for (c = 0U; c < ROGUE_LOGICAL_COLS; ++c) {
            if (chebyshev(player_col, player_row, c, r) > fov_radius) {
                continue;
            }
            vis_mark_cell(c, r);
        }
    }
    strip_invisible_pit_reveals();
}
