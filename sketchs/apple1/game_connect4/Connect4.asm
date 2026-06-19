; =============================================
; CONNECT 4 (text mode) for Apple 1
; VERHILLE Arnaud - 2026
; Two-player drop-piece game, 7x6 grid
; =============================================
; Assemble:
;   Build: make
;
; Load via File > Load Memory (Connect4.txt), 280R.
;
; Controls: 1-7 drop piece in column, R restarts the game.
; Red starts.  First to align four wins (row / column / diagonal).
; =============================================

.include "apple1.inc"

NCOLS   = 7
NROWS   = 6
NCELLS  = 42        ; 7 * 6

TILE_EMPTY  = 0
TILE_RED    = 1
TILE_YELLOW = 2

; Grid storage. Must sit in RAM that is available under EVERY supported
; preset, including the Juke-Box (RAM16/ROM32 maps $4000-$BFFF as ROM, so
; the original $4000 vanished into the JukeBox firmware). $0F00 lives in
; the 4 kB stock Apple-1 RAM, well above the program code ($0280..~$067D),
; and stays RAM regardless of which expansion card is plugged.
GRID_BASE = $0F00

; --- Zero page ---
.zeropage
temp:           .res 1  ; $00
temp2:          .res 1  ; $01
temp3:          .res 1  ; $02
temp4:          .res 1  ; $03  offset for check_4_at_x
str_lo:         .res 1  ; $04
str_hi:         .res 1  ; $05
current_player: .res 1  ; $06
move_count:     .res 1  ; $07
winner:         .res 1  ; $08
last_row:       .res 1  ; $09  (reserved — no current reader; see @place)
last_col:       .res 1  ; $0A
row_cnt:        .res 1  ; $0B
col_cnt:        .res 1  ; $0C
render_r:       .res 1  ; $0D
render_c:       .res 1  ; $0E

.code

; =============================================
; MAIN
; =============================================
main:
        LDA #<str_title
        LDX #>str_title
        JSR print_str_ax
        JSR wait_key

new_game:
        ; Clear grid
        LDY #$00
        TYA
@clr:   STA GRID_BASE,Y
        INY
        CPY #NCELLS
        BNE @clr

        LDA #TILE_RED
        STA current_player
        LDA #$00
        STA move_count
        STA winner

        JSR render_screen

move_loop:
        JSR wait_key

        CMP #'R'
        BNE @not_r
        JMP new_game
