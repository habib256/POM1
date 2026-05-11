#include "rogue.h"
#include "tms9918.h"
#include "screen1.h"
#include "sprites.h"

static const unsigned char tile_char_base[8] = {
    0U, 4U, 12U, 8U, 16U, 20U, 0U, 0U
};

/* vis_buffer: bit (col & 7) du byte (row*2 + col/8) — TMS_Rogue.asm commentaire vis_test. */
static unsigned char vis_bit(unsigned char row, unsigned char col) {
    unsigned char b;
    if (rogue_full_map != 0U) {
        return 1U;
    }
    b = vis_buffer[(unsigned)row * 2U + (col >> 3U)];
    return (unsigned char)((b >> (col & 7U)) & 1U);
}

static void emit_pair_tl(unsigned char dense_nibble) {
    unsigned char id = (unsigned char)(dense_nibble & 7U);
    unsigned char base;
    if ((dense_nibble & TILE_REVEAL_BIT) == 0U && id == TILE_TRAP_PIT) {
        TMS_WRITE_DATA_PORT(0U);
        TMS_IO_DELAY();
        TMS_WRITE_DATA_PORT(0U);
        TMS_IO_DELAY();
        return;
    }
    base = tile_char_base[id];
    TMS_WRITE_DATA_PORT(base);
    TMS_IO_DELAY();
    TMS_WRITE_DATA_PORT((unsigned char)(base + 1U));
    TMS_IO_DELAY();
}

static void emit_pair_bl(unsigned char dense_nibble) {
    unsigned char id = (unsigned char)(dense_nibble & 7U);
    unsigned char base;
    if ((dense_nibble & TILE_REVEAL_BIT) == 0U && id == TILE_TRAP_PIT) {
        TMS_WRITE_DATA_PORT(0U);
        TMS_IO_DELAY();
        TMS_WRITE_DATA_PORT(0U);
        TMS_IO_DELAY();
        return;
    }
    base = tile_char_base[id];
    TMS_WRITE_DATA_PORT((unsigned char)(base + 2U));
    TMS_IO_DELAY();
    TMS_WRITE_DATA_PORT((unsigned char)(base + 3U));
    TMS_IO_DELAY();
}

void rogue_render_map(void) {
    unsigned char lr;
    unsigned char ypack;
    unsigned char pak;
    unsigned char c0;
    unsigned char c1;

    for (lr = 0U; lr < ROGUE_LOGICAL_ROWS; ++lr) {
        unsigned rowpair = (unsigned)lr * 2U;
        unsigned base_tl = TMS_NAME_TABLE + rowpair * 32U;
        unsigned base_bl = TMS_NAME_TABLE + (rowpair + 1U) * 32U;

        tms_wait_end_of_frame();
        tms_set_vram_write_addr(base_tl);
        for (ypack = 0U; ypack < 8U; ++ypack) {
            pak = map_buffer[(unsigned)lr * 8U + ypack];
            c0 = (unsigned char)(ypack * 2U);
            c1 = (unsigned char)(c0 + 1U);
            if (vis_bit(lr, c0)) {
                emit_pair_tl((unsigned char)(pak & 15U));
            } else {
                TMS_WRITE_DATA_PORT(0U);
                TMS_IO_DELAY();
                TMS_WRITE_DATA_PORT(0U);
                TMS_IO_DELAY();
            }
            if (vis_bit(lr, c1)) {
                emit_pair_tl((unsigned char)(pak >> 4));
            } else {
                TMS_WRITE_DATA_PORT(0U);
                TMS_IO_DELAY();
                TMS_WRITE_DATA_PORT(0U);
                TMS_IO_DELAY();
            }
        }

        tms_wait_end_of_frame();
        tms_set_vram_write_addr(base_bl);
        for (ypack = 0U; ypack < 8U; ++ypack) {
            pak = map_buffer[(unsigned)lr * 8U + ypack];
            c0 = (unsigned char)(ypack * 2U);
            c1 = (unsigned char)(c0 + 1U);
            if (vis_bit(lr, c0)) {
                emit_pair_bl((unsigned char)(pak & 15U));
            } else {
                TMS_WRITE_DATA_PORT(0U);
                TMS_IO_DELAY();
                TMS_WRITE_DATA_PORT(0U);
                TMS_IO_DELAY();
            }
            if (vis_bit(lr, c1)) {
                emit_pair_bl((unsigned char)(pak >> 4));
            } else {
                TMS_WRITE_DATA_PORT(0U);
                TMS_IO_DELAY();
                TMS_WRITE_DATA_PORT(0U);
                TMS_IO_DELAY();
            }
        }
    }
}

