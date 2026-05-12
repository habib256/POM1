/*
 * apple1-videocard-lib — POM1 CodeTank cc65 port
 * Derived from nippur72/apple1-videocard-lib (Antonino "Nino" Porcino).
 *   https://github.com/nippur72/apple1-videocard-lib
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
void tms_write_reg(unsigned char regnum, unsigned char val);
void tms_set_color(unsigned char col);
void tms_init_regs(const unsigned char *table);
void tms_set_interrupt_bit(unsigned char val);
void tms_set_blank(unsigned char val);
void tms_set_external_video(unsigned char val);
void tms_wait_end_of_frame(void);
void tms_copy_to_vram(const unsigned char *source, unsigned size, unsigned dest);

#define tms_read_status() ((unsigned char)TMS_READ_CTRL_PORT)

/* --- ca65 fast paths (tms_fast.s) --------------------------------------- */

/* Burst-fill VRAM with `val` for `count` bytes. No per-byte IO delay. */
void tms_fill_vram(unsigned addr, unsigned char val, unsigned count);

/* Burst-copy from host (RAM/ROM) to VRAM. No per-byte IO delay.
 * ~3-4x faster than the C tms_copy_to_vram() above; use when destination
 * tolerates back-to-back writes (POM1 burst-safe outside VBLANK contention). */
void tms_copy_to_vram_fast(const void *src, unsigned size, unsigned dest);

#endif
