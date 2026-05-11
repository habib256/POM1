#include "rogue.h"
#include "screen1.h"
#include "apple1.h"
#include "tms9918.h"

static const unsigned char k_mon_hp[] = {0U, 1U, 2U, 3U, 2U, 3U, 15U};
static const unsigned char k_mon_name[] = {0U, 4U, 8U, 48U, 20U, 12U, 56U};
static const unsigned char k_mon_color[] = {0U, 15U, 14U, 2U, 7U, 10U, 8U};
static const unsigned char k_mon_dmg[] = {0U, 1U, 1U, 1U, 2U, 2U, 4U};
static const signed char gdx[4] = {0, 0, -1, 1};
static const signed char gdy[4] = {-1, 1, 0, 0};

static void recompute_player_stats(void) {
    player_dmg = (unsigned char)(PLAYER_BASE_DMG + xp_atk_bonus);
    if (weapon_timer != 0U) {
        player_dmg = (unsigned char)(weapon_boost + xp_atk_bonus);
    }
    player_def = xp_def_bonus;
    if (armor_timer != 0U) {
        player_def = (unsigned char)(armor_boost + xp_def_bonus);
    }
}

void rogue_init_game(void) {
    hp = ROGUE_HP_MAX;
    hp_max = ROGUE_HP_MAX;
    hp_tick = 5U;
    atk_tick = 10U;
    def_tick = 20U;
    player_xp = 0U;
    xp_atk_bonus = xp_def_bonus = 0U;
    weapon_timer = weapon_boost = armor_timer = armor_boost = 0U;
    torch_timer = ring_timer = ring_boost = ring_flags = regen_tick = 0U;
    player_hurt = last_attacker = dagger_qty = rogue_full_map = 0U;
    recompute_player_stats();
}

static unsigned char d3(void) {
    unsigned char d = depth;
    unsigned char q = 0U;
    while (d >= 3U) {
        d = (unsigned char)(d - 3U);
        ++q;
    }
    return q;
}

unsigned char rogue_item_slot_at(unsigned char col, unsigned char row) {
    unsigned char i;
    for (i = 0U; i < ITEM_COUNT; ++i) {
        unsigned char o = (unsigned char)(i * ITEM_SIZE);
        if (items[o + ITEM_TYPE] != 0U && items[o + ITEM_COL] == col && items[o + ITEM_ROW] == row) {
            return i;
        }
    }
    return 255U;
}

unsigned char rogue_monster_slot_at(unsigned char col, unsigned char row) {
    unsigned char i;
    for (i = 0U; i < MON_COUNT; ++i) {
        unsigned char o = (unsigned char)(i * MON_SIZE);
        unsigned char t = monsters[o + MON_TYPE];
        if (t == 0U) {
            continue;
        }
        if (t == MON_TYPE_BOSS) {
            if (col == monsters[o + MON_COL] && row == monsters[o + MON_ROW]) {
                return i;
            }
            continue;
        }
        if (monsters[o + MON_COL] == col && monsters[o + MON_ROW] == row) {
            return i;
        }
    }
    return 255U;
}

static unsigned char find_empty(void) {
    unsigned char cur = rogue_rand_mod(160U);
    unsigned char n = 160U;
    while (n--) {
        unsigned char tc = (unsigned char)(cur & 15U);
        unsigned char tr = (unsigned char)(cur >> 4);
        if (rogue_tile_at(tc, tr) == TILE_EMPTY && (tc != player_col || tr != player_row) &&
            rogue_monster_slot_at(tc, tr) == 255U && rogue_item_slot_at(tc, tr) == 255U) {
            battle_tgt_col = tc;
            battle_tgt_row = tr;
            return 1U;
        }
        if (++cur >= 160U) {
            cur = 0U;
        }
    }
    return 0U;
}

static void wipe(void) {
    unsigned char i;
    for (i = 0U; i < (unsigned char)(MON_COUNT * MON_SIZE + ITEM_COUNT * ITEM_SIZE); ++i) {
        monsters[i] = 0U;
    }
}

static void put_item(unsigned char typ, unsigned char sub) {
    unsigned char i;
    for (i = 0U; i < ITEM_COUNT; ++i) {
        unsigned char o = (unsigned char)(i * ITEM_SIZE);
        if (items[o + ITEM_TYPE] == 0U) {
            items[o + ITEM_TYPE] = typ;
            items[o + ITEM_COL] = battle_tgt_col;
            items[o + ITEM_ROW] = battle_tgt_row;
            items[o + ITEM_SUBTYPE] = sub;
            return;
        }
    }
}

