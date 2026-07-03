/*
 * GEN2Pang.c — a PANG / Buster Bros clone for Uncle Bernie's GEN2 HGR card,
 *              built on the rendering techniques of demo_bounces/GEN2Bounces.c.
 *
 *   GEN2 Pang (bubble buster) / VERHILLE Arnaud 2026
 *
 * You drive a cannon along the floor; falling bubbles bounce in gravity arcs,
 * also off a few mid-air platforms. Fire fast bullet pellets straight up; a hit
 * splits the bubble. A big bubble (48) splits into two medium (32), each medium
 * into two small (16), each small pops. Bubble/bullet hits use CIRCLE collision
 * (centre distance vs radii), so the sphere's radius really matters. Shooting a
 * platform erodes the segment you hit — the most-shot spots wear into holes that
 * bullets and bubbles fall through. Clear the screen to advance; a bubble
 * touching you costs a life.
 *
 * Controls inspired by TMS Galaga: the ship GLIDES — a key latches a direction
 * applied every frame — and SPACE fires discrete, fast-moving bullet pellets (a
 * small pool, short cooldown), not an instant ray. S stops AND fires an aimed
 * shot (Galaga). Pick US (QWERTY, A=left) or FR (AZERTY, Q=left) at the title.
 *
 * Techniques inherited from Bounces (the "good ones"):
 *   - DOUBLE BUFFERING  gen2_set_draw_page / gen2_show_page  (tear free)
 *   - INCREMENTAL XOR   each bubble/cannon/bullet is erased by re-blitting at
 *                       the same spot (no box to scrub); the decor is drawn ONCE
 *                       per page and never redrawn.
 *   - FAST 7PX BLIT     bubbles use gen2_hgr_blit7 (XOR whole BYTES, 7px/byte);
 *                       their x steps by 7 (1 byte) to stay byte-aligned.
 *   - PRE-TINTED SPRITE the artifact-colour bubbles are tinted ONCE at startup,
 *                       in both byte-column phases, then XOR-blitted for free.
 * New here vs Bounces: bubbles fall under gravity (vy free, vx held at +/-7),
 * and the cannon + bullets use the 1px-precise gen2_hgr_blit so the ship glides
 * smoothly and pellets travel at any x.
 *
 *   Build : make    -> "software/Graphic HGR/GEN2Pang.bin"
 *   Run   : build/POM1 --preset 11 \
 *               --load 6000:"software/Graphic HGR/GEN2Pang.bin" --run 6000
 *   Keys  : A/Q = glide left, D = glide right, S = stop, SPACE = fire.
 */
#include "gen2.h"

/* ---- playfield ---------------------------------------------------------- */
#define FL      7u                  /* left wall  (multiple of 7)            */
#define FR    273u                  /* right wall                            */
#define FT     16u                  /* ceiling                               */
#define FB    176u                  /* floor (bubbles + cannon rest here)    */

#define PLAYER_PX  16u              /* cannon width (pixels, 1px-positioned)  */
#define PLAYER_H   12u
#define PLAYER_Y  (FB - PLAYER_H)   /* cannon top                            */
#define PLAYER_STEP 6               /* glide px per frame (Galaga feel)       */
#define HSTEP      7u               /* bubble horizontal step (byte aligned)  */

#define BULLET_W   2u               /* thin white bolt (2 px wide)            */
#define BULLET_H   4u
#define BULLET_SPEED 24             /* px/frame upward — very fast (3x)        */
#define BULLET_STEP  6              /* collision sub-step so it never tunnels  */
#define MAXBUL     2u               /* max 2 bullets on screen at once        */
#define FIRE_COOLDOWN 5u            /* frames between shots                    */

#define GRAV    6                   /* gravity, 1/16 px per frame^2          */
#define MAXB   12u                  /* bubble slots (peak alive is 8: 2 big   */
                                    /* balloons -> 4 med -> 8 small)          */

/* HUD (above the ceiling, bubbles never reach here) */
#define HUDY     3u
#define SCOREX  48u
#define LIVESX 232u

/* ---- platforms: bubbles bounce on their TOP, bullets are stopped by them.
 * Each platform is a row of SEG_W-wide segments with hit points; a bullet erodes
 * the segment it strikes, and a broken segment lets bullets AND bubbles through
 * (so the spots you shoot most wear away first). */
#define NPLAT   3                   /* max simultaneous platforms            */
#define PLAT_H  3                   /* bar thickness (pixels)                */
#define SEG_W   7                   /* segment width = 1 byte column         */
#define MAXSEG 12                   /* widest platform / SEG_W               */
#define SEG_HP  2                   /* bullet hits to break a segment        */

/* Per-level layouts: only 1 balloon ever, so the platforms ARE the difficulty.
 * Placement changes and platforms thin out as levels rise — fewer ledges means
 * the balloon comes all the way down to your level. Levels past the last layout
 * keep the hardest one. */
