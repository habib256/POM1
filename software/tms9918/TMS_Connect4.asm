; =============================================
; CONNECT 4 - P-LAB TMS9918 Graphic Card
; VERHILLE Arnaud - 2026
; Two-player drop-piece game, 7x6 grid
; =============================================
; Assemble:
;   ca65 -o build/TMS_Connect4.o software/tms9918/TMS_Connect4.asm
;   ld65 -C software/hgr/apple1_gen2.cfg -o build/TMS_Connect4.bin build/TMS_Connect4.o
;
; Load via File > Load Memory, 280R.  Enable the TMS9918 card.
;
; Graphics I mode, 32x24 chars.  Board 7x6 centred at (row 9, col 12).
; Each board slot is a single 8x8 tile with a 6x6 circle cut-out.
; Colour groups give us 3 tile types: blue board with black hole
; (empty), blue board with red circle (red piece), blue board with
; yellow circle (yellow piece).  Outside the board: invisible.
; =============================================

ECHO     = $FFEF
KBD      = $D010
KBDCR    = $D011
VDP_DATA = $CC00
VDP_CTRL = $CC01

NCOLS   = 7
NROWS   = 6
NCELLS  = 42

TILE_EMPTY  = 0
TILE_RED    = 1
TILE_YELLOW = 2

GRID_BASE = $4000

; --- Zero page ---
.zeropage
temp:          .res 1
temp2:         .res 1
temp3:         .res 1
temp4:         .res 1
src_lo:        .res 1
src_hi:        .res 1
str_lo:        .res 1
str_hi:        .res 1
current_player:.res 1
move_count:    .res 1
winner:        .res 1
last_row:      .res 1
last_col:      .res 1
row_cnt:       .res 1
col_cnt:       .res 1
draw_row:      .res 1
draw_col:      .res 1

.code

; =============================================
; MAIN
; =============================================
main:
        LDA #<str_title
        LDX #>str_title
        JSR print_str_ax
        JSR wait_key

        JSR init_vdp

new_game:
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

        JSR render_all
        JSR print_status

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
        SEC
        SBC #'1'

        JSR drop_piece
        CMP #$00
        BEQ move_loop

        ; Delta redraw placed piece
        LDA last_row
        STA draw_row
        LDA last_col
        STA draw_col
        LDA current_player
        JSR draw_cell

        INC move_count

        JSR check_win
        STA winner
        CMP #$00
        BNE @game_won

        LDA move_count
        CMP #NCELLS
        BCS @draw

        LDA current_player
        EOR #$03
        STA current_player
        JSR print_status
        JMP move_loop

@game_won:
        LDA winner
        CMP #TILE_RED
        BNE @y
        LDA #<str_red_wins
        LDX #>str_red_wins
        JMP @show
@y:
        LDA #<str_yellow_wins
        LDX #>str_yellow_wins
@show:
        JSR print_str_ax
        JSR wait_key
        JMP new_game

@draw:
        LDA #<str_draw
        LDX #>str_draw
        JSR print_str_ax
        JSR wait_key
        JMP new_game

; =============================================
; drop_piece / check_win / check_4_at_x (identical to text/HGR)
; =============================================
drop_piece:
        STA last_col
        LDX #NROWS-1
@f:     LDA row_x7,X
        CLC
        ADC last_col
        TAY
        LDA GRID_BASE,Y
        BEQ @place
        DEX
        BPL @f
        LDA #$00
        RTS
@place:
        STX last_row
        LDA current_player
        STA GRID_BASE,Y
        LDA #$01
        RTS

check_win:
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

check_4_at_x:
        LDA GRID_BASE,X
        BEQ @no
        STA temp
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
; init_vdp: set up TMS9918 and upload patterns / colours / name table
; =============================================
init_vdp:
        LDX #$00
@regloop:
        LDA vdp_regs,X
        STA VDP_CTRL
        TXA
        ORA #$80
        STA VDP_CTRL
        INX
        CPX #$08
        BNE @regloop

        ; Upload 4 patterns: chars 0, 8, 16, 24 (pattern offsets 0, 64, 128, 192)
        LDX #$00
@patloop:
        LDA tile_vram_lo,X
        STA VDP_CTRL
        LDA tile_vram_hi,X
        ORA #$40
        STA VDP_CTRL

        TXA
        PHA
        ASL A
        ASL A
        ASL A
        CLC
        ADC #<tile_patterns
        STA src_lo
        LDA #>tile_patterns
        ADC #$00
        STA src_hi

        LDY #$00
@pb:    LDA (src_lo),Y
        STA VDP_DATA
        INY
        CPY #$08
        BCC @pb

        PLA
        TAX
        INX
        CPX #$04
        BNE @patloop

        ; Colours for groups 0..3 at $2000
        LDA #$00
        STA VDP_CTRL
        LDA #$60                        ; $20 | $40
        STA VDP_CTRL
        LDX #$00
@colloop:
        LDA tile_colors,X
        STA VDP_DATA
        INX
        CPX #$04
        BNE @colloop

        ; Clear name table 768 bytes to char 0
        LDA #$00
        STA VDP_CTRL
        LDA #$58                        ; $18 | $40
        STA VDP_CTRL
        LDX #$03
        LDA #$00
@np:    LDY #$00
@nb:    STA VDP_DATA
        INY
        BNE @nb
        DEX
        BNE @np

        ; Disable sprites
        LDA #$00
        STA VDP_CTRL
        LDA #$5B
        STA VDP_CTRL
        LDA #$D0
        STA VDP_DATA
        RTS

