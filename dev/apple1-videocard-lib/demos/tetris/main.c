/*
 * tetris — Tetris jouable (cc65) sur TMS9918 screen1 (32x24).
 *
 * Touches:
 *   A/D : gauche/droite
 *   W   : rotation
 *   S   : descendre plus vite
 *   ESPACE : drop
 *   Q   : quitter
 */
#include "tms9918.h"
#include "screen1.h"
#include "apple1.h"

#define BW 10U
#define BH 20U
#define BX 2U
#define BY 2U

static unsigned char board[BH][BW];

static unsigned long score;
static unsigned int lines;
static unsigned char level;

static unsigned char lfsr = 0xA7U;

static unsigned char rand8(void) {
    /* xorshift-ish LFSR, cheap and deterministic */
    unsigned char x = lfsr;
    x ^= (unsigned char)(x << 3);
    x ^= (unsigned char)(x >> 5);
    x ^= (unsigned char)(x << 1);
    lfsr = x;
    return x;
}

static void put_u32(unsigned long v) {
    /* decimal, minimal */
    unsigned char buf[11];
    unsigned char n = 0;
    if (v == 0UL) {
        screen1_putc('0');
        return;
    }
    while (v != 0UL && n < 10U) {
        buf[n++] = (unsigned char)('0' + (v % 10UL));
        v /= 10UL;
    }
    while (n != 0U) {
        screen1_putc(buf[--n]);
    }
}

static void put_u16(unsigned int v) {
    put_u32((unsigned long)v);
}

/* 7 pieces, 4 rotations, 4x4 bitmask in row-major */
static const unsigned short kPieces[7][4] = {
    /* I */
    { 0x0F00, 0x2222, 0x00F0, 0x4444 },
    /* O */
    { 0x6600, 0x6600, 0x6600, 0x6600 },
    /* T */
    { 0x4E00, 0x4640, 0x0E40, 0x4C40 },
    /* S */
    { 0x6C00, 0x4620, 0x06C0, 0x8C40 },
    /* Z */
    { 0xC600, 0x2640, 0x0C60, 0x4C80 },
    /* J */
    { 0x8E00, 0x6440, 0x0E20, 0x44C0 },
    /* L */
    { 0x2E00, 0x4460, 0x0E80, 0xC440 }
};

static unsigned char cur_piece, cur_rot;
static signed char cur_x, cur_y;
static unsigned char next_piece;

static void cell(unsigned char x, unsigned char y, unsigned char filled) {
    screen1_locate(x, y);
    if (filled) {
        screen1_putc(CHR_REVSPACE);
    } else {
        screen1_putc(CHR_SPACE);
    }
}

static unsigned char piece_cell(unsigned char piece, unsigned char rot, unsigned char px, unsigned char py) {
    unsigned short m = kPieces[piece][rot & 3U];
    unsigned char bit = (unsigned char)(1U << (3U - px));
    unsigned short row = (unsigned short)(((m >> (12U - 4U * py)) & 0x0FU));
    return (unsigned char)((row & bit) != 0U);
}

static unsigned char collides(signed char x, signed char y, unsigned char piece, unsigned char rot) {
    unsigned char px, py;
    for (py = 0U; py < 4U; ++py) {
        for (px = 0U; px < 4U; ++px) {
            if (!piece_cell(piece, rot, px, py)) continue;
            {
                signed char bx = (signed char)(x + (signed char)px);
                signed char by = (signed char)(y + (signed char)py);
                if (bx < 0 || bx >= (signed char)BW || by >= (signed char)BH) return 1U;
                if (by >= 0 && board[(unsigned char)by][(unsigned char)bx]) return 1U;
            }
        }
    }
    return 0U;
}

static void draw_static_ui(void) {
    unsigned char y;
    unsigned char x;
    screen1_putc(CHR_CLS);

    screen1_locate(0, 0);
    screen1_puts((const unsigned char *)"TETRIS\n");
    screen1_puts((const unsigned char *)"A/D W S SPC Q\n");

    /* border */
    for (y = 0U; y < BH; ++y) {
        screen1_locate(BX - 1U, (unsigned char)(BY + y));
        screen1_putc('|');
        screen1_locate(BX + BW, (unsigned char)(BY + y));
        screen1_putc('|');
    }
    for (x = 0U; x < BW; ++x) {
        screen1_locate((unsigned char)(BX + x), (unsigned char)(BY + BH));
        screen1_putc('-');
    }

    screen1_locate(16, 12);
    screen1_puts((const unsigned char *)"NEXT");
}

