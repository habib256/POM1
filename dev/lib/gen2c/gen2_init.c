/* gen2_init.c — initialisation, look-up tables, page management.
 *
 * Always linked (every gen2c consumer pulls at least the table builder via
 * gen2_build_tables). Carries the soft-switch sink, the HIRES + LORES
 * cross-module globals, the wait-VBL helper and the draw-page setter. See
 * gen2.h for the public API doc; see gen2_internal.h for what the other
 * modules import. */

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
 * gen2_set_draw_page; defaults to page 1. */
unsigned char gen2_hgr_base = 0x20u;

/* Apple II interleaved HIRES scanline base, in the CURRENT draw page:
   (gen2_hgr_base<<8) + (y&7)*$400 + ((y>>3)&7)*$80 + (y>>6)*40
   gen2_hgr_base=$20 gives the classic $2000 page-1 layout; $40 is page 2. */
unsigned char *gen2_hgr_row(unsigned char y)
{
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

void gen2_wait_vbl(void)
{
    unsigned n;
    while (gen2_blank()) { }            /* wait for the live raster */
    for (;;) {
        while (!gen2_blank()) { }        /* wait for a blanking edge */
        for (n = 0; n < 400u && gen2_blank(); ++n) { }
        if (n >= 400u) return;          /* sustained blank => V-blank */
        /* short blank => it was just an H-blank; find the next edge */
    }
}

/* --- Look-up tables (referenced by every drawing module via internal.h) ---- */
unsigned char gen2_rowlo[192];
unsigned char gen2_rowhi[192];
unsigned char gen2_col7[280];
unsigned char gen2_mask7[280];

/* LORES tables + state live here so gen2_set_draw_page can shift them in
 * place without forcing gen2_lores.c to link. A HIRES-only program pays the
 * 50 bytes (gen2_lo_rowlo[24] + gen2_lo_rowhi[24] + 2 flags) but does NOT
 * pull in the 8 LORES drawing functions (~200 bytes). gen2_lo_ready stays 0
 * until gen2_lores.c's first build call, so the shift below is a harmless
 * no-op for HIRES-only programs (the zero array stays zero). */
unsigned char gen2_lo_rowlo[24];
unsigned char gen2_lo_rowhi[24];
unsigned char gen2_lo_base = 0x04u;
unsigned char gen2_lo_ready = 0;

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

static unsigned char gen2_tables_ready = 0;
/* Build the lookup tables once (all the asm fast paths read them): the
 * non-linear Apple II HIRES scanline bases (gen2_rowlo/gen2_rowhi) and the
 * x->(byte column, bit mask) tables that replace per-pixel division. Built
 * division-free: walk x and carry the column/bit, wrapping every 7 pixels. */
void gen2_build_tables(void)
{
    unsigned x;
    unsigned char col, bit;
    if (gen2_tables_ready) return;
    gen2_fill_rows();
    col = 0; bit = 0;
    for (x = 0; x < 280u; ++x) {
        gen2_col7[x]  = col;
        gen2_mask7[x] = (unsigned char)(1u << bit);
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
 * gen2_lo_base on first lores use anyway. */
static unsigned char gen2_draw_page = 1u;

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
    gen2_draw_page = page;
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
    if (gen2_draw_page == 2u) gen2_page2();
    else                      gen2_page1();
}
