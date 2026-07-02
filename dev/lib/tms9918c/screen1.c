/*
 * P-LAB TMS9918 (Apple-1) — cc65 C runtime, POM1 CodeTank port
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: derived from Antonino "Nino" Porcino's apple1-videocard-lib
 *   (https://github.com/nippur72/apple1-videocard-lib).
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 */
#include "screen1.h"
#include "c64font.h"
#include "sprites.h"

/* R1 = $80: 16K, display OFF. The display is enabled by screen1_prepare()
 * once the pattern/colour/name tables and the SAT park are valid — enabling
 * it here (the old $C0) flashed power-on VRAM garbage + noise sprites on
 * real DRAM until prepare caught up (best-practices §1 blank-first order). */
const unsigned char SCREEN1_TABLE[8] = {
    0x00U, 0x80U, 0x0EU, 0x80U, 0x00U, 0x76U, 0x03U, 0x25U
};

void screen1_load_font(void) {
    unsigned i;

    unsigned char g;
    unsigned addr = TMS_PATTERN_TABLE + (32U * 8U);

    /* Glyphs ASCII 32..127 from FONT[], plus inverted copies (bit 7 set). */
    for (g = 0U; g < 96U; ++g) {
        tms_set_vram_write_addr(addr);
        for (i = 0U; i < 8U; ++i) {
            TMS_WRITE_DATA_PORT(tms_c64font[(unsigned)g * 8U + i]);
            TMS_IO_DELAY();
        }
        addr += 8U;
    }

    addr = TMS_PATTERN_TABLE + ((128U + 32U) * 8U);
    for (g = 0U; g < 96U; ++g) {
        tms_set_vram_write_addr(addr);
        for (i = 0U; i < 8U; ++i) {
            TMS_WRITE_DATA_PORT((unsigned char)~tms_c64font[(unsigned)g * 8U + i]);
            TMS_IO_DELAY();
        }
        addr += 8U;
    }
}

void screen1_cls(void) {
    unsigned i;
    tms_set_vram_write_addr(TMS_NAME_TABLE);
    for (i = SCREEN1_SIZE; i != 0U; --i) {
        TMS_WRITE_DATA_PORT(32U);
        /* Explicit pacing: this loop is reachable with the display ON
         * (screen1_putc(CHR_CLS)) and was the only display-ON burst with
         * NO delay at all — measured ~70c/write today purely from cc65
         * -Oirs codegen, which any optimizer change could shrink below
         * the ~22c silicon floor. The RAM-sink delay pins the margin. */
        TMS_IO_DELAY();
    }
    tms_cursor_x = 0;
    tms_cursor_y = 0;
}

/*
 * Burst VRAM in VBlank: POM1 silicon-strict uses a dense slot table while
 * frameCycleCounter is in vertical blank (TMS9918.cpp). Chunks must stay
 * under one VBlank budget (~4.5k cyc @ 1x); 128 B/burst is conservative.
 */
#define SCREEN1_SCROLL_VBLANK_CHUNK 128U

/* RAM cost: 768 B (~9% of the 8 KB CodeTank machine), allocated whenever a
 * program links screen1_putc/screen1_scroll. It mirrors one full text page so
 * the scroll can copy VRAM->RAM->VRAM in VBlank-sized bursts; a line-by-line
 * scroll through a 32 B buffer would reclaim most of it at the cost of more
 * VBlank round-trips. */
static unsigned char screen1_scroll_buf[768];

static void screen1_vblank_burst_read(unsigned base, unsigned char *dst, unsigned total) {
    unsigned done = 0;
    while (done < total) {
        unsigned n;
        unsigned i;
        tms_wait_end_of_frame();
        n = SCREEN1_SCROLL_VBLANK_CHUNK;
        if (n > total - done) {
            n = total - done;
        }
        tms_set_vram_read_addr(base + done);
        for (i = 0; i < n; ++i) {
            dst[done + i] = TMS_READ_DATA_PORT;
        }
        done += n;
    }
}

static void screen1_vblank_burst_write(unsigned base, const unsigned char *src, unsigned total) {
    unsigned done = 0;
    while (done < total) {
        unsigned n;
        unsigned i;
        tms_wait_end_of_frame();
        n = SCREEN1_SCROLL_VBLANK_CHUNK;
        if (n > total - done) {
            n = total - done;
        }
        tms_set_vram_write_addr(base + done);
        for (i = 0; i < n; ++i) {
            TMS_WRITE_DATA_PORT(src[done + i]);
        }
        done += n;
    }
}

