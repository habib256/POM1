/*
 * GEN2Countdown.c — a 20 -> 0 counter for Uncle Bernie's GEN2 HGR card,
 *                   then return to the WOZ Monitor.
 *
 *   GEN2 HGR Countdown / VERHILLE Arnaud 2026
 *
 * Displays a large centred digit on the GEN2 card's 280x192 HIRES screen,
 * counting down from 20 to 0 (one digit per ~second), then returns to the
 * Wozniak monitor (the "\" prompt on the Apple-1 text screen).
 *
 *   Build : make            -> "software/Graphic HGR/GEN2Countdown.bin" (+ .txt)
 *   Run   : DevBench -> POM1 Bench -> new C sketch / GEN2 HGR
 *           or  build/POM1 --preset 11 \
 *                          --load 6000:"software/Graphic HGR/GEN2Countdown.bin" \
 *                          --run 6000
 *
 * INCREMENTAL rendering: the static decor (the title + a LARGE CIRCLE around
 * the digit) is drawn ONLY ONCE, before the loop. Each tick erases ONLY the
 * digit band then draws the new digit; neither the title nor the circle is
 * touched up. The circle is sized to stay entirely outside the erase band
 * (radius > 28 px = half-width of the band), so clear_band() never nibbles
 * into it.
 *
 * WARNING (lesson learned): the erase goes through gen2_hgr_fill_rect — a
 * whole-BYTE framebuffer fill written in ASSEMBLY (gen2_blit.s), NOT pixel
 * by pixel. gen2_hgr_unplot performs a software division (x/7, x%7) per
 * pixel — ~700 calls per erase, slow and pointless here. The digit fits in
 * known byte-columns, so zeroing those bytes is faster AND unambiguous.
 * Furthermore, NO static text must touch the digit band (otherwise the
 * erase would eat part of it -> "ghosting").
 */
#include "gen2.h"
#include "apple1io.h"

/* Coarse wait loop (~1 s at ~1 MHz). cc65 does NOT remove this empty loop.
 * Overridden by the test harness via -D; shipping value ~1 s. */
#ifndef TICK_SPINS
#define TICK_SPINS 45000u
#endif
/* Number of mode re-assertions during settle (DevBench deferred plug). */
#ifndef SETTLE_ITERS
#define SETTLE_ITERS 12u
#endif
#ifndef SETTLE_SPINS
#define SETTLE_SPINS 4000u
#endif

/* --- Digit band (in BYTE-COLUMNS and HIRES scanlines) -----------------------
 * The digit is drawn in 16x16 cells (Beautiful Boot font) at x=123 (2
 * digits) or x=132 (1 digit), so it spans at most x 123..156 on scanlines
 * y 88..103. In byte-columns (7 px/byte): x123..156 -> bytes 17..22.
 * We erase 16..23 (x112..167) for a safe margin, never reaching the
 * title (y=16..31). */
#define BAND_Y0   88u
#define BAND_Y1  104u    /* exclusive: scanlines 88..103                      */
#define BAND_C0   16u
#define BAND_C1   24u     /* exclusive: bytes 16..23                          */

static void spin(unsigned int spins)
{
    unsigned int i;
    for (i = 0; i < spins; ++i) { /* busy spin */ }
}

/* Erases ONLY the digit band via the library's assembly filler
 * (whole bytes, no per-pixel division). The title is outside this band
 * and stays intact. */
static void clear_band(void)
{
    gen2_hgr_fill_rect(BAND_Y0, BAND_Y1 - BAND_Y0, BAND_C0, BAND_C1 - BAND_C0, 0);
}

void main(void)
{
    unsigned char n;
    unsigned char s;

    /* --- STATIC decor: drawn ONCE ONLY. Title at y=16 and big circle
     * (centred on the digit band, radius 55) — both outside the erase
     * band (y=88..103, x=112..167), so never redrawn. The circle is
     * 110 px in diameter, the BBFont title spans y=16..31 -> 9 px gap
     * between the top of the arc (y=41) and the bottom of the title. --- */
    gen2_hgr_init();
    gen2_hgr_clear(0);
    gen2_hgr_puts(15, 16, "GEN2 COUNTDOWN");   /* 14 chars, centred, out of band */
    gen2_hgr_circle(140u, 96u, 30u);           /* big circle around the digit    */

    /* Settle: re-assert the mode while the GEN2 card finishes its
     * deferred plug (DevBench plugs cards ~15 frames after launch).
     * We do NOT redraw — just re-assert the soft-switches. */
    for (s = 0; s < SETTLE_ITERS; ++s) { gen2_hgr_init(); spin(SETTLE_SPINS); }

    /* --- Countdown 20, 19, ... 1, 0 --- n is unsigned: draw then wait
     * BEFORE testing for zero (otherwise --n wraps below zero). */
    for (n = 20u; ; --n) {
        clear_band();                                   /* erase old digit only */
        gen2_hgr_putu((n >= 10u) ? 123u : 132u, 88, n); /* draw the new one     */
        spin(TICK_SPINS);
        if (n == 0u) break;
    }

    /* Keep the "0" on screen for a moment, then hand back to the WOZ Monitor
     * (no-return jump to $FF1F — nothing runs after). */
    spin(TICK_SPINS);
    woz_mon();
}

