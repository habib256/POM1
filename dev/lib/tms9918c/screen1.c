/*
 * apple1-videocard-lib — POM1 CodeTank cc65 port
 * Derived from nippur72/apple1-videocard-lib (Antonino "Nino" Porcino).
 *   https://github.com/nippur72/apple1-videocard-lib
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 */
#include "screen1.h"
#include "c64font.h"
#include "sprites.h"

const unsigned char SCREEN1_TABLE[8] = {
    0x00U, 0xC0U, 0x0EU, 0x80U, 0x00U, 0x76U, 0x03U, 0x25U
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
    }
    tms_cursor_x = 0;
    tms_cursor_y = 0;
}

/*
 * Burst VRAM in VBlank: POM1 silicon-strict uses a dense slot table while
 * frameCycleCounter is in vertical blank (TMS9918.cpp). Chunks must stay
 * under one VBlank budget (~4.5k cyc @ 1x); 128 B/rafale is conservative.
 */
#define SCREEN1_SCROLL_VBLANK_CHUNK 128U

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

    /* Disable sprites (avoid linking sprites.c when not needed). */
    tms_set_vram_write_addr(TMS_SPRITE_ATTRS);
    TMS_WRITE_DATA_PORT(SPRITE_OFF_MARKER);
    screen1_cls();

    tms_set_vram_write_addr(TMS_PATTERN_TABLE);
    for (i = 256U * 8U; i != 0U; --i) {
        TMS_WRITE_DATA_PORT(0);
    }

    tms_set_vram_write_addr(TMS_COLOR_TABLE);
    for (i = 32U; i != 0U; --i) {
        TMS_WRITE_DATA_PORT(FG_BG(COLOR_BLACK, COLOR_WHITE));
    }
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
