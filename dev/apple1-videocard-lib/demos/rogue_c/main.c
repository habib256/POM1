/*
 * rogue_c — port C (cc65) du noyau donjon / rendu de TMS_Rogue.asm
 * Preset POM1 8, Wozmon 4000R.
 */
#include "rogue.h"
#include "tms9918.h"
#include "screen1.h"
#include "sprites.h"
#include "apple1.h"
#include "rogue_gfx_data.h"
#include "rogue_sprites_data.h"

static void init_vdp(void) {
    tms_init_regs(SCREEN1_TABLE);
    tms_set_sprite_double_size(1U);
    tms_set_sprite_magnification(0U);
    tms_clear_collisions();
    screen1_prepare();
    tms_copy_to_vram(rogue_tileset, 2048U, TMS_PATTERN_TABLE);
    tms_copy_to_vram(rogue_color_table, 32U, TMS_COLOR_TABLE);
    tms_write_reg(7U, 0x01U);
    screen1_load_font();
    tms_copy_to_vram(rogue_sprite_pats, 448U, TMS_SPRITE_PATTERNS);
    tms_copy_to_vram(rogue_boss_sprite_pats, 128U, TMS_SPRITE_PATTERNS + 448U);
}

static void handle_move_key(unsigned char k, signed char *dc, signed char *dr) {
    *dc = 0;
    *dr = 0;
    if (k == 'h' || k == 'H') {
        *dc = -1;
    } else if (k == 'l' || k == 'L') {
        *dc = 1;
    } else if (k == 'k' || k == 'K') {
        *dr = -1;
    } else if (k == 'j' || k == 'J') {
        *dr = 1;
    }
}

void main(void) {
    unsigned char k;
    signed char dc;
    signed char dr;
    unsigned char tc;
    unsigned char tr;
    unsigned char tt;
    unsigned char mslot;

    init_vdp();
    screen1_cls();
    rogue_prng_seed(0xA7U, 0x3DU);
    depth = 1U;
    trans_mode = 0U;
    rogue_init_game();
    rogue_new_level();
    rogue_render_map();
    rogue_hud_paint();
    rogue_place_all_sprites();

    for (;;) {
        tms_wait_end_of_frame();
        if (!apple1_iskeypressed()) {
            continue;
        }
        k = apple1_readkey();
        if (k == 0U) {
            continue;
        }
        if (k >= 'a' && k <= 'z') {
            k = (unsigned char)(k - 32U);
        }
        if (k == 'Q') {
            woz_mon();
        }
        rogue_clear_hurt_flags();
        handle_move_key(k, &dc, &dr);
        if (dc == 0 && dr == 0) {
            continue;
        }
        tc = (unsigned char)((int)player_col + dc);
        tr = (unsigned char)((int)player_row + dr);
        battle_tgt_col = tc;
        battle_tgt_row = tr;
        mslot = rogue_monster_slot_at(tc, tr);
        if (mslot != 255U) {
            rogue_player_bump_attack(mslot);
            continue;
        }
        if (rogue_check_collision(tc, tr) != 0U) {
            continue;
        }
        tt = rogue_tile_at(tc, tr);
        if (tt == TILE_STAIRS_DOWN) {
            trans_mode = 1U;
            rogue_new_level();
            rogue_render_map();
            rogue_place_all_sprites();
            rogue_finish_turn();
            continue;
        }
        if (tc == 0U || tc == 15U || tr == 0U || tr == 9U) {
            if (tt == TILE_DOOR) {
                if (tc == 15U) {
                    trans_mode = 2U;
                    wrap_anchor = tr;
                } else if (tc == 0U) {
                    trans_mode = 3U;
                    wrap_anchor = tr;
                } else if (tr == 0U) {
                    trans_mode = 4U;
                    wrap_anchor = tc;
                } else {
                    trans_mode = 5U;
                    wrap_anchor = tc;
                }
                rogue_new_level();
                rogue_render_map();
                rogue_place_all_sprites();
                rogue_finish_turn();
                continue;
            }
        }
        player_col = tc;
        player_row = tr;
        if (tt == TILE_DOOR) {
            rogue_clear_vis();
        }
        if (tt == TILE_TRAP_PIT) {
            rogue_trigger_pit();
        }
        rogue_try_pickup_item();
        rogue_after_player_move();
    }
}
