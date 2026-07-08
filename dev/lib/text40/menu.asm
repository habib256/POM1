; ============================================================================
; menu.asm -- numbered menu selector with bounds + echo
; ============================================================================
; Promoted from the inline wait/dispatch loops of the CodeTank menus,
; which now .include this file (dev/codetank/game1_menu/ +
; demos_menu/ — see the README's Adoption section).
;
;   menu_select -- block on a digit in [min..max], echo it, return.
;     Inputs:   A = min digit (e.g. '1')
;               X = max digit (e.g. '4', '5', '9')
;     Output:   A = chosen key (bit 7 cleared)
;     Clobbers: A. X, Y preserved.
;
; Echo is done via ECHO ($FFEF) with bit 7 set so the user sees what
; they typed — matches the visual feedback in codetank_menu's
; "PICK 1, 2, 3 OR 4 ? " prompt followed by the digit on screen.
;
; ZP usage: 2 bytes (uses tmp + tmp2 from lib/apple1/zp.inc).
;   - Default: imports tmp/tmp2 via the unified convention. The caller
;     must `.include "zp.inc"` (or otherwise declare tmp/tmp2) before
;     this module.
;   - Stand-alone (no zp.inc): the caller can declare tmp/tmp2 in
;     ZEROPAGE before the .include and this module will use them.
;
; Caller responsibility:
;   - wait_key (lib/apple1/kbd.asm) and ECHO from apple1.inc in scope.
;   - tmp / tmp2 declared somewhere (typically via zp.inc).
; ============================================================================

.segment "CODE"

menu_select:
        STA     tmp                     ; tmp  = min digit
        STX     tmp2                    ; tmp2 = max digit
@w:     JSR     wait_key
        CMP     tmp
        BCC     @w                      ; key < min
        CMP     tmp2
        BEQ     @ok
        BCS     @w                      ; key > max
@ok:    PHA
        ORA     #$80
        JSR     ECHO
        PLA
        RTS
