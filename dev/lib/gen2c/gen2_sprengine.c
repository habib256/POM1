/* gen2_sprengine.c -- masked save-under sprite ENGINE for GEN2 HGR (cc65).
 *
 * The SPRENGINE family (needs SPRMASK + CORE): up to GEN2_SPR_MAX opaque
 * masked sprites over an arbitrary background, restored byte-exact every
 * frame. The per-sprite state lives in cc65-friendly PARALLEL STATIC ARRAYS
 * (no structs of pointers, no dynamic allocation) and every blit goes through
 * the gen2_ms_* zero-page parameter block into the gen2_sprmask.s kernels --
 * the same ZP-block discipline as the rest of the runtime.
 *
 * === HST0, the flip, and the V-blank budget (why this engine is shaped so) ==
 *
 * HST0 poll idiom. The GEN2 soft switches $C250-$C257 are READ-only; a read
 * toggles the addressed switch AND returns HST0 (the blanking flag) in bit 7:
 * 1 during H-blank (hcnt 0-24) OR V-blank (lines >= 192). The low 7 bits are
 * floating bus -- never use them. Two hardware traps shape every poll:
 *   (a) the 3-cycle colour-burst notch at hcnt 13-15 can read HST0 = 0 even
 *       in the middle of a blank, so a single sample lies: always OR TWO
 *       samples >= 4 cycles apart (gen2_wait_vbl's gen2_blank() does
 *       `GEN2_SS[7] | GEN2_SS[7]` -- two indirect reads are ~6 cycles apart);
 *   (b) the read IS the toggle, so you must poll a switch you are already IN.
 *       gen2_wait_vbl polls $C257 (re-asserts HIRES -- harmless in HIRES);
 *       this engine's flip is a $C254/$C255 READ (gen2_show_page), which is
 *       both the page selection AND a valid HST0 sample point for the same
 *       reason: reading the page switch you are switching to costs nothing
 *       extra and cannot disturb any other latch.
 *
 * V-blank budget. A frame is 17030 cycles at ~60 Hz; the V-blank window is
 * ~4200-4550 cycles (24-26 blanked lines x 65 cycles, minus what the poll
 * itself burns before returning -- gen2_wait_vbl returns ~3 lines in). Cost
 * of one sprite of stride S x H rows through this engine, per frame:
 *
 *     draw    (gen2_msu_run)        35 cyc/byte  (save-under fused in)
 *     restore (gen2_ms_restore_run) 19 cyc/byte
 *     + per-row overhead  ~200 cyc  (2 passes x rowbase + 3-pointer advance)
 *     + per-call overhead ~1200 cyc (2 x setup: deref, clip, h*stride
 *       loop-add, phase offset)
 *
 *     T(S,H) ~= 54*S*H + 204*H + 1200
 *
 * So a 3x8 sprite costs ~4.1k cycles (JUST fits one V-blank window), a 3x16
 * ~7.1k (does NOT -- ~0.6 of one fits 4200 cycles), a 16x16 creature (stride
 * 4, 64 bytes) ~7.9k. Hence the TWO modes:
 *
 *   double-buffered (the default to reach 8 sprites): all restore+draw work
 *     happens on the HIDDEN page while the beam scans the other -- the budget
 *     is the whole frame (17030 cycles), and the only thing inside V-blank is
 *     the flip itself. Tear-free BY CONSTRUCTION, whatever the sprite load.
 *     Each page holds a 1-frame-old background, so under-buffers and
 *     "previous position" are kept PER PAGE.
 *
 *   single-buffered: gen2_wait_vbl() FIRST, then restore-all + draw-all
 *     inside the V-blank window. Cheaper (no second background copy, no page
 *     discipline) but the budget math above applies: past ~1 small (3x8)
 *     sprite the beam catches the update mid-frame and you see a tear/flash
 *     near the top scanlines. The overflow only ever TEARS -- the save-under/
 *     restore pairing stays coherent, so the background is never corrupted.
 * ========================================================================== */

#include "gen2.h"
#include "gen2_internal.h"

/* --- per-sprite state: parallel static arrays ------------------------------ */
static const gen2_mspr_t *spr_shape[GEN2_SPR_MAX]; /* 0 = undefined            */
static unsigned      spr_x[GEN2_SPR_MAX];          /* target position (move)   */
static unsigned char spr_y[GEN2_SPR_MAX];
static unsigned char spr_active[GEN2_SPR_MAX];     /* 0 = hidden               */

/* Per PAGE (index 0 = page 1, 1 = page 2): where the sprite was drawn on THAT
 * page and whether it is currently drawn there. Two copies because in double-
 * buffer mode each page holds a 1-frame-old background with its own set of
 * sprites stamped in. Single-buffer mode only ever uses index 0's discipline
 * on whatever the draw page is (see gen2_spr_init). */
static unsigned      spr_px[2][GEN2_SPR_MAX];
static unsigned char spr_py[2][GEN2_SPR_MAX];
static unsigned char spr_drawn[2][GEN2_SPR_MAX];

/* The engine-owned save-under pool: GEN2_SPR_MAX slots of GEN2_SPR_UNDER_BYTES
 * per page = 2 * 8 * 96 = 1536 bytes of BSS. A slot holds the stride*h byte
 * rectangle a draw covered (cap documented in gen2.h; gen2_spr_define rejects
 * bigger shapes). */
