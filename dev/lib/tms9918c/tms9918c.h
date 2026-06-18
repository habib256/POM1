/* tms9918c.h — umbrella header for the P-LAB TMS9918 C runtime (nippur72 port).
 *
 * One include for a TMS9918 C program:
 *     #include "tms9918c.h"
 *
 * Pulls in every public header the upstream nippur72/apple1-videocard-lib
 * shipped here (screen1, screen2, sprites + shadow, vsync, utils, printlib,
 * tms9918) plus the lib's own `apple1.h` for the Wozmon I/O + keyboard helpers
 * the lib was built against.
 *
 * Pure preprocessor — no code is generated. Costs zero bytes in the linked
 * binary unless you actually call something. The linker still dead-strips
 * unused .o files at the `.o` granularity (cc65 ld65 behaviour), so a sprite
 * program does not drag in the screen2 bitmap helpers and vice versa.
 *
 * --- Function chooser (which file owns what) -------------------------------
 *   Text mode (Graphics I):  screen1_*    in screen1.h
 *   Bitmap mode (Graphics II): screen2_*  in screen2.h
 *   Sprites + collisions:    tms_set_sprite / tms_shadow_*  in sprites.h /
 *                            sprite_shadow.h
 *   Register / VRAM bus:     tms_*  in tms9918.h
 *   IO delay / FG/BG macro:  utils.h (FG_BG, TMS_IO_DELAY)
 *   VBlank counter:          vsync_*  in vsync.h
 *   Keyboard + Wozmon I/O:   woz_* / apple1_*  in apple1.h (lib's own)
 *   Random:                  rand8/rand16  in random.h
 *
 * --- Sprite golden rule ----------------------------------------------------
 *   Use tms_shadow_init() + tms_shadow_set/move/clear() + tms_shadow_flush()
 *   instead of tms_set_sprite() per frame. The shadow path defers the SAT
 *   write to one burst inside VBlank (via tms_fast.s), avoiding the
 *   per-attribute tear visible when you write the SAT live. See sprite_shadow.h. */

#ifndef TMS9918C_H
#define TMS9918C_H

#include "tms9918.h"
#include "screen1.h"
#include "screen2.h"
#include "sprites.h"
#include "sprite_shadow.h"
#include "vsync.h"
#include "utils.h"
#include "printlib.h"
#include "apple1.h"

/* --- Argument-order helpers (zero-cost macros) -------------------------------
 * The upstream signatures put the character before the coordinates:
 *     screen2_putc(ch, x, y, col)
 *     screen2_puts(s,  x, y, col)
 * Every other primitive in the codebase (`screen2_plot`, `screen1_putcharxy`,
 * `gen2_hgr_puts`, etc.) follows the (x, y, …) convention. The wrappers below
 * give a unified (x, y, …) order so a reader does not have to remember which
 * function is the outlier. Pure preprocessor — no extra bytes, no extra JSR,
 * and they evaluate each argument exactly once. */
#define TMS_PUTC(x, y, ch, col) screen2_putc((ch), (x), (y), (col))
#define TMS_PUTS(x, y, s,  col) screen2_puts((s),  (x), (y), (col))

#endif /* TMS9918C_H */
