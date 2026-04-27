; =============================================
; CONNECT 4 - GEN2 Color Graphics Card
; VERHILLE Arnaud - 2026
; Two-player drop-piece game, 7x6 grid
; =============================================
; Assemble:
;   ca65 -o build/HGR7_Connect4.o software/hgr/HGR7_Connect4.asm
;   ld65 -C software/hgr/apple1_gen2.cfg -o build/HGR7_Connect4.bin build/HGR7_Connect4.o
;
; Load via File > Load Memory (HGR7_Connect4.txt), 280R.
; GEN2 card must be enabled in Hardware menu.
;
; Pieces are 14x16 rounded rectangles, dithered in NTSC artefact
; colour:  "red" = orange (group 2, odd X), "yellow" = green (group 0, odd X).
; Board is centred: byte cols 13..26 (pixels 91..188), scanlines 48..143.
; =============================================

ECHO    = $FFEF
KBD     = $D010
KBDCR   = $D011

NCOLS   = 7
NROWS   = 6
NCELLS  = 42

TILE_EMPTY  = 0
TILE_RED    = 1
TILE_YELLOW = 2

BYTE_OFFSET = 13        ; left byte column of the board
SCAN_OFFSET = 48        ; top scanline of the board

GRID_BASE = $4000

; --- Zero page ---
.zeropage
temp:          .res 1
temp2:         .res 1
temp3:         .res 1
temp4:         .res 1   ; direction offset for check_4_at_x
ptr_lo:        .res 1
ptr_hi:        .res 1
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
; Required by hgr_tables.inc (referenced even if unused)
cur_x:         .res 1
cur_y:         .res 1
mul_tmp:       .res 1
mul_res0:      .res 1

.code

; =============================================
; MAIN
; =============================================
main:
        LDA #<str_title
        LDX #>str_title
        JSR print_str_ax
        JSR wait_key

        JSR clear_hgr

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

        ; Delta-draw just the placed piece
        LDA last_row
        STA draw_row
        LDA last_col
        STA draw_col
        LDA current_player
        JSR draw_tile

        INC move_count

        JSR check_win
        STA winner
        CMP #$00
        BNE @game_won

        LDA move_count
        CMP #NCELLS
        BCS @draw

        ; Toggle player
        LDA current_player
        EOR #$03
        STA current_player
        JSR print_status
        JMP move_loop

@game_won:
        LDA winner
        CMP #TILE_RED
        BNE @y_win
        LDA #<str_red_wins
        LDX #>str_red_wins
        JMP @show_over
@y_win:
        LDA #<str_yellow_wins
        LDX #>str_yellow_wins
@show_over:
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
; drop_piece, check_win, check_4_at_x
; Same logic as the text version.
; =============================================
drop_piece:
        STA last_col
        LDX #NROWS-1
@find:
        LDA row_x7,X
        CLC
        ADC last_col
        TAY
        LDA GRID_BASE,Y
        BEQ @place
        DEX
        BPL @find
        LDA #$00
        RTS
@place:
        STX last_row
        LDA current_player
        STA GRID_BASE,Y
        LDA #$01
        RTS

check_win:
        ; Horizontal
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
; render_all: draw the 7x6 board
; =============================================
render_all:
        LDA #$00
        STA draw_row
@rlp:
        LDA #$00
        STA draw_col
@clp:
        LDX draw_row
        LDA row_x7,X
        CLC
        ADC draw_col
        TAX
        LDA GRID_BASE,X
        JSR draw_tile

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
; draw_tile: draw 14x16 piece at (draw_row, draw_col)
; Input: A = tile type (0..2)
; Board is offset by BYTE_OFFSET bytes and SCAN_OFFSET scanlines.
; =============================================
draw_tile:
        ; src = tile_bitmaps + A*32
        ASL A
        ASL A
        ASL A
        ASL A
        ASL A
        CLC
        ADC #<tile_bitmaps
        STA src_lo
        LDA #>tile_bitmaps
        ADC #$00
        STA src_hi

        ; temp = byte offset into HGR line = draw_col*2 + BYTE_OFFSET
        LDA draw_col
        ASL A
        CLC
        ADC #BYTE_OFFSET
        STA temp

        ; temp2 = start scanline = draw_row*16 + SCAN_OFFSET
        LDA draw_row
        ASL A
        ASL A
        ASL A
        ASL A
        CLC
        ADC #SCAN_OFFSET
        STA temp2

        LDX #$00
