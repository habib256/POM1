; ============================================================================
; apple1.inc -- shared hardware equates for Apple-1 programs
; ============================================================================
; Include this from any .asm that talks to Apple-1 ROM/PIA I/O. Pure machine
; equates, no code, no segments. Safe to include from any segment.
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
; inline in dev/projects/gt6144_hello/GT1_Hello.asm:39-43.
WOZMON   = $FF1A        ; Wozmon prompt entry — prints "\" + CR
WOZ_RST  = $FF1F        ; Wozmon warm-restart (post-prompt entry, silent)

.endif  ; _APPLE1_INC_LOADED_
