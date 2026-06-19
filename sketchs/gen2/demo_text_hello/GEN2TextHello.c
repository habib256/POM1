/*
 * GEN2TextHello.c — small demo of the 40x24 TEXT mode on Uncle Bernie's GEN2
 *                   colour card, written in C with the gen2c runtime.
 *
 *   GEN2 TEXT demo / VERHILLE Arnaud 2026
 *
 * The GEN2 is the Apple II video subsystem grafted onto the Apple-1 bus. Its
 * TEXT mode reads the Apple IIe Enhanced char-gen (POM1 ships
 * `roms/apple2e_char.rom` and consumes it from src/GraphicsCard.cpp), which
 * finally enables true LOWERCASE — POM1's legacy built-in 5x7 fallback folded
 * every lowercase letter back to uppercase. This demo shows off the new glyph
 * set:
 *
 *     hello world
 *     Uncle Bernie's GEN2 Color Card
 *
 *   Build : make    -> "software/Graphic HGR/GEN2TextHello.bin" (+ .txt)
 *   Run   : build/POM1 --preset 11 \
 *               --load 6000:"software/Graphic HGR/GEN2TextHello.bin" --run 6000
 *           or : DevBench -> POM1 Bench -> New sketch C / GEN2 HGR, paste
 *                this file (the gen2c runtime handles TEXT, LORES and HIRES).
 *
 * Screen-byte encoding (Apple II convention, GEN2 char-gen follows the same):
 *   $00-$3F  inverse  (low 6 bits = char index, always inverse video)
 *   $40-$7F  flashing (alternates normal / inverse ~2 Hz)
 *   $80-$FF  normal   (low 7 bits = raw ASCII)
 * We feed plain ASCII and force bit 7 to land in the NORMAL attribute
 * (white-on-black).
 *
 * TEXT page 1 = $0400-$07FF, organised in Apple II interleave:
 *   addr(row) = $0400 + 0x80 * (row & 7) + 0x28 * (row >> 3)
 * (same formula as GraphicsCard::textRowAddress on the emulator side.)
 *
 * DRAW FIRST: writes to the text page $0400 PERSIST in RAM. We then loop
 * re-asserting TEXT mode — it's instant (one soft-switch read) and covers
 * the DevBench's deferred card plug (~15 frames): as soon as the card is
 * live the mode takes effect and the already-written screen appears.
 */
#include "gen2.h"

/* Apple II text-page-1 row address (decimal interleave). row: 0..23. */
static unsigned char *text_row(unsigned char row)
{
    return (unsigned char*)(0x0400u
        + ((unsigned)(row & 7u) << 7)
        + (unsigned)(row >> 3) * 40u);
}

/* Write a NUL-terminated string at (col, row) with bit 7 forced: the char-gen
 * NORMAL attribute renders the glyphs white-on-black (uppercase AND lowercase
 * thanks to the Apple IIe char ROM). col: 0..39 ; row: 0..23. */
static void putstr(unsigned char col, unsigned char row, const char *s)
{
    unsigned char *p = text_row(row) + col;
    while (*s) *p++ = (unsigned char)(*s++ | 0x80u);
}

/* Clear text page 1 by filling 24x40 bytes with $A0 (NORMAL space = ' ' | $80).
 * Required because the initial DRAM contents are random (bistable bytes at
 * cold plug, Silicon Strict ON by default). */
static void text_clear(void)
{
    unsigned char r, c;
    for (r = 0; r < 24u; ++r) {
        unsigned char *p = text_row(r);
        for (c = 0; c < 40u; ++c) p[c] = 0xA0u;
    }
}

void main(void)
{
    /* TEXT + page 1 + full screen. Three soft-switch reads are enough — TEXT
     * mode has no heavier init (no bitmap to clear, $0400 IS the source of
     * truth). */
    gen2_text();
    gen2_full();
    gen2_page1();

    text_clear();

    /* "hello world" centred: 11 chars, padding (40-11)/2 = 14. */
    putstr(14u, 11u, "hello world");
    /* "Uncle Bernie's GEN2 Color Card": 30 chars, padding (40-30)/2 = 5. */
    putstr(5u, 13u, "Uncle Bernie's GEN2 Color Card");

    /* Hold the screen: re-assert TEXT mode in a tight loop. Covers the
     * DevBench's deferred plug (~15 frames) without blocking the draw. */
    for (;;) { gen2_text(); }
}
