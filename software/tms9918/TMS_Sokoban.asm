; =============================================
; SOKOBAN for P-LAB TMS9918 Graphic Card
; VERHILLE Arnaud - 2026
; Classic push-boxes puzzle
; Levels 4-47 = Microban I #1..#44 by David W. Skinner
; =============================================
; Assemble with cc65:
;   ca65 -o build/TMS_Sokoban.o software/tms9918/TMS_Sokoban.asm
;   ld65 -C software/hgr/apple1_gen2.cfg -o build/TMS_Sokoban.bin build/TMS_Sokoban.o
;
; Load in POM1 via File > Load Memory (TMS_Sokoban.txt), then 280R.
; The TMS9918 card must be enabled (Hardware menu).
;
; Display: 32x24 Graphics I mode. Each Sokoban tile = one 8x8
; character. 7 tile types each occupy their own "colour group"
; (chars 0, 8, 16, 24, 32, 40, 48) so each has its own colour.
; The 20x12 Sokoban playfield is centred in the 32x24 screen.
;
; State grid at $4000 (one byte per cell, 240 bytes).
; Delta rendering: each move only updates 2-4 name-table cells.
; Apple 1 text screen carries the title, layout prompt and win msg.
; =============================================

; --- Apple 1 I/O ---
ECHO    = $FFEF
KBD     = $D010
KBDCR   = $D011

; --- TMS9918 I/O ---
VDP_DATA = $CC00
VDP_CTRL = $CC01

; --- Game constants ---
NCOLS   = 20
NROWS   = 12
NUM_LEVELS = 47

; --- Tile types ---
TILE_FLOOR         = 0
TILE_WALL          = 1
TILE_TARGET        = 2
TILE_BOX           = 3
TILE_BOX_TARGET    = 4
TILE_PLAYER        = 5
TILE_PLAYER_TARGET = 6

STATE_GRID = $4000

; --- Zero page ---
.zeropage
temp:          .res 1   ; $00
temp2:         .res 1   ; $01
sptr_lo:       .res 1   ; $02
sptr_hi:       .res 1   ; $03
str_lo:        .res 1   ; $04
str_hi:        .res 1   ; $05
src_lo:        .res 1   ; $06  VDP source pointer (tile patterns)
src_hi:        .res 1   ; $07
level_idx:     .res 1   ; $08
player_row:    .res 1   ; $09
player_col:    .res 1   ; $0A
lvl_w:         .res 1   ; $0B
lvl_h:         .res 1   ; $0C
row_offset:    .res 1   ; $0D
col_offset:    .res 1   ; $0E
new_row:       .res 1   ; $0F
new_col:       .res 1   ; $10
box_row:       .res 1   ; $11
box_col:       .res 1   ; $12
dir_dy:        .res 1   ; $13
dir_dx:        .res 1   ; $14
draw_row:      .res 1   ; $15
draw_col:      .res 1   ; $16
key_up_code:   .res 1   ; $17
key_left_code: .res 1   ; $18

; --- Code ---
.code

; =============================================
; MAIN
; =============================================
main:
        LDA #<str_title
        LDX #>str_title
        JSR print_str_ax

        ; Layout prompt
        LDA #<str_layout
        LDX #>str_layout
        JSR print_str_ax
@lp_wait:
        JSR wait_key
        CMP #'1'
        BEQ @qwerty
        CMP #'2'
        BEQ @azerty
        JMP @lp_wait
@qwerty:
        LDA #'W'
        STA key_up_code
        LDA #'A'
        STA key_left_code
        JMP @start
@azerty:
        LDA #'Z'
        STA key_up_code
        LDA #'Q'
        STA key_left_code
@start:
        JSR init_vdp                    ; set up TMS9918
        LDA #$00
        STA level_idx

game_loop:
        JSR init_level
        JSR render_all

move_loop:
        JSR wait_key
        CMP key_up_code
        BEQ key_up
        CMP #'S'
        BEQ key_down
        CMP key_left_code
        BEQ key_left
        CMP #'D'
        BEQ key_right
        CMP #'R'
        BEQ key_reset
        CMP #'N'
        BEQ key_next
        JMP move_loop

key_up:
        LDA #$FF
        STA dir_dy
        LDA #$00
        STA dir_dx
        JMP do_move
key_down:
        LDA #$01
        STA dir_dy
        LDA #$00
        STA dir_dx
        JMP do_move
