#include "rogue.h"

unsigned char map_buffer[ROGUE_MAP_BYTES];
unsigned char vis_buffer[ROGUE_VIS_BYTES];
unsigned char player_col;
unsigned char player_row;
unsigned char depth;
unsigned char trans_mode;
unsigned char wrap_anchor;

unsigned char hp;
unsigned char hp_max;
unsigned char hp_tick;
unsigned char atk_tick;
unsigned char def_tick;
unsigned char player_dmg;
unsigned char player_def;
unsigned char player_xp;
unsigned char xp_atk_bonus;
unsigned char xp_def_bonus;
unsigned char weapon_timer;
unsigned char weapon_boost;
unsigned char armor_timer;
unsigned char armor_boost;
unsigned char torch_timer;
unsigned char ring_timer;
unsigned char ring_boost;
unsigned char ring_flags;
unsigned char regen_tick;
unsigned char player_hurt;
unsigned char last_attacker;
unsigned char dagger_qty;
unsigned char rogue_full_map;
unsigned char monsters[128];
unsigned char items[32];
unsigned char battle_tgt_col;
unsigned char battle_tgt_row;

void rogue_map_clear_walls(void) {
    unsigned char i;
    unsigned char b = (unsigned char)((TILE_WALL << 4) | TILE_WALL);
    for (i = 0U; i < ROGUE_MAP_BYTES; ++i) {
        map_buffer[i] = b;
    }
}

static unsigned char byte_index(unsigned char col, unsigned char row) {
    return (unsigned char)((row << 3) + (col >> 1));
}

unsigned char rogue_map_get(unsigned char col, unsigned char row) {
    unsigned char y = byte_index(col, row);
    unsigned char b = map_buffer[y];
    if ((col & 1U) == 0U) {
        return (unsigned char)(b & 15U);
    }
    return (unsigned char)((b >> 4) & 15U);
}

void rogue_map_set(unsigned char col, unsigned char row, unsigned char dense_nibble) {
    unsigned char y = byte_index(col, row);
    unsigned char b = map_buffer[y];
    dense_nibble &= 15U;
    if ((col & 1U) == 0U) {
        map_buffer[y] = (unsigned char)((b & 0xF0U) | dense_nibble);
    } else {
        map_buffer[y] = (unsigned char)((b & 0x0FU) | (unsigned char)(dense_nibble << 4));
    }
}

unsigned char rogue_tile_at(unsigned char col, unsigned char row) {
    return (unsigned char)(rogue_map_get(col, row) & 7U);
}

unsigned char rogue_check_collision(unsigned char tgt_col, unsigned char tgt_row) {
    unsigned char t = rogue_tile_at(tgt_col, tgt_row);
    if (t == TILE_DOOR) {
        return 0U;
    }
    if (tgt_row < PLAY_TOP_ROW || tgt_row > PLAY_BOT_ROW) {
        return 1U;
    }
    if (tgt_col < PLAY_LEFT_COL || tgt_col > PLAY_RIGHT_COL) {
        return 1U;
    }
    if (t == TILE_EMPTY || t == TILE_STAIRS_DOWN || t == TILE_TRAP_PIT) {
        return 0U;
    }
    return 1U;
}

void rogue_vis_fill_lit(void) {
    unsigned char i;
    for (i = 0U; i < ROGUE_VIS_BYTES; ++i) {
        vis_buffer[i] = 0xFFU;
    }
}
