/*
 * P-LAB TMS9918 (Apple-1) — cc65 C runtime, POM1 CodeTank port
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: derived from Antonino "Nino" Porcino's apple1-videocard-lib
 *   (https://github.com/nippur72/apple1-videocard-lib).
 * Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
 */
#ifndef TMS9918_H
#define TMS9918_H

#include "utils.h"

#define TMS9918 1

extern volatile unsigned char *const VDP_DATA;
extern volatile unsigned char *const VDP_REG;

#define WRITE_TO_VRAM    0x40U
#define READ_FROM_VRAM   0x00U
#define HIADDRESS_MASK   0x3FU
#define WRITE_TO_REG     0x80U
#define REGNUM_MASK      0x07U

#define REG0_EXTVID_MASK 0x01U
#define REG1_16K_MASK    0x80U
#define REG1_BLANK_MASK  0x40U
#define REG1_IE_MASK     0x20U
#define REG1_M1M2_MASK   0x18U
#define REG1_SIZE_MASK   0x02U
#define REG1_MAG_MASK    0x01U

#define COLOR_TRANSPARENT    0x0U
#define COLOR_BLACK          0x1U
#define COLOR_MEDIUM_GREEN   0x2U
#define COLOR_LIGHT_GREEN    0x3U
#define COLOR_DARK_BLUE      0x4U
#define COLOR_LIGHT_BLUE     0x5U
#define COLOR_DARK_RED       0x6U
#define COLOR_CYAN           0x7U
#define COLOR_MEDIUM_RED     0x8U
#define COLOR_LIGHT_RED      0x9U
#define COLOR_DARK_YELLOW    0xAU
#define COLOR_LIGHT_YELLOW   0xBU
#define COLOR_DARK_GREEN     0xCU
#define COLOR_MAGENTA        0xDU
#define COLOR_GREY           0xEU
#define COLOR_WHITE          0xFU

#define TMS_NAME_TABLE         0x3800U
#define TMS_COLOR_TABLE        0x2000U
#define TMS_PATTERN_TABLE      0x0000U
#define TMS_SPRITE_ATTRS       0x3B00U
#define TMS_SPRITE_PATTERNS    0x1800U

#define FG_BG(f, b) ((unsigned char)(((f) << 4) | (b)))

#define FRAME_BIT(a)      ((a) & 0x80U)
#define FIVESPR_BIT(a)    ((a) & 0x40U)
#define COLLISION_BIT(a)  ((a) & 0x20U)
#define SPRITE_NUM(a)     ((a) & 0x1FU)

#define TMS_WRITE_CTRL_PORT(a)  (*(VDP_REG) = (unsigned char)(a))
#define TMS_WRITE_DATA_PORT(a)  (*(VDP_DATA) = (unsigned char)(a))
#define TMS_READ_CTRL_PORT      (*(VDP_REG))
#define TMS_READ_DATA_PORT      (*(VDP_DATA))

extern unsigned char tms_regs_latch[8];
extern unsigned char tms_cursor_x;
extern unsigned char tms_cursor_y;
extern unsigned char tms_reverse;

void tms_set_vram_write_addr(unsigned addr);
void tms_set_vram_read_addr(unsigned addr);

/* SILICON WARNING (openMSX/dvik, modelled by POM1 since juillet 2026): a VDP
 * register write ALSO loads the VRAM address counter's high bits, and the
 * first control byte lands in its low bits immediately. NEVER call
 * tms_write_reg / tms_set_color / tms_set_blank / tms_set_interrupt_bit /
 * tms_set_external_video (or the sprite size/mag setters) between
 * tms_set_vram_*_addr() and the data-port stream it primes — re-set the
 * address after any register write. */
void tms_write_reg(unsigned char regnum, unsigned char val);
void tms_set_color(unsigned char col);
void tms_init_regs(const unsigned char *table);
void tms_set_interrupt_bit(unsigned char val);
void tms_set_blank(unsigned char val);
void tms_set_external_video(unsigned char val);
/* Blocks until the NEXT VBlank edge (drain-then-poll — a stale F cannot
 * satisfy it). Returns a status snapshot: fresh F, plus C/5S observed on
 * either the drain or the terminal read (a status read clears all three
 * atomically, so this return value is the one chance to see them). */
unsigned char tms_wait_end_of_frame(void);
void tms_copy_to_vram(const unsigned char *source, unsigned size, unsigned dest);

/* SILICON WARNING: reading the status port latch-clears F (bit 7), 5S (bit 6)
 * AND C (bit 5) atomically — TI datasheet §2.2. Read ONCE per frame, snapshot
 * the byte, and test FRAME_BIT/FIVESPR_BIT/COLLISION_BIT on the copy. */
#define tms_read_status() ((unsigned char)TMS_READ_CTRL_PORT)

/* --- ca65 fast paths (tms_fast.s) --------------------------------------- */

/* Burst-fill VRAM with `val` for `count` bytes. Silicon-safe: blanks the
 * display around the burst when it was enabled (backdrop shows for the rest
 * of the current frame), restores it after. */
void tms_fill_vram(unsigned addr, unsigned char val, unsigned count);

/* Burst-copy from host (RAM/ROM) to VRAM. Same display-blank bracket as
 * tms_fill_vram. For tear-free incremental updates prefer the VBlank-chunked
 * helpers (screen1_scroll_up) or tms_shadow_flush. */
void tms_copy_to_vram_fast(const void *src, unsigned size, unsigned dest);

#endif
