/*
 * GEN2Snake.c — Snake for Uncle Bernie's GEN2 HGR colour graphics card.
 *               Reports its state over POM1's telemetry side channel.
 *
 *   Snake for Uncle Bernie's GEN2 HGR card / VERHILLE Arnaud 2026
 *
 * The classic game on the Apple-1 GEN2 card's 280x192 HIRES screen, plus a live
 * "telemetry" tap: it declares a self-describing schema once at startup, then
 * emits one DATA frame per game tick so an external POM1 harness (or the
 * Telemetry window) can watch head_x / head_y / length / alive as you play.
 *
 *   Build : make                 -> software/Telemetry/GEN2Snake.bin (+ .txt)
 *   Run   : DevBench -> POM1 Bench -> Examples -> Snake telemetry  (C / GEN2 HGR)
 *           or  build/POM1 --headless --telemetry-port 6602 \
 *                          --preset 12 --load 6000:<bin> --run 6000
 *
 * Telemetry: FREE-RUN (no lock-step) so the game plays live. Schema declares
 * EXACTLY 4 fields, in order: head_x:U8, head_y:U8, length:U8, alive:BOOL.
 * Per-tick DATA frame = [head_x, head_y, length, alive]. Harness can also drive
 * the snake by pushing a direction byte to TELE_IN (1=up 2=down 3=left 4=right).
 *
 * --- Cell -> pixel mapping -------------------------------------------------
 * The 280x192 HIRES screen is divided into 8x8-pixel CELLS: 35 columns
 * (0..34, x = col*8) by 24 rows (0..23, y = row*8). A snake segment / the food
 * fills a 6x6 block inside its cell (a 1px gap on the right + bottom keeps the
 * grid readable). A 1px white border is drawn around the whole 35x24 field.
 * ---------------------------------------------------------------------------
 */
#include "gen2.h"
#include "apple1io.h"
#include "telemetry.h"

/* --- Playfield geometry (cells) --- */
#define CELL      8u            /* pixels per cell side                          */
#define COLS      35u           /* 280 / 8                                       */
#define ROWS      24u           /* 192 / 8                                       */
#define BLOCK     6u            /* filled block size inside a cell (1px gap)     */
#define MAXLEN    96u           /* snake body ring-buffer capacity               */
#define TOP_WALL  2u            /* top wall row; rows 0-1 above it are the score HUD,
                                 * which the snake (rows 3..ROWS-2) can never reach */

/* --- Direction codes (also the harness TELE_IN protocol) --- */
#define DIR_UP    1
#define DIR_DOWN  2
#define DIR_LEFT  3
#define DIR_RIGHT 4

/* Snake body as parallel ring-buffer arrays of cell coordinates. Index 0..len-1
 * are live segments; segment 0 is the head. We rebuild the body each move by
 * shifting, which is trivial at MAXLEN<=96 and keeps the code obvious. */
static unsigned char sx[MAXLEN];   /* body cell columns */
static unsigned char sy[MAXLEN];   /* body cell rows    */
static unsigned char slen;         /* current length    */
static unsigned char dir;          /* current heading   */
static unsigned char pending;      /* queued heading (applied next tick) */
static unsigned char alive;        /* 1 = playing, 0 = dead */
static unsigned char foodx, foody; /* food cell */
static unsigned int  score;        /* food eaten */

/* 16-bit LFSR PRNG (Galois), seeded from key-press timing at startup. */
static unsigned int rng_state = 0xACE1u;

static unsigned int prng(void)
{
    unsigned int lsb = rng_state & 1u;
    rng_state >>= 1;
    if (lsb) rng_state ^= 0xB400u;     /* taps 16,14,13,11 */
    return rng_state;
}