@not_r:
        CMP #'1'
        BCC move_loop
        CMP #'8'
        BCS move_loop
        ; key 1..7 -> column 0..6
        SEC
        SBC #'1'

        JSR drop_piece
        CMP #$00
        BEQ move_loop           ; column full

        INC move_count

        JSR check_win
        STA winner
        CMP #$00
        BNE @game_won

        ; Draw if board full
        LDA move_count
        CMP #NCELLS
        BCS @draw

        ; Toggle player (RED=1 <-> YELLOW=2 via XOR #3)
        LDA current_player
        EOR #$03
        STA current_player

        JSR render_screen
        JMP move_loop

@game_won:
        JSR render_screen
        LDA winner
        CMP #TILE_RED
        BNE @y_wins
        LDA #<str_red_wins
        LDX #>str_red_wins
        JMP @show_over
@y_wins:
        LDA #<str_yellow_wins
        LDX #>str_yellow_wins
@show_over:
        JSR print_str_ax
        JSR wait_key
        JMP new_game

@draw:
        JSR render_screen
        LDA #<str_draw
        LDX #>str_draw
        JSR print_str_ax
        JSR wait_key
        JMP new_game

; =============================================
; drop_piece: drop current_player's piece in col A
; Input: A = column (0..6)
; Returns: A=1 on success (piece placed), A=0 if column full
; =============================================
drop_piece:
        STA last_col
        ; Find lowest empty row in this column
        LDX #NROWS-1            ; start at row 5, go up
@find:
        LDA row_x7,X
        CLC
        ADC last_col
        TAY                     ; Y = cell idx
        LDA GRID_BASE,Y
        BEQ @place
        DEX
        BPL @find
        ; Column full
        LDA #$00
        RTS
@place:
        LDA current_player
        STA GRID_BASE,Y
        LDA #$01
        RTS

; =============================================
; check_win: scan grid for 4 in a row in any direction
; Returns A=winner (1 or 2), or 0 if none
; =============================================
check_win:
        ; Horizontal (offset 1), rows 0..5, cols 0..3
        LDA #$01
        STA temp4
        LDA #$00
        STA row_cnt
@h_row:
        LDA #$00
        STA col_cnt
@h_col:
        LDX row_cnt
        LDA row_x7,X
        CLC
        ADC col_cnt
        TAX
        JSR check_4_at_x
        CMP #$00
        BNE @win_tr
        INC col_cnt
        LDA col_cnt
        CMP #$04
        BCC @h_col
        INC row_cnt
        LDA row_cnt
        CMP #$06
        BCC @h_row

        JMP @v_start
@win_tr:
        JMP @winner
@v_start:
        ; Vertical (offset 7), rows 0..2, cols 0..6
        LDA #$07
        STA temp4
        LDA #$00
        STA row_cnt
@v_row:
        LDA #$00
        STA col_cnt
@v_col:
        LDX row_cnt
        LDA row_x7,X
        CLC
        ADC col_cnt
        TAX
        JSR check_4_at_x
        CMP #$00
        BNE @win_tr
        INC col_cnt
        LDA col_cnt
        CMP #$07
        BCC @v_col
        INC row_cnt
        LDA row_cnt
        CMP #$03
        BCC @v_row

        ; Diagonal \ (down-right, offset 8), rows 0..2, cols 0..3
        LDA #$08
        STA temp4
        LDA #$00
        STA row_cnt
@d1_row:
        LDA #$00
        STA col_cnt
@d1_col:
        LDX row_cnt
        LDA row_x7,X
        CLC
        ADC col_cnt
        TAX
        JSR check_4_at_x
        CMP #$00
        BNE @win_tr
        INC col_cnt
        LDA col_cnt
        CMP #$04
        BCC @d1_col
        INC row_cnt
        LDA row_cnt
        CMP #$03
        BCC @d1_row

        ; Diagonal / (down-left, offset 6), rows 0..2, cols 3..6
        LDA #$06
        STA temp4
        LDA #$00
        STA row_cnt
@d2_row:
        LDA #$03
        STA col_cnt
@d2_col:
        LDX row_cnt
        LDA row_x7,X
        CLC
        ADC col_cnt
        TAX
        JSR check_4_at_x
        CMP #$00
        BNE @win_tr
        INC col_cnt
        LDA col_cnt
        CMP #$07
        BCC @d2_col
        INC row_cnt
        LDA row_cnt
        CMP #$03
        BCC @d2_row

        LDA #$00
@winner:
        RTS

; =============================================
; check_4_at_x: check if 4 cells in direction temp4 from idx X are all same non-zero
; Input: X = start cell idx, temp4 = direction offset
; Output: A = winning value (1 or 2), or 0
; =============================================
check_4_at_x:
        LDA GRID_BASE,X
        BEQ @no
        STA temp                ; candidate value
        TXA
        CLC
        ADC temp4
        TAX
        LDA GRID_BASE,X
        CMP temp
        BNE @no
        TXA
        CLC
        ADC temp4
        TAX
        LDA GRID_BASE,X
        CMP temp
        BNE @no
        TXA
        CLC
        ADC temp4
        TAX
        LDA GRID_BASE,X
        CMP temp
        BNE @no
        LDA temp
        RTS
@no:
        LDA #$00
        RTS

; =============================================
; render_screen: draw frame (blank + header + grid + status)
; =============================================
render_screen:
        LDA #$8D
        JSR ECHO                ; leading blank line

        ; Column-number header
        LDA #<str_cols
        LDX #>str_cols
        JSR print_str_ax

        ; Top separator
        LDA #<str_sep
        LDX #>str_sep
        JSR print_str_ax

        ; 6 grid rows
        LDA #$00
        STA render_r
@rowlp:
        ; Left indent + '|'
        LDA #<str_indent
        LDX #>str_indent
        JSR print_str_ax

        LDA #$00
        STA render_c
@collp:
        LDX render_r
        LDA row_x7,X
        CLC
        ADC render_c
        TAX
        LDA GRID_BASE,X
        TAY                     ; Y = tile index
        ; Cell content is 3 chars wide to match "+---+" separator spacing:
        ; leading space, char, trailing space, then '|'
        LDA #$A0
        JSR ECHO
        LDA tile_char,Y
        ORA #$80
        JSR ECHO
        LDA #$A0
        JSR ECHO
        LDA #$FC                ; '|' with bit 7 = $7C | $80
        JSR ECHO

        INC render_c
        LDA render_c
        CMP #NCOLS
        BCC @collp

        LDA #$8D
        JSR ECHO
        INC render_r
        LDA render_r
        CMP #NROWS
        BCC @rowlp

        ; Bottom separator
        LDA #<str_sep
        LDX #>str_sep
        JSR print_str_ax

        ; Status line: current player
        LDA current_player
        CMP #TILE_RED
        BNE @p_y
        LDA #<str_red_turn
        LDX #>str_red_turn
        JMP @do_status
@p_y:
        LDA #<str_yellow_turn
        LDX #>str_yellow_turn
@do_status:
        JSR print_str_ax
        RTS

; =============================================
; wait_key: returns ASCII in A (bit 7 stripped)
; =============================================
.include "kbd.asm"      ; wait_key, poll_key (Tier 2 mutualization)

; =============================================
; print_str_ax — promoted to dev/lib/apple1/print.asm (Tier 2 mutualization).
; =============================================
.include "print.asm"

; =============================================
; DATA
; =============================================

; row * 7 for rows 0..5
row_x7:
        .byte 0, 7, 14, 21, 28, 35

; ASCII char per tile type
tile_char:
        .byte ' ', 'R', 'Y'

; --- Strings ---
str_title:
        .byte $0D, " * CONNECT 4 *", $0D
        .byte " APPLE 1 TEXT MODE", $0D
        .byte " BY VERHILLE ARNAUD, 2026", $0D
        .byte $0D
        .byte " RED (R) PLAYS FIRST.  DROP A", $0D
        .byte " PIECE WITH KEYS 1-7.  ALIGN", $0D
        .byte " FOUR IN A ROW (H/V/DIAG) TO", $0D
        .byte " WIN.  R RESTARTS THE GAME.", $0D
        .byte $0D, " PRESS ANY KEY TO START...", $0D, 0

str_cols:
        .byte "         1   2   3   4   5   6   7", $0D, 0

str_sep:
        .byte "       +---+---+---+---+---+---+---+", $0D, 0

str_indent:
        .byte "       |", 0

str_red_turn:
        .byte $0D, " RED'S TURN   (1-7 / R=RESTART)", $0D, 0

str_yellow_turn:
        .byte $0D, " YELLOW'S TURN   (1-7 / R=RESTART)", $0D, 0

str_red_wins:
        .byte $0D, " **** RED WINS! **** PRESS A KEY", $0D, 0

str_yellow_wins:
        .byte $0D, " *** YELLOW WINS! *** PRESS A KEY", $0D, 0

str_draw:
        .byte $0D, " *** DRAW. *** PRESS A KEY", $0D, 0
