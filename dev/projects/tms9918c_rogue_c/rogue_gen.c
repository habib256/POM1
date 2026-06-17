#include "rogue.h"

static unsigned char prng_lo;
static unsigned char prng_hi;
static unsigned char room_x;
static unsigned char room_y;
static unsigned char room_w;
static unsigned char room_h;
static unsigned char room_idx;
static unsigned char cx;
static unsigned char cy;
static unsigned char prev_cx;
static unsigned char prev_cy;
static unsigned char corr_row;
static unsigned char tgt_col;
static unsigned char tgt_row;

void rogue_prng_seed(unsigned char lo, unsigned char hi) {
    prng_lo = (lo == 0U) ? 1U : lo;
    prng_hi = hi;
}

static void prng16_step(void) {
    unsigned char c = (unsigned char)(prng_lo & 1U);
    prng_lo = (unsigned char)((prng_lo >> 1) | ((prng_hi & 1U) ? 0x80U : 0U));
    prng_hi = (unsigned char)(prng_hi >> 1);
    if (c != 0U) {
        prng_hi = (unsigned char)(prng_hi ^ 0xB4U);
    }
}

unsigned char rogue_rand_mod(unsigned char max) {
    unsigned char a;
    if (max == 0U) {
        return 0U;
    }
    prng16_step();
    a = prng_lo;
    while (a >= max) {
        a = (unsigned char)(a - max);
    }
    return a;
}

static void set_tile(unsigned char col, unsigned char row, unsigned char tile) {
    rogue_map_set(col, row, tile);
}

static unsigned char tile_at(unsigned char col, unsigned char row) {
    return rogue_tile_at(col, row);
}

static void carve_corridor_marker(void) {
    if (tile_at(tgt_col, tgt_row) == TILE_WALL) {
        set_tile(tgt_col, tgt_row, TILE_CORR);
    }
}

static void dig_corridor(void) {
    unsigned char t;
    /* Horizontal at row prev_cy, inclusive [min(prev_cx,cx) .. max] */
    tgt_row = prev_cy;
    if (prev_cx < cx) {
        tgt_col = prev_cx;
        t = cx;
    } else {
        tgt_col = cx;
        t = prev_cx;
    }
    for (;;) {
        carve_corridor_marker();
        if (tgt_col >= t) {
            break;
        }
        ++tgt_col;
    }
    /* Vertical at col cx */
    tgt_col = cx;
    if (prev_cy < cy) {
        tgt_row = prev_cy;
        t = cy;
    } else {
        tgt_row = cy;
        t = prev_cy;
    }
    for (;;) {
        carve_corridor_marker();
        if (tgt_row >= t) {
            break;
        }
        ++tgt_row;
    }
}

static unsigned char map_get_idx(unsigned char idx) {
    unsigned char y = (unsigned char)(idx >> 1);
    unsigned char b = map_buffer[y];
    if ((idx & 1U) == 0U) {
        return (unsigned char)(b & 15U);
    }
    return (unsigned char)((b >> 4) & 15U);
}

static void map_set_idx(unsigned char idx, unsigned char nib) {
    unsigned char y = (unsigned char)(idx >> 1);
    unsigned char b = map_buffer[y];
    nib &= 15U;
    if ((idx & 1U) == 0U) {
        map_buffer[y] = (unsigned char)((b & 0xF0U) | nib);
    } else {
        map_buffer[y] = (unsigned char)((b & 0x0FU) | (unsigned char)(nib << 4));
    }
}

static void finalize_doors(void) {
    unsigned char idx;
    unsigned char t;
    /* Pass 1 */
    for (idx = 160U; idx != 0U;) {
        --idx;
        t = (unsigned char)(map_get_idx(idx) & 7U);
        if (t != TILE_CORR) {
            continue;
        }
        tgt_col = (unsigned char)(idx & 15U);
        tgt_row = (unsigned char)(idx >> 4);
        /* neighbours */
        if (tgt_col > 0U) {
            if (tile_at((unsigned char)(tgt_col - 1U), tgt_row) == TILE_EMPTY) {
                goto p1_door;
            }
        }
        if (tgt_col < 15U) {
            if (tile_at((unsigned char)(tgt_col + 1U), tgt_row) == TILE_EMPTY) {
                goto p1_door;
            }
        }
        if (tgt_row > 0U) {
            if (tile_at(tgt_col, (unsigned char)(tgt_row - 1U)) == TILE_EMPTY) {
                goto p1_door;
            }
        }
        if (tgt_row < 9U) {
            if (tile_at(tgt_col, (unsigned char)(tgt_row + 1U)) == TILE_EMPTY) {
                goto p1_door;
            }
        }
        map_set_idx(idx, TILE_CORR_DROP);
        continue;
p1_door:
        map_set_idx(idx, TILE_DOOR);
    }
    /* Pass 2 */
    for (idx = 160U; idx != 0U;) {
        --idx;
        t = (unsigned char)(map_get_idx(idx) & 7U);
        if (t == TILE_CORR_DROP) {
            map_set_idx(idx, TILE_EMPTY);
        }
    }
    /* Pass 3 */
    for (idx = 160U; idx != 0U;) {
        --idx;
        t = (unsigned char)(map_get_idx(idx) & 7U);
        if (t != TILE_DOOR) {
            continue;
        }
        tgt_col = (unsigned char)(idx & 15U);
        tgt_row = (unsigned char)(idx >> 4);
        if (tgt_col > 0U) {
            if (tile_at((unsigned char)(tgt_col - 1U), tgt_row) == TILE_DOOR) {
                map_set_idx(idx, TILE_EMPTY);
                continue;
            }
        }
        if (tgt_row > 0U) {
            if (tile_at(tgt_col, (unsigned char)(tgt_row - 1U)) == TILE_DOOR) {
                map_set_idx(idx, TILE_EMPTY);
            }
        }
    }
}

