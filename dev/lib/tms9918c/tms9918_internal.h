/* tms9918_internal.h — private declarations shared by the tms9918c modules.
 * NOT included by any public *.h, NOT intended for consumer code.
 *
 * Mirror of gen2c/gen2_internal.h. As the upstream nippur72/apple1-videocard-lib
 * port grew, several mutable globals leaked into the public headers
 * (random.h, vsync.h, tms9918.h). This header centralises the cross-module
 * `extern` declarations + matching `#pragma zpsym` so they can be migrated to
 * the zero page (faster indexed access) without touching the public surface.
 *
 * Anything declared here is a maintenance contract between the .c files of
 * dev/lib/tms9918c/. Consumer code keeps using the public headers
 * (random.h, vsync.h, sprite_shadow.h, tms9918.h, screen1.h, screen2.h).
 */

#ifndef TMS9918_INTERNAL_H
#define TMS9918_INTERNAL_H

/* Zero-page hot globals are declared in the public headers (random.h,
 * vsync.h, tms9918.h) for the consumer-facing API; the matching
 * `#pragma zpsym` directives live here so the .c files producing those
 * globals see them as ZEROPAGE storage rather than .bss.
 *
 * The actual ZP placement is done by the project's linker config via
 * `#pragma bss-name(push, "ZEROPAGE")` around the definition (see each
 * .c file). cc65 still needs `zpsym` in *every* TU that references the
 * symbol indexed.
 */

/* tms9918.c — text cursor + last-written register cache */
#pragma zpsym("tms_cursor_x")
#pragma zpsym("tms_cursor_y")
#pragma zpsym("tms_reverse")

/* vsync.c — frame counter (updated on every vsync_wait()) */
#pragma zpsym("vsync_frames")

/* random.c — LFSR state for rand8 / rand16 */
#pragma zpsym("rand8_state")
#pragma zpsym("rand16_state")

#endif /* TMS9918_INTERNAL_H */
