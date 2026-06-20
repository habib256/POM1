/*
 * P-LAB TMS9918 (Apple-1) — gfx_text cell backend (AXIS 3)
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: forwards to screen2 via Antonino "Nino" Porcino's tms9918c runtime
 *   (apple1-videocard-lib, https://github.com/nippur72/apple1-videocard-lib).
 *
 * gfx_text_backend_tms.c — maps the card-neutral 8x8 cell model onto the
 * TMS9918 bitmap char cell (screen2_putc). 256x192 / 8 = 32x24, a direct
 * 1:1 mapping (cell == screen2 char column/row).
 *
 * Colour: TMS9918 carries a per-cell colour attribute, so the façade's
 * gfx_cell_color maps straight to screen2_putc's FG_BG byte. GFX_TEXT_DEFAULT
 * (0) selects white-on-black; any other value is a raw FG_BG(fg,bg) the caller
 * built with the tms9918.h palette macros.
 *
 * Build: compiled in a TMS9918 program with -I dev/lib/tms9918c (screen2.h /
 *        tms9918.h) and -I dev/lib/gfx (gfx.h). Linked from gfx-tms.lib.
 */
#include "gfx.h"
#include "screen2.h"       /* -I dev/lib/tms9918c (screen2_putc)        */
#include "tms9918.h"       /* FG_BG / COLOR_WHITE / COLOR_BLACK          */

const unsigned char gfx_text_cols = 32u;   /* 256 / 8 */
const unsigned char gfx_text_rows = 24u;   /* 192 / 8 */

/* 0 = GFX_TEXT_DEFAULT (white-on-black), resolved at DRAW time. Deliberately a
 * ZERO initial value: a CodeTank ROM target (apple1-videocard-lib cfg) does not
 * copy the .data segment, so a non-zero static initializer here would arrive as
 * garbage and the text would render in an invisible colour. .bss IS zeroed by
 * crt0, so a 0 default is the only value we can trust without the caller first
 * calling gfx_cell_color(). */
static unsigned char s_color = 0u;

void gfx_cell_color(unsigned char color)
{
    s_color = color;
}

void gfx_cell_glyph(char ch, unsigned char col, unsigned char row)
{
    /* Materialise the default as a runtime literal (not a .data static) so it is
     * correct even when .data was not initialised — see s_color above. */
    const unsigned char col_attr =
        s_color ? s_color : (unsigned char)FG_BG(COLOR_WHITE, COLOR_BLACK);
    screen2_putc((unsigned char)ch, col, row, col_attr);
}