#define NLAYOUT 4
static const unsigned char lay_n[NLAYOUT] = { 3u, 3u, 2u, 1u };
static const int lay_x0[NLAYOUT][NPLAT] = {
    {  28, 168, 116 }, {  18, 190,  96 }, {  56, 168,   0 }, { 104,   0,   0 },
};
static const int lay_x1[NLAYOUT][NPLAT] = {
    { 104, 244, 164 }, {  92, 266, 180 }, { 132, 250,   0 }, { 182,   0,   0 },
};
static const int lay_y [NLAYOUT][NPLAT] = {
    { 124, 112, 148 }, { 118, 104, 150 }, { 108, 140,   0 }, { 120,   0,   0 },
};
static int plat_x0[NPLAT], plat_x1[NPLAT], plat_y[NPLAT]; /* this level's bars */
static unsigned char plat_count;              /* active platforms this level  */
static unsigned char seg_hp[NPLAT][MAXSEG];   /* 0 = broken                  */
static unsigned char plat_nseg[NPLAT];
static unsigned char plat_dirty;              /* a segment broke -> rebuild   */

/* ---- per-tier geometry: 0=big(48), 1=med(32), 2=small(16) --------------- */
static const unsigned char tsz[3] = { 48u, 32u, 16u };   /* side, px         */
static const unsigned char twb[3] = {  7u,  5u,  3u };   /* bytes per row    */
static const unsigned char tcol[3] = { GEN2_ORANGE, GEN2_BLUE, GEN2_GREEN };
static const int   tbounce[3] = { 150, 126, 104 };       /* floor kick (up)  */
static const unsigned tscore[3] = { 10u, 20u, 50u };     /* points per pop   */

/* Raw filled discs, packed 7px/byte (bit n of byte = pixel). The 48 and 16
 * tables are Bounces' proven kBig7 / kBall7 verbatim; the 32 is generated to
 * match. Pre-packed (not rasterised at runtime) because cc65 div/mod/mul are
 * slow function calls — a startup rasteriser cost ~10s of emulated time. */
static const unsigned char kDisc48[336] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x78,0x7F,0x07,0x00,0x00,
    0x00,0x00,0x7E,0x7F,0x1F,0x00,0x00, 0x00,0x40,0x7F,0x7F,0x7F,0x00,0x00,
    0x00,0x70,0x7F,0x7F,0x7F,0x03,0x00, 0x00,0x78,0x7F,0x7F,0x7F,0x07,0x00,
    0x00,0x7E,0x7F,0x7F,0x7F,0x1F,0x00, 0x00,0x7F,0x7F,0x7F,0x7F,0x3F,0x00,
    0x40,0x7F,0x7F,0x7F,0x7F,0x7F,0x00, 0x40,0x7F,0x7F,0x7F,0x7F,0x7F,0x00,
    0x60,0x7F,0x7F,0x7F,0x7F,0x7F,0x01, 0x70,0x7F,0x7F,0x7F,0x7F,0x7F,0x03,
    0x70,0x7F,0x7F,0x7F,0x7F,0x7F,0x03, 0x78,0x7F,0x7F,0x7F,0x7F,0x7F,0x07,
    0x78,0x7F,0x7F,0x7F,0x7F,0x7F,0x07, 0x7C,0x7F,0x7F,0x7F,0x7F,0x7F,0x0F,
    0x7C,0x7F,0x7F,0x7F,0x7F,0x7F,0x0F, 0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F,
    0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F, 0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F,
    0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F, 0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F,
    0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F, 0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F,
    0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F, 0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F,
    0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F, 0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F,
    0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F, 0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F,
    0x7E,0x7F,0x7F,0x7F,0x7F,0x7F,0x1F, 0x7C,0x7F,0x7F,0x7F,0x7F,0x7F,0x0F,
    0x7C,0x7F,0x7F,0x7F,0x7F,0x7F,0x0F, 0x78,0x7F,0x7F,0x7F,0x7F,0x7F,0x07,
    0x78,0x7F,0x7F,0x7F,0x7F,0x7F,0x07, 0x70,0x7F,0x7F,0x7F,0x7F,0x7F,0x03,
    0x70,0x7F,0x7F,0x7F,0x7F,0x7F,0x03, 0x60,0x7F,0x7F,0x7F,0x7F,0x7F,0x01,
    0x40,0x7F,0x7F,0x7F,0x7F,0x7F,0x00, 0x40,0x7F,0x7F,0x7F,0x7F,0x7F,0x00,
    0x00,0x7F,0x7F,0x7F,0x7F,0x3F,0x00, 0x00,0x7E,0x7F,0x7F,0x7F,0x1F,0x00,
    0x00,0x78,0x7F,0x7F,0x7F,0x07,0x00, 0x00,0x70,0x7F,0x7F,0x7F,0x03,0x00,
    0x00,0x40,0x7F,0x7F,0x7F,0x00,0x00, 0x00,0x00,0x7E,0x7F,0x1F,0x00,0x00,
    0x00,0x00,0x78,0x7F,0x07,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};
