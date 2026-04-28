; ============================================================================
; repeat.asm -- repeat-character output for borders, padding, separators
; ============================================================================
;   repeat_char_ax -- print A (low 7 bits) X times via ECHO.
;     Inputs:   A = character (bit 7 ignored — set internally)
;               X = repeat count (0..255; 0 prints nothing)
;     Output:   X = 0
;     Clobbers: A. Y preserved.
;
; Models the GRAPH/GRAPHIT subroutine inlined in
; games_little_tower/LittleTower-1.0.asm:566-575 and the per-line
; "+---+---+---+..." build-up in games_connect4/Connect4.asm. Lets
; future projects build grids and separators without re-deriving the
; counter loop.
;
; No ZP usage — character lives on the 6502 stack across each ECHO call.
;
; Caller responsibility: ECHO ($FFEF) in scope (from apple1.inc).
; ============================================================================

.segment "CODE"

repeat_char_ax:
        CPX     #0
        BEQ     @done                   ; X = 0 → no-op
        ORA     #$80                    ; bit 7 for ECHO
@lp:    PHA
        JSR     ECHO
        PLA
        DEX
        BNE     @lp
@done:  RTS
