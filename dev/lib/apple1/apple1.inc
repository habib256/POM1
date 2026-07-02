; ============================================================================
; apple1.inc -- shared hardware equates for Apple-1 programs
; ============================================================================
; Include this from any .asm that talks to Apple-1 ROM/PIA I/O. Machine
; equates plus the APPLE1_PREAMBLE reset macro (emits no code until invoked),
; no segments. Safe to include from any segment.
;
; Idempotent — the outer .ifndef guard makes multiple .include "apple1.inc"
; in the same TU a no-op (e.g. when the project includes it AND a textual
; library include also pulls it in).
;
; cc65 include path: dev/lib/apple1/
; ============================================================================

.ifndef _APPLE1_INC_LOADED_
_APPLE1_INC_LOADED_ = 1

; --- Apple 1 I/O (Wozmon ROM + PIA 6821) -----------------------------------
ECHO     = $FFEF        ; Wozmon character output (A = char | $80)
PRBYTE   = $FFDC        ; Wozmon: print A as two hex digits
PRHEX    = $FFE5        ; Wozmon: print low nibble of A as one hex digit
KBD      = $D010        ; PIA: keyboard data (read clears strobe)
KBDCR    = $D011        ; PIA: keyboard control (bit 7 = key ready)
DSP      = $D012        ; PIA: display data + busy (bit 7)
DSPCR    = $D013        ; PIA: display control

; --- Wozmon return entries -------------------------------------------------
; $FF1A is the prompt entry: prints "\" + CR, then falls through to the
; line-editor wait loop. This is the canonical "return control to the
; monitor" target — `JMP WOZMON` from any program leaves a clean prompt.
;
; $FF1F is the post-prompt warm-restart: enters the line editor WITHOUT
; printing "\". Tempting (it's the label after `LDA #$8D / JSR ECHO`)
; but visually equivalent to a hang from the user's POV (no prompt
; visible). Use WOZ_RST only for explicit warm-restart paths where the
; caller has just printed its own status line. The gotcha is documented
; inline in sketchs/apple1/gt6144_demo_hello/GT1_Hello.asm:39-43.
WOZMON   = $FF1A        ; Wozmon prompt entry — prints "\" + CR
WOZ_RST  = $FF1F        ; Wozmon warm-restart (post-prompt entry, silent)

; --- Canonical reset preamble ------------------------------------------------
; Put APPLE1_PREAMBLE first in every program entered via Wozmon `xxxxR`.
; Wozmon's R command does NOT re-run the hardware reset path: whatever the
; previous program (or a BASIC session) left in the flags and stack pointer is
; still live. In particular the DECIMAL flag survives — if a prior program used
; SED (or crashed mid-BCD), every ADC/SBC in your code silently goes BCD and
; produces garbage arithmetic. Hence:
;   SEI        mask IRQs (P-LAB cards can have /INT wired to /IRQ)
;   CLD        clear a possibly stale decimal flag (the classic warm re-entry bug)
;   LDX #$FF   \ fresh full-depth stack — don't inherit a half-unwound SP
;   TXS        /
; Zero bytes unless invoked; existing inline preambles are equivalent.
.macro APPLE1_PREAMBLE
        sei                     ; mask IRQs
        cld                     ; clear stale decimal mode (warm re-entry!)
        ldx #$FF                ; fresh stack pointer
        txs
.endmacro

.endif  ; _APPLE1_INC_LOADED_