static const unsigned char kDisc32[160] = {
    0x00,0x00,0x00,0x00,0x00, 0x00,0x70,0x7F,0x00,0x00, 0x00,0x7E,0x7F,0x07,0x00,
    0x00,0x7F,0x7F,0x0F,0x00, 0x40,0x7F,0x7F,0x1F,0x00, 0x60,0x7F,0x7F,0x3F,0x00,
    0x70,0x7F,0x7F,0x7F,0x00, 0x78,0x7F,0x7F,0x7F,0x01, 0x7C,0x7F,0x7F,0x7F,0x03,
    0x7C,0x7F,0x7F,0x7F,0x03, 0x7C,0x7F,0x7F,0x7F,0x03, 0x7E,0x7F,0x7F,0x7F,0x07,
    0x7E,0x7F,0x7F,0x7F,0x07, 0x7E,0x7F,0x7F,0x7F,0x07, 0x7E,0x7F,0x7F,0x7F,0x07,
    0x7E,0x7F,0x7F,0x7F,0x07, 0x7E,0x7F,0x7F,0x7F,0x07, 0x7E,0x7F,0x7F,0x7F,0x07,
    0x7E,0x7F,0x7F,0x7F,0x07, 0x7E,0x7F,0x7F,0x7F,0x07, 0x7E,0x7F,0x7F,0x7F,0x07,
    0x7C,0x7F,0x7F,0x7F,0x03, 0x7C,0x7F,0x7F,0x7F,0x03, 0x7C,0x7F,0x7F,0x7F,0x03,
    0x78,0x7F,0x7F,0x7F,0x01, 0x70,0x7F,0x7F,0x7F,0x00, 0x60,0x7F,0x7F,0x3F,0x00,
    0x40,0x7F,0x7F,0x1F,0x00, 0x00,0x7F,0x7F,0x0F,0x00, 0x00,0x7E,0x7F,0x07,0x00,
    0x00,0x70,0x7F,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,
};
static const unsigned char kDisc16[48] = {
    0x00,0x00,0x00, 0x70,0x1F,0x00, 0x78,0x3F,0x00, 0x7C,0x7F,0x00,
    0x7E,0x7F,0x01, 0x7E,0x7F,0x01, 0x7E,0x7F,0x01, 0x7E,0x7F,0x01,
    0x7E,0x7F,0x01, 0x7E,0x7F,0x01, 0x7E,0x7F,0x01, 0x7E,0x7F,0x01,
    0x7C,0x7F,0x00, 0x78,0x3F,0x00, 0x70,0x1F,0x00, 0x00,0x00,0x00,
};
static const unsigned char *const kDiscRaw[3] = { kDisc48, kDisc32, kDisc16 };

/* Pre-tinted disc tables, sized to each tier (48->336, 32->160, 16->48 bytes)
 * to keep BSS small. Even so, the six variants total ~1 KB and — together with
 * the full gen2c runtime the DevBench links (~20 KB, no dead-strip) — overflow
 * the $6000-$BEFF budget. They (and the per-page erase memory below) are parked
 * in LOWBSS: idle user RAM at $0C00-$1FFF beneath the HIRES framebuffers (see
 * apple1_gen2_c.cfg). Safe there because build_sprites() writes every byte
 * before the first blit reads it (LOWBSS is NOT crt0-zeroed). A const pointer
 * table indexes them by [tier][byte-column phase]. */
#pragma bss-name (push, "LOWBSS")
static unsigned char tnt48[2][336];
static unsigned char tnt32[2][160];
static unsigned char tnt16[2][48];
#pragma bss-name (pop)
static unsigned char *const discTint[3][2] = {
    { tnt48[0], tnt48[1] },
    { tnt32[0], tnt32[1] },
    { tnt16[0], tnt16[1] },
};

/* Cannon: 16x12, MSB-first 1bpp (gen2_hgr_blit format) so it can sit at any
 * pixel x and glide smoothly. White (the blit never touches the palette bit). */
static const unsigned char kCannon[PLAYER_H * 2] = {
    0x01,0x80,  0x01,0x80,  0x03,0xC0,  0x03,0xC0,
    0x1F,0xF8,  0x3F,0xFC,  0x7F,0xFE,  0xFF,0xFF,
    0xFF,0xFF,  0xFF,0xFF,  0x7F,0xFE,  0x7F,0xFE,
};

/* Bullet: a 2x4 white bolt, MSB-first 1bpp (bits 7,6 -> 0xC0 per row). */
static const unsigned char kBullet[BULLET_H] = {
    0xC0, 0xC0, 0xC0, 0xC0,
};