key_left:
        LDA #$00
        STA dir_dy
        LDA #$FF
        STA dir_dx
        JMP do_move
key_right:
        LDA #$00
        STA dir_dy
        LDA #$01
        STA dir_dx
        JMP do_move
key_reset:
        JMP game_loop
key_next:
        JMP advance_level

do_move:
        JSR execute_move
        CMP #$00
        BEQ move_loop_j

        JSR check_win
        CMP #$00
        BEQ move_loop_j

        ; Victory
        LDA #<str_win
        LDX #>str_win
        JSR print_str_ax
        JSR wait_key

advance_level:
        INC level_idx
        LDA level_idx
        CMP #NUM_LEVELS
        BCC game_loop_j
        LDA #$00
        STA level_idx
game_loop_j:
        JMP game_loop
move_loop_j:
        JMP move_loop

; =============================================
; init_vdp: set up TMS9918 registers, patterns, colours and clear name table
; =============================================
init_vdp:
        ; --- Program 8 VDP registers ---
        LDX #$00
@regloop:
        LDA vdp_regs,X
        STA VDP_CTRL
        TXA
        ORA #$80                        ; register write flag
        STA VDP_CTRL
        INX
        CPX #$08
        BNE @regloop

        ; --- Load 7 tile patterns at pattern-table offsets 0, 64, 128, ... ---
        LDX #$00
@patloop:
        ; Set VRAM write addr = $00{vram_lo tbl}
        LDA tile_vram_lo,X
        STA VDP_CTRL
        LDA tile_vram_hi,X
        ORA #$40                        ; write flag
        STA VDP_CTRL

        ; src = tile_patterns + X*8
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
        CPX #$07
        BNE @patloop

        ; --- Load 7 colour bytes at colour table $2000 (groups 0..6) ---
        LDA #$00
        STA VDP_CTRL
        LDA #$60                        ; $20 | $40 (write flag)
        STA VDP_CTRL
        LDX #$00
@colloop:
        LDA tile_colors,X
        STA VDP_DATA
        INX
        CPX #$07
        BNE @colloop

        ; --- Clear name table ($1800, 768 bytes = char 0 = blank floor) ---
        LDA #$00
        STA VDP_CTRL
        LDA #$58                        ; $18 | $40
        STA VDP_CTRL
        LDX #$03                        ; 3 pages
        LDA #$00
@np:    LDY #$00
@nb:    STA VDP_DATA
        INY
        BNE @nb
        DEX
        BNE @np

        ; --- Disable sprites: first sprite Y = $D0 stops the chain ---
        LDA #$00
        STA VDP_CTRL
        LDA #$5B                        ; $1B | $40
        STA VDP_CTRL
        LDA #$D0
        STA VDP_DATA
        RTS

; =============================================
; render_all: draw the 20x12 playfield into the name table
; Playfield is centred at (row=6, col=6) of the 32x24 screen
; VRAM addr of a cell = $1800 + (6+r)*32 + (6+c) = $18C6 + r*32 + c
; =============================================
render_all:
        LDA #$00
        STA draw_row
@rowlp:
        ; Set VRAM write addr to start of this playfield row
        ;   addr_lo = row_x32_lo[r] + $C6
        ;   addr_hi = row_x32_hi[r] + $18
        LDX draw_row
        CLC
        LDA row_x32_lo,X
        ADC #$C6
        STA VDP_CTRL
        LDA row_x32_hi,X
        ADC #$18
        ORA #$40
        STA VDP_CTRL

        ; Write 20 char codes for this row
        LDA #$00
        STA draw_col
@collp:
        LDX draw_row
        LDA row_x20,X
        CLC
        ADC draw_col
        TAX
        LDA STATE_GRID,X
        ASL A
        ASL A
        ASL A                            ; char = tile * 8
        STA VDP_DATA

        INC draw_col
        LDA draw_col
        CMP #NCOLS
        BCC @collp

        INC draw_row
        LDA draw_row
        CMP #NROWS
        BCC @rowlp
        RTS

; =============================================
; draw_cell: update a single name-table cell
; Input: A = tile type, draw_row, draw_col
; =============================================
draw_cell:
        PHA                             ; save tile type

        ; VRAM addr = $18C6 + draw_row*32 + draw_col
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
        ADC #$C6
        STA VDP_CTRL
        LDA temp2
        ADC #$18
        ORA #$40
        STA VDP_CTRL

        PLA
        ASL A
        ASL A
        ASL A
        STA VDP_DATA
        RTS

