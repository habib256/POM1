; ============================================================================
; chess_text_io.asm -- algebraic move parser and feedback strings
; ============================================================================
; Public symbols:
;   read_player_move   -- prompt user, parse "E2E4" or "E7E8Q", return CC=ok
;                         and (mv_from, mv_to, mv_promo) populated.
;                         Carry set if user typed an unrecognised line.
;   error_message_ax   -- given A=error code, return string ptr in A:X.
;
; Engine imports: mv_from, mv_to, mv_promo (BSS, populated on success).
; Apple-1 imports: KBD, KBDCR, ECHO via apple1.inc.
; Library imports: wait_key (kbd.asm), print_str_ax (print.asm).
;
; Parse rules:
;   - 4 chars: file rank file rank, e.g. E2E4
;   - 5th char optional: promotion piece (Q/R/B/N)
;   - Special: "OO" (or "O-O") = short castle, "OOO" = long castle (v0.2+)
;   - Special: "Q" alone = quit, "U" alone = undo (handled by caller)
;   - Anything else → reject with carry set.
;
; Apple-1 keyboard forces uppercase, so we compare against uppercase chars.
; ============================================================================

.include "apple1.inc"
.include "chess_common.inc"

.import wait_key
.import print_str_ax
.import mv_from, mv_to, mv_promo, mv_flags
.import board, side_to_move

.export read_player_move
.export read_player_command
.export error_message_ax
.export print_square_ax

.importzp tmp, tmp2

.code

; ============================================================================
; read_player_command -- read one keystroke for top-level menu
; ============================================================================
; Returns A = uppercase ASCII of key pressed (echoed for feedback).
read_player_command:
        JSR wait_key
        ; Echo the keystroke for visual feedback.
        PHA
        ORA #$80
        JSR ECHO
        PLA
        RTS

; ============================================================================
; read_player_move -- read 4-5 chars and parse to (mv_from, mv_to, mv_promo)
; ============================================================================
; Output:
;   carry clear: success, mv_from/mv_to/mv_promo set
;   carry set:   parse error or special command (see A code below)
;     A = $01  user typed Q (quit/exit to menu)
;     A = $02  user typed U (undo)
;     A = $03  malformed input
read_player_move:
        LDA #$00
        STA mv_promo
        STA mv_flags
        ; --- char 1: file letter A..H, OR special command Q/U ---
@c1:    JSR wait_key
        CMP #'Q'
        BNE @notq
        ; Echo and bail with code 1.
        ORA #$80
        JSR ECHO
        JSR newline
        SEC
        LDA #$01
        RTS
@notq:  CMP #'U'
        BNE @notu
        ORA #$80
        JSR ECHO
        JSR newline
        SEC
        LDA #$02
        RTS
@notu:  CMP #'D'
        BNE @notd
        ; D = depth toggle (handled by caller); echo and bail with code 4.
        ORA #$80
        JSR ECHO
        JSR newline
        SEC
        LDA #$04
        RTS
@notd:  CMP #'C'
        BNE @notcc
        ORA #$80
        JSR ECHO
        JSR newline
        SEC
        LDA #$05            ; cursor-mode toggle (TMS/HGR variants only)
        RTS
@notcc: CMP #'P'
        BNE @notp
        ORA #$80
        JSR ECHO
        JSR newline
        SEC
        LDA #$06            ; perft self-test
        RTS
@notp:  CMP #'A'
        BNE @notai
        ORA #$80
        JSR ECHO
        JSR newline
        SEC
        LDA #$07            ; AI plays this turn
        RTS
@notai: CMP #'M'
        BNE @nota
        ORA #$80
        JSR ECHO
        JSR newline
        SEC
        LDA #$08            ; cycle play mode (HvH / W=AI / B=AI / AvA)
        RTS
@nota:  CMP #'O'
        BEQ @go_castle
        JMP @noto
@go_castle:
        ; Castling input: 'O' then optional more 'O's. Echo and parse.
        ORA #$80
        JSR ECHO
        JSR wait_key
        CMP #'O'
        BEQ @ok_o2
        ; Single 'O' is malformed.
        JMP @bad
