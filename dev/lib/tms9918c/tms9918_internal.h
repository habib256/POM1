/* tms9918_internal.h — private declarations shared by the tms9918c modules.
 * NOT included by any public *.h, NOT intended for consumer code.
 *
 * Mirror of gen2c/gen2_internal.h. Currently a scaffolding header: the
 * tms9918c port has no C↔asm ZP parameter blocks (its asm fast paths in
 * tms_fast.s use the cc65 software stack via popa/popax, not ZEROPAGE
 * parameters like gen2_blit.s does). The header is in place so a future
 * sprint can park `#pragma zpsym` directives for the hot-path globals
 * (tms_cursor_x/y/reverse, vsync_frames, rand*_state) once each one is
 * migrated to ZEROPAGE via `#pragma bss-name(push, "ZEROPAGE")` in its
 * defining .c + a tested runtime on preset 9 (CodeTank+TMS9918).
 *
 * The ZP move is a real win (~2 cycles per indexed access on the hot
 * scroll path) but it lives in a shared 256-byte address space with
 * Wozmon ($24-$2B) and the cc65 runtime, so it must be benchmarked
 * empirically, not just compiled.
 *
 * Anything declared here is a maintenance contract between the .c files
 * of dev/lib/tms9918c/. Consumer code keeps using the public headers
 * (random.h, vsync.h, sprite_shadow.h, tms9918.h, screen1.h, screen2.h).
 */

#ifndef TMS9918_INTERNAL_H
#define TMS9918_INTERNAL_H

/* (no entries yet — see header comment) */

#endif /* TMS9918_INTERNAL_H */