/* ---- bubble state (parallel arrays, fixed slots) ------------------------ */
static unsigned char alive[MAXB];
static unsigned char tier[MAXB];
static unsigned char phase[MAXB];
static int   bx[MAXB];          /* top-left x (multiple of 7)                */
static int   by[MAXB];          /* top-left y (pixels)                       */
static int   vx[MAXB];          /* +/- HSTEP                                 */
static int   vyq[MAXB];         /* vertical velocity, 1/16 px                */
static int   yq[MAXB];          /* vertical position, 1/16 px (by = yq>>4)   */

/* Per-page erase memory (the Bounces trick): each page remembers exactly what
 * it last drew in each slot, so it can XOR-erase it next time it is the draw
 * page. Index [slot][page-1]. Parked in LOWBSS (see the tint tables above):
 * prime_pages() -> rebuild_page() writes every one of these for BOTH pages
 * before the first render() reads them, so the un-zeroed segment is safe. */
#pragma bss-name (push, "LOWBSS")
static unsigned char oalive[MAXB][2];
static unsigned char otier[MAXB][2];
static unsigned char ophase[MAXB][2];
static int   obx[MAXB][2];
static int   oby[MAXB][2];
static int   opx[2];            /* cannon old x per page                     */
static unsigned char obul_a[MAXBUL][2];      /* per-page erase memory        */
static int   obul_x[MAXBUL][2], obul_y[MAXBUL][2];
#pragma bss-name (pop)

/* ---- bullets (discrete fast pellets) ------------------------------------ */
static unsigned char bul_a[MAXBUL];          /* active flag                  */
static int   bul_x[MAXBUL], bul_y[MAXBUL];   /* top-left, pixels             */

/* ---- globals ------------------------------------------------------------ */
static int           px;        /* cannon x (pixels)                         */
static signed char   player_dir;/* -1 / 0 / +1, latched (Galaga glide)       */
static unsigned char key_left;  /* 'A' (US/QWERTY) or 'Q' (FR/AZERTY)        */
static unsigned char plat_redraw;/* frames left to repaint pages after a break*/
static unsigned char cooldown;
static unsigned      score;
static unsigned char lives, level, huddirty;

/* ===================== sprite construction ============================== */

/* Carrier table for an artifact colour (verbatim from Bounces). */
static void carrier_for(unsigned char color, unsigned char *even,
                        unsigned char *odd, unsigned char *hi)
{
    switch (color) {
        case GEN2_GREEN:  *even = 0x2Au; *odd = 0x55u; *hi = 0x00u; break;
        case GEN2_ORANGE: *even = 0x2Au; *odd = 0x55u; *hi = 0x80u; break;
        case GEN2_BLUE:   *even = 0x55u; *odd = 0x2Au; *hi = 0x80u; break;
        default:          *even = 0x55u; *odd = 0x2Au; *hi = 0x00u; break; /* violet */
    }
}

/* Tint a raw bitmap into an artifact-colour bitmap (verbatim from Bounces). */
static void tint_sprite(const unsigned char *src, unsigned char *dst,
                        unsigned char rows, unsigned char wbytes,
                        unsigned char ph, unsigned char color)
{
    unsigned char even, odd, hi, y, x, b, carrier;
    carrier_for(color, &even, &odd, &hi);
    for (y = 0u; y < rows; ++y) {
        for (x = 0u; x < wbytes; ++x) {
            b = *src++;
            carrier = ((x + ph) & 1u) ? odd : even;
            *dst++ = b ? (unsigned char)((b & carrier) | hi) : 0u;
        }
    }
}

static void build_sprites(void)
{
    unsigned char t;
    for (t = 0u; t < 3u; ++t) {     /* tint each disc in both column phases */
        tint_sprite(kDiscRaw[t], discTint[t][0], tsz[t], twb[t], 0u, tcol[t]);
        tint_sprite(kDiscRaw[t], discTint[t][1], tsz[t], twb[t], 1u, tcol[t]);
    }
}

/* ===================== drawing helpers ================================== */

static void draw_bubble(unsigned char t, int x, int y, unsigned char ph)
{
    gen2_hgr_blit7((unsigned)x, (unsigned char)y, twb[t], tsz[t],
                   discTint[t][ph], GEN2_XOR);
}

static void draw_cannon(int x)
{
    gen2_hgr_blit((unsigned)x, PLAYER_Y, PLAYER_PX, PLAYER_H, kCannon, GEN2_XOR);
}

static void draw_bullet(int x, int y)
{
    gen2_hgr_blit((unsigned)x, (unsigned char)y, BULLET_W, BULLET_H, kBullet, GEN2_XOR);
}

