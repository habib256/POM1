/* gen2_init.c — initialisation, look-up tables, page management.
 *
 * Always linked (every gen2c consumer pulls at least the table builder via
 * gen2_build_tables). Carries the soft-switch sink, the HIRES + LORES
 * cross-module globals, the wait-VBL helper and the draw-page setter. See
 * gen2.h for the public API doc; see gen2_internal.h for what the other
 * modules import.
 *
 * NO INITIALIZED GLOBALS (the BSS-zero-default rule). The cc65 -t none crt0
 * used by every consumer has NO copydata: a DATA-segment initializer is only
 * "applied" because the loader happened to place the bytes — it is one-shot
 * under load==run configs (a warm re-run inherits the previous run's values)
 * and pure garbage under ROM configs (DATA in ROM is never copied to RAM).
 * Even `= 0` lands a variable in DATA. So every global here is uninitialized
 * (BSS, zeroed by zerobss at EVERY entry) and encodes its default as zero:
 * flags default clear, gen2_draw_page2 zero = page 1, and the page bases
 * (for which 0 is never a legal value) are lazily defaulted at their read
 * sites (gen2_hgr_row / gen2_build_tables here, gen2_lores.c for LORES, and
 * _gen2_hgr_clear in gen2_blit.s — the one asm reader of gen2_hgr_base). */

#include "gen2.h"
#include "gen2_internal.h"

/* Sink for the individual soft-switch macros. A soft-switch READ is the toggle;
 * its value is meaningless. cc65 -Oirs drops a volatile read cast to void, so
 * the macros store the read into this sink — a store cc65 cannot prove dead, so
 * the (volatile) load that feeds it is always emitted. The whole-mode init
 * routines gen2_hgr_init / gen2_lores_init live in asm (gen2_blit.s) for the
 * same reason; see the note there and gen2.h. */
volatile unsigned char gen2_ss_sink;

/* HIRES draw-page high-byte base: $20 = page 1 ($2000), $40 = page 2 ($4000).
 * GLOBAL (not static) on purpose: the asm _gen2_hgr_clear imports _gen2_hgr_base
 * so it erases whichever page is currently being drawn. Set by
 * gen2_set_draw_page. BSS (no initializer — see the header): 0 means "unset",
 * lazily defaulted to $20 (page 1) at every read site that can run before
 * gen2_set_draw_page — here, gen2_build_tables, and _gen2_hgr_clear in
 * gen2_blit.s. 0 is never a legal base, so the encoding is unambiguous. */
unsigned char gen2_hgr_base;

/* Apple II interleaved HIRES scanline base, in the CURRENT draw page:
   (gen2_hgr_base<<8) + (y&7)*$400 + ((y>>3)&7)*$80 + (y>>6)*40
   gen2_hgr_base=$20 gives the classic $2000 page-1 layout; $40 is page 2. */
unsigned char *gen2_hgr_row(unsigned char y)
{
    if (!gen2_hgr_base) gen2_hgr_base = 0x20u;   /* BSS default: page 1 */
    return (unsigned char *)(((unsigned)gen2_hgr_base << 8)
        + ((unsigned)(y & 7) << 10)
        + ((unsigned)((y >> 3) & 7) << 7)
        + (unsigned)(y >> 6) * 40u);
}

/* Double-sampled HST0 (the burst notch can zero one of two close reads). */
static unsigned char gen2_blank(void)
{
    return (unsigned char)((GEN2_SS[7] | GEN2_SS[7]) & GEN2_HST0);
}

/* Tell V-blank (lines 192+, ~4550 CPU cycles of sustained blank) from a per-line
 * H-blank (~25 cycles) by counting CONSECUTIVE blank samples. The threshold must
 * sit between the two: gen2_blank() is a ~40-50 cycle call (pointer rebuild + two
 * indirect reads + jsr/rts), so one H-blank yields at most ~1-2 samples while a
 * V-blank yields ~90 (4550/50). 4 is safely above the H-blank noise yet returns
 * EARLY in V-blank (~3 lines in) -- which maximises the V-blank time left for the
 * caller's redraw, the difference between a single-buffer erase+redraw fitting
 * (no beam-race tearing) or spilling into the visible scan. NB: the original
 * value (400) was UNREACHABLE (needs ~20000 cyc of blank vs ~4550) so wait_vbl
 * hung; 16 worked but returned ~30 lines into V-blank, eating the redraw budget. */
#define GEN2_VBL_SAMPLES 4u
void gen2_wait_vbl(void)
{
    unsigned n;
    while (gen2_blank()) { }            /* wait for the live raster */
    for (;;) {
        while (!gen2_blank()) { }        /* wait for a blanking edge */
        for (n = 0; n < GEN2_VBL_SAMPLES && gen2_blank(); ++n) { }
        if (n >= GEN2_VBL_SAMPLES) return;   /* sustained blank => V-blank */
        /* short blank => it was just an H-blank; find the next edge */
    }
}

/* --- Look-up tables (referenced by every drawing module via internal.h) ---- */
unsigned char gen2_rowlo[192];
unsigned char gen2_rowhi[192];
unsigned char gen2_col7[280];
unsigned char gen2_mask7[280];
unsigned char gen2_phase7[280];   /* x % 7 (sub-byte phase 0..6) — no runtime divide */