static void spawn_boss(void) {
    unsigned char b = d3();
    monsters[MON_TYPE] = MON_TYPE_BOSS;
    monsters[MON_HP] = (unsigned char)(k_mon_hp[6] + b);
    monsters[MON_COL] = 7U;
    monsters[MON_ROW] = 4U;
    monsters[MON_NAME] = k_mon_name[6];
    monsters[MON_COLOR] = k_mon_color[6];
    monsters[MON_DMG] = (unsigned char)(k_mon_dmg[6] + (unsigned char)(b >> 1));
    monsters[MON_HURT] = 0U;
}

static void spawn_mons(void) {
    unsigned char lim = (unsigned char)(depth + 2U);
    unsigned char pool = 0U;
    unsigned char tp;
    unsigned char hb = d3();
    unsigned char db = (unsigned char)(hb >> 1);
    if (lim > MON_COUNT) {
        lim = MON_COUNT;
    }
    lim = (unsigned char)(lim * MON_SIZE);
    tp = (depth >= 10U) ? 5U : ((depth >= 5U) ? 4U : 3U);
    while (pool < lim && find_empty() != 0U) {
        unsigned char ty = (unsigned char)(rogue_rand_mod(tp) + 1U);
        monsters[pool + MON_TYPE] = ty;
        monsters[pool + MON_HP] = (unsigned char)(k_mon_hp[ty] + hb);
        monsters[pool + MON_COL] = battle_tgt_col;
        monsters[pool + MON_ROW] = battle_tgt_row;
        monsters[pool + MON_NAME] = k_mon_name[ty];
        monsters[pool + MON_COLOR] = k_mon_color[ty];
        monsters[pool + MON_DMG] = (unsigned char)(k_mon_dmg[ty] + db);
        monsters[pool + MON_HURT] = 0U;
        pool = (unsigned char)(pool + MON_SIZE);
    }
}

static const unsigned char th[] = {6U, 14U, 17U, 19U, 23U, 26U, 28U, 32U};
static const unsigned char ty[] = {6U, 7U, 4U, 5U, 1U, 2U, 3U, 8U};

static void spawn_items(void) {
    unsigned char n = (unsigned char)(rogue_rand_mod(3U) + 1U);
    while (n-- != 0U && find_empty() != 0U) {
        unsigned char r = rogue_rand_mod(32U);
        unsigned char i = 0U;
        while (i < 7U && r >= th[i]) {
            ++i;
        }
        put_item(ty[i], 0U);
    }
}

static void spawn_pits(void) {
    unsigned char n = rogue_rand_mod(3U);
    while (n-- != 0U && find_empty() != 0U) {
        rogue_map_set(battle_tgt_col, battle_tgt_row, TILE_TRAP_PIT);
    }
}

void rogue_post_map_generate(unsigned char is_boss) {
    rogue_full_map = 0U;
    wipe();
    if (is_boss != 0U) {
        spawn_boss();
    } else {
        spawn_mons();
        spawn_items();
        spawn_pits();
    }
}

void rogue_reveal_pit_at(unsigned char col, unsigned char row) {
    rogue_map_set(col, row, (unsigned char)(rogue_map_get(col, row) | TILE_REVEAL_BIT));
}

void rogue_trigger_pit(void) {
    hp = (hp > PIT_DMG) ? (unsigned char)(hp - PIT_DMG) : 0U;
    player_hurt = 1U;
    last_attacker = LAST_ATTACKER_PIT;
    rogue_reveal_pit_at(player_col, player_row);
}

void rogue_try_pickup_item(void) {
    unsigned char s = rogue_item_slot_at(player_col, player_row);
    unsigned char o;
    unsigned char t;
    if (s == 255U) {
        return;
    }
    o = (unsigned char)(s * ITEM_SIZE);
    t = items[o + ITEM_TYPE];
    if (t == ITEM_T_FOOD || t == ITEM_T_POTION) {
        unsigned char add = (t == ITEM_T_FOOD) ? FOOD_HEAL : 8U;
        if (hp < hp_max) {
            hp = (unsigned char)(hp + add);
            if (hp > hp_max) {
                hp = hp_max;
            }
        }
    } else if (t == ITEM_T_WEAPON) {
        weapon_timer = 20U;
        weapon_boost = 2U;
        recompute_player_stats();
    } else if (t == ITEM_T_ARMOR) {
        armor_timer = 30U;
        armor_boost = 2U;
        recompute_player_stats();
    } else if (t == ITEM_T_RING) {
        ring_timer = 15U;
        ring_boost = RING_F_REGEN;
        ring_flags = (unsigned char)(ring_flags | RING_F_REGEN);
        regen_tick = 0U;
    } else if (t == ITEM_T_TORCH) {
        torch_timer = 50U;
    } else if (t == ITEM_T_SCROLL) {
        rogue_full_map = 1U;
    } else if (t == ITEM_T_DAGGER) {
        ++dagger_qty;
    }
    items[o + ITEM_TYPE] = 0U;
}