static void draw_static(void)
{
    unsigned char i, s;
    int x0, w;
    gen2_hgr_rect(FL - 5u, FT, FR + 4u, FB);
    for (i = 0u; i < plat_count; ++i)                 /* only the alive segments */
        for (s = 0u; s < plat_nseg[i]; ++s)
            if (seg_hp[i][s]) {
                x0 = plat_x0[i] + (int)s * SEG_W;
                w  = SEG_W;
                if (x0 + w > plat_x1[i]) w = plat_x1[i] - x0;
                gen2_hgr_fill_pixrect((unsigned)x0, (unsigned char)plat_y[i],
                                      (unsigned char)w, PLAT_H);
            }
    gen2_hgr_puts8(SCOREX - 44u, HUDY, "SCORE");
    gen2_hgr_puts8(LIVESX - 48u, HUDY, "LIVES");
}

/* 1 if any alive segment of platform p overlaps [xa, xb) — i.e. solid there. */
static unsigned char seg_solid(unsigned char p, int xa, int xb)
{
    unsigned char s;
    int sx0;
    for (s = 0u; s < plat_nseg[p]; ++s) {
        if (!seg_hp[p][s]) continue;
        sx0 = plat_x0[p] + (int)s * SEG_W;
        if (xa < sx0 + SEG_W && xb > sx0) return 1u;
    }
    return 0u;
}

static void draw_hud(void)
{
    gen2_hgr_putu_field(SCOREX, HUDY, score, 5u);
    gen2_hgr_putu_field(LIVESX, HUDY, lives, 1u);
}

/* ===================== level / bubble management ======================== */

static unsigned char free_slot(void)
{
    unsigned char i;
    for (i = 0u; i < (unsigned char)MAXB; ++i)
        if (!alive[i]) return i;
    return 0xFFu;
}

static void spawn(unsigned char t, int x, int y, int vxx, int vyy)
{
    unsigned char s = free_slot();
    if (s == 0xFFu) return;
    alive[s] = 1u;
    tier[s]  = t;
    bx[s]    = x;
    by[s]    = y;
    yq[s]    = y << 4;
    vx[s]    = vxx;
    vyq[s]   = vyy;
    phase[s] = (unsigned char)(((unsigned)x / 7u) & 1u);
}

static int snap7(int x)            /* clamp to playfield, align to 7px column */
{
    if (x < (int)FL) x = (int)FL;
    return (x / 7) * 7;
}

static void split(unsigned char i)
{
    unsigned char t = tier[i];
    int cx = bx[i] + (int)tsz[t] / 2;
    int cy = by[i] + (int)tsz[t] / 2;
    alive[i] = 0u;
    score += tscore[t];
    huddirty = 2u;
    if (t < 2u) {
        unsigned char nt = (unsigned char)(t + 1u);
        int nx = snap7(cx - (int)tsz[nt] / 2);
        int ny = cy - (int)tsz[nt] / 2;
        int up = -tbounce[nt] / 2;          /* upward pop on birth           */
        int rmax = (((int)FR - (int)tsz[nt]) / 7) * 7;
        if (nx > rmax) nx = rmax;
        if (ny < (int)FT) ny = (int)FT;
        spawn(nt, nx, ny, -(int)HSTEP, up);
        spawn(nt, nx, ny,  (int)HSTEP, up);
    }
}

static void init_level(void)
{
    unsigned char i, s, idx;
    for (i = 0u; i < (unsigned char)MAXB; ++i) alive[i] = 0u;
    for (i = 0u; i < (unsigned char)MAXBUL; ++i) bul_a[i] = 0u;

    /* pick this level's platform layout (placement + progressive thinning) */
    idx = (unsigned char)(level - 1u);
    if (idx >= (unsigned char)NLAYOUT) idx = (unsigned char)(NLAYOUT - 1);
    plat_count = lay_n[idx];
    for (i = 0u; i < plat_count; ++i) {
        plat_x0[i] = lay_x0[idx][i];
        plat_x1[i] = lay_x1[idx][i];
        plat_y[i]  = lay_y[idx][i];
        plat_nseg[i] = (unsigned char)((plat_x1[i] - plat_x0[i] + SEG_W - 1) / SEG_W);
        if (plat_nseg[i] > (unsigned char)MAXSEG) plat_nseg[i] = (unsigned char)MAXSEG;
        for (s = 0u; s < plat_nseg[i]; ++s) seg_hp[i][s] = SEG_HP;
    }
    plat_dirty = 0u;

    /* one big balloon (the platforms carry the difficulty); a SECOND joins at
     * level 10 and up */
    if (level >= 10u) {
        spawn(0u, snap7((int)FL +     (int)(FR - FL) / 3 - (int)tsz[0] / 2), (int)FT + 4,  (int)HSTEP, 0);
        spawn(0u, snap7((int)FL + 2 * (int)(FR - FL) / 3 - (int)tsz[0] / 2), (int)FT + 4, -(int)HSTEP, 0);
    } else {
        spawn(0u, snap7(((int)FL + (int)FR) / 2 - (int)tsz[0] / 2), (int)FT + 4, (int)HSTEP, 0);
    }

    px = ((int)FL + (int)FR) / 2 - (int)PLAYER_PX / 2;
    player_dir = 0;
    cooldown = 0u;
    plat_redraw = 0u;
}

