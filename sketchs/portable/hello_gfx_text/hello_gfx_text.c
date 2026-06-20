/*
 * hello_gfx_text.c — portable gfx_text façade demo (AXIS 3 of the GEN2/TMS9918
 * graphics-library factoring; see dev/lib/gfx/README.md).
 *
 * THE DRAWING BODY IS CARD-NEUTRAL: it positions text and numbers purely through
 * the gfx.h cell cursor (gfx_gotoxy / gfx_text / gfx_putu / gfx_putx). The SAME
 * source compiles for Uncle Bernie's GEN2 HGR *and* the P-LAB TMS9918 bitmap
 * card — only the one-time card bring-up is #ifdef'd, because powering up a video
 * card is inherently card-specific (Parmigiani's "one board at a time").
 *
 * Build (see Makefile): -DCARD_GEN2 links gfx-gen2.lib + the gen2c runtime;
 * the default (TMS) links gfx-tms.lib + the tms9918c runtime. The cell glyph
 * backend is chosen entirely at link time — no source change, no dispatch.
 */
#include "gfx.h"

#ifdef CARD_GEN2
#include "gen2.h"
static void card_init(void)
{
    gen2_hgr_init();
    gen2_hgr_clear(0u);
}
#else
#include "tms9918.h"
#include "screen2.h"
static void card_init(void)
{
    tms_init_regs(SCREEN2_TABLE);
    tms_set_color(COLOR_BLACK);
    screen2_init_bitmap(FG_BG(COLOR_WHITE, COLOR_BLACK));
    screen2_plot_mode = PLOT_MODE_SET;
}
#endif

void main(void)
{
    card_init();

    gfx_gotoxy(2u, 2u);
    gfx_text("GFX TEXT FACADE");

    /* Read the backend-supplied grid extent through the neutral façade — proves
     * which backend got linked without a single #ifdef in the drawing code. */
    gfx_gotoxy(2u, 4u);
    gfx_text("GRID ");
    gfx_putu(gfx_text_cols);
    gfx_text("x");
    gfx_putu(gfx_text_rows);

    gfx_gotoxy(2u, 6u);
    gfx_text("SCORE ");
    gfx_putu(12345u);

    gfx_gotoxy(2u, 8u);
    gfx_text("LIVES ");
    gfx_puti(-3);

    gfx_gotoxy(2u, 10u);
    gfx_text("ADDR $");
    gfx_putx(0xC250u);

    for (;;) {
        /* idle — the frame is static */
    }
}