/* Fill a BLOCK x BLOCK white square at cell (cx, cy). */
static void draw_cell(unsigned char cx, unsigned char cy)
{
    unsigned px = (unsigned)cx * CELL;
    unsigned char py = cy * (unsigned char)CELL;
    unsigned char r, c;
    for (r = 0; r < BLOCK; ++r) {
        for (c = 0; c < BLOCK; ++c) {
            gen2_hgr_plot(px + c, (unsigned char)(py + r));
        }
    }
}

/* Erase a cell — clears the BLOCK x BLOCK area so the snake's vacated tail can
 * be removed without touching the walls or the rest of the field. */
static void erase_cell(unsigned char cx, unsigned char cy)
{
    unsigned px = (unsigned)cx * CELL;
    unsigned char py = cy * (unsigned char)CELL;
    unsigned char r, c;
    for (r = 0; r < BLOCK; ++r) {
        for (c = 0; c < BLOCK; ++c) {
            gen2_hgr_unplot(px + c, (unsigned char)(py + r));
        }
    }
}

/* Draw the food as a small hollow diamond (a 6x6 block with corners clipped) so
 * it reads differently from the solid snake segments. */
static void draw_food(unsigned char cx, unsigned char cy)
{
    unsigned px = (unsigned)cx * CELL;
    unsigned char py = cy * (unsigned char)CELL;
    /* a plus / diamond shape inside the 6x6 cell */
    gen2_hgr_plot(px + 2, (unsigned char)(py + 0));
    gen2_hgr_plot(px + 3, (unsigned char)(py + 0));
    gen2_hgr_plot(px + 1, (unsigned char)(py + 1));
    gen2_hgr_plot(px + 4, (unsigned char)(py + 1));
    gen2_hgr_plot(px + 0, (unsigned char)(py + 2));
    gen2_hgr_plot(px + 5, (unsigned char)(py + 2));
    gen2_hgr_plot(px + 0, (unsigned char)(py + 3));
    gen2_hgr_plot(px + 5, (unsigned char)(py + 3));
    gen2_hgr_plot(px + 1, (unsigned char)(py + 4));
    gen2_hgr_plot(px + 4, (unsigned char)(py + 4));
    gen2_hgr_plot(px + 2, (unsigned char)(py + 5));
    gen2_hgr_plot(px + 3, (unsigned char)(py + 5));
}

/* Border around the play area. The top wall sits at row TOP_WALL (leaving the
 * two rows above it as the score HUD); the snake never reaches it. */
static void draw_border(void)
{
    unsigned x;
    unsigned char y;
    const unsigned char top = (unsigned char)(TOP_WALL * CELL);   /* y = 16 */
    const unsigned char bot = (unsigned char)(ROWS * CELL - 1u);  /* y = 191 */
    for (x = 0; x < COLS * CELL; ++x) {
        gen2_hgr_plot(x, top);
        gen2_hgr_plot(x, bot);
    }
    for (y = top; y <= bot; ++y) {
        gen2_hgr_plot(0, y);
        gen2_hgr_plot((unsigned)(COLS * CELL - 1u), y);
    }
}

/* Place food on an empty cell (avoid the border ring and the snake body). */
static void place_food(void)
{
    unsigned char ok, i;
    do {
        foodx = (unsigned char)(1u + prng() % (COLS - 2u));
        foody = (unsigned char)(1u + prng() % (ROWS - 2u));
        ok = 1;
        for (i = 0; i < slen; ++i) {
            if (sx[i] == foodx && sy[i] == foody) { ok = 0; break; }
        }
    } while (!ok);
}

/* Declare the telemetry schema ONCE (head_x, head_y, length, alive). */
static void emit_schema(void)
{
    tele_field(TELE_T_U8,   "head_x");
    tele_field(TELE_T_U8,   "head_y");
    tele_field(TELE_T_U8,   "length");
    tele_field(TELE_T_BOOL, "alive");
    tele_schema_close();
}

/* Emit one per-tick DATA frame: [head_x, head_y, length, alive]. */
static void emit_state(void)
{
    tele_put(sx[0]);
    tele_put(sy[0]);
    tele_put(slen);
    tele_put(alive);
    tele_frame();
}