@ok_o2:
        ORA #$80
        JSR ECHO
        ; We have OO so far. Read one more, expect O for queenside, else CR/anything = kingside.
        JSR wait_key
        CMP #'O'
        BNE @castle_ks
        ORA #$80
        JSR ECHO
        ; OOO = queenside castling. Consume optional CR.
        LDA #MV_FLAG_CASTLE_Q
        STA mv_flags
        JMP @castle_done
@castle_ks:
        ; Last char is anything else; keep it as a "completion" trigger.
        ; Tag move as kingside castle.
        PHA                   ; stash whatever the trailing char was
        LDA #MV_FLAG_CASTLE_K
        STA mv_flags
        PLA
        ; If the trailing char is CR or space, just complete the line silently.
        ; Otherwise accept it as the completion (no echo for non-printables).
@castle_done:
        ; Synthesise mv_from / mv_to so apply_user_move sees them.
        ; King is on E1 (white) or E8 (black); destination is G1/G8 (KS) or C1/C8 (QS).
        LDA side_to_move
        BNE @cb
        LDA #$04            ; E1 = $04
        STA mv_from
        LDA mv_flags
        AND #MV_FLAG_CASTLE_Q
        BEQ @ksw
        LDA #$02            ; C1 = $02 (queenside)
        JMP @stwt
@ksw:   LDA #$06            ; G1 = $06 (kingside)
@stwt:  STA mv_to
        JMP @castle_finish
@cb:    LDA #$74            ; E8 = $74
        STA mv_from
        LDA mv_flags
        AND #MV_FLAG_CASTLE_Q
        BEQ @ksb
        LDA #$72
        JMP @stbt
@ksb:   LDA #$76
@stbt:  STA mv_to
@castle_finish:
        LDA #$00
        STA mv_promo
        JSR newline
        CLC
        RTS
@noto:
        JSR is_file
        BCC @nf_ok
        JMP @bad
@nf_ok:
        STA tmp             ; tmp = file 0..7
        LDA tmp
        CLC
        ADC #'A'
        ORA #$80
        JSR ECHO

        ; --- char 2: rank digit 1..8 ---
        JSR wait_key
        JSR is_rank
        BCC @ok_c2
        JMP @bad
@ok_c2:
        STA tmp2            ; tmp2 = rank 0..7
        LDA tmp2
        CLC
        ADC #'1'
        ORA #$80
        JSR ECHO
        ; Compose mv_from = rank*16 + file
        LDA tmp2
        ASL A
        ASL A
        ASL A
        ASL A
        CLC
        ADC tmp
        STA mv_from

        ; --- char 3: file ---
        JSR wait_key
        JSR is_file
        BCC @ok_c3
        JMP @bad
@ok_c3:
        STA tmp
        LDA tmp
        CLC
        ADC #'A'
        ORA #$80
        JSR ECHO

        ; --- char 4: rank ---
        JSR wait_key
        JSR is_rank
        BCC @ok_c4
        JMP @bad
@ok_c4:
        STA tmp2
        LDA tmp2
        CLC
        ADC #'1'
        ORA #$80
        JSR ECHO
        LDA tmp2
        ASL A
        ASL A
        ASL A
        ASL A
        CLC
        ADC tmp
        STA mv_to

        ; --- Auto-detect castling (king moves 2 squares horizontally) ---
        ; Standard algebraic notation: type "E1G1" for kingside or "E1C1" for
        ; queenside. The OO / OOO syntax is also accepted (handled earlier).
        LDX mv_from
        LDA board,X
        AND #PIECE_MASK
        CMP #PIECE_KING
        BNE @no_autocastle
        LDA mv_to
        SEC
        SBC mv_from
        CMP #$02
        BEQ @auto_ks
        CMP #$FE                ; -2 (two's complement)
        BEQ @auto_qs
        JMP @no_autocastle
@auto_ks:
        LDA #MV_FLAG_CASTLE_K
        STA mv_flags
        JMP @no_autocastle
@auto_qs:
        LDA #MV_FLAG_CASTLE_Q
        STA mv_flags
@no_autocastle:

        ; --- Determine if a promotion choice is required ---
        ; Promotion needed iff: piece at mv_from is a pawn AND mv_to is on
        ; rank 1 (black promo) or rank 8 (white promo).
        LDX mv_from
        LDA board,X
        AND #PIECE_MASK
        CMP #PIECE_PAWN
        BNE @no_promo_needed
        LDA mv_to
        AND #$70
        BEQ @needs_promo            ; rank 0 → black promo
        CMP #$70
        BEQ @needs_promo            ; rank 7 → white promo
