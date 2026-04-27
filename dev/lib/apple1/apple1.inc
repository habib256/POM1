; ============================================================================
; apple1.inc -- shared hardware equates for Apple-1 programs
; ============================================================================
; Include this from any .asm that talks to Apple-1 ROM/PIA I/O. Pure machine
; equates, no code, no segments. Safe to include from any segment.
;
; cc65 include path: dev/lib/apple1/
; ============================================================================

; --- Apple 1 I/O (Wozmon ROM + PIA 6821) -----------------------------------
ECHO     = $FFEF        ; Wozmon character output (A = char | $80)
PRBYTE   = $FFDC        ; Wozmon: print A as two hex digits
PRHEX    = $FFE5        ; Wozmon: print low nibble of A as one hex digit
KBD      = $D010        ; PIA: keyboard data (read clears strobe)
KBDCR    = $D011        ; PIA: keyboard control (bit 7 = key ready)
DSP      = $D012        ; PIA: display data + busy (bit 7)
DSPCR    = $D013        ; PIA: display control
WOZ_RST  = $FF1F        ; Wozmon reset entry (warm restart, prints "\")
GETLINE  = $FF1F        ; alias for legacy code (Wozmon line editor entry)
