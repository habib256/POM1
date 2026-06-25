; ----------------------------------------------------------------------------
; P-LAB TMS9918 (Apple-1) — cc65 C runtime, POM1 CodeTank port
; Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
; Software: derived from Antonino "Nino" Porcino's apple1-videocard-lib
;   (https://github.com/nippur72/apple1-videocard-lib).
; Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
; ----------------------------------------------------------------------------
;
; Apple-1 Wozmon hooks — P-LAB / POM1 CodeTank preset
.export _woz_putc, _woz_print_hex, _woz_mon, _apple1_getkey

ECHO    = $FFEF
PRBYTE  = $FFDC
WOZMON  = $FF1F
KEYCR   = $D011
KEYDATA = $D010

_woz_putc:
        ora     #$80            ; WozMon ECHO does not set bit 7; the PIA display
        jsr     ECHO            ; latches only when PB7=1, so OR it in (cf. print.asm)
        rts

_woz_print_hex:
        jsr     PRBYTE
        rts

_woz_mon:
        jmp     WOZMON

_apple1_getkey:
wait:   lda     KEYCR
        bpl     wait
        lda     KEYDATA
        and     #$7F
        rts
