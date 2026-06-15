; ----------------------------------------------------------------------------
; apple1io_asm.s — shared Apple-1 Wozmon text + keyboard hooks for cc65.
;
; Named *_asm.s (not apple1io.s) on purpose: cc65 compiles apple1io.c through an
; intermediate apple1io.s, so a hand-written apple1io.s would be clobbered. This
; mirrors nippur72/apple1-videocard-lib's apple1.c + apple1_asm.s split.
;
; Asm shim ported verbatim from nippur72/apple1-videocard-lib (Antonino "Nino"
;   Porcino).  https://github.com/nippur72/apple1-videocard-lib
; Upstream license unspecified at fork time (2026); preserve attribution.
;
; cc65 calling convention: an `unsigned char` argument arrives in A, an
; `unsigned char` return value leaves in A — so each routine is a thin wrapper
; around the WOZ Monitor ROM entry points.
; ----------------------------------------------------------------------------
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