; =============================================
; init_level: parse level data into state grid
; =============================================
init_level:
        LDX level_idx
        LDA level_ptrs_lo,X
        STA sptr_lo
        LDA level_ptrs_hi,X
        STA sptr_hi

        LDY #$00
        LDA (sptr_lo),Y
        STA lvl_w
        INY
        LDA (sptr_lo),Y
        STA lvl_h
        INY
        LDA (sptr_lo),Y
        STA row_offset
        INY
        LDA (sptr_lo),Y
        STA col_offset

        CLC
        LDA sptr_lo
        ADC #$04
        STA sptr_lo
        LDA sptr_hi
        ADC #$00
        STA sptr_hi

        ; Clear state grid
        LDY #$00
        TYA
@clr:   STA STATE_GRID,Y
        INY
        CPY #240
        BNE @clr

        ; Parse ASCII data row by row
        LDA #$00
        STA temp                        ; parse_row
@rowlp:
        LDY #$00                        ; parse_col
@collp:
        LDA (sptr_lo),Y
        JSR ascii_to_tile
        PHA

        CMP #TILE_PLAYER
        BEQ @save_p
        CMP #TILE_PLAYER_TARGET
        BNE @no_p
@save_p:
        TYA
        CLC
        ADC col_offset
        STA player_col
        LDA temp
        CLC
        ADC row_offset
        STA player_row
@no_p:

        LDA temp
        CLC
        ADC row_offset
        TAX
        LDA row_x20,X
        STA temp2
        TYA
        CLC
        ADC col_offset
        CLC
        ADC temp2
        TAX
        PLA
        STA STATE_GRID,X

        INY
        CPY lvl_w
        BCC @collp

        ; Advance sptr by lvl_w
        CLC
        LDA sptr_lo
        ADC lvl_w
        STA sptr_lo
        LDA sptr_hi
        ADC #$00
        STA sptr_hi

        INC temp
        LDA temp
        CMP lvl_h
        BCS @done
        JMP @rowlp
@done:  RTS

; =============================================
; ascii_to_tile
; =============================================
ascii_to_tile:
        CMP #'#'
        BEQ @w
        CMP #'.'
        BEQ @t
        CMP #'$'
        BEQ @b
        CMP #'*'
        BEQ @bt
        CMP #'@'
        BEQ @p
        CMP #'+'
        BEQ @pt
        LDA #TILE_FLOOR
        RTS
@w:     LDA #TILE_WALL
        RTS
@t:     LDA #TILE_TARGET
        RTS
@b:     LDA #TILE_BOX
        RTS
@bt:    LDA #TILE_BOX_TARGET
        RTS
@p:     LDA #TILE_PLAYER
        RTS
@pt:    LDA #TILE_PLAYER_TARGET
        RTS

; =============================================
; execute_move: update state grid + delta-redraw affected cells
; Returns A=0 blocked, A!=0 moved
; =============================================
execute_move:
        LDA player_row
        CLC
        ADC dir_dy
        STA new_row
        LDA player_col
        CLC
        ADC dir_dx
        STA new_col

        LDA new_row
        CMP #NROWS
        BCS @blk_tr
        LDA new_col
        CMP #NCOLS
        BCS @blk_tr

        LDX new_row
        LDA row_x20,X
        CLC
        ADC new_col
        TAX
        LDA STATE_GRID,X

        CMP #TILE_WALL
        BEQ @blk_tr
        CMP #TILE_BOX
        BEQ @try_push
        CMP #TILE_BOX_TARGET
        BEQ @try_push
        JMP @simple_move

@blk_tr:
        JMP @blocked

@try_push:
        LDA new_row
        CLC
        ADC dir_dy
        STA box_row
        LDA new_col
        CLC
        ADC dir_dx
        STA box_col

        LDA box_row
        CMP #NROWS
        BCS @blk_tr
        LDA box_col
        CMP #NCOLS
        BCS @blk_tr

        LDX box_row
        LDA row_x20,X
        CLC
        ADC box_col
        TAX
        LDA STATE_GRID,X

        CMP #TILE_FLOOR
        BEQ @push_floor
        CMP #TILE_TARGET
        BEQ @push_target
        JMP @blocked