; =============================================
; render_all: fill the 7x6 board area with current state
; Board centred at (row 9, col 12). Name table cell =
;   $1800 + (9+r)*32 + (12+c) = $192C + r*32 + c
; =============================================
render_all:
        LDA #$00
        STA draw_row
@rlp:
        ; Set VRAM addr at start of this board row
        LDX draw_row
        CLC
        LDA row_x32_lo,X
        ADC #$2C                        ; low byte of $192C
        STA VDP_CTRL
        LDA row_x32_hi,X
        ADC #$19
        ORA #$40
        STA VDP_CTRL

        LDA #$00
        STA draw_col
@clp:
        LDX draw_row
        LDA row_x7,X
        CLC
        ADC draw_col
        TAX
        LDA GRID_BASE,X
        CLC
        ADC #$01                        ; state 0..2 -> 1..3
        ASL A
        ASL A
        ASL A                           ; char = (state+1) * 8
        STA VDP_DATA

        INC draw_col
        LDA draw_col
        CMP #NCOLS
        BCC @clp

        INC draw_row
        LDA draw_row
        CMP #NROWS
        BCC @rlp
        RTS

; =============================================
; draw_cell: redraw one board cell
; Input: A = state (0, 1, 2), draw_row, draw_col
; =============================================
draw_cell:
        CLC
        ADC #$01
        ASL A
        ASL A
        ASL A
        PHA                             ; save char code

        LDX draw_row
        CLC
        LDA row_x32_lo,X
        ADC draw_col
        STA temp
        LDA row_x32_hi,X
        ADC #$00
        STA temp2

        CLC
        LDA temp
        ADC #$2C
        STA VDP_CTRL
        LDA temp2
        ADC #$19
        ORA #$40
        STA VDP_CTRL

        PLA
        STA VDP_DATA
        RTS

; =============================================
; print_status: print whose turn it is on the Apple 1 text screen
; =============================================
print_status:
        LDA current_player
        CMP #TILE_RED
        BNE @y
        LDA #<str_red_turn
        LDX #>str_red_turn
        JMP @do
@y:
        LDA #<str_yellow_turn
        LDX #>str_yellow_turn
@do:
        JSR print_str_ax
        RTS

wait_key:
@wk:    LDA KBDCR
        BPL @wk
        LDA KBD
        AND #$7F
        RTS

print_str_ax:
        STA str_lo
        STX str_hi
        LDY #$00
@lp:    LDA (str_lo),Y
        BEQ @dn
        ORA #$80
        JSR ECHO
        INY
        BNE @lp
@dn:    RTS

; =============================================
; DATA
; =============================================

row_x7:
        .byte 0, 7, 14, 21, 28, 35

; row * 32 for rows 0..5
row_x32_lo:
        .byte $00, $20, $40, $60, $80, $A0
row_x32_hi:
        .byte $00, $00, $00, $00, $00, $00

; VDP register setup (Graphics I)
vdp_regs:
        .byte $00, $C0, $06, $80, $00, $36, $07, $01

; Pattern-table VRAM offsets for chars 0, 8, 16, 24
tile_vram_lo:
        .byte $00, $40, $80, $C0
tile_vram_hi:
        .byte $00, $00, $00, $00

; Tile patterns (4 tiles x 8 bytes)
tile_patterns:
; Char 0 (outside board): invisible, group 0 colour is black-on-black anyway
        .byte $00,$00,$00,$00,$00,$00,$00,$00
; Char 8 (empty slot): 6x6 circle cut-out
        .byte $3C,$7E,$FF,$FF,$FF,$FF,$7E,$3C
; Char 16 (red piece): same circle, red via group colour
        .byte $3C,$7E,$FF,$FF,$FF,$FF,$7E,$3C
; Char 24 (yellow piece): same circle, yellow via group colour
        .byte $3C,$7E,$FF,$FF,$FF,$FF,$7E,$3C

; Colour bytes (fg<<4 | bg) for groups 0..3
tile_colors:
        .byte $11       ; Group 0 outside: fg=1 black, bg=1 black (invisible)
        .byte $14       ; Group 1 empty slot: fg=1 black hole, bg=4 dark blue
        .byte $84       ; Group 2 red piece: fg=8 medium red, bg=4 dark blue
        .byte $B4       ; Group 3 yellow piece: fg=11 light yellow, bg=4 dark blue

; --- Strings ---
str_title:
        .byte $0D, " * CONNECT 4 *  TMS9918", $0D
        .byte " BY VERHILLE ARNAUD, 2026", $0D
        .byte $0D
        .byte " RED VS YELLOW ON BLUE BOARD.", $0D
        .byte " DROP WITH 1-7, ALIGN 4 TO WIN.", $0D
        .byte " R RESTARTS THE GAME.", $0D
        .byte $0D, " PRESS ANY KEY TO START...", $0D, 0

str_red_turn:
        .byte $0D, " RED'S TURN - 1-7 TO DROP", $0D, 0
str_yellow_turn:
        .byte $0D, " YELLOW'S TURN - 1-7 TO DROP", $0D, 0
str_red_wins:
        .byte $0D, " **** RED WINS! **** KEY TO RESTART", $0D, 0
str_yellow_wins:
        .byte $0D, " *** YELLOW WINS! *** KEY TO RESTART", $0D, 0
str_draw:
        .byte $0D, " *** DRAW. *** KEY TO RESTART", $0D, 0