/* ===================== rendering one frame ============================== */

/* Full clear+redraw of ONE (hidden) page: decor + every current sprite, with
 * the per-page erase memory set to the drawn state. Used at level start and to
 * repaint after a platform segment breaks — only ever the off-screen page, so
 * the visible frame never flashes. */
static void rebuild_page(unsigned char page)
{
    unsigned char pidx = (unsigned char)(page - 1u);
    unsigned char i;
    gen2_set_draw_page(page);
    gen2_hgr_clear(0u);
    draw_static();
    for (i = 0u; i < (unsigned char)MAXB; ++i) {
        if (alive[i]) draw_bubble(tier[i], bx[i], by[i], phase[i]);
        oalive[i][pidx] = alive[i];
        otier[i][pidx]  = tier[i];
        obx[i][pidx]    = bx[i];
        oby[i][pidx]    = by[i];
        ophase[i][pidx] = phase[i];
    }
    for (i = 0u; i < (unsigned char)MAXBUL; ++i) {
        if (bul_a[i]) draw_bullet(bul_x[i], bul_y[i]);
        obul_a[i][pidx] = bul_a[i];
        obul_x[i][pidx] = bul_x[i];
        obul_y[i][pidx] = bul_y[i];
    }
    draw_cannon(px);
    opx[pidx] = px;
    draw_hud();
}

static void render(unsigned char page)
{
    unsigned char pidx = (unsigned char)(page - 1u);
    unsigned char i;

    if (plat_redraw) {                 /* repaint this hidden page wholesale */
        rebuild_page(page);
        --plat_redraw;
        gen2_wait_vbl();               /* flip in V-blank — see below         */
        gen2_show_page();
        return;
    }

    gen2_set_draw_page(page);

    /* bubbles: XOR-erase what THIS page last drew, XOR-draw current state */
    for (i = 0u; i < (unsigned char)MAXB; ++i) {
        if (oalive[i][pidx])
            draw_bubble(otier[i][pidx], obx[i][pidx], oby[i][pidx], ophase[i][pidx]);
        if (alive[i])
            draw_bubble(tier[i], bx[i], by[i], phase[i]);
        oalive[i][pidx] = alive[i];
        otier[i][pidx]  = tier[i];
        obx[i][pidx]    = bx[i];
        oby[i][pidx]    = by[i];
        ophase[i][pidx] = phase[i];
    }

    /* bullets: same per-page XOR erase/draw */
    for (i = 0u; i < (unsigned char)MAXBUL; ++i) {
        if (obul_a[i][pidx]) draw_bullet(obul_x[i][pidx], obul_y[i][pidx]);
        if (bul_a[i])        draw_bullet(bul_x[i], bul_y[i]);
        obul_a[i][pidx] = bul_a[i];
        obul_x[i][pidx] = bul_x[i];
        obul_y[i][pidx] = bul_y[i];
    }

    /* cannon */
    draw_cannon(opx[pidx]);            /* erase */
    draw_cannon(px);                   /* draw  */
    opx[pidx] = px;

    if (huddirty) { draw_hud(); --huddirty; }

    /* Flip the freshly-drawn hidden page in DURING V-blank so the beam never
     * catches the $C254/$C255 page switch mid-scan. Without the wait the flip
     * lands at an arbitrary raster line — the top of the screen shows the new
     * page, the bottom the old one (a tear line), and a bubble straddling that
     * line appears at both its old and new spots (a "ghost"). One C frame spans
     * more than one 60 Hz refresh, so this only aligns the single per-frame flip
     * to the next blanking interval; it never paces the loop down. */
    gen2_wait_vbl();
    gen2_show_page();
}

/* Build BOTH pages up front (level start). Each page is drawn while hidden by
 * the other being on display; the first render() flips to a finished frame. */
static void prime_pages(void)
{
    rebuild_page(1u);
    rebuild_page(2u);
    huddirty = 2u;
}

/* ===================== physics + input ================================== */