@push_floor:
        LDA #TILE_BOX
        STA STATE_GRID,X
        JMP @box_drawn
@push_target:
        LDA #TILE_BOX_TARGET
        STA STATE_GRID,X
@box_drawn:
        ; Delta-draw box destination
        LDA box_row
        STA draw_row
        LDA box_col
        STA draw_col
        LDA STATE_GRID,X
        JSR draw_cell
        ; Fall through

@simple_move:
        ; Leave old player cell
        LDX player_row
        LDA row_x20,X
        CLC
        ADC player_col
        TAX
        LDA STATE_GRID,X
        JSR leave_tile
        STA STATE_GRID,X
        ; Delta-draw it
        LDA player_row
        STA draw_row
        LDA player_col
        STA draw_col
        LDA STATE_GRID,X
        JSR draw_cell

        ; Enter new cell
        LDX new_row
        LDA row_x20,X
        CLC
        ADC new_col
        TAX
        LDA STATE_GRID,X

        CMP #TILE_BOX
        BEQ @strip_fl
        CMP #TILE_BOX_TARGET
        BEQ @strip_tg
        JMP @enter
@strip_fl:
        LDA #TILE_FLOOR
        STA STATE_GRID,X
        JMP @enter
@strip_tg:
        LDA #TILE_TARGET
        STA STATE_GRID,X
@enter:
        LDA STATE_GRID,X
        JSR enter_player
        STA STATE_GRID,X
        ; Delta-draw
        LDA new_row
        STA draw_row
        LDA new_col
        STA draw_col
        LDA STATE_GRID,X
        JSR draw_cell

        LDA new_row
        STA player_row
        LDA new_col
        STA player_col

        LDA #$01
        RTS
@blocked:
        LDA #$00
        RTS

; =============================================
; leave_tile / enter_player (preserve X, use Y for scratch)
; =============================================
leave_tile:
        TAY
        LDA leave_tbl,Y
        RTS
enter_player:
        TAY
        LDA enter_player_tbl,Y
        RTS

; =============================================
; check_win: all targets filled with boxes?
; =============================================
check_win:
        LDY #$00
@loop:  LDA STATE_GRID,Y
        CMP #TILE_TARGET
        BEQ @no
        CMP #TILE_PLAYER_TARGET
        BEQ @no
        INY
        CPY #240
        BNE @loop
        LDA #$01
        RTS
@no:    LDA #$00
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
; print_str_ax
; =============================================
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

; --- TMS9918 Graphics I register set ---
; R0 = $00 mode, R1 = $C0 (16K, display on, Graphics I)
; R2 = $06  name table      = $1800
; R3 = $80  colour table    = $2000
; R4 = $00  pattern table   = $0000
; R5 = $36  sprite attr     = $1B00
; R6 = $07  sprite pattern  = $3800
; R7 = $01  backdrop black, text colour unused in Graphics I
vdp_regs:
        .byte $00, $C0, $06, $80, $00, $36, $07, $01

; --- Pattern table offsets for the 7 tiles (char 0, 8, 16, 24, 32, 40, 48) ---
tile_vram_lo:
        .byte $00, $40, $80, $C0, $00, $40, $80
tile_vram_hi:
        .byte $00, $00, $00, $00, $01, $01, $01

; --- row * 32 (name table row stride) for rows 0..11 ---
row_x32_lo:
        .byte $00, $20, $40, $60, $80, $A0, $C0, $E0
        .byte $00, $20, $40, $60
row_x32_hi:
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $01, $01, $01, $01

; --- row * 20 (state grid row stride) for rows 0..11 ---
row_x20:
        .byte   0,  20,  40,  60,  80, 100, 120, 140
        .byte 160, 180, 200, 220

; --- Tile state transitions ---
leave_tbl:
        .byte 0, 1, 2, 0, 2, 0, 2
enter_player_tbl:
        .byte 5, 0, 6, 0, 0, 0, 0