@no_promo_needed:
        ; No promotion. Move is complete after 4 chars — return immediately.
        ; (User does NOT need to press RETURN. Big UX win.)
        JSR newline
        CLC
        RTS
@needs_promo:
        ; Tell the user we want a promotion choice.
        LDA #'='
        ORA #$80
        JSR ECHO
        JMP @c5

        ; Trampoline for the early-exit @bad path (BCS branches above
        ; otherwise overshoot the ±127 byte branch range).
@bad_tr:
        JMP @bad

        ; --- char 5: promotion piece (Q R B N), only reached if needed ---
@c5:    JSR wait_key
        CMP #'Q'
        BNE @ntq
        LDA #PIECE_QUEEN
        STA mv_promo
        LDA #'Q'
        JMP @echo_done
@ntq:   CMP #'R'
        BNE @ntr
        LDA #PIECE_ROOK
        STA mv_promo
        LDA #'R'
        JMP @echo_done
@ntr:   CMP #'B'
        BNE @ntb
        LDA #PIECE_BISHOP
        STA mv_promo
        LDA #'B'
        JMP @echo_done
@ntb:   CMP #'N'
        BNE @c5             ; invalid — re-prompt (silently)
        LDA #PIECE_KNIGHT
        STA mv_promo
        LDA #'N'
@echo_done:
        ORA #$80
        JSR ECHO
@done:
        JSR newline
        CLC
        RTS
@bad:
        JSR newline
        SEC
        LDA #$03
        RTS

; --- helpers ---------------------------------------------------------------

; is_file: A in. If A in 'A'..'H', return A=file (0..7), CC. Else CS.
is_file:
        CMP #'A'
        BCC @no
        CMP #'I'
        BCS @no
        SEC
        SBC #'A'
        CLC
        RTS
@no:    SEC
        RTS

; is_rank: A in. If A in '1'..'8', return A=rank (0..7), CC. Else CS.
is_rank:
        CMP #'1'
        BCC @no
        CMP #'9'
        BCS @no
        SEC
        SBC #'1'
        CLC
        RTS
@no:    SEC
        RTS

newline:
        LDA #$8D
        JSR ECHO
        RTS

; ============================================================================
; print_square_ax -- print a 0x88 square as algebraic, e.g. "E4"
; ============================================================================
; Input: A = 0x88 square (assumed on-board).
print_square_ax:
        PHA
        AND #$07            ; file
        CLC
        ADC #'A'
        ORA #$80
        JSR ECHO
        PLA
        LSR A
        LSR A
        LSR A
        LSR A
        AND #$07            ; rank
        CLC
        ADC #'1'
        ORA #$80
        JSR ECHO
        RTS

; ============================================================================
; error_message_ax -- A = error code, return string ptr in A:X
; ============================================================================
error_message_ax:
        CMP #ERR_EMPTY
        BNE @n1
        LDA #<msg_empty
        LDX #>msg_empty
        RTS
@n1:    CMP #ERR_WRONG_COLOR
        BNE @n2
        LDA #<msg_wrong
        LDX #>msg_wrong
        RTS
@n2:    CMP #ERR_BAD_GEOMETRY
        BNE @n3
        LDA #<msg_geom
        LDX #>msg_geom
        RTS
@n3:    CMP #ERR_KING_IN_CHECK
        BNE @n4
        LDA #<msg_chk
        LDX #>msg_chk
        RTS
@n4:    CMP #ERR_NOT_IMPL
        BNE @n5
        LDA #<msg_nyi
        LDX #>msg_nyi
        RTS
@n5:    LDA #<msg_unk
        LDX #>msg_unk
        RTS

msg_empty:  .byte $0D, " EMPTY SQUARE.", $0D, 0
msg_wrong:  .byte $0D, " NOT YOUR PIECE.", $0D, 0
msg_geom:   .byte $0D, " ILLEGAL MOVE.", $0D, 0
msg_chk:    .byte $0D, " WOULD LEAVE KING IN CHECK.", $0D, 0
msg_nyi:    .byte $0D, " NOT YET IMPLEMENTED.", $0D, 0
msg_unk:    .byte $0D, " ?", $0D, 0