static void put_dec2(unsigned char v) {
    screen1_putc((unsigned char)('0' + (v / 10U) % 10U));
    screen1_putc((unsigned char)('0' + (v % 10U)));
}

void rogue_hud_paint(void) {
    screen1_locate(0U, 20U);
    screen1_puts((const unsigned char *)"D");
    put_dec2(depth);
    screen1_puts((const unsigned char *)" HP");
    put_dec2(hp);
    screen1_putc('/');
    put_dec2(hp_max);
    screen1_puts((const unsigned char *)" AT");
    put_dec2(player_dmg);
    screen1_puts((const unsigned char *)" DF");
    put_dec2(player_def);
    screen1_puts((const unsigned char *)" T");
    put_dec2(torch_timer);
    screen1_puts((const unsigned char *)" DG");
    put_dec2(dagger_qty);
    screen1_locate(0U, 21U);
    screen1_puts((const unsigned char *)"HJKL Q=quit  EDGE=WRAP  SCROLL=CARTE");
}

static const unsigned char item_pat[9] = {0U, 36U, 40U, 44U, 28U, 32U, 16U, 24U, 52U};
static const unsigned char item_col[9] = {0U, 15U, 6U, 10U, 13U, 7U, 11U, 14U, 9U};

void rogue_place_all_sprites(void) {
    tms_sprite s;
    unsigned char sn = 0U;
    unsigned char i;

    s.y = (signed char)((int)player_row * 16 - 1);
    s.x = (unsigned char)(player_col * 16U);
    s.name = 0U;
    s.color = (unsigned char)((player_hurt != 0U) ? (COLOR_MEDIUM_RED & 0x0FU) : (COLOR_LIGHT_BLUE & 0x0FU));
    tms_set_sprite(sn, &s);
    ++sn;

    for (i = 0U; i < MON_COUNT; ++i) {
        unsigned char off = (unsigned char)(i * MON_SIZE);
        unsigned char mt = monsters[off + MON_TYPE];
        unsigned char mc = monsters[off + MON_COL];
        unsigned char mr = monsters[off + MON_ROW];
        unsigned char pat;
        unsigned char col;
        if (mt == 0U || sn >= 32U) {
            continue;
        }
        if (vis_bit(mr, mc) == 0U) {
            continue;
        }
        pat = monsters[off + MON_NAME];
        col = monsters[off + MON_COLOR];
        if (monsters[off + MON_HURT] != 0U) {
            col = (unsigned char)(COLOR_MEDIUM_RED & 0x0FU);
        }
        s.name = pat;
        s.color = col;
        s.x = (unsigned char)(mc * 16U);
        s.y = (signed char)((int)mr * 16 - 1);
        tms_set_sprite(sn, &s);
        ++sn;
    }

    for (i = 0U; i < ITEM_COUNT; ++i) {
        unsigned char off = (unsigned char)(i * ITEM_SIZE);
        unsigned char it = items[off + ITEM_TYPE];
        unsigned char ic = items[off + ITEM_COL];
        unsigned char ir = items[off + ITEM_ROW];
        if (it == 0U || sn >= 32U) {
            continue;
        }
        if (vis_bit(ir, ic) == 0U) {
            continue;
        }
        if (it > 8U) {
            continue;
        }
        s.name = item_pat[it];
        s.color = (unsigned char)(item_col[it] & 0x0FU);
        s.x = (unsigned char)(ic * 16U);
        s.y = (signed char)((int)ir * 16 - 1);
        tms_set_sprite(sn, &s);
        ++sn;
    }

    tms_set_total_sprites(sn);
}

void rogue_place_player_sprite(void) {
    rogue_place_all_sprites();
}
