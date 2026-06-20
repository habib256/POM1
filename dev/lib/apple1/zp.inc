; ============================================================================
; zp.inc -- shared zero-page convention for lib/apple1 + lib/m6502
; ============================================================================
; *** DANGER -- POSITIONAL CONTRACT. ***  This file is a shared GLOBAL resource:
; it claims the 8 zero-page bytes $00-$07, in the fixed declaration order below.
; A collision does NOT raise an assembler/linker error -- it silently corrupts
; whoever else parked a variable in that range, surfacing as a runtime memory
; bug. The two rules that keep you safe: (1) `.include "zp.inc"` BEFORE any
; other ZP `.res`, so the layout still starts at $00; (2) if ZP is tight, ALIAS
; a slot onto existing scratch BEFORE the include (the `.ifndef` guards detect
; it). The fixed $00-$07 layout is PINNED by dev/lib/apple1/test/ (zp_layout) --
; if you reorder or insert a slot here, that test must move in lockstep.
; ----------------------------------------------------------------------------
; Optional, opt-in. Include ONCE near the top of your .asm to declare the
; canonical scratch slots used across the .include-style libs (print, kbd,
; delay, print_num, multiply, prng8, prng16) AND to .exportzp them so
; separately-compiled libs (lib/m6502/math.asm, lib/tms9918/tms9918m2.asm)
; can `.importzp` them at link time.
;
; Two mechanisms unified:
;   - Textual includes (Mechanism A) — print.asm / multiply.asm / prng8.asm /
;     prng16.asm each guard their own .res 1 with .ifndef. zp.inc pre-declares
;     the slot, the .ifndef inside each lib then skips the duplicate alloc.
;   - Cross-object .importzp (Mechanism B) — math.o / tms9918m2.o expect
;     `tmp`, `tmp2` (and math.o expects more — see its README) to be exported
;     by the project's translation unit. zp.inc emits .exportzp directives
;     so the linker can resolve those imports without manual work.
;
; Layout (8 bytes from $00, declaration order):
;   $00 tmp           -- general scratch (kbd.asm, math.asm, tms9918m2.asm)
;   $01 tmp2          -- general scratch (math.asm, tms9918m2.asm)
;   $02 print_ptr_lo  -- lib/apple1/print.asm
;   $03 print_ptr_hi
;   $04 mul_tmp       -- lib/m6502/multiply.asm
;   $05 mul_res0
;   $06 prng_lo       -- lib/m6502/prng8.asm AND lib/m6502/prng16.asm
;   $07 prng_hi
;
; Usage rules:
;   1. `.include "zp.inc"` MUST come BEFORE any other ZP allocation in your
;      project, otherwise the layout shifts away from $00.
;   2. ZP-tight projects can ALIAS any subset of these slots onto existing
;      scratch BEFORE the include — the per-slot .ifndef guards detect the
;      pre-existing definition and skip the .res 1:
;
;          str_lo:  .res 1     ; project's existing scratch
;          str_hi:  .res 1
;          print_ptr_lo = str_lo
;          print_ptr_hi = str_hi
;          .include "zp.inc"           ; reuses str_lo/str_hi for print
;
;   3. zp.inc deliberately does NOT include apple1.inc. Projects do
;      `.include "apple1.inc" / .include "zp.inc"` separately, mirroring
;      print.asm's convention (the old comment block in print.asm:32-33
;      explains why: avoid duplicate-symbol errors in projects that
;      declare ECHO inline).
;
; Heavy libs (math.asm, tms9918m2.asm) own additional ZP slots beyond these
; 8 bytes; their callers still declare/export those manually. This file
; covers ONLY the shared baseline.
; ============================================================================

.ifndef _ZP_INC_LOADED_
_ZP_INC_LOADED_ = 1

.segment "ZEROPAGE"

.ifndef tmp
tmp:            .res 1
.endif
.ifndef tmp2
tmp2:           .res 1
.endif
.ifndef print_ptr_lo
print_ptr_lo:   .res 1
.endif
.ifndef print_ptr_hi
print_ptr_hi:   .res 1
.endif
.ifndef mul_tmp
mul_tmp:        .res 1
.endif
.ifndef mul_res0
mul_res0:       .res 1
.endif
.ifndef prng_lo
prng_lo:        .res 1
.endif
.ifndef prng_hi
prng_hi:        .res 1
.endif

; Export every slot so separately-compiled libs (math.o, tms9918m2.o) can
; resolve their .importzp at link. Harmless for projects that only use
; textual includes — ld65 silently ignores unused exports.
.exportzp tmp, tmp2
.exportzp print_ptr_lo, print_ptr_hi
.exportzp mul_tmp, mul_res0
.exportzp prng_lo, prng_hi

.endif  ; _ZP_INC_LOADED_