static void pick_random_room(void) {
    room_w = (unsigned char)(rogue_rand_mod(3U) + 4U);
    room_h = (unsigned char)(rogue_rand_mod(3U) + 3U);
    room_y = (unsigned char)(rogue_rand_mod((unsigned char)(9U - room_h)) + 1U);
    if (room_idx == 0U) {
        room_x = (unsigned char)(rogue_rand_mod((unsigned char)(8U - room_w)) + 1U);
    } else {
        room_x = (unsigned char)(rogue_rand_mod((unsigned char)(7U - room_w)) + 9U);
    }
}

static void carve_room(void) {
    unsigned char r;
    unsigned char c;
    for (r = room_y; r < (unsigned char)(room_y + room_h); ++r) {
        for (c = room_x; c < (unsigned char)(room_x + room_w); ++c) {
            set_tile(c, r, TILE_EMPTY);
        }
    }
}

static void gen_two_rooms(void) {
    room_idx = 0U;
    for (;;) {
        pick_random_room();
        carve_room();
        cx = (unsigned char)((room_w >> 1) + room_x);
        cy = (unsigned char)((room_h >> 1) + room_y);
        if (room_idx == 0U) {
            player_col = (unsigned char)(room_x + room_w - 2U);
            player_row = room_y;
            corr_row = cy;
        } else {
            dig_corridor();
        }
        prev_cx = cx;
        prev_cy = cy;
        ++room_idx;
        if (room_idx >= 2U) {
            break;
        }
    }
    finalize_doors();
    tgt_col = (unsigned char)(player_col + 1U);
    tgt_row = player_row;
    set_tile(tgt_col, tgt_row, TILE_STAIRS_UP);
    tgt_col = room_x;
    tgt_row = room_y;
    if (tgt_row == corr_row) {
        tgt_row = (unsigned char)(tgt_row + 1U);
    }
    set_tile(tgt_col, tgt_row, TILE_STAIRS_DOWN);
}

static void place_perimeter_doors(void) {
    static const unsigned char door_table[] = {
        5U, 10U, 0U, 0U,
        4U, 10U, 0U, 9U,
        2U, 4U, 1U, 0U,
        3U, 4U, 1U, 15U
    };
    unsigned char x;
    for (x = 0U; x < 16U; x += 4U) {
        unsigned char span;
        if (door_table[x] == trans_mode) {
            continue;
        }
        span = door_table[(unsigned)x + 1U];
        if (door_table[(unsigned)x + 2U] == 0U) {
            tgt_col = (unsigned char)(rogue_rand_mod(span) + 3U);
            tgt_row = door_table[(unsigned)x + 3U];
        } else {
            tgt_row = (unsigned char)(rogue_rand_mod(span) + 3U);
            tgt_col = door_table[(unsigned)x + 3U];
        }
        set_tile(tgt_col, tgt_row, TILE_DOOR);
    }
}

static void gen_big_room(void) {
    room_x = 1U;
    room_y = 1U;
    room_w = 14U;
    room_h = 8U;
    carve_room();
    set_tile(14U, 2U, TILE_STAIRS_UP);
    player_col = 13U;
    player_row = 2U;
    set_tile(1U, 8U, TILE_STAIRS_DOWN);
    place_perimeter_doors();
}

void rogue_gen_dungeon(void) {
    rogue_map_clear_walls();
    if (rogue_rand_mod(2U) == 0U) {
        gen_big_room();
    } else {
        gen_two_rooms();
    }
}

static void apply_wrap_spawn(void) {
    if (trans_mode == 2U) {
        player_col = 1U;
        player_row = wrap_anchor;
        tgt_col = 0U;
        tgt_row = wrap_anchor;
    } else if (trans_mode == 3U) {
        player_col = 14U;
        player_row = wrap_anchor;
        tgt_col = 15U;
        tgt_row = wrap_anchor;
    } else if (trans_mode == 4U) {
        player_row = 8U;
        player_col = wrap_anchor;
        tgt_col = wrap_anchor;
        tgt_row = 9U;
    } else {
        player_row = 1U;
        player_col = wrap_anchor;
        tgt_col = wrap_anchor;
        tgt_row = 0U;
    }
    set_tile(tgt_col, tgt_row, TILE_DOOR);
}

void rogue_gen_boss_room(void) {
    unsigned char r;
    unsigned char c;
    room_x = 1U;
    room_y = 1U;
    room_w = 14U;
    room_h = 8U;
    for (r = room_y; r < (unsigned char)(room_y + room_h); ++r) {
        for (c = room_x; c < (unsigned char)(room_x + room_w); ++c) {
            set_tile(c, r, TILE_EMPTY);
        }
    }
    player_col = 7U;
    player_row = 8U;
}

void rogue_new_level(void) {
    unsigned char is_boss = 0U;
    if (trans_mode == 1U) {
        if (depth < 255U) {
            ++depth;
        }
        if (depth >= 13U) {
            is_boss = 1U;
        }
    }
    if (trans_mode >= 2U && trans_mode <= 5U) {
        rogue_map_clear_walls();
        gen_big_room();
        apply_wrap_spawn();
    } else if (is_boss != 0U) {
        rogue_map_clear_walls();
        rogue_gen_boss_room();
    } else {
        rogue_gen_dungeon();
    }
    rogue_post_map_generate(is_boss);
    rogue_clear_vis();
    rogue_compute_fov();
}