; --- Tile patterns (7 x 8 bytes = 56 bytes) ---
tile_patterns:
        ; Tile 0 FLOOR: blank
        .byte $00,$00,$00,$00,$00,$00,$00,$00
        ; Tile 1 WALL: solid 8x8 (grey)
        .byte $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF
        ; Tile 2 TARGET: 4x4 dot (red)
        .byte $00,$00,$18,$3C,$3C,$18,$00,$00
        ; Tile 3 BOX: outlined 6x6 (yellow)
        .byte $00,$7E,$42,$42,$42,$42,$7E,$00
        ; Tile 4 BOX-ON-TARGET: filled 6x6 (green)
        .byte $00,$7E,$7E,$7E,$7E,$7E,$7E,$00
        ; Tile 5 PLAYER: stick figure (blue)
        .byte $00,$18,$18,$7E,$18,$18,$24,$42
        ; Tile 6 PLAYER-ON-TARGET: same shape, different colour group
        .byte $00,$18,$18,$7E,$18,$18,$24,$42

; --- Tile colours (bg=black, fg = per tile type) ---
tile_colors:
        .byte $11       ; Tile 0 floor         fg=1  black  (invisible)
        .byte $E1       ; Tile 1 wall          fg=14 grey
        .byte $81       ; Tile 2 target        fg=8  medium red
        .byte $A1       ; Tile 3 box           fg=10 dark yellow
        .byte $31       ; Tile 4 box+target    fg=3  light green
        .byte $51       ; Tile 5 player        fg=5  light blue
        .byte $21       ; Tile 6 player+target fg=2  medium green

; --- Strings (ASCII, high bit set by print_str_ax) ---
str_title:
        .byte $0D, " * SOKOBAN - TMS9918 *", $0D
        .byte " APPLE 1 + P-LAB GRAPHIC CARD", $0D
        .byte " PORT BY VERHILLE ARNAUD, 2026", $0D
        .byte $0D
        .byte " LEVELS 4-47: MICROBAN I", $0D
        .byte " CLASSIC SET BY D.W. SKINNER", $0D
        .byte $0D
        .byte " PUSH ALL BOXES ONTO TARGETS.", $0D
        .byte " BOXES CAN ONLY BE PUSHED!", $0D
        .byte $0D
        .byte " R = RESET   N = NEXT LEVEL", $0D, 0

str_layout:
        .byte $0D, " KEYBOARD LAYOUT ?", $0D
        .byte "  1 = QWERTY  (W/A/S/D)", $0D
        .byte "  2 = AZERTY  (Z/Q/S/D)", $0D, 0

str_win:
        .byte $0D, " LEVEL CLEARED! PRESS A KEY", $0D, 0

; --- Level data ---

; Level 1: minimal push (5x3)
level1:
        .byte 5, 3, 4, 7
        .byte "#####"
        .byte "#.$@#"
        .byte "#####"

; Level 2: up-then-left (7x6)
level2:
        .byte 7, 6, 3, 6
        .byte "#######"
        .byte "#.    #"
        .byte "#  $  #"
        .byte "#     #"
        .byte "#  @  #"
        .byte "#######"

; Level 3: two boxes, two targets (9x7)
level3:
        .byte 9, 7, 2, 5
        .byte "#########"
        .byte "#.     .#"
        .byte "#       #"
        .byte "# $   $ #"
        .byte "#       #"
        .byte "#   @   #"
        .byte "#########"

; --- Microban I #1..#44 (by David W. Skinner, 2000) ---

; Microban #1 (6x7) -> level4
level4:
        .byte 6, 7, 2, 7
        .byte "####  "
        .byte "# .#  "
        .byte "#  ###"
        .byte "#*@  #"
        .byte "#  $ #"
        .byte "#  ###"
        .byte "####  "

; Microban #2 (6x7) -> level5
level5:
        .byte 6, 7, 2, 7
        .byte "######"
        .byte "#    #"
        .byte "# #@ #"
        .byte "# $* #"
        .byte "# .* #"
        .byte "#    #"
        .byte "######"

; Microban #3 (9x6) -> level6
level6:
        .byte 9, 6, 3, 5
        .byte "  ####   "
        .byte "###  ####"
        .byte "#     $ #"
        .byte "# #  #$ #"
        .byte "# . .#@ #"
        .byte "#########"

; Microban #4 (8x6) -> level7
level7:
        .byte 8, 6, 3, 6
        .byte "########"
        .byte "#      #"
        .byte "# .**$@#"
        .byte "#      #"
        .byte "#####  #"
        .byte "    ####"

; Microban #5 (8x7) -> level8
level8:
        .byte 8, 7, 2, 6
        .byte " #######"
        .byte " #     #"
        .byte " # .$. #"
        .byte "## $@$ #"
        .byte "#  .$. #"
        .byte "#      #"
        .byte "########"