/* returns 1 if a bubble touched the cannon */
static unsigned char step_world(void)
{
    unsigned char i, p, hit = 0u;
    int t, floorY, rmax, prevb, pt, sz;

    for (i = 0u; i < (unsigned char)MAXB; ++i) {
        if (!alive[i]) continue;
        t = tier[i];
        sz = (int)tsz[t];
        rmax = (((int)FR - sz) / 7) * 7;

        /* horizontal: byte-aligned bounce (phase flips with each 7px move) */
        bx[i] += vx[i];
        phase[i] ^= 1u;
        if (bx[i] <= (int)FL)      { bx[i] = (int)FL; phase[i] = (unsigned char)(((unsigned)FL/7u)&1u); vx[i] = -vx[i]; }
        else if (bx[i] >= rmax)    { bx[i] = rmax;    phase[i] = (unsigned char)(((unsigned)rmax/7u)&1u); vx[i] = -vx[i]; }

        /* vertical: gravity arc, constant-height bounce off floor + platforms */
        prevb = by[i] + sz;             /* bubble bottom BEFORE this step */
        vyq[i] += GRAV;
        yq[i]  += vyq[i];
        by[i]   = yq[i] >> 4;
        if (by[i] <= (int)FT) { by[i] = (int)FT; yq[i] = (int)FT << 4; if (vyq[i] < 0) vyq[i] = -vyq[i]; }
        floorY = (int)FB - sz;
        if (by[i] >= floorY)  { by[i] = floorY; yq[i] = floorY << 4; vyq[i] = -tbounce[t]; }

        /* land on a platform TOP only while falling, crossing it downward, and
         * only where the platform is still solid (broken gaps drop through) */
        if (vyq[i] > 0) {
            for (p = 0u; p < plat_count; ++p) {
                pt = plat_y[p];
                if (prevb <= pt && by[i] + sz >= pt &&
                    bx[i] < plat_x1[p] && bx[i] + sz > plat_x0[p] &&
                    seg_solid(p, bx[i], bx[i] + sz)) {
                    by[i] = pt - sz; yq[i] = by[i] << 4; vyq[i] = -tbounce[t];
                    break;
                }
            }
        }

        /* cannon collision: bubble CIRCLE (centre, radius sz/2) vs the cannon
         * rectangle — nearest-point distance, so the radius genuinely matters */
        {
            int r = sz / 2;
            int cx = bx[i] + r, cy = by[i] + r;
            int nx = cx, ny = cy;
            if (nx < px)                        nx = px;
            else if (nx > px + (int)PLAYER_PX)  nx = px + (int)PLAYER_PX;
            if (ny < (int)PLAYER_Y)             ny = (int)PLAYER_Y;
            else if (ny > (int)FB)              ny = (int)FB;
            cx -= nx; cy -= ny;
            if (cx < r && cx > -r && cy < r && cy > -r &&  /* box reject first */
                cx * cx + cy * cy < r * r) hit = 1u;
        }
    }
    return hit;
}

static void fire(void)
{
    unsigned char i;
    for (i = 0u; i < (unsigned char)MAXBUL; ++i) {
        if (!bul_a[i]) {
            bul_a[i] = 1u;
            bul_x[i] = px + (int)PLAYER_PX / 2 - (int)BULLET_W / 2;
            bul_y[i] = (int)PLAYER_Y - (int)BULLET_H;
            cooldown = (unsigned char)FIRE_COOLDOWN;
            return;
        }
    }
}

/* CIRCLE collision pellet(ucx,ucy) vs bubble j, with a cheap bounding-box reject
 * first (compares only) so the 16-bit squared-distance multiply runs only for
 * genuinely-near pairs. */
static unsigned char pellet_hits(unsigned char j, int ucx, int ucy)
{
    int r  = (int)tsz[tier[j]] / 2;
    int rr = r + (int)BULLET_W / 2;
    int dx = bx[j] + r - ucx;
    int dy;
    if (dx > rr || dx < -rr) return 0u;
    dy = by[j] + r - ucy;
    if (dy > rr || dy < -rr) return 0u;
    return (unsigned char)(dx * dx + dy * dy <= rr * rr);
}

/* Move bullets up. The pellet is fast (BULLET_SPEED px/frame), so it advances in
 * sub-steps (<= BULLET_STEP) testing the thin platforms and the SMALL bubbles
 * each one — those are narrower than a frame's jump and would otherwise tunnel.
 * MEDIUM and BIG bubbles are wider than the jump, so they cannot be skipped:
 * they are tested just ONCE per frame at the final position (lever 1 — cuts the
 * per-bubble work for the common big targets by ~4x). A hit splits the bubble /
 * erodes the platform and consumes the pellet. */