static unsigned char spr_under[2][GEN2_SPR_MAX * GEN2_SPR_UNDER_BYTES];

static unsigned char spr_dbuf;       /* 1 = double-buffered                    */
static unsigned char spr_drawpage;   /* page the NEXT update draws on (1 or 2) */

/* Init the engine. double_buffered = 1: the engine displays page 1, draws on
 * page 2, and flips every gen2_spr_update -- the caller must have drawn the
 * background on BOTH pages first. double_buffered = 0: everything happens on
 * page 1 (displayed immediately). Re-init FORGETS all under-buffers, so call
 * it only over clean backgrounds (e.g. right after redrawing them). */
void gen2_spr_init(unsigned char double_buffered)
{
    unsigned char i;
    spr_dbuf = double_buffered;
    for (i = 0u; i < GEN2_SPR_MAX; ++i) {
        spr_shape[i]    = 0;
        spr_active[i]   = 0u;
        spr_drawn[0][i] = 0u;
        spr_drawn[1][i] = 0u;
    }
    /* Display page 1 in both modes (gen2_show_page shows the CURRENT draw
     * page, so select 1 first). Double buffering then moves the pen to the
     * hidden page 2. */
    gen2_set_draw_page(1u);
    gen2_show_page();
    spr_drawpage = double_buffered ? 2u : 1u;
    if (double_buffered) gen2_set_draw_page(2u);
}

/* Bind a shape to a sprite slot. Rejected (slot deactivated) when the covered
 * rectangle stride*h exceeds the GEN2_SPR_UNDER_BYTES save-under cap.
 * Redefining a shape while the sprite is still drawn on a page is undefined
 * (the pending restore would use the new geometry) -- hide + update first. */
void gen2_spr_define(unsigned char id, const gen2_mspr_t *shape)
{
    if (id >= GEN2_SPR_MAX) return;
    if (shape != 0 &&
        (unsigned)shape->stride * shape->h > GEN2_SPR_UNDER_BYTES) {
        spr_shape[id]  = 0;
        spr_active[id] = 0u;
        return;
    }
    spr_shape[id]  = shape;
    spr_active[id] = (shape != 0) ? 1u : 0u;
}

/* Record the target position; nothing is drawn until gen2_spr_update. */
void gen2_spr_move(unsigned char id, unsigned x, unsigned char y)
{
    if (id >= GEN2_SPR_MAX) return;
    spr_x[id]      = x;
    spr_y[id]      = y;
    spr_active[id] = (spr_shape[id] != 0) ? 1u : 0u;
}

/* Hide a sprite: it stops being drawn and its under-rect is restored by the
 * next update (the next TWO updates in double-buffer mode -- one per page). */
void gen2_spr_hide(unsigned char id)
{
    if (id >= GEN2_SPR_MAX) return;
    spr_active[id] = 0u;
}

/* One frame: restore every drawn sprite (REVERSE draw order, so overlapping
 * under-rects unwind exactly), then save-under + masked-draw every active one
 * at its CURRENT position. Double-buffer: all of it on the hidden page, then
 * flip in V-blank and swap the pen. Single-buffer: V-blank first, then the
 * whole restore+draw races the beam (see the budget note above). */
void gen2_spr_update(void)
{
    unsigned char pg, id;
    unsigned char *ub;

    if (!spr_dbuf) gen2_wait_vbl();          /* single: work INSIDE V-blank   */

    pg = (unsigned char)(spr_drawpage - 1u); /* page index 0/1                */
    gen2_set_draw_page(spr_drawpage);        /* row tables -> this page       */

    /* restore pass, reverse order; ub walks the pool backwards slot by slot
     * (no per-sprite 16-bit multiply) */
    ub = &spr_under[pg][GEN2_SPR_MAX * GEN2_SPR_UNDER_BYTES];
    id = GEN2_SPR_MAX;
    while (id-- > 0u) {
        ub -= GEN2_SPR_UNDER_BYTES;
        if (!spr_drawn[pg][id]) continue;
        gen2_ms_x     = spr_px[pg][id];
        gen2_ms_y     = spr_py[pg][id];
        gen2_ms_spr   = spr_shape[id];
        gen2_ms_under = ub;
        gen2_ms_restore_run();
        spr_drawn[pg][id] = 0u;
    }

    /* draw pass, forward order (later ids paint over earlier ones) */
    ub = &spr_under[pg][0];
    for (id = 0u; id < GEN2_SPR_MAX; ++id, ub += GEN2_SPR_UNDER_BYTES) {
        if (!spr_active[id] || spr_shape[id] == 0) continue;
        if (spr_x[id] > 279u || spr_y[id] > 191u) continue;  /* off-screen    */
        gen2_ms_x     = spr_x[id];
        gen2_ms_y     = spr_y[id];
        gen2_ms_spr   = spr_shape[id];
        gen2_ms_under = ub;
        gen2_msu_run();                      /* save-under + draw, one pass   */
        spr_px[pg][id]    = spr_x[id];
        spr_py[pg][id]    = spr_y[id];
        spr_drawn[pg][id] = 1u;
    }

    if (spr_dbuf) {
        gen2_wait_vbl();                     /* flip only ever in V-blank     */
        gen2_show_page();                    /* $C254/$C255 READ = the flip   */
        spr_drawpage = (spr_drawpage == 1u) ? 2u : 1u;
    }
}
