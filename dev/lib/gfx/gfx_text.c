/*
 * gfx_text.c — card-NEUTRAL positioned-text façade (AXIS 3). See gfx.h.
 *
 * Owns the 8x8 cell cursor, the advance/wrap rules and the number formatting;
 * draws nothing itself — every glyph goes through the per-card backend
 * gfx_cell_glyph (gfx_text_backend_<card>.c). Includes ONLY gfx.h, so the same
 * object links against either backend (GEN2 or TMS9918) unchanged.
 *
 * The cursor is module-static (one text pen per program, matching the single
 * video card). It is NOT reset on entry: a program calls gfx_gotoxy once to
 * anchor it (default 0,0 from the BSS zero-init), then streams gfx_text /
 * gfx_putu.
 */
#include "gfx.h"

static unsigned char s_col = 0u;   /* cursor cell column (0..gfx_text_cols-1) */
static unsigned char s_row = 0u;   /* cursor cell row    (0..gfx_text_rows-1) */

void gfx_gotoxy(unsigned char col, unsigned char row)
{
    /* Clamp into the grid so a caller using the WIDER card's extent (GEN2 35)
     * on the NARROWER one (TMS 32) lands on-screen instead of off the edge. */
    if (col >= gfx_text_cols) col = (unsigned char)(gfx_text_cols - 1u);
    if (row >= gfx_text_rows) row = (unsigned char)(gfx_text_rows - 1u);
    s_col = col;
    s_row = row;
}

void gfx_putc(char ch)
{
    if (ch == '\n') {                /* newline: column 0 of the next row */
        s_col = 0u;
        if (s_row + 1u < gfx_text_rows) ++s_row;
        return;
    }
    if (ch == '\r') {                /* carriage return: column 0, same row */
        s_col = 0u;
        return;
    }
    if ((unsigned char)ch < 0x20u)   /* other control codes: ignore */
        return;

    gfx_cell_glyph(ch, s_col, s_row);
    if (++s_col >= gfx_text_cols) {  /* wrap at the right margin */
        s_col = 0u;
        if (s_row + 1u < gfx_text_rows) ++s_row;   /* last row clamps (no scroll) */
    }
}

void gfx_text(const char *s)
{
    while (*s)
        gfx_putc(*s++);
}

void gfx_putu(unsigned value)
{
    char buf[6];                     /* 65535 = 5 digits + NUL */
    gfx_utoa(buf, value);
    gfx_text(buf);
}

void gfx_puti(int value)
{
    char buf[7];                     /* '-' + 5 digits + NUL */
    gfx_itoa(buf, value);
    gfx_text(buf);
}

void gfx_putx(unsigned value)
{
    char buf[5];                     /* 4 hex digits + NUL */
    gfx_hexstr(buf, value);
    gfx_text(buf);
}