static void update_bullets(void)
{
    unsigned char i, j, p;
    int left, st, ucx;
    for (i = 0u; i < (unsigned char)MAXBUL; ++i) {
        if (!bul_a[i]) continue;
        ucx = bul_x[i] + (int)BULLET_W / 2;        /* pellet centre x (fixed)  */
        for (left = BULLET_SPEED; left > 0 && bul_a[i]; left -= st) {
            st = (left > BULLET_STEP) ? BULLET_STEP : left;
            bul_y[i] -= st;
            if (bul_y[i] <= (int)FT) { bul_a[i] = 0u; break; }

            /* a SOLID platform segment stops the pellet and takes a hit; a
             * broken gap lets it pass (so repeated fire wears a hole through) */
            for (p = 0u; p < plat_count; ++p) {
                if (bul_y[i] < plat_y[p] + PLAT_H &&
                    bul_y[i] + (int)BULLET_H + BULLET_STEP > plat_y[p] &&
                    bul_x[i] + (int)BULLET_W > plat_x0[p] && bul_x[i] < plat_x1[p]) {
                    int s = (bul_x[i] + (int)BULLET_W / 2 - plat_x0[p]) / SEG_W;
                    if (s >= 0 && s < (int)plat_nseg[p] && seg_hp[p][s]) {
                        if (--seg_hp[p][s] == 0u) plat_dirty = 1u;
                        bul_a[i] = 0u;
                        break;
                    }
                }
            }
            if (!bul_a[i]) break;

            /* small bubbles only (tier 2): they can tunnel, so test every step */
            for (j = 0u; j < (unsigned char)MAXB; ++j) {
                if (alive[j] && tier[j] == 2u &&
                    pellet_hits(j, ucx, bul_y[i] + (int)BULLET_H / 2)) {
                    bul_a[i] = 0u; split(j); break;
                }
            }
        }

        /* medium + big bubbles: one test per frame at the final position */
        if (bul_a[i]) {
            int ucy = bul_y[i] + (int)BULLET_H / 2;
            for (j = 0u; j < (unsigned char)MAXB; ++j) {
                if (alive[j] && tier[j] < 2u && pellet_hits(j, ucx, ucy)) {
                    bul_a[i] = 0u; split(j); break;
                }
            }
        }
    }
}

/* Apply the latched glide direction, clamped to the playfield. */
static void move_player(void)
{
    px += (int)player_dir * PLAYER_STEP;
    if (px < (int)FL)                       px = (int)FL;
    else if (px > (int)FR - (int)PLAYER_PX) px = (int)FR - (int)PLAYER_PX;
}

static unsigned char any_alive(void)
{
    unsigned char i;
    for (i = 0u; i < (unsigned char)MAXB; ++i) if (alive[i]) return 1u;
    return 0u;
}

static void input(void)
{
    unsigned char k = apple1_readkey();
    if (!k) return;
    if      (k == key_left) player_dir = -1;          /* latch: keep gliding   */
    else if (k == 'D')      player_dir =  1;
    else if (k == 'S')    { player_dir =  0; fire(); }/* Galaga: stop + aimed  */
    else if (k == ' ' && cooldown == 0u) fire();      /* rapid fire (cooldown) */
}

/* ===================== overlays ========================================= */

static void center_msg(const char *a, const char *b)
{
    gen2_set_draw_page(1u);
    gen2_hgr_clear(0u);
    gen2_hgr_puts(96u, 70u, a);
    gen2_hgr_puts8(72u, 110u, b);
    gen2_show_page();
}

/* ===================== main ============================================= */

void main(void)
{
    unsigned char page;

    gen2_hgr_init();
    build_sprites();

    for (;;) {
        unsigned char k;
        /* ---- title + keyboard-layout select ---- */
        gen2_set_draw_page(1u);
        gen2_hgr_clear(0u);
        gen2_hgr_puts(96u, 38u, "PANG");
        gen2_hgr_puts8(40u, 84u, "GLIDE LEFT-RIGHT  D=RIGHT");
        gen2_hgr_puts8(40u, 96u, "S=STOP+FIRE   SPACE=FIRE");
        gen2_hgr_puts8(28u, 124u, "PRESS  U: US QWERTY (A=LEFT)");
        gen2_hgr_puts8(28u, 136u, "       F: FR AZERTY (Q=LEFT)");
        gen2_show_page();
        for (;;) {
            k = apple1_getkey();
            if (k == 'U') { key_left = 'A'; break; }
            if (k == 'F') { key_left = 'Q'; break; }
        }

        score = 0u; lives = 3u; level = 1u;

    new_level:
        init_level();
        prime_pages();
        page = 1u;

        for (;;) {
            gen2_graphics(); gen2_hires(); gen2_full();
            render(page);
            page = (page == 1u) ? 2u : 1u;

            input();
            move_player();
            update_bullets();
            if (plat_dirty) { plat_dirty = 0u; plat_redraw = 2u; }
            if (cooldown) --cooldown;

            if (step_world()) {                 /* hit -> lose a life */
                if (--lives == 0u) break;
                huddirty = 2u;
                init_level();
                prime_pages();
                page = 1u;
                continue;
            }

            if (!any_alive()) {                 /* board cleared -> next */
                ++level;
                goto new_level;
            }
        }

        /* ---- game over ---- */
        center_msg("GAME", "OVER  -  PRESS SPACE");
        while (apple1_getkey() != ' ') { }
    }
}