static unsigned char bite(unsigned char raw) {
    return (raw <= player_def) ? 0U : (unsigned char)(raw - player_def);
}

static unsigned char mon_abs_dx;
static unsigned char mon_abs_dy;
static unsigned char sc;
static unsigned char sr;

static void absd(unsigned char m) {
    unsigned char o = (unsigned char)(m * MON_SIZE);
    mon_abs_dx = (player_col >= monsters[o + MON_COL])
                     ? (unsigned char)(player_col - monsters[o + MON_COL])
                     : (unsigned char)(monsters[o + MON_COL] - player_col);
    mon_abs_dy = (player_row >= monsters[o + MON_ROW])
                     ? (unsigned char)(player_row - monsters[o + MON_ROW])
                     : (unsigned char)(monsters[o + MON_ROW] - player_row);
}

static unsigned char apply(unsigned char m) {
    unsigned char o = (unsigned char)(m * MON_SIZE);
    unsigned char dm = bite(monsters[o + MON_DMG]);
    unsigned char tt;
    if (sc == player_col && sr == player_row) {
        hp = (hp > dm) ? (unsigned char)(hp - dm) : 0U;
        player_hurt = 1U;
        last_attacker = monsters[o + MON_TYPE];
        return 1U;
    }
    if (sr < PLAY_TOP_ROW || sr > PLAY_BOT_ROW || sc < PLAY_LEFT_COL || sc > PLAY_RIGHT_COL) {
        return 0U;
    }
    tt = rogue_tile_at(sc, sr);
    if (tt == TILE_DOOR) {
        return 0U;
    }
    if (tt != TILE_EMPTY && tt != TILE_STAIRS_DOWN && tt != TILE_TRAP_PIT) {
        return 0U;
    }
    if (rogue_monster_slot_at(sc, sr) != 255U || rogue_item_slot_at(sc, sr) != 255U) {
        return 0U;
    }
    if (tt == TILE_TRAP_PIT) {
        unsigned char mh = monsters[o + MON_HP];
        mh = (mh > PIT_DMG) ? (unsigned char)(mh - PIT_DMG) : 0U;
        monsters[o + MON_HP] = mh;
        rogue_reveal_pit_at(sc, sr);
        if (mh == 0U) {
            monsters[o + MON_TYPE] = 0U;
            return 1U;
        }
        monsters[o + MON_HURT] = 1U;
    }
    monsters[o + MON_COL] = sc;
    monsters[o + MON_ROW] = sr;
    return 1U;
}

static unsigned char tryx(unsigned char m) {
    unsigned char o = (unsigned char)(m * MON_SIZE);
    if (mon_abs_dx == 0U) {
        return 0U;
    }
    sr = monsters[o + MON_ROW];
    sc = (monsters[o + MON_COL] < player_col) ? (unsigned char)(monsters[o + MON_COL] + 1U)
                                              : (unsigned char)(monsters[o + MON_COL] - 1U);
    return apply(m);
}

static unsigned char tryy(unsigned char m) {
    unsigned char o = (unsigned char)(m * MON_SIZE);
    if (mon_abs_dy == 0U) {
        return 0U;
    }
    sc = monsters[o + MON_COL];
    sr = (monsters[o + MON_ROW] < player_row) ? (unsigned char)(monsters[o + MON_ROW] + 1U)
                                              : (unsigned char)(monsters[o + MON_ROW] - 1U);
    return apply(m);
}

static void ai_greedy(unsigned char m) {
    absd(m);
    if (mon_abs_dx == 0U && mon_abs_dy == 0U) {
        return;
    }
    if (mon_abs_dx >= mon_abs_dy) {
        if (!tryx(m)) {
            (void)tryy(m);
        }
    } else if (!tryy(m)) {
        (void)tryx(m);
    }
}

static void ai_ghost(unsigned char m) {
    unsigned char o = (unsigned char)(m * MON_SIZE);
    unsigned char d = rogue_rand_mod(4U);
    signed char nc = (signed char)monsters[o + MON_COL] + gdx[d];
    signed char nr = (signed char)monsters[o + MON_ROW] + gdy[d];
    if (nc < 1 || nc > 14 || nr < 1 || nr > 8) {
        return;
    }
    sc = (unsigned char)nc;
    sr = (unsigned char)nr;
    (void)apply(m);
}