/* LORES tables + state live here so gen2_set_draw_page can shift them in
 * place without forcing gen2_lores.c to link. A HIRES-only program pays the
 * 50 bytes (gen2_lo_rowlo[24] + gen2_lo_rowhi[24] + 2 flags) but does NOT
 * pull in the 8 LORES drawing functions (~200 bytes). gen2_lo_ready stays 0
 * until gen2_lores.c's first build call, so the shift below is a harmless
 * no-op for HIRES-only programs (the zero array stays zero).
 * gen2_lo_base is BSS like gen2_hgr_base: 0 = unset, lazily defaulted to $04
 * (page 1) by its readers in gen2_lores.c (build + clear). */
unsigned char gen2_lo_rowlo[24];
unsigned char gen2_lo_rowhi[24];
unsigned char gen2_lo_base;
unsigned char gen2_lo_ready;

/* (Re)derive the HIRES scanline-base tables for the current gen2_hgr_base. The
 * low bytes are page-independent; the high bytes carry the page. Cheap enough to
 * redo on a page flip — gen2_set_draw_page calls this when the draw page moves. */
static void gen2_fill_rows(void)
{
    unsigned y;
    for (y = 0; y < 192u; ++y) {
        unsigned a = (unsigned)gen2_hgr_row((unsigned char)y);
        gen2_rowlo[y] = (unsigned char)(a & 0xFFu);
        gen2_rowhi[y] = (unsigned char)(a >> 8);
    }
}

static unsigned char gen2_tables_ready;      /* BSS: 0 = not built yet */
/* Build the lookup tables once (all the asm fast paths read them): the
 * non-linear Apple II HIRES scanline bases (gen2_rowlo/gen2_rowhi) and the
 * x->(byte column, bit mask) tables that replace per-pixel division. Built
 * division-free: walk x and carry the column/bit, wrapping every 7 pixels. */
void gen2_build_tables(void)
{
    unsigned x;
    unsigned char col, bit;
    if (gen2_tables_ready) return;
    if (!gen2_hgr_base) gen2_hgr_base = 0x20u;   /* BSS default: page 1 */
    gen2_fill_rows();
    col = 0; bit = 0;
    for (x = 0; x < 280u; ++x) {
        gen2_col7[x]   = col;
        gen2_mask7[x]  = (unsigned char)(1u << bit);
        gen2_phase7[x] = bit;
        if (++bit == 7u) { bit = 0; ++col; }
    }
    gen2_tables_ready = 1;
}

/* ===========================================================================
 * Double buffering — draw page vs display page (PAGE2)
 * ===========================================================================
 * The card has two framebuffers: page 1 (HIRES $2000 / LORES $0400) and page 2
 * (HIRES $4000 / LORES $0800). gen2_set_draw_page picks where the drawing
 * primitives WRITE; gen2_show_page flips the card to DISPLAY the current draw
 * page (the $C254/$C255 soft switch). Tear-free animation = always draw into the
 * hidden page, then show it. Both pages share the same mode — pick it once
 * (gen2_hgr_init / gen2_lores_init, which also display PAGE1). The LORES branch
 * below is LAZY (guarded by gen2_lo_ready) so a HIRES-only program does not pull
 * gen2_lores.c into the link; gen2_lores_build() will pick up the right
 * gen2_lo_base on first lores use anyway.
 *
 * INVERTED ENCODING (BSS-zero-default rule, see the header): the flag stores
 * "drawing on page 2", so the BSS zero IS the page-1 default — no DATA
 * initializer, no lazy fixup at the read site. */
static unsigned char gen2_draw_page2;        /* 0 = page 1, nonzero = page 2 */

void gen2_set_draw_page(unsigned char page)
{
    unsigned char newh, newl, i;
    page = (page == 2u) ? 2u : 1u;
    newh = (page == 2u) ? 0x40u : 0x20u;            /* HIRES $4000 / $2000 */
    newl = (page == 2u) ? 0x08u : 0x04u;            /* LORES $0800 / $0400 */
    /* Ensure the HIRES tables exist (first call builds col7/mask7 + the row
     * tables; later calls are guarded no-ops). LORES tables are built lazily
     * by gen2_lores.c — see the conditional shift below. */
    gen2_build_tables();
    gen2_draw_page2 = (unsigned char)(page == 2u);
    /* Flip HIRES pages by SHIFTING the existing scanline-base HIGH bytes by the
     * page delta — the low bytes are page-independent, so this is a few hundred
     * byte adds instead of recomputing every base from the interleave formula.
     * Wrap-around makes the -$20 case work in uchar. */
    if (newh != gen2_hgr_base) {
        unsigned char dh = (unsigned char)(newh - gen2_hgr_base);
        for (i = 0u; i < 192u; ++i)
            gen2_rowhi[i] = (unsigned char)(gen2_rowhi[i] + dh);
        gen2_hgr_base = newh;
    }
    /* LORES: only shift the existing tables if gen2_lores_build has run. A
     * HIRES-only program will leave gen2_lo_ready = 0 forever and never pull
     * gen2_lores.c into the link. When LORES IS used later, gen2_lores_build
     * sees gen2_lo_base = newl (set unconditionally below) and computes the
     * right values for the current page. */
    if (gen2_lo_ready && newl != gen2_lo_base) {
        unsigned char dl = (unsigned char)(newl - gen2_lo_base);
        for (i = 0u; i < 24u; ++i)
            gen2_lo_rowhi[i] = (unsigned char)(gen2_lo_rowhi[i] + dl);
    }
    gen2_lo_base = newl;
}

void gen2_show_page(void)
{
    if (gen2_draw_page2) gen2_page2();
    else                 gen2_page1();
}
