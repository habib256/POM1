; ----------------------------------------------------------------------------
; apple1-videocard-lib — POM1 CodeTank cc65 port
; Derived from nippur72/apple1-videocard-lib (Antonino "Nino" Porcino).
;   https://github.com/nippur72/apple1-videocard-lib
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
        jsr     ECHO
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
