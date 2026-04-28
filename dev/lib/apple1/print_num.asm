; ============================================================================
; print_num.asm -- decimal byte output for Apple-1
; ============================================================================
;   print_byte_dec -- A = unsigned byte (0..255). Prints exactly three ASCII
;                     decimal digits via Wozmon ECHO ($FFEF). Leading zeros
;                     are emitted ("042", not " 42") — keeps column layout
;                     stable in tabular output. Clobbers A, X. Y preserved.
;
; Modelled on the inline print_3_digits from games_sokoban/Sokoban.asm:592
; (where it ships in 4 KB), promoted to lib/apple1 so any project can drop
; in via `.include "print_num.asm"`.
;
; No ZP usage — intermediate digits live on the 6502 stack via PHA/PLA.
;
; Caller responsibility: ECHO ($FFEF) must be in scope (inline equate or
; `.include "apple1.inc"`). This module does NOT re-include apple1.inc.
; ============================================================================

.segment "CODE"

; ----------------------------------------------------------------------------
; print_byte_dec -- A = byte (0..255), prints "000".."255" via ECHO.
;                   Clobbers A, X. Y preserved.
; ----------------------------------------------------------------------------
print_byte_dec:
        ; Hundreds digit -- subtract 100 until carry clear, X counts how many.
        LDX     #$00
@h:     CMP     #100
        BCC     @hd
        SBC     #100            ; C set by the CMP, no SEC needed
        INX
        JMP     @h
@hd:    PHA                     ; save remainder for the tens digit
        TXA
        ORA     #'0' | $80
        JSR     ECHO
        PLA

        ; Tens digit -- same pattern.
        LDX     #$00
@t:     CMP     #10
        BCC     @td
        SBC     #10
        INX
        JMP     @t
@td:    PHA
        TXA
        ORA     #'0' | $80
        JSR     ECHO
        PLA

        ; Units digit -- whatever's left in A.
        ORA     #'0' | $80
        JSR     ECHO
        RTS
