; ----------------------------------------------------------------------------
; P-LAB TMS9918 (Apple-1) — cc65 C runtime, POM1 CodeTank port
; Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
; Software: derived from Antonino "Nino" Porcino's apple1-videocard-lib
;   (https://github.com/nippur72/apple1-videocard-lib).
; Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
; ----------------------------------------------------------------------------
;
; Apple-1 Wozmon hooks — P-LAB / POM1 CodeTank preset
.export _woz_putc, _woz_print_hex, _woz_mon, _woz_mon_silent, _apple1_getkey

ECHO    = $FFEF
PRBYTE  = $FFDC
; $FF1A is the Wozmon PROMPT entry: prints "\" + CR then drops into the line
; editor — the house rule (dev/lib/apple1/apple1.inc). woz_mon() therefore
; leaves the familiar "\" prompt, matching dev/lib/apple1c and dev/lib/gen2c
; (this runtime historically jumped $FF1F — the silent post-prompt warm
; restart, which looks like a hang to the user; unified June 2026).
; woz_mon_silent() keeps $FF1F for callers that just printed their own
; status line and genuinely want the silent restart.
WOZMON  = $FF1A
WOZ_RST = $FF1F
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

_woz_mon_silent:
        jmp     WOZ_RST

_apple1_getkey:
wait:   lda     KEYCR
        bpl     wait
        lda     KEYDATA
        and     #$7F
        rts