; Microban #6 (12x6) -> level9
level9:
        .byte 12, 6, 3, 4
        .byte "###### #####"
        .byte "#    ###   #"
        .byte "# $$     #@#"
        .byte "# $ #...   #"
        .byte "#   ########"
        .byte "#####       "

; Microban #7 (7x8) -> level10
level10:
        .byte 7, 8, 2, 6
        .byte "#######"
        .byte "#     #"
        .byte "# .$. #"
        .byte "# $.$ #"
        .byte "# .$. #"
        .byte "# $.$ #"
        .byte "#  @  #"
        .byte "#######"

; Microban #8 (8x12) -> level11
level11:
        .byte 8, 12, 0, 6
        .byte "  ######"
        .byte "  # ..@#"
        .byte "  # $$ #"
        .byte "  ## ###"
        .byte "   # #  "
        .byte "   # #  "
        .byte "#### #  "
        .byte "#    ## "
        .byte "# #   # "
        .byte "#   # # "
        .byte "###   # "
        .byte "  ##### "

; Microban #9 (6x7) -> level12
level12:
        .byte 6, 7, 2, 7
        .byte "##### "
        .byte "#.  ##"
        .byte "#@$$ #"
        .byte "##   #"
        .byte " ##  #"
        .byte "  ##.#"
        .byte "   ###"

; Microban #10 (11x8) -> level13
level13:
        .byte 11, 8, 2, 4
        .byte "      #####"
        .byte "      #.  #"
        .byte "      #.# #"
        .byte "#######.# #"
        .byte "# @ $ $ $ #"
        .byte "# # # # ###"
        .byte "#       #  "
        .byte "#########  "

; Microban #11 (9x8) -> level14
level14:
        .byte 9, 8, 2, 5
        .byte "  ###### "
        .byte "  #    # "
        .byte "  # ##@##"
        .byte "### # $ #"
        .byte "# ..# $ #"
        .byte "#       #"
        .byte "#  ######"
        .byte "####     "

; Microban #12 (9x8) -> level15
level15:
        .byte 9, 8, 2, 5
        .byte "#####    "
        .byte "#   ##   "
        .byte "# $  #   "
        .byte "## $ ####"
        .byte " ###@.  #"
        .byte "  #  .# #"
        .byte "  #     #"
        .byte "  #######"

; Microban #13 (7x9) -> level16
level16:
        .byte 7, 9, 1, 6
        .byte "####   "
        .byte "#. ##  "
        .byte "#.@ #  "
        .byte "#. $#  "
        .byte "##$ ###"
        .byte " # $  #"
        .byte " #    #"
        .byte " #  ###"
        .byte " ####  "

; Microban #14 (7x6) -> level17
level17:
        .byte 7, 6, 3, 6
        .byte "#######"
        .byte "#     #"
        .byte "# # # #"
        .byte "#. $*@#"
        .byte "#   ###"
        .byte "#####  "

; Microban #15 (9x7) -> level18
level18:
        .byte 9, 7, 2, 5
        .byte "     ### "
        .byte "######@##"
        .byte "#    .* #"
        .byte "#   #   #"
        .byte "#####$# #"
        .byte "    #   #"
        .byte "    #####"

; Microban #16 (10x8) -> level19
level19:
        .byte 10, 8, 2, 5
        .byte " ####     "
        .byte " #  ####  "
        .byte " #     ## "
        .byte "## ##   # "
        .byte "#. .# @$##"
        .byte "#   # $$ #"
        .byte "#  .#    #"
        .byte "##########"

; Microban #17 (6x7) -> level20
level20:
        .byte 6, 7, 2, 7
        .byte "##### "
        .byte "# @ # "
        .byte "#...# "
        .byte "#$$$##"
        .byte "#    #"
        .byte "#    #"
        .byte "######"

; Microban #18 (7x9) -> level21
level21:
        .byte 7, 9, 1, 6
        .byte "#######"
        .byte "#     #"
        .byte "#. .  #"
        .byte "# ## ##"
        .byte "#  $ # "
        .byte "###$ # "
        .byte "  #@ # "
        .byte "  #  # "
        .byte "  #### "