/* Reset to a fresh game (3-segment snake heading right, mid-field). */
static void new_game(void)
{
    unsigned char i;
    slen = 3;
    for (i = 0; i < slen; ++i) {
        sx[i] = (unsigned char)(COLS / 2u - i);   /* head ... tail going left */
        sy[i] = (unsigned char)(ROWS / 2u);
    }
    dir = DIR_RIGHT;
    pending = DIR_RIGHT;
    alive = 1;
    score = 0;
    place_food();
}

/* Set a pending direction, forbidding a 180-degree reversal. */
static void set_dir(unsigned char d)
{
    if (d == DIR_UP    && dir == DIR_DOWN)  return;
    if (d == DIR_DOWN  && dir == DIR_UP)    return;
    if (d == DIR_LEFT  && dir == DIR_RIGHT) return;
    if (d == DIR_RIGHT && dir == DIR_LEFT)  return;
    pending = d;
}

/* Read keyboard (WASD + ZQSD) and the harness TELE_IN direction byte. */
static void read_input(void)
{
    unsigned char k = apple1_readkey();
    if (k) {
        switch (k) {
            case 'W': case 'w': case 'Z': case 'z': set_dir(DIR_UP);    break;
            case 'S': case 's':                     set_dir(DIR_DOWN);  break;
            case 'A': case 'a': case 'Q': case 'q': set_dir(DIR_LEFT);  break;
            case 'D': case 'd':                     set_dir(DIR_RIGHT); break;
            default: break;
        }
    }
    /* Harness-driven direction: 1=up 2=down 3=left 4=right. */
    while (tele_inlen() != 0) {
        unsigned char b = tele_in();
        if (b >= DIR_UP && b <= DIR_RIGHT) set_dir(b);
    }
}

/* Advance one logical tick: move the head, resolve collisions / food. */
static void tick(void)
{
    unsigned char nx, ny, i, grow;

    dir = pending;
    nx = sx[0];
    ny = sy[0];
    switch (dir) {
        case DIR_UP:    --ny; break;
        case DIR_DOWN:  ++ny; break;
        case DIR_LEFT:  --nx; break;
        case DIR_RIGHT: ++nx; break;
        default: break;
    }

    /* Wall collision: the playable area is cells 1..COLS-2 / 1..ROWS-2 (the
     * outer ring is the border). */
    if (nx == 0 || nx >= (unsigned char)(COLS - 1u) ||
        ny <= TOP_WALL || ny >= (unsigned char)(ROWS - 1u)) {
        alive = 0;
        return;
    }
    /* Self collision (skip the tail, which is about to move unless we grow). */
    for (i = 0; i < slen; ++i) {
        if (sx[i] == nx && sy[i] == ny) {
            if (!(i == slen - 1u)) { alive = 0; return; }
        }
    }

    grow = (nx == foodx && ny == foody) ? 1u : 0u;

    /* Shift the body down by one, then write the new head at index 0. */
    if (grow && slen < MAXLEN) ++slen;
    for (i = (unsigned char)(slen - 1u); i > 0; --i) {
        sx[i] = sx[i - 1u];
        sy[i] = sy[i - 1u];
    }
    sx[0] = nx;
    sy[0] = ny;

    if (grow) {
        ++score;
        place_food();
    }
}

/* Full draw — clear + walls/border + food + whole snake + score. Called ONCE
 * per game (start / restart), NOT per tick: the per-tick loop only erases the
 * vacated tail and draws the new head, so the expensive clear + border don't
 * run every frame. */
static void redraw(void)
{
    unsigned char i;
    gen2_hgr_clear(0);
    draw_border();
    draw_food(foodx, foody);
    for (i = 0; i < slen; ++i) draw_cell(sx[i], sy[i]);
    /* Score, top-left inside the field (BBFont 16x16 cells). */
    gen2_hgr_putu(8, 0, score);
}