void screen1_scroll_up(void) {
    unsigned i;

    /* Snapshot rows 1..23, then rewrite full 768 B name table (scroll + blank). */
    screen1_vblank_burst_read(TMS_NAME_TABLE + 32U, screen1_scroll_buf, 736U);
    for (i = 0; i < 32U; ++i) {
        screen1_scroll_buf[736U + i] = 32U;
    }
    screen1_vblank_burst_write(TMS_NAME_TABLE, screen1_scroll_buf, 768U);
}

void screen1_prepare(void) {
    unsigned i;

    /* Blank UNCONDITIONALLY. tms_regs_latch is BSS — on a warm start
     * (Wozmon 4000R re-run, CodeTank game switch) the mirror can read
     * "blanked" while the REAL chip still has the display ON from the
     * previous program; trusting the mirror ran this bulk init hot and
     * dropped most bytes on real silicon. (mirror & $3F) | $80 keeps the
     * known mode bits, forces 16K and clears the display bit — fail-safe
     * is always "display off during init". Note: the R1 write clobbers
     * the VRAM address counter (real-silicon behaviour, openMSX/dvik),
     * so every upload below re-primes its own address. */
    tms_write_reg(1, (unsigned char)((tms_regs_latch[1] & 0x3FU) | 0x80U));

    /* Park the SAT: $D0 terminator at slot 0 + $D1 prefill of the remaining
     * 127 bytes. A lone $D0 renders fine on POM1's bistable power-on VRAM
     * but leaves SAT noise armed behind it on real silicon — ghost sprites
     * the moment slot 0 is overwritten (TMS9918-SPRITE_INIT.md §4.2/§11). */
    tms_set_vram_write_addr(TMS_SPRITE_ATTRS);
    TMS_WRITE_DATA_PORT(SPRITE_OFF_MARKER);
    for (i = 127U; i != 0U; --i) {
        TMS_WRITE_DATA_PORT(0xD1U);
    }
    screen1_cls();

    tms_set_vram_write_addr(TMS_PATTERN_TABLE);
    for (i = 256U * 8U; i != 0U; --i) {
        TMS_WRITE_DATA_PORT(0);
    }

    tms_set_vram_write_addr(TMS_COLOR_TABLE);
    for (i = 32U; i != 0U; --i) {
        TMS_WRITE_DATA_PORT(FG_BG(COLOR_BLACK, COLOR_WHITE));
    }

    /* Contract (juillet 2026): prepare LEAVES THE DISPLAY ON — the tables
     * and the SAT park are valid now, and SCREEN1_TABLE ships R1 with the
     * display bit clear precisely so this is the first enable the viewer
     * ever sees (no garbage frame on real DRAM). */
    tms_set_blank(1);
}

void screen1_putc(unsigned char c) {
    if (c == CHR_CLS) {
        screen1_cls();
    } else if (c == CHR_HOME) {
        tms_cursor_x = 0;
        tms_cursor_y = 0;
    } else if (c == CHR_REVERSE_OFF) {
        tms_reverse = 0;
    } else if (c == CHR_REVERSE_ON) {
        tms_reverse = 1;
    } else if (c == CHR_BACKSPACE) {
        if (tms_cursor_x != 0) {
            --tms_cursor_x;
        } else if (tms_cursor_y != 0) {
            --tms_cursor_y;
            tms_cursor_x = 31;
        }
    } else {
        if (c == '\r' || c == '\n') {
            tms_cursor_x = 31;
        } else {
            unsigned char ch = c;
            unsigned addr;
            if (tms_reverse) {
                ch = (unsigned char)(ch | 128U);
            }
            addr = TMS_NAME_TABLE + (unsigned)tms_cursor_y * 32U + tms_cursor_x;
            tms_set_vram_write_addr(addr);
            TMS_WRITE_DATA_PORT(ch);
        }
        if (tms_cursor_x == 31) {
            tms_cursor_x = 0;
            if (tms_cursor_y == 23) {
                screen1_scroll_up();
            } else {
                ++tms_cursor_y;
            }
        } else {
            ++tms_cursor_x;
        }
    }
}

void screen1_puts(const unsigned char *s) {
    unsigned char c;
    while ((c = *s++) != 0) {
        screen1_putc(c);
    }
}

void screen1_locate(unsigned char x, unsigned char y) {
    tms_cursor_x = x;
    tms_cursor_y = y;
}

/* screen1_strinput lives in screen1_input.c so a program without text input
 * doesn't drag in apple1_getkey + the input-echo dance. */