static void draw_hud(void) {
    static unsigned long last_score = 0xFFFFFFFFUL;
    static unsigned int last_lines = 0xFFFFU;
    static unsigned char last_level = 0xFFU;

    /* score */
    if (score != last_score) {
    screen1_locate(16, 6);
        screen1_puts((const unsigned char *)"SCORE ");
    put_u32(score);
        last_score = score;
    }
    /* lines */
    if (lines != last_lines) {
    screen1_locate(16, 8);
        screen1_puts((const unsigned char *)"LINES ");
    put_u16(lines);
        last_lines = lines;
    }
    /* level */
    if (level != last_level) {
    screen1_locate(16, 10);
        screen1_puts((const unsigned char *)"LEVEL ");
    put_u16(level);
        last_level = level;
    }
}

static void draw_board(void) {
    unsigned char y, x;
    for (y = 0U; y < BH; ++y) {
        for (x = 0U; x < BW; ++x) {
            cell((unsigned char)(BX + x), (unsigned char)(BY + y), board[y][x]);
        }
    }
}

static void draw_piece_at(signed char x, signed char y, unsigned char piece, unsigned char rot, unsigned char filled) {
    unsigned char px, py;
    for (py = 0U; py < 4U; ++py) {
        for (px = 0U; px < 4U; ++px) {
            if (!piece_cell(piece, rot, px, py)) continue;
            {
                signed char bx = (signed char)(x + (signed char)px);
                signed char by = (signed char)(y + (signed char)py);
                if (by >= 0 && by < (signed char)BH && bx >= 0 && bx < (signed char)BW) {
                    cell((unsigned char)(BX + (unsigned char)bx), (unsigned char)(BY + (unsigned char)by), filled);
                }
            }
        }
    }
}

static void draw_next(void) {
    unsigned char px, py;
    /* clear area */
    for (py = 0U; py < 4U; ++py) {
        for (px = 0U; px < 4U; ++px) {
            cell((unsigned char)(16U + px), (unsigned char)(14U + py), 0);
        }
    }
    /* draw */
    for (py = 0U; py < 4U; ++py) {
        for (px = 0U; px < 4U; ++px) {
            if (piece_cell(next_piece, 0U, px, py)) {
                cell((unsigned char)(16U + px), (unsigned char)(14U + py), 1U);
            }
        }
    }
}

static void lock_piece(void) {
    unsigned char px, py;
    for (py = 0U; py < 4U; ++py) {
        for (px = 0U; px < 4U; ++px) {
            if (!piece_cell(cur_piece, cur_rot, px, py)) continue;
            {
                signed char bx = (signed char)(cur_x + (signed char)px);
                signed char by = (signed char)(cur_y + (signed char)py);
                if (by >= 0 && by < (signed char)BH && bx >= 0 && bx < (signed char)BW) {
                    board[(unsigned char)by][(unsigned char)bx] = 1U;
                }
            }
        }
    }
}

static void clear_lines(void) {
    signed char y;
    unsigned char x;
    unsigned char cleared = 0U;
    for (y = (signed char)(BH - 1U); y >= 0; --y) {
        unsigned char full = 1U;
        for (x = 0U; x < BW; ++x) {
            if (!board[(unsigned char)y][x]) { full = 0U; break; }
        }
        if (full) {
            /* shift down */
            signed char yy;
            for (yy = y; yy > 0; --yy) {
                for (x = 0U; x < BW; ++x) {
                    board[(unsigned char)yy][x] = board[(unsigned char)(yy - 1)][x];
                }
            }
            for (x = 0U; x < BW; ++x) board[0][x] = 0U;
            ++cleared;
            ++lines;
            ++y; /* re-check same line */
        }
        if (y == 0) break;
    }

    if (cleared) {
        /* scoring simple */
        score += (unsigned long)cleared * (unsigned long)cleared * 100UL;
        level = (unsigned char)(lines / 10U);
    }
}

static void spawn(void) {
    cur_piece = next_piece;
    next_piece = (unsigned char)(rand8() % 7U);
    cur_rot = 0U;
    cur_x = 3;
    cur_y = -1;
}

