; ============================================================================
; print.asm -- ASCIIZ string output for Apple-1 (Wozmon ECHO at $FFEF)
; ============================================================================
; Single tiny routine that was previously copy-pasted into ~15 projects:
; print a NUL-terminated string via Wozmon ECHO. Lives in dev/lib/apple1/
; so every project can `.include "print.asm"` (with the existing
; `-I ../../lib/apple1` Makefile flag) instead of carrying a private copy.
;
; Drop-in semantics — identical to every previous inline copy:
;   Input    A = low byte, X = high byte of pointer to NUL-terminated string.
;   Output   prints each byte ORed with $80 to ECHO ($FFEF), stops at $00.
;            No length cap — when Y wraps past 255 the high byte of the
;            pointer is bumped so strings can span pages.
;   Clobbers A, Y. X preserved. print_ptr_hi is also modified for long strings.
;
; ZP usage   2 bytes named print_ptr_lo / print_ptr_hi.
;            BY DEFAULT this module reserves a fresh pair in the caller's
;            ZEROPAGE segment. Three ways to integrate:
;
;            (a) Use the unified ZP convention. `.include "zp.inc"` once
;                before any other ZP allocation — it pre-declares
;                print_ptr_lo / print_ptr_hi (and friends) at $02/$03,
;                and the .ifndef guard below skips the duplicate alloc.
;
;            (b) Stand-alone: just `.include "print.asm"`. The .ifndef
;                allocates two fresh bytes in your ZEROPAGE segment.
;
;            (c) ZP-tight projects (.cfg ZP region already maxed out)
;                can alias the slot pair to existing scratch BEFORE the
;                include:
;
;                    print_ptr_lo = str_lo
;                    print_ptr_hi = str_hi
;                    .include "print.asm"
;
;                The .ifndef detects the alias and skips reservation.
;
; Caller does:  LDA #<msg / LDX #>msg / JSR print_str_ax
;
; Caller responsibility: ECHO ($FFEF) must already be defined in scope —
; either via inline equate (`ECHO = $FFEF`) or via `.include "apple1.inc"`.
; This module deliberately does NOT include apple1.inc itself, so projects
; that already declare ECHO inline don't get a duplicate-symbol error.
; ============================================================================

.ifndef print_ptr_lo
.segment "ZEROPAGE"
print_ptr_lo:   .res 1
print_ptr_hi:   .res 1
.endif

.segment "CODE"

print_str_ax:
        STA     print_ptr_lo
        STX     print_ptr_hi
        LDY     #$00
@lp:    LDA     (print_ptr_lo),Y
        BEQ     @done
        ORA     #$80
        JSR     ECHO
        INY
        BNE     @lp
        INC     print_ptr_hi    ; Y wrapped → advance pointer to next page
        BNE     @lp             ; always taken (high byte just got non-zero)
@done:  RTS
