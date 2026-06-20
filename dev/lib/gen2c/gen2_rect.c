/* gen2_rect.c — byte-aligned and pixel-aligned rectangle fills + colorize.
 *
 * Routes through gen2_blit.s for the inner loop. The C side clips the
 * rectangle and stuffs the parameter block; the asm derives byte columns +
 * edge masks and applies (byte & keep) | set on each scanline. */

#include "gen2.h"
#include "gen2_internal.h"

/* Fast rectangular byte-fill (assembly inner loop). See gen2.h. Clips the
 * rectangle to the screen, then hands the on-screen part to gen2_fill_rect_asm. */
void gen2_hgr_fill_rect(unsigned char y0, unsigned char rows,
                        unsigned char col0, unsigned char ncols,
                        unsigned char val)
{
    gen2_build_tables();
    if (rows == 0u || ncols == 0u || y0 > 191u || col0 >= 40u) return;
    if ((unsigned)y0   + rows  > 192u) rows  = (unsigned char)(192u - y0);
    if ((unsigned)col0 + ncols >  40u) ncols = (unsigned char)(40u  - col0);
    gen2_f_y0   = y0;
    gen2_f_rows = rows;
    gen2_f_col0 = col0;
    gen2_f_cols = ncols;
    gen2_f_val  = val;
    gen2_fill_rect_asm();
}

/* Shared core for fill_pixrect (set=1, white) and clear_pixrect (set=0, erase).
 * The C side only clips the rectangle to the screen and picks the mode; the asm
 * derives the byte columns and edge masks (col7/mask7 LUTs) and does the fill. */
static void gen2_pixrect(unsigned x, unsigned char y,
                         unsigned char w, unsigned char h, unsigned char set)
{
    unsigned xr;
    gen2_build_tables();
    if (w == 0u || h == 0u || x > 279u || y > 191u) return;
    xr = x + (unsigned)w - 1u;                 /* rightmost pixel               */
    if (xr > 279u) xr = 279u;                  /* clip right edge               */
    if ((unsigned)y + h > 192u) h = (unsigned char)(192u - y);   /* clip bottom */
    gen2_r_x    = x;
    gen2_r_xr   = xr;
    gen2_r_y0   = y;
    gen2_r_rows = h;
    gen2_r_mode = set;
    gen2_pixrect_asm();
}

void gen2_hgr_fill_pixrect(unsigned x, unsigned char y, unsigned char w, unsigned char h)
{
    gen2_pixrect(x, y, w, h, 1u);
}

void gen2_hgr_clear_pixrect(unsigned x, unsigned char y, unsigned char w, unsigned char h)
{
    gen2_pixrect(x, y, w, h, 0u);
}

/* Fill (set=1, white) or erase (set=0) a 6x6 block inside the 8x8 grid cell
 * (cx, cy). One call: the cx*8 / cy*8 happen in asm (no cc65 aslax3 16-bit
 * shift helper) and the pixrect clip is skipped (grid cells are always
 * on-screen). For 8px-grid games (Snake, tiles) that draw one cell at a time. */
void gen2_hgr_cell(unsigned char cx, unsigned char cy, unsigned char set)
{
    gen2_build_tables();
    gen2_c_cx  = cx;
    gen2_c_cy  = cy;
    gen2_c_set = set;
    gen2_cell_asm();
}

/* Map a GEN2_* colour constant onto the colorize carrier parameter block
 * (carrier bytes + palette high bit, verified against the GEN2 renderer):
 *   violet even=$55 odd=$2A hi=0 ; green even=$2A odd=$55 hi=0 ;
 *   orange = green|hi ; blue = violet|hi.
 * Returns 1 for a known artifact colour, 0 for white/unknown. Duplicated in
 * gen2_text.c (same name, both static) so a text-only program does not pull
 * gen2_rect.c, and a graphics-only program does not pull gen2_text.c. The
 * function is ~30 bytes after cc65 codegen — duplicating costs less than
 * either module being force-linked. */
static unsigned char gen2_set_carrier(unsigned char color)
{
    switch (color) {
        case GEN2_GREEN:  gen2_z_ce = 0x2Au; gen2_z_co = 0x55u; gen2_z_hi = 0x00u; break;
        case GEN2_ORANGE: gen2_z_ce = 0x2Au; gen2_z_co = 0x55u; gen2_z_hi = 0x80u; break;
        case GEN2_BLUE:   gen2_z_ce = 0x55u; gen2_z_co = 0x2Au; gen2_z_hi = 0x80u; break;
        case GEN2_VIOLET: gen2_z_ce = 0x55u; gen2_z_co = 0x2Au; gen2_z_hi = 0x00u; break;
        default: return 0u;                 /* unknown / white -> leave as drawn */
    }
    return 1u;
}

/* Tint an arbitrary PIXEL rectangle to one of the four artifact colours — the
 * graphics analogue of gen2_hgr_puts_color (for food / sprites / bars, not text).
 * Draw the shape WHITE first, then call this to recolour it. x,w,y,h are pixels;
 * the covering byte-column box [x..x+w) is tinted (HIRES colour is byte-granular,
 * so keep ~1 empty cell between differently-coloured shapes). Black pixels in the
 * box stay black (only the palette bit flips), so an isolated shape tints clean. */
void gen2_hgr_colorize(unsigned x, unsigned char y, unsigned char w,
                       unsigned char h, unsigned char color)
{
    unsigned right;

    if (w == 0u || h == 0u || y > 191u) return;
    if (!gen2_set_carrier(color)) return;
    if ((unsigned)y + h > 192u) h = (unsigned char)(192u - y);

    right = x + (unsigned)w - 1u;
    if (right > 279u) right = 279u;
    gen2_z_col0  = (unsigned char)(x / 7u);
    gen2_z_ncols = (unsigned char)(right / 7u - x / 7u + 1u);
    gen2_z_y0    = y;
    gen2_z_rows  = h;
    gen2_colorize_asm();
}