@scan:
        TXA
        CLC
        ADC temp2
        TAY

        LDA hgr_lo,Y
        CLC
        ADC temp
        STA ptr_lo
        LDA hgr_hi,Y
        ADC #$00
        STA ptr_hi

        LDY #$00
        LDA (src_lo),Y
        STA (ptr_lo),Y
        INY
        LDA (src_lo),Y
        STA (ptr_lo),Y

        CLC
        LDA src_lo
        ADC #$02
        STA src_lo
        BCC @no_sinc
        INC src_hi
@no_sinc:

        INX
        CPX #$10
        BCC @scan
        RTS

; =============================================
; print_status: show which player plays next (Apple 1 text)
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

; =============================================
; wait_key
; =============================================
wait_key:
@wk:    LDA KBDCR
        BPL @wk
        LDA KBD
        AND #$7F
        RTS

; =============================================
; print_str_ax — promoted to dev/lib/apple1/print.asm (Tier 2 mutualization).
; =============================================
.include "print.asm"

; =============================================
; DATA
; =============================================

row_x7:
        .byte 0, 7, 14, 21, 28, 35

; --- Tile bitmaps (3 tiles x 32 bytes = 96 bytes) ---
; Each tile: 16 scanlines, 2 bytes per scanline
; Pieces start at HGR byte col 13 (odd).  For that parity:
;   odd X = bits 0,2,4,6 of odd byte / bits 1,3,5 of even byte
;   "orange" pattern (group 2, odd X):  odd byte = $D5   even byte = $AA
;   "green"  pattern (group 0, odd X):  odd byte = $55   even byte = $2A
;
; Shape: 14x16 rounded rectangle (corners notched at rows 0 and 15, 2px in)
tile_bitmaps:
; Tile 0: EMPTY (all black)
        .byte $00,$00, $00,$00, $00,$00, $00,$00
        .byte $00,$00, $00,$00, $00,$00, $00,$00
        .byte $00,$00, $00,$00, $00,$00, $00,$00
        .byte $00,$00, $00,$00, $00,$00, $00,$00
; Tile 1: RED piece (orange dither)
        .byte $D4,$8A, $D4,$AA
        .byte $D5,$AA, $D5,$AA, $D5,$AA, $D5,$AA
        .byte $D5,$AA, $D5,$AA, $D5,$AA, $D5,$AA
        .byte $D5,$AA, $D5,$AA, $D5,$AA, $D5,$AA
        .byte $D4,$AA, $D4,$8A
; Tile 2: YELLOW piece (green dither)
        .byte $54,$0A, $54,$2A
        .byte $55,$2A, $55,$2A, $55,$2A, $55,$2A
        .byte $55,$2A, $55,$2A, $55,$2A, $55,$2A
        .byte $55,$2A, $55,$2A, $55,$2A, $55,$2A
        .byte $54,$2A, $54,$0A

; --- Strings ---
str_title:
        .byte $0D, " * CONNECT 4 *   GEN2 HGR", $0D
        .byte " BY VERHILLE ARNAUD, 2026", $0D
        .byte $0D
        .byte " RED (ORANGE) VS YELLOW (GREEN).", $0D
        .byte " DROP WITH 1-7, ALIGN 4 TO WIN.", $0D
        .byte " R RESTARTS THE GAME.", $0D
        .byte $0D, " PRESS ANY KEY TO START...", $0D, 0

str_red_turn:
        .byte $0D, " RED (ORANGE) - 1-7 TO DROP", $0D, 0
str_yellow_turn:
        .byte $0D, " YELLOW (GREEN) - 1-7 TO DROP", $0D, 0
str_red_wins:
        .byte $0D, " **** RED WINS! **** KEY TO RESTART", $0D, 0
str_yellow_wins:
        .byte $0D, " *** YELLOW WINS! *** KEY TO RESTART", $0D, 0
str_draw:
        .byte $0D, " *** DRAW. *** KEY TO RESTART", $0D, 0

.include "hgr_tables.inc"
.include "multiply.asm"
