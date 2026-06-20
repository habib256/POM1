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
 *                          --preset 11 --load 6000:<bin> --run 6000
 *
 * Telemetry: FREE-RUN (no lock-step) so the game plays live. Schema declares
 * EXACTLY 5 fields, in order: head_x:U8, head_y:U8, length:U8, alive:BOOL,
 * score:U16. Per-tick DATA frame = [head_x, head_y, length, alive, score].
 * Harness can also drive the snake by pushing a direction byte to TELE_IN
 * (1=up 2=down 3=left 4=right).
 *
 * --- Cell -> pixel mapping -------------------------------------------------
 * The 280x192 HIRES screen is divided into 8x8-pixel CELLS: 35 columns
 * (0..34, x = col*8) by 24 rows (0..23, y = row*8). A snake segment / the food
 * fills a 6x6 block inside its cell (a 1px gap on the right + bottom keeps the
 * grid readable). Top + bottom walls only (1px rules); the left/right sides are
 * OPEN — the snake wraps horizontally from one edge to the other.
 *
 * --- Visuals & gameplay ----------------------------------------------------
 * Snake = white blocks; the apple = a SOLID RED disk (orange is HIRES's warmest
 * tone — there is no true red); the bonus = a SOLID GREEN block; the score reads
 * in WHITE via the flicker-free gen2_hgr_putu_field HUD renderer (top-right);
 * the "HGR Snake" label sits top-left in cycling violet/green/orange/blue
 * (mixed case shows off BBFont's lowercase glyphs). HIRES colour
 * is a byte-pattern artifact (the tint keeps only ~half the pixels), so the game
 * sprites are drawn FILLED — a hollow outline gets halved into scattered dots.
 * Gameplay extra: every BONUS_EVERY apples a time-limited bonus gem appears for
 * BONUS_TTL ticks — grab it before it fades for BONUS_POINTS extra points (no
 * growth, so it is pure risk/reward).
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

/* --- Bonus gem (gameplay extra) --- */
#define BONUS_EVERY  4u         /* drop a bonus every Nth apple                  */
#define BONUS_TTL    36u        /* bonus lifetime in ticks before it fades       */
#define BONUS_POINTS 20u        /* score for grabbing a bonus (4x a normal apple)*/

/* --- Direction codes (also the harness TELE_IN protocol) --- */
#define DIR_UP    1
#define DIR_DOWN  2
#define DIR_LEFT  3
#define DIR_RIGHT 4

/* Snake body as a TRUE circular ring buffer of cell coordinates. `head` is the
 * array index of segment 0 (the head); `tail` the index of the last segment.
 * Live segments walk FORWARD tail -> head (wrapping at MAXLEN). Moving the snake
 * is O(1): write the new head one slot past `head`, then advance `tail` to drop
 * the old tail (unless we grew) — no per-segment shift (was O(length)/tick). */
static unsigned char sx[MAXLEN];   /* body cell columns */
static unsigned char sy[MAXLEN];   /* body cell rows    */
static unsigned char slen;         /* current length    */
static unsigned char head;         /* array index of the head segment       */
static unsigned char tail;         /* array index of the tail segment       */
static unsigned char dir;          /* current heading   */
static unsigned char pending;      /* queued heading (applied next tick) */
static unsigned char alive;        /* 1 = playing, 0 = dead */
static unsigned char foodx, foody; /* food cell */
static unsigned int  score;        /* total points (5/apple + 20/bonus) */
static unsigned int  score_shown;  /* last score value drawn to the HUD (redraw-on-change) */
static unsigned char apples;       /* apples eaten this game (drives the bonus cadence)   */
/* Time-limited bonus gem. Logic in tick() sets the flags; the main loop does the
 * drawing (keeps tick() free of framebuffer writes, like the rest of the game). */
static unsigned char bonus_active; /* 1 = a bonus is currently on the field */
static unsigned char bonusx, bonusy; /* bonus cell */
static unsigned char bonus_ttl;    /* ticks left before the bonus fades        */
static unsigned char bonus_new;    /* set the tick a bonus spawns  -> loop draws it  */
static unsigned char bonus_gone;   /* set the tick a bonus expires -> loop erases it */
static unsigned char layout = 1u;  /* keyboard: 1 = QWERTY (WASD), 2 = AZERTY (ZQSD) */
/* "HGR Snake" title colour, cycled through the four NTSC artifact colours on every
 * apple — a little visual reward. The recolour is all-asm: gen2_hgr_clear_pixrect
 * (gen2_pixrect_asm) wipes the label box, gen2_hgr_puts_color draws it again with
 * the one-pass tinted glyph blitter (gen2_blit_glyph_color). */
static const unsigned char title_hues[4] = { GEN2_VIOLET, GEN2_GREEN, GEN2_ORANGE, GEN2_BLUE };
static unsigned char title_hue;    /* index into title_hues, advanced per apple */
/* Throttle iterations; lower = faster, shrinks per apple. The starting value is
 * tuned so the per-tick busy-wait is HALF the original 9000-constant loop (a
 * genuine x2 start speed): the variable-bound loop costs ~96 cyc/iter vs ~76 for
 * a constant bound, so 9000*76/2/96 ~= 3565. */
static unsigned int  tick_spins = 3565u;

/* 16-bit LFSR PRNG (Galois), seeded from key-press timing at startup. */
static unsigned int rng_state = 0xACE1u;

static unsigned int prng(void)
{
    unsigned int lsb = rng_state & 1u;
    rng_state >>= 1;
    if (lsb) rng_state ^= 0xB400u;     /* taps 16,14,13,11 */
    return rng_state;
}

/* Fill a BLOCK x BLOCK white square at cell (cx, cy) in ONE asm call
 * (gen2_hgr_fill_pixrect) instead of a 36-plot double loop — ~10x faster. */
static void draw_cell(unsigned char cx, unsigned char cy)
{
    gen2_hgr_fill_pixrect((unsigned)cx * CELL, cy * (unsigned char)CELL, BLOCK, BLOCK);
}

/* Erase a cell — clears the BLOCK x BLOCK area (asm gen2_hgr_clear_pixrect) so
 * the snake's vacated tail goes without touching the walls or the rest. */
static void erase_cell(unsigned char cx, unsigned char cy)
{
    gen2_hgr_clear_pixrect((unsigned)cx * CELL, cy * (unsigned char)CELL, BLOCK, BLOCK);
}

/* Draw the apple as a SOLID round RED disk. HIRES colour is a byte-pattern
 * artifact: the tint ANDs each byte with a half-density carrier, so it KEEPS
 * only ~half the pixels. A FILLED, symmetric disk stays a full red blob through
 * that; the old "fill block + knock the corners off" left an asymmetric pattern
 * the carrier turned into a half-circle. Built as filled rows (orange = HIRES's
 * warmest tone, the closest it has to red). */
static void draw_food(unsigned char cx, unsigned char cy)
{
    unsigned px = (unsigned)cx * CELL;
    unsigned char py = cy * (unsigned char)CELL;
    gen2_hgr_fill_pixrect(px + 2u, py,                       2u, 1u);  /* ..##.. */
    gen2_hgr_fill_pixrect(px + 1u, (unsigned char)(py + 1u), 4u, 1u);  /* .####. */
    gen2_hgr_fill_pixrect(px,      (unsigned char)(py + 2u), 6u, 2u);  /* ###### */
    gen2_hgr_fill_pixrect(px + 1u, (unsigned char)(py + 4u), 4u, 1u);  /* .####. */
    gen2_hgr_fill_pixrect(px + 2u, (unsigned char)(py + 5u), 2u, 1u);  /* ..##.. */
    gen2_hgr_colorize(px, py, BLOCK, BLOCK, GEN2_ORANGE);
}

/* Draw the bonus gem as a SOLID GREEN block. The old hollow diamond outline was
 * halved by the tint into a few scattered dots — "not very visible, not a closed
 * figure". A filled cell stays a clearly visible, closed green gem: its GREEN
 * tells it apart from the white snake, and the round RED apple is its own shape. */
static void draw_bonus(unsigned char cx, unsigned char cy)
{
    unsigned px = (unsigned)cx * CELL;
    unsigned char py = cy * (unsigned char)CELL;
    gen2_hgr_fill_pixrect(px, py, BLOCK, BLOCK);
    gen2_hgr_colorize(px, py, BLOCK, BLOCK, GEN2_GREEN);
}

/* Top and bottom walls only — the left/right sides are OPEN so the snake wraps
 * horizontally (see the wrap in tick()). Each wall is a 1px rule hugging the
 * outermost playable row (so the snake dies exactly when it reaches it, with no
 * empty-cell gap), drawn with a whole-byte fill_rect across the interior byte
 * columns — entirely on the asm fast path, no per-pixel plot loop. */
static void draw_border(void)
{
    const unsigned char top = (unsigned char)((TOP_WALL + 1u) * CELL - 1u);  /* y = 23  */
    const unsigned char bot = (unsigned char)((ROWS - 1u) * CELL);           /* y = 184 */
    gen2_hgr_fill_rect(top, 1u, 1u, (COLS * CELL) / 7u - 2u, 0x7Fu);         /* top rule    */
    gen2_hgr_fill_rect(bot, 1u, 1u, (COLS * CELL) / 7u - 2u, 0x7Fu);         /* bottom rule */
}

/* Place food on an empty cell INSIDE the playable field (avoid the border ring,
 * the score HUD above the top wall, and the snake body). The playable rows are
 * TOP_WALL+1 .. ROWS-2 — same bounds the wall-collision test enforces — so food
 * can never land in the HUD strip above the top wall. */
/* Is cell (cx,cy) on the snake body? skip_tail=1 ignores the tail segment (the
 * self-collision case: the tail vacates this tick unless we grow). Walks the
 * ring tail->head — the single body scan every caller shares. */
static unsigned char body_hits(unsigned char cx, unsigned char cy, unsigned char skip_tail)
{
    unsigned char idx = tail, k;
    for (k = 0; k < slen; ++k) {
        if (!(skip_tail && k == 0u) && sx[idx] == cx && sy[idx] == cy) return 1u;
        if (++idx == MAXLEN) idx = 0u;
    }
    return 0u;
}

static void place_food(void)
{
    unsigned char ok;
    do {
        foodx = (unsigned char)(1u + prng() % (COLS - 2u));               /* cols 1..COLS-2 */
        foody = (unsigned char)((TOP_WALL + 1u) + prng() % (ROWS - TOP_WALL - 2u)); /* rows TOP_WALL+1..ROWS-2 */
        ok = 1;
        if (bonus_active && foodx == bonusx && foody == bonusy) ok = 0;   /* not on the bonus */
        else if (body_hits(foodx, foody, 0u)) ok = 0;
    } while (!ok);
}

/* Place the bonus on an empty playable cell — same bounds as place_food, but it
 * must also avoid the apple. */
static void place_bonus(void)
{
    unsigned char ok;
    do {
        bonusx = (unsigned char)(1u + prng() % (COLS - 2u));
        bonusy = (unsigned char)((TOP_WALL + 1u) + prng() % (ROWS - TOP_WALL - 2u));
        ok = 1;
        if (bonusx == foodx && bonusy == foody) ok = 0;                   /* not on the apple */
        else if (body_hits(bonusx, bonusy, 0u)) ok = 0;
    } while (!ok);
}

/* Declare the telemetry schema ONCE (head_x, head_y, length, alive, score). */
static void emit_schema(void)
{
    tele_field(TELE_T_U8,   "head_x");
    tele_field(TELE_T_U8,   "head_y");
    tele_field(TELE_T_U8,   "length");
    tele_field(TELE_T_BOOL, "alive");
    tele_field(TELE_T_U16,  "score");    /* total points (5/apple + 20/bonus)    */
    tele_schema_close();
}

/* Emit one per-tick DATA frame: [head_x, head_y, length, alive, score]. */
static void emit_state(void)
{
    tele_put(sx[head]);
    tele_put(sy[head]);
    tele_put(slen);
    tele_put(alive);
    tele_put16(score);                   /* 2 bytes LE, matches TELE_T_U16       */
    tele_frame();
}

/* Reset to a fresh game (3-segment snake heading right, mid-field). */
static void new_game(void)
{
    unsigned char i;
    slen = 3u;
    tail = 0u;
    head = (unsigned char)(slen - 1u);            /* ring: index 0 = tail .. slen-1 = head */
    for (i = 0; i < slen; ++i) {
        /* i=0 is the tail (leftmost); i=slen-1 the head (rightmost), heading right */
        sx[i] = (unsigned char)(COLS / 2u - (slen - 1u) + i);
        sy[i] = (unsigned char)(ROWS / 2u);
    }
    dir = DIR_RIGHT;
    pending = DIR_RIGHT;
    alive = 1;
    score = 0;
    score_shown = 0;           /* redraw() draws score 0; the loop redraws only on change */
    tick_spins = 3565u;        /* reset to the (genuine x2) starting speed (layout kept) */
    apples = 0;
    title_hue = 0;             /* HGR Snake starts violet, shifts colour per apple */
    bonus_active = 0;          /* no bonus until the cadence drops one */
    bonus_new = 0;
    bonus_gone = 0;
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

/* Read the keyboard for the layout chosen at startup, plus the harness TELE_IN
 * direction byte. Down/right are S/D in both layouts; only up/left differ —
 * QWERTY = W/A, AZERTY = Z/Q — so a key meant for the other layout is ignored. */
static void read_input(void)
{
    unsigned char k = apple1_readkey();
    if (k) {
        if (k == 'S' || k == 's')      set_dir(DIR_DOWN);
        else if (k == 'D' || k == 'd') set_dir(DIR_RIGHT);
        else if (layout == 2u) {                 /* AZERTY: ZQSD */
            if (k == 'Z' || k == 'z')      set_dir(DIR_UP);
            else if (k == 'Q' || k == 'q') set_dir(DIR_LEFT);
        } else {                                 /* QWERTY: WASD */
            if (k == 'W' || k == 'w')      set_dir(DIR_UP);
            else if (k == 'A' || k == 'a') set_dir(DIR_LEFT);
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
    unsigned char nx, ny, grow;

    dir = pending;
    nx = sx[head];
    ny = sy[head];
    switch (dir) {
        case DIR_UP:    --ny; break;
        case DIR_DOWN:  ++ny; break;
        case DIR_LEFT:  --nx; break;
        case DIR_RIGHT: ++nx; break;
        default: break;
    }

    /* Horizontal WRAP — no left/right walls: cols 1..COLS-2 form a ring, so a
     * head leaving one side reappears on the other. (nx is unsigned char, so
     * --nx from col 1 gives 0 and ++nx from col COLS-2 gives COLS-1.) */
    if (nx < 1u)                                   nx = (unsigned char)(COLS - 2u);
    else if (nx > (unsigned char)(COLS - 2u))      nx = 1u;
    /* Top and bottom walls still kill. */
    if (ny <= TOP_WALL || ny >= (unsigned char)(ROWS - 1u)) {
        alive = 0;
        return;
    }
    /* Self collision (skip the tail, which is about to move unless we grow). */
    if (body_hits(nx, ny, 1u)) { alive = 0; return; }

    /* Bonus pickup: worth BONUS_POINTS but no growth. The new head lands on the
     * gem and draw_cell() will paint over it, so just clear the flag (no erase). */
    if (bonus_active && nx == bonusx && ny == bonusy) {
        score += BONUS_POINTS;
        bonus_active = 0u;
    }

    grow = (nx == foodx && ny == foody) ? 1u : 0u;

    /* O(1) ring move: append the new head one slot past `head`; drop the old
     * tail by advancing `tail` (the main loop erased its cell) — unless we grew,
     * in which case the tail stays and the body gets one segment longer. */
    if (++head == MAXLEN) head = 0u;
    sx[head] = nx;
    sy[head] = ny;
    if (grow && slen < MAXLEN) {
        ++slen;                                   /* grew: keep the tail */
    } else {
        if (++tail == MAXLEN) tail = 0u;          /* moved: drop the old tail */
    }

    if (grow) {
        score += 5u;                  /* each apple is worth 5 points */
        ++apples;
        /* Each apple speeds the snake up: shorten the throttle. Low floor + a
         * gentle step so the speed keeps climbing for many apples instead of
         * capping early (3565 -> ~365 over ~16 apples) — the end game gets
         * genuinely fast. The floor stays > 0 so the throttle never vanishes. */
        if (tick_spins > 400u) tick_spins -= 200u;
        place_food();
        /* Every BONUS_EVERY apples, drop a time-limited bonus gem (if one is not
         * already out). Drawing is the main loop's job, so just flag it. */
        if (!bonus_active && (apples % BONUS_EVERY) == 0u) {
            place_bonus();
            bonus_active = 1u;
            bonus_ttl    = BONUS_TTL;
            bonus_new    = 1u;
        }
    }

    /* Age an active bonus; when its timer runs out, flag it for erasure. */
    if (bonus_active) {
        --bonus_ttl;
        if (bonus_ttl == 0u) { bonus_active = 0u; bonus_gone = 1u; }
    }
}

/* Draw the score HUD (top-right) with the OPTIMIZED number renderer:
 * gen2_hgr_putu_field is the flicker-free HUD path — it wipes EXACTLY its own
 * 5-cell field (no separate gen2_hgr_clear_pixrect — the self-bounded wipe
 * can't bleed into the HGR Snake label that ends at x=170) and draws the
 * digits right-aligned in one pass. We pick width 5 so 0..65535 always fits
 * with no leading-zero leak from the shrinking case. Field width = 4*18+16 =
 * 88px; x=184 leaves an 8px right margin (mirrors the title's left margin). */
static void draw_score(void)
{
    gen2_hgr_putu_field(184u, 0u, score, 5u);
}

/* Draw the "HGR Snake" label, top-left, in the current cycling colour. 9 glyphs
 * x 18px pitch = 162px wide; x=8 ends it at pixel 170, leaving 14px of gap
 * before the right-hand score field. Mixed-case showcases BBFont's lowercase
 * glyphs. Wipe the box first so the new colour's pixels do not OR-mix with
 * the old one's (both passes are asm). */
static void draw_title(void)
{
    gen2_hgr_clear_pixrect(8u, 0u, 162u, 16u);
    gen2_hgr_puts_color(8, 0, "HGR Snake", title_hues[title_hue]);
}

/* Full draw — clear + walls/border + food + whole snake + score. Called ONCE
 * per game (start / restart), NOT per tick: the per-tick loop only erases the
 * vacated tail and draws the new head, so the expensive clear + border don't
 * run every frame. */
static void redraw(void)
{
    unsigned char idx = tail, k;
    gen2_hgr_clear(0);
    draw_border();
    draw_food(foodx, foody);
    if (bonus_active) draw_bonus(bonusx, bonusy);
    for (k = 0; k < slen; ++k) {        /* walk the ring tail -> head */
        draw_cell(sx[idx], sy[idx]);
        if (++idx == MAXLEN) idx = 0u;
    }
    draw_score();                       /* score, top-right, WHITE via optimized putu_field */
    draw_title();                       /* "HGR Snake", top-left, current cycling hue */
}

/* Plain CPU-spin throttle for a playable tick rate. We deliberately do NOT call
 * gen2_wait_vbl() here: it busy-polls HST0 ($C257) thousands of times per frame,
 * and every soft-switch read is journaled for the beam-raced renderer — that
 * flood was the real cause of the "very slow then crash". The blocky snake
 * doesn't need vblank-tight timing. */
static void throttle(void)
{
    unsigned int n;
    unsigned int lim = tick_spins;   /* LOCAL copy: a global loop bound makes cc65
                                       * reload it every iteration (~3x slower per
                                       * spin) — a local keeps the busy loop tight,
                                       * so halving tick_spins really doubles speed. */
    for (n = 0; n < lim; ++n) { /* busy spin; tick_spins shrinks per apple */ }
}

void main(void)
{
    unsigned int t;

    /* ---- Title page: the name + the two control layouts, each prefixed with the
     * key that selects it. Press 1 for QWERTY (WASD) or 2 for AZERTY (ZQSD). ---- */
    gen2_hgr_init();
    gen2_hgr_clear(0);
    /* Title card: Apple-1  "SNAKE HGR", split over two lines so the whole name
     * stays on screen, with every line in a different artifact colour for a
     * livelier look (HIRES's four colours: violet / green / orange / blue). */
    gen2_hgr_puts_color(114, 12, "A-1",           GEN2_GREEN);   /* line 1, centred */
    gen2_hgr_puts_color(42,  40, "\"SNAKE HGR\"", GEN2_VIOLET);  /* line 2: game name */
    gen2_hgr_puts_color(10,  72, "1 QWERTY  WASD", GEN2_BLUE);
    gen2_hgr_puts_color(10, 102, "2 AZERTY  ZQSD", GEN2_ORANGE);
    gen2_hgr_puts_color(38, 150, "PRESS 1 OR 2",   GEN2_GREEN);

    /* Telemetry: declare the schema once, then run free (live play, fire-hose).
     * tele_arm / tele_stat are unused in this free-running tap but cost nothing:
     * they are function-like macros (telemetry.h), not real functions. */
    emit_schema();
    tele_freerun();

    /* Hold the title until the player picks a layout with '1' or '2', or AUTO-START
     * with the QWERTY default after the timeout (under the DevBench the Apple-1
     * keyboard is usually unfocused, and the harness drives play with no key).
     * gen2_hgr_init() is re-asserted DURING this hold so the title appears once
     * the GEN2 card finishes its deferred plug. */
    layout = 1u;                       /* default if the title times out */
    for (t = 0; t < 90u; ++t) {
        unsigned char k;
        gen2_hgr_init();
        k = apple1_readkey();
        if (k == '1') { layout = 1u; break; }
        if (k == '2') { layout = 2u; break; }
        if (tele_inlen() != 0) break;
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
            unsigned char otx = sx[tail], oty = sy[tail];
            unsigned char olen = slen;
            read_input();
            tick();
            if (alive) {
                /* INCREMENTAL redraw — walls/border were drawn once in redraw();
                 * here we only touch the snake, the food, and the score. */
                if (slen == olen) {
                    erase_cell(otx, oty);          /* snake moved: clear old tail */
                } else {
                    draw_food(foodx, foody);       /* grew: show the new food     */
                    title_hue = (unsigned char)((title_hue + 1u) & 3u);
                    draw_title();                  /* apple eaten: shift HGR Snake colour */
                }
                draw_cell(sx[head], sy[head]);     /* draw the new head           */
                /* Bonus gem: tick() flags a spawn or a time-out; do the drawing
                 * here. (Eating it needs no erase — the head already covers it.) */
                if (bonus_new)  { draw_bonus(bonusx, bonusy); bonus_new  = 0u; }
                if (bonus_gone) { erase_cell(bonusx, bonusy); bonus_gone = 0u; }
                /* Score: redraw ONLY when it actually changes — not every frame. */
                if (score != score_shown) {
                    draw_score();
                    score_shown = score;
                }
            }
            emit_state();          /* one DATA frame per tick */
            throttle();
        } else {
            /* Death: show GAME OVER and emit alive=0 frames. A minimum hold of
             * ~4 seconds applies first (no early-exit) so the banner is always
             * readable, then a second window polls for a key / harness byte to
             * restart early. The minimum is timing-stable because we lock
             * tick_spins to its max (new_game() will reset it anyway). */
            unsigned int hold;
            /* Clear a black panel BEHIND the banner first — otherwise "GAME OVER"
             * is OR'd straight over the snake/apple under it and smears into
             * garbage. The 9-glyph word spans x=40..199 at y=80..95; pad it. */
            gen2_hgr_clear_pixrect(36u, 78u, 168u, 20u);
            gen2_hgr_puts_color(40, 80, "GAME OVER", GEN2_ORANGE);
            tick_spins = 3565u;             /* pin the throttle to its slow cadence */
            /* Phase 1 — ~4-second mandatory hold (no input read). */
            for (hold = 0; hold < 40u; ++hold) {
                emit_state();
                throttle();
            }
            /* Phase 2 — extra window where a key or harness byte ends the pause. */
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
