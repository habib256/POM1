; ============================================================================
; kbd.asm -- Apple-1 keyboard primitives factored out of 15+ projects
; ============================================================================
; Two routines, no ZP usage. X and Y are preserved.
;
;   wait_key  -- block until a key is ready, return A = key with bit 7
;                cleared. Standard Apple-1 PIA bit-7 strobe convention.
;
;   poll_key  -- non-blocking read. A = key (bit 7 cleared) if a key was
;                pending, A = 0 otherwise. The 6502 Z flag reflects the
;                same: BEQ branches when no key was available.
;
; Caller responsibility: KBD ($D010) and KBDCR ($D011) must be in scope —
; either via inline equates or `.include "apple1.inc"`. This module does
; NOT re-include apple1.inc to avoid duplicate-symbol errors in projects
; that declare KBD/KBDCR inline.
;
; Drop in via `.include "kbd.asm"` (Makefile already passes -I ../../lib/apple1).
; ============================================================================

.segment "CODE"

; ----------------------------------------------------------------------------
; wait_key -- block until a key is ready. A = key with bit 7 cleared.
;             Clobbers A. X, Y preserved.
; ----------------------------------------------------------------------------
wait_key:
@wk:    LDA     KBDCR
        BPL     @wk             ; bit 7 = 0 → no key, keep polling
        LDA     KBD
        AND     #$7F            ; strip the PIA bit-7 strobe
        RTS

; ----------------------------------------------------------------------------
; poll_key -- non-blocking. A = key (bit 7 stripped) if KBDCR strobe set,
;             A = 0 otherwise. Z flag reflects the same.
;             Clobbers A. X, Y preserved.
; ----------------------------------------------------------------------------
poll_key:
        LDA     KBDCR
        BPL     @none           ; bit 7 = 0 → no key
        LDA     KBD
        AND     #$7F
        RTS
@none:  LDA     #$00
        RTS
