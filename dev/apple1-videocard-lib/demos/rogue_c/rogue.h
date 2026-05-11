#ifndef ROGUE_H
#define ROGUE_H

#include "utils.h"

#define ROGUE_LOGICAL_COLS 16U
#define ROGUE_LOGICAL_ROWS 10U
#define ROGUE_MAP_BYTES 80U
#define ROGUE_VIS_BYTES 20U

#define TILE_EMPTY 0U
#define TILE_WALL 1U
#define TILE_DOOR 2U
#define TILE_STAIRS_DOWN 3U
#define TILE_STAIRS_UP 4U
#define TILE_TRAP_PIT 5U
#define TILE_CORR 6U
#define TILE_CORR_DROP 7U
#define TILE_REVEAL_BIT 8U

#define PLAY_TOP_ROW 1U
#define PLAY_BOT_ROW 8U
#define PLAY_LEFT_COL 1U
#define PLAY_RIGHT_COL 14U

#define ROGUE_HP_MAX 14U
#define PLAYER_BASE_DMG 1U
#define FOOD_HEAL 3U
#define PIT_DMG 3U

#define MON_COUNT 16U
#define MON_SIZE 8U
#define MON_TYPE 0U
#define MON_HP 1U
#define MON_COL 2U
#define MON_ROW 3U
#define MON_NAME 4U
#define MON_COLOR 5U
#define MON_DMG 6U
#define MON_HURT 7U

#define MON_TYPE_UNDEAD 1U
#define MON_TYPE_GHOST 2U
#define MON_TYPE_TROLL 3U
#define MON_TYPE_SKELETON 4U
#define MON_TYPE_DEATH 5U
#define MON_TYPE_BOSS 6U

#define ITEM_COUNT 8U
#define ITEM_SIZE 4U
#define ITEM_TYPE 0U
#define ITEM_COL 1U
#define ITEM_ROW 2U
#define ITEM_SUBTYPE 3U

#define ITEM_T_WEAPON 1U
#define ITEM_T_ARMOR 2U
#define ITEM_T_RING 3U
#define ITEM_T_POTION 4U
#define ITEM_T_SCROLL 5U
#define ITEM_T_FOOD 6U
#define ITEM_T_DAGGER 7U
#define ITEM_T_TORCH 8U

#define RING_F_REGEN 0x01U
#define RING_REGEN_PERIOD 5U

#define WEAPON_DURATION 20U
#define ARMOR_DURATION 30U
#define RING_DURATION 15U
#define TORCH_DURATION 50U

#define LAST_ATTACKER_PIT 7U

extern unsigned char map_buffer[ROGUE_MAP_BYTES];
extern unsigned char vis_buffer[ROGUE_VIS_BYTES];
extern unsigned char player_col;
extern unsigned char player_row;
extern unsigned char depth;
extern unsigned char trans_mode;
extern unsigned char wrap_anchor;

extern unsigned char hp;
extern unsigned char hp_max;
extern unsigned char hp_tick;
extern unsigned char atk_tick;
extern unsigned char def_tick;
extern unsigned char player_dmg;
extern unsigned char player_def;
extern unsigned char player_xp;
extern unsigned char xp_atk_bonus;
extern unsigned char xp_def_bonus;
extern unsigned char weapon_timer;
extern unsigned char weapon_boost;
extern unsigned char armor_timer;
extern unsigned char armor_boost;
extern unsigned char torch_timer;
extern unsigned char ring_timer;
extern unsigned char ring_boost;
extern unsigned char ring_flags;
extern unsigned char regen_tick;
extern unsigned char player_hurt;
extern unsigned char last_attacker;
extern unsigned char dagger_qty;
extern unsigned char rogue_full_map;
extern unsigned char monsters[128];
extern unsigned char items[32];

extern unsigned char battle_tgt_col;
extern unsigned char battle_tgt_row;

void rogue_map_clear_walls(void);
unsigned char rogue_tile_at(unsigned char col, unsigned char row);
void rogue_map_set(unsigned char col, unsigned char row, unsigned char dense_nibble);
unsigned char rogue_map_get(unsigned char col, unsigned char row);
unsigned char rogue_check_collision(unsigned char tgt_col, unsigned char tgt_row);
void rogue_vis_fill_lit(void);

void rogue_clear_vis(void);
void rogue_compute_fov(void);

void rogue_render_map(void);
void rogue_hud_paint(void);
void rogue_place_all_sprites(void);

void rogue_prng_seed(unsigned char lo, unsigned char hi);
unsigned char rogue_rand_mod(unsigned char max);
void rogue_gen_dungeon(void);
void rogue_new_level(void);
void rogue_gen_boss_room(void);

void rogue_init_game(void);
void rogue_post_map_generate(unsigned char is_boss);
void rogue_clear_hurt_flags(void);
void rogue_finish_turn(void);
void rogue_move_monsters(void);
void rogue_try_pickup_item(void);
void rogue_trigger_pit(void);
void rogue_reveal_pit_at(unsigned char col, unsigned char row);
unsigned char rogue_monster_slot_at(unsigned char col, unsigned char row);
void rogue_player_attack_monster(unsigned char slot);
void rogue_after_player_move(void);

unsigned char rogue_item_slot_at(unsigned char col, unsigned char row);
void rogue_player_bump_attack(unsigned char slot);

#endif
