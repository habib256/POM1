/* gen2_pixel.c — single-pixel plot/unplot via the asm col7/mask7 fast path. */

#include "gen2.h"
#include "gen2_internal.h"

/* Set / clear one white pixel via the assembly fast path (col7/mask7 LUTs +
 * the scanline-base table — NO per-pixel division). x:0..279, y:0..191.
 * Drop-in replacements for the old per-pixel-division versions; this is what
 * makes per-pixel games (Snake's 6x6 blocks, etc.) usable on the GEN2. */
void gen2_hgr_plot(unsigned x, unsigned char y)
{
    if (x > 279u || y > 191u) return;
    gen2_build_tables();
    gen2_p_x = x;
    gen2_p_y = y;
    gen2_plot_asm();
}

void gen2_hgr_unplot(unsigned x, unsigned char y)
{
    if (x > 279u || y > 191u) return;
    gen2_build_tables();
    gen2_p_x = x;
    gen2_p_y = y;
    gen2_unplot_asm();
}
