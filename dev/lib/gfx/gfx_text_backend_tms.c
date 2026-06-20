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

static unsigned char s_color = FG_BG(COLOR_WHITE, COLOR_BLACK);

void gfx_cell_color(unsigned char color)
{
    s_color = color ? color : (unsigned char)FG_BG(COLOR_WHITE, COLOR_BLACK);
}

void gfx_cell_glyph(char ch, unsigned char col, unsigned char row)
{
    screen2_putc((unsigned char)ch, col, row, s_color);
}