/* Plain CPU-spin throttle for a playable tick rate. We deliberately do NOT call
 * gen2_wait_vbl() here: it busy-polls HST0 ($C257) thousands of times per frame,
 * and every soft-switch read is journaled for the beam-raced renderer — that
 * flood was the real cause of the "very slow then crash". The blocky snake
 * doesn't need vblank-tight timing. */
static void throttle(void)
{
    unsigned int n;
    for (n = 0; n < 9000u; ++n) { /* busy spin */ }
}

void main(void)
{
    unsigned int t;

    /* ---- Title page: the name + BOTH control layouts, clearly labelled. Both
     * work simultaneously (no selection needed), so we just show them. ---- */
    gen2_hgr_init();
    gen2_hgr_clear(0);
    gen2_hgr_puts(56,  16, "GEN2 SNAKE");
    gen2_hgr_puts(20,  64, "QWERTY  WASD");
    gen2_hgr_puts(20,  96, "AZERTY  ZQSD");
    gen2_hgr_puts(56, 150, "GET READY");

    /* Telemetry: declare the schema once, then run free (live play, fire-hose). */
    emit_schema();
    tele_freerun();

    /* Hold the title briefly, then AUTO-START — no blocking "press a key" gate
     * (under the DevBench the Apple-1 keyboard usually isn't focused, which would
     * freeze the game and its telemetry). A key / harness byte starts early.
     * gen2_hgr_init() is re-asserted DURING this hold so the title appears once
     * the GEN2 card finishes its deferred plug — but NOT inside the game loop:
     * keeping the soft-switch journal empty there lets the fast per-scanline
     * diffed render path pick up the incremental tail-erase. (Re-asserting every
     * frame forces the beam-race path, which left the old tail on screen — the
     * snake then looked like it grew every step.) */
    for (t = 0; t < 40u; ++t) {
        gen2_hgr_init();
        if (apple1_readkey() != 0 || tele_inlen() != 0) break;
        throttle();
    }

    new_game();
    redraw();
    emit_state();

    for (;;) {
        /* Re-assert the HGR mode every frame. Two reasons: (1) the GEN2 card's
         * deferred plug under the DevBench, and (2) — crucially — it journals
         * soft-switch events, so the emulator renders us through the BEAM path
         * (the one that works for every other GEN2 program). With an empty
         * journal the fast diffed path is used instead, where the incremental
         * tail-erase / score don't show. Four cheap soft-switch reads. */
        gen2_hgr_init();
        if (alive) {
            /* Remember the tail BEFORE the move so we can erase exactly the cell
             * it vacates — the only cell that changes besides the new head. */
            unsigned char otx = sx[slen - 1u], oty = sy[slen - 1u];
            unsigned char olen = slen;
            read_input();
            tick();
            if (alive) {
                /* INCREMENTAL redraw — walls/border were drawn once in redraw();
                 * here we only touch the snake (+ score on growth). */
                if (slen == olen) {
                    erase_cell(otx, oty);          /* snake moved: clear old tail */
                } else {
                    draw_food(foodx, foody);       /* grew: show the new food ... */
                    gen2_hgr_putu(8, 0, score);   /* ... and the new score       */
                }
                draw_cell(sx[0], sy[0]);           /* draw the new head           */
            }
            emit_state();          /* one DATA frame per tick */
            throttle();
        } else {
            /* Death: show GAME OVER and emit alive=0 frames for a short, visible
             * pause so the telemetry / UI registers it, then AUTO-RESTART (a key
             * or harness byte restarts early) — the demo never gets stuck. */
            unsigned int hold;
            gen2_hgr_puts(40, 80, "GAME OVER");
            for (hold = 0; hold < 50u; ++hold) {
                emit_state();
                if (apple1_readkey() != 0 || tele_inlen() != 0) break;
                throttle();
            }
            new_game();
            redraw();
            emit_state();
        }
    }
}