static unsigned char game_over(void) {
    return collides(cur_x, cur_y, cur_piece, cur_rot);
}

static void reset_game(void) {
    unsigned char y, x;
    for (y = 0U; y < BH; ++y) for (x = 0U; x < BW; ++x) board[y][x] = 0U;
    score = 0UL;
    lines = 0U;
    level = 0U;
    next_piece = (unsigned char)(rand8() % 7U);
    spawn();
}

static void delay_cycles(unsigned int n) {
    volatile unsigned int i;
    for (i = 0U; i < n; ++i) {
        /* busy */
    }
}

void main(void) {
    unsigned int drop = 0U;

    tms_init_regs(SCREEN1_TABLE);
    tms_set_color(COLOR_LIGHT_YELLOW);
    screen1_prepare();
    screen1_load_font();

    reset_game();
    draw_static_ui();
    draw_board();
    draw_hud();
    draw_next();
    draw_piece_at(cur_x, cur_y, cur_piece, cur_rot, 1U);

    for (;;) {
        unsigned char k = apple1_readkey();

        if (k) {
            if (k >= 'a' && k <= 'z') k = (unsigned char)(k - 32U);
            if (k == 'Q') {
                woz_mon();
            } else if (k == 'A') {
                if (!collides((signed char)(cur_x - 1), cur_y, cur_piece, cur_rot)) {
                    draw_piece_at(cur_x, cur_y, cur_piece, cur_rot, 0U);
                    --cur_x;
                    draw_piece_at(cur_x, cur_y, cur_piece, cur_rot, 1U);
                }
            } else if (k == 'D') {
                if (!collides((signed char)(cur_x + 1), cur_y, cur_piece, cur_rot)) {
                    draw_piece_at(cur_x, cur_y, cur_piece, cur_rot, 0U);
                    ++cur_x;
                    draw_piece_at(cur_x, cur_y, cur_piece, cur_rot, 1U);
                }
            } else if (k == 'W') {
                unsigned char nr = (unsigned char)((cur_rot + 1U) & 3U);
                if (!collides(cur_x, cur_y, cur_piece, nr)) {
                    draw_piece_at(cur_x, cur_y, cur_piece, cur_rot, 0U);
                    cur_rot = nr;
                    draw_piece_at(cur_x, cur_y, cur_piece, cur_rot, 1U);
                }
            } else if (k == 'S') {
                drop = 9999U; /* trigger immediate fall */
            } else if (k == ' ') {
                /* hard drop */
                draw_piece_at(cur_x, cur_y, cur_piece, cur_rot, 0U);
                while (!collides(cur_x, (signed char)(cur_y + 1), cur_piece, cur_rot)) {
                    ++cur_y;
                }
                draw_piece_at(cur_x, cur_y, cur_piece, cur_rot, 1U);
                drop = 9999U;
            }
        }

        /* gravity */
        ++drop;
        {
            unsigned int speed = (unsigned int)(2500U - (unsigned int)level * 150U);
            if (speed < 600U) speed = 600U;
            if (drop >= speed) {
                drop = 0U;
                if (!collides(cur_x, (signed char)(cur_y + 1), cur_piece, cur_rot)) {
                    draw_piece_at(cur_x, cur_y, cur_piece, cur_rot, 0U);
                    ++cur_y;
                    draw_piece_at(cur_x, cur_y, cur_piece, cur_rot, 1U);
                } else {
                    lock_piece();
                    clear_lines();
                    draw_board();
                    draw_hud();
                    spawn();
                    draw_hud();
                    draw_next();
                    if (game_over()) {
                        screen1_locate(16, 18);
                        screen1_puts((const unsigned char *)"GAME OVER");
                        screen1_locate(16, 20);
                        screen1_puts((const unsigned char *)"R=RESTART Q=QUIT");
                        for (;;) {
                            unsigned char kk = apple1_getkey();
                            if (kk >= 'a' && kk <= 'z') kk = (unsigned char)(kk - 32U);
                            if (kk == 'Q') woz_mon();
                            if (kk == 'R') break;
                        }
                        reset_game();
                        draw_static_ui();
                        draw_board();
                        draw_hud();
                        draw_next();
                    }
                    draw_piece_at(cur_x, cur_y, cur_piece, cur_rot, 1U);
                }
            }
        }

        delay_cycles(600U);
    }
}