; Microban #19 (8x8) -> level22
level22:
        .byte 8, 8, 2, 6
        .byte "########"
        .byte "#   .. #"
        .byte "#  @$$ #"
        .byte "##### ##"
        .byte "   #  # "
        .byte "   #  # "
        .byte "   #  # "
        .byte "   #### "

; Microban #20 (9x8) -> level23
level23:
        .byte 9, 8, 2, 5
        .byte "#######  "
        .byte "#     ###"
        .byte "#  @$$..#"
        .byte "#### ## #"
        .byte "  #     #"
        .byte "  #  ####"
        .byte "  #  #   "
        .byte "  ####   "

; Microban #21 (7x6) -> level24
level24:
        .byte 7, 6, 3, 6
        .byte "####   "
        .byte "#  ####"
        .byte "# . . #"
        .byte "# $$#@#"
        .byte "##    #"
        .byte " ######"

; Microban #22 (7x9) -> level25
level25:
        .byte 7, 9, 1, 6
        .byte "#####  "
        .byte "#   ###"
        .byte "#. .  #"
        .byte "#   # #"
        .byte "## #  #"
        .byte " #@$$ #"
        .byte " #    #"
        .byte " #  ###"
        .byte " ####  "

; Microban #23 (7x7) -> level26
level26:
        .byte 7, 7, 2, 6
        .byte "#######"
        .byte "#  *  #"
        .byte "#     #"
        .byte "## # ##"
        .byte " #$@.# "
        .byte " #   # "
        .byte " ##### "

; Microban #24 (7x7) -> level27
level27:
        .byte 7, 7, 2, 6
        .byte "# #####"
        .byte "  #   #"
        .byte "###$$@#"
        .byte "#   ###"
        .byte "#     #"
        .byte "# . . #"
        .byte "#######"

; Microban #25 (7x7) -> level28
level28:
        .byte 7, 7, 2, 6
        .byte " ####  "
        .byte " #  ###"
        .byte " # $$ #"
        .byte "##... #"
        .byte "#  @$ #"
        .byte "#   ###"
        .byte "#####  "

; Microban #26 (6x8) -> level29
level29:
        .byte 6, 8, 2, 7
        .byte " #####"
        .byte " # @ #"
        .byte " #   #"
        .byte "###$ #"
        .byte "# ...#"
        .byte "# $$ #"
        .byte "###  #"
        .byte "  ####"

; Microban #27 (7x7) -> level30
level30:
        .byte 7, 7, 2, 6
        .byte "###### "
        .byte "#   .# "
        .byte "# ## ##"
        .byte "#  $$@#"
        .byte "# #   #"
        .byte "#.  ###"
        .byte "#####  "

; Microban #28 (7x7) -> level31
level31:
        .byte 7, 7, 2, 6
        .byte "#####  "
        .byte "#   #  "
        .byte "# @ #  "
        .byte "# $$###"
        .byte "##. . #"
        .byte " #    #"
        .byte " ######"

; Microban #29 (11x9) -> level32
level32:
        .byte 11, 9, 1, 4
        .byte "     ##### "
        .byte "     #   ##"
        .byte "     #    #"
        .byte " ######   #"
        .byte "##     #. #"
        .byte "# $ $ @  ##"
        .byte "# ######.# "
        .byte "#        # "
        .byte "########## "

; Microban #30 (6x7) -> level33
level33:
        .byte 6, 7, 2, 7
        .byte "####  "
        .byte "#  ###"
        .byte "# $$ #"
        .byte "#... #"
        .byte "# @$ #"
        .byte "#   ##"
        .byte "##### "

; Microban #31 (7x7) -> level34
level34:
        .byte 7, 7, 2, 6
        .byte "  #### "
        .byte " ##  # "
        .byte "##@$.##"
        .byte "# $$  #"
        .byte "# . . #"
        .byte "###   #"
        .byte "  #####"

; Microban #32 (7x7) -> level35
level35:
        .byte 7, 7, 2, 6
        .byte " ####  "
        .byte "##  ###"
        .byte "#     #"
        .byte "#.**$@#"
        .byte "#   ###"
        .byte "##  #  "
        .byte " ####  "

; Microban #33 (7x7) -> level36
level36:
        .byte 7, 7, 2, 6
        .byte "#######"
        .byte "#. #  #"
        .byte "#  $  #"
        .byte "#. $#@#"
        .byte "#  $  #"
        .byte "#. #  #"
        .byte "#######"

