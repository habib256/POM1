/*
 * gfx_backend_tms_rect.c — gfx_filled_rect + gfx_clear forwarders for the
 * TMS9918 bitmap backend.
 *
 * Split from gfx_backend_tms.c so a TMS program that only calls
 * line/circle/ellipse never references screen2_filled_rect or screen2_clear,
 * which lets ld65 dead-strip screen2_ext.c (and the burst-fill asm in
 * tms_fast.s it pulls). A program that DOES call gfx_filled_rect or
 * gfx_clear opts in by linking screen2_ext.c (which it would already need
 * anyway to call the underlying screen2 helpers).
 */
#include "gfx.h"
#include "screen2.h"       /* -I dev/lib/tms9918c */

void gfx_filled_rect(unsigned x0, unsigned char y0,
                     unsigned x1, unsigned char y1)
{
    /* screen2_filled_rect already sorts the bounds; cast x0/x1 down to the
     * TMS 256-pixel address space. */
    screen2_filled_rect((unsigned char)x0, y0, (unsigned char)x1, y1);
}

/* TMS bitmap clear is always 0 — `color` honoured for API symmetry but the
 * underlying primitive (screen2_clear via tms_fill_vram) writes 0x00 into
 * the pattern table. A future colour-mask extension would land here. */
void gfx_clear(unsigned char color) { (void)color; screen2_clear(); }