static void step1(unsigned char m) {
    unsigned char o = (unsigned char)(m * MON_SIZE);
    unsigned char t = monsters[o + MON_TYPE];
    if (t == MON_TYPE_GHOST) {
        ai_ghost(m);
    } else {
        ai_greedy(m);
    }
}

void rogue_move_monsters(void) {
    unsigned char m;
    for (m = 0U; m < MON_COUNT; ++m) {
        if (monsters[(unsigned char)(m * MON_SIZE) + MON_TYPE] != 0U) {
            step1(m);
        }
    }
}

static void award(void) {
    if (player_xp != 255U) {
        ++player_xp;
    }
    if (--hp_tick == 0U) {
        hp_tick = 5U;
        if (hp_max < 255U) {
            ++hp_max;
        }
        if (hp < hp_max) {
            ++hp;
        }
    }
    if (--atk_tick == 0U) {
        atk_tick = 10U;
        ++xp_atk_bonus;
        recompute_player_stats();
    }
    if (--def_tick == 0U) {
        def_tick = 20U;
        ++xp_def_bonus;
        recompute_player_stats();
    }
}

static void win(void) {
    screen1_cls();
    screen1_locate(0U, 10U);
    screen1_puts((const unsigned char *)"GAGNE! TOUCHE.");
    for (;;) {
        tms_wait_end_of_frame();
        if (apple1_iskeypressed()) {
            (void)apple1_readkey();
            woz_mon();
        }
    }
}

static void die(void) {
    screen1_cls();
    screen1_locate(0U, 10U);
    screen1_puts((const unsigned char *)"MORT. TOUCHE.");
    for (;;) {
        tms_wait_end_of_frame();
        if (apple1_iskeypressed()) {
            (void)apple1_readkey();
            woz_mon();
        }
    }
}

void rogue_player_attack_monster(unsigned char slot) {
    unsigned char o = (unsigned char)(slot * MON_SIZE);
    unsigned char t = monsters[o + MON_TYPE];
    unsigned char nh;
    if (t == 0U) {
        return;
    }
    nh = (monsters[o + MON_HP] > player_dmg) ? (unsigned char)(monsters[o + MON_HP] - player_dmg) : 0U;
    monsters[o + MON_HP] = nh;
    if (nh != 0U) {
        monsters[o + MON_HURT] = 1U;
        return;
    }
    battle_tgt_col = monsters[o + MON_COL];
    battle_tgt_row = monsters[o + MON_ROW];
    if (t == MON_TYPE_BOSS) {
        monsters[o + MON_TYPE] = 0U;
        award();
        win();
    }
    monsters[o + MON_TYPE] = 0U;
    award();
    if (rogue_rand_mod(4U) == 0U) {
        put_item(ITEM_T_FOOD, 0U);
    }
}

void rogue_clear_hurt_flags(void) {
    unsigned char m;
    player_hurt = 0U;
    for (m = 0U; m < MON_COUNT; ++m) {
        unsigned char o = (unsigned char)(m * MON_SIZE);
        if (monsters[o + MON_TYPE] != MON_TYPE_TROLL) {
            monsters[o + MON_HURT] = 0U;
        }
    }
}

void rogue_finish_turn(void) {
    if ((ring_flags & RING_F_REGEN) != 0U) {
        if (regen_tick != 0U) {
            --regen_tick;
        } else {
            regen_tick = RING_REGEN_PERIOD;
            if (hp < hp_max) {
                ++hp;
            }
        }
    }
    if (weapon_timer != 0U && --weapon_timer == 0U) {
        recompute_player_stats();
    }
    if (armor_timer != 0U && --armor_timer == 0U) {
        recompute_player_stats();
    }
    if (torch_timer != 0U) {
        --torch_timer;
    }
    if (ring_timer != 0U && --ring_timer == 0U) {
        ring_flags = (unsigned char)(ring_flags & (unsigned char)~ring_boost);
        ring_boost = 0U;
    }
    rogue_hud_paint();
    if (hp == 0U) {
        die();
    }
}

void rogue_after_player_move(void) {
    rogue_move_monsters();
    rogue_compute_fov();
    rogue_render_map();
    rogue_place_all_sprites();
    rogue_finish_turn();
}

void rogue_player_bump_attack(unsigned char slot) {
    rogue_player_attack_monster(slot);
    rogue_move_monsters();
    rogue_place_all_sprites();
    rogue_finish_turn();
}