; Microban #34 (9x6) -> level37
level37:
        .byte 9, 6, 3, 5
        .byte "  ####   "
        .byte "###  ####"
        .byte "#       #"
        .byte "#@$***. #"
        .byte "#       #"
        .byte "#########"

; Microban #35 (6x10) -> level38
level38:
        .byte 6, 10, 1, 7
        .byte " #### "
        .byte "##  # "
        .byte "#. $# "
        .byte "#.$ # "
        .byte "#.$ # "
        .byte "#.$ # "
        .byte "#. $##"
        .byte "#   @#"
        .byte "##   #"
        .byte " #####"

; Microban #36 (15x5) -> level39
level39:
        .byte 15, 5, 3, 2
        .byte "####           "
        .byte "#  ############"
        .byte "# $ $ $ $ $ @ #"
        .byte "# .....       #"
        .byte "###############"

; Microban #37 (9x8) -> level40
level40:
        .byte 9, 8, 2, 5
        .byte "      ###"
        .byte "##### #.#"
        .byte "#   ###.#"
        .byte "#   $ #.#"
        .byte "# $  $  #"
        .byte "#####@# #"
        .byte "    #   #"
        .byte "    #####"

; Microban #38 (10x7) -> level41
level41:
        .byte 10, 7, 2, 5
        .byte "##########"
        .byte "#        #"
        .byte "# ##.### #"
        .byte "# # $$ . #"
        .byte "# . @$## #"
        .byte "#####    #"
        .byte "    ######"

; Microban #39 (10x9) -> level42
level42:
        .byte 10, 9, 1, 5
        .byte "#####     "
        .byte "#   ####  "
        .byte "# # # .#  "
        .byte "#    $ ###"
        .byte "### #$.  #"
        .byte "#   #@   #"
        .byte "# # ######"
        .byte "#   #     "
        .byte "#####     "

; Microban #40 (7x6) -> level43
level43:
        .byte 7, 6, 3, 6
        .byte " ##### "
        .byte " #   # "
        .byte "##   ##"
        .byte "# $$$ #"
        .byte "# .+. #"
        .byte "#######"

; Microban #41 (8x6) -> level44
level44:
        .byte 8, 6, 3, 6
        .byte "####### "
        .byte "#     # "
        .byte "#@$$$ ##"
        .byte "#  #...#"
        .byte "##    ##"
        .byte " ###### "

; Microban #42 (7x8) -> level45
level45:
        .byte 7, 8, 2, 6
        .byte "   ####"
        .byte "   #  #"
        .byte "   #@ #"
        .byte "####$.#"
        .byte "#   $.#"
        .byte "# # $.#"
        .byte "#    ##"
        .byte "###### "

; Microban #43 (9x9) -> level46
level46:
        .byte 9, 9, 1, 5
        .byte "     ####"
        .byte "     # @#"
        .byte "     #  #"
        .byte "###### .#"
        .byte "#   $  .#"
        .byte "#  $$# .#"
        .byte "#    ####"
        .byte "###  #   "
        .byte "  ####   "

; Microban #44 (5x3) -> level47
level47:
        .byte 5, 3, 4, 7
        .byte "#####"
        .byte "#@$.#"
        .byte "#####"

level_ptrs_lo:
        .byte <level1, <level2, <level3, <level4, <level5
        .byte <level6, <level7, <level8, <level9, <level10
        .byte <level11, <level12, <level13, <level14, <level15
        .byte <level16, <level17, <level18, <level19, <level20
        .byte <level21, <level22, <level23, <level24, <level25
        .byte <level26, <level27, <level28, <level29, <level30
        .byte <level31, <level32, <level33, <level34, <level35
        .byte <level36, <level37, <level38, <level39, <level40
        .byte <level41, <level42, <level43, <level44, <level45
        .byte <level46, <level47
level_ptrs_hi:
        .byte >level1, >level2, >level3, >level4, >level5
        .byte >level6, >level7, >level8, >level9, >level10
        .byte >level11, >level12, >level13, >level14, >level15
        .byte >level16, >level17, >level18, >level19, >level20
        .byte >level21, >level22, >level23, >level24, >level25
        .byte >level26, >level27, >level28, >level29, >level30
        .byte >level31, >level32, >level33, >level34, >level35
        .byte >level36, >level37, >level38, >level39, >level40
        .byte >level41, >level42, >level43, >level44, >level45
        .byte >level46, >level47
