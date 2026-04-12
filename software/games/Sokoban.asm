; =============================================
; SOKOBAN (text mode) for Apple 1
; VERHILLE Arnaud - 2026
; Classic push-boxes puzzle
; Levels 4-23 = Microban I by David W. Skinner
; =============================================
; Assemble with cc65:
;   ca65 -o build/Sokoban.o software/games/Sokoban.asm
;   ld65 -C software/apple1.cfg -o build/Sokoban.bin build/Sokoban.o
;
; Load in POM1 via File > Load Memory (Sokoban.txt),
; then type 280R in Woz Monitor.
;
; Classic Sokoban ASCII rendering on the 40x24 text display:
;   #  wall       .  target        $  box
;   *  box-on-target   @  player   +  player-on-target
; 20x12 playfield, 23 levels.  No HGR required.
; =============================================

; --- Apple 1 I/O ---
ECHO    = $FFEF
KBD     = $D010
KBDCR   = $D011

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

; --- State grid: 240 bytes at $4000 (page-aligned, fits in one page + a bit) ---
STATE_GRID = $4000

; --- Zero page (apple1.cfg gives $00-$22) ---
.zeropage
temp:          .res 1   ; $00
temp2:         .res 1   ; $01
sptr_lo:       .res 1   ; $02
sptr_hi:       .res 1   ; $03
str_lo:        .res 1   ; $04
str_hi:        .res 1   ; $05
level_idx:     .res 1   ; $06
player_row:    .res 1   ; $07
player_col:    .res 1   ; $08
lvl_w:         .res 1   ; $09
lvl_h:         .res 1   ; $0A
row_offset:    .res 1   ; $0B
col_offset:    .res 1   ; $0C
new_row:       .res 1   ; $0D
new_col:       .res 1   ; $0E
box_row:       .res 1   ; $0F
box_col:       .res 1   ; $10
dir_dy:        .res 1   ; $11  (signed -1, 0, 1)
dir_dx:        .res 1   ; $12
render_r:      .res 1   ; $13
render_c:      .res 1   ; $14
key_up_code:   .res 1   ; $15  'W' or 'Z'
key_left_code: .res 1   ; $16  'A' or 'Q'

; --- Code at $0280 ---
.code

; =============================================
; MAIN
; =============================================
main:
        LDA #<str_title
        LDX #>str_title
        JSR print_str_ax

        ; Keyboard layout prompt
        LDA #<str_layout
        LDX #>str_layout
        JSR print_str_ax
@layout_wait:
        JSR wait_key
        CMP #'1'
        BEQ @qwerty
        CMP #'2'
        BEQ @azerty
        JMP @layout_wait
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
        LDA #$00
        STA level_idx

game_loop:
        JSR init_level
        JSR render_screen

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

        JSR render_screen

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

        ; Clear 240-byte state grid
        LDY #$00
        TYA
@clr:   STA STATE_GRID,Y
        INY
        CPY #240
        BNE @clr

        ; Parse ASCII data
        LDA #$00
        STA temp                        ; parse_row
@rowlp:
        LDY #$00                        ; parse_col
@collp:
        LDA (sptr_lo),Y
        JSR ascii_to_tile
        PHA

        CMP #TILE_PLAYER
        BEQ @save_player
        CMP #TILE_PLAYER_TARGET
        BNE @no_player
@save_player:
        TYA
        CLC
        ADC col_offset
        STA player_col
        LDA temp
        CLC
        ADC row_offset
        STA player_row
@no_player:

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

        ; End of row: advance sptr by lvl_w
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
; ascii_to_tile: convert Sokoban ASCII to tile type
; =============================================
ascii_to_tile:
        CMP #'#'
        BEQ @wall
        CMP #'.'
        BEQ @target
        CMP #'$'
        BEQ @box
        CMP #'*'
        BEQ @box_t
        CMP #'@'
        BEQ @player
        CMP #'+'
        BEQ @player_t
        LDA #TILE_FLOOR
        RTS
@wall:
        LDA #TILE_WALL
        RTS
@target:
        LDA #TILE_TARGET
        RTS
@box:
        LDA #TILE_BOX
        RTS
@box_t:
        LDA #TILE_BOX_TARGET
        RTS
@player:
        LDA #TILE_PLAYER
        RTS
@player_t:
        LDA #TILE_PLAYER_TARGET
        RTS

; =============================================
; execute_move: try to move player, A=0 blocked, A!=0 moved
; Mutates state grid only (no rendering — caller calls render_screen).
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
        BCS @blocked
        LDA box_col
        CMP #NCOLS
        BCS @blocked

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
        JMP @simple_move
@push_target:
        LDA #TILE_BOX_TARGET
        STA STATE_GRID,X
        ; Fall through

@simple_move:
        ; Leave player old cell
        LDX player_row
        LDA row_x20,X
        CLC
        ADC player_col
        TAX
        LDA STATE_GRID,X
        JSR leave_tile
        STA STATE_GRID,X

        ; Enter new cell
        LDX new_row
        LDA row_x20,X
        CLC
        ADC new_col
        TAX
        LDA STATE_GRID,X

        CMP #TILE_BOX
        BEQ @strip_floor
        CMP #TILE_BOX_TARGET
        BEQ @strip_target
        JMP @enter
@strip_floor:
        LDA #TILE_FLOOR
        STA STATE_GRID,X
        JMP @enter
@strip_target:
        LDA #TILE_TARGET
        STA STATE_GRID,X
@enter:
        LDA STATE_GRID,X
        JSR enter_player
        STA STATE_GRID,X

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
; leave_tile / enter_player
; Preserve X (caller uses X for cell index) — use Y for scratch.
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
; check_win: A=1 if all targets filled with boxes
; Target "empty" = tile 2 OR tile 6 (player standing on it).
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
; render_screen: minimal frame
;   blank line, 12 grid rows, blank line, footer text, blank line
; The screen scrolls naturally between frames.
; =============================================
render_screen:
        ; Leading blank line
        LDA #$8D
        JSR ECHO

        ; Grid (12 rows x 20 cols)
        LDA #$00
        STA render_r
@rowlp:
        LDA #$00
        STA render_c
@collp:
        ; cell = state[render_r*20 + render_c]
        LDX render_r
        LDA row_x20,X
        CLC
        ADC render_c
        TAX
        LDA STATE_GRID,X
        TAY
        LDA tile_char,Y
        ORA #$80
        JSR ECHO

        INC render_c
        LDA render_c
        CMP #NCOLS
        BCC @collp

        LDA #$8D                        ; CR at end of row
        JSR ECHO
        INC render_r
        LDA render_r
        CMP #NROWS
        BCC @rowlp

        ; Blank line + footer text + CR (handled inside str_footer)
        LDA #<str_footer
        LDX #>str_footer
        JSR print_str_ax
        RTS

; =============================================
; wait_key: returns ASCII in A (bit 7 stripped)
; =============================================
wait_key:
@wk:    LDA KBDCR
        BPL @wk
        LDA KBD
        AND #$7F
        RTS

; =============================================
; print_str_ax: print null-terminated ASCII string
; Input: A=lo, X=hi
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

row_x20:
        .byte   0,  20,  40,  60,  80, 100, 120, 140
        .byte 160, 180, 200, 220

leave_tbl:
        .byte 0, 1, 2, 0, 2, 0, 2

enter_player_tbl:
        .byte 5, 0, 6, 0, 0, 0, 0

; ASCII representation of each tile type
tile_char:
        .byte ' ', '#', '.', '$', '*', '@', '+'

; --- Strings ---
str_title:
        .byte $0D, " * SOKOBAN - TEXT MODE *", $0D
        .byte " APPLE 1  BY VERHILLE ARNAUD, 2026", $0D
        .byte $0D
        .byte " LEVELS 4-47: MICROBAN I", $0D
        .byte " BY DAVID W. SKINNER (2000)", $0D
        .byte $0D
        .byte " PUSH ALL BOXES ($) ONTO", $0D
        .byte " THE TARGETS (.).  BOXES", $0D
        .byte " CAN ONLY BE PUSHED!", $0D
        .byte $0D
        .byte " R = RESET   N = NEXT LEVEL", $0D, 0

str_layout:
        .byte $0D, " KEYBOARD LAYOUT ?", $0D
        .byte "  1 = QWERTY  (W/A/S/D)", $0D
        .byte "  2 = AZERTY  (Z/Q/S/D)", $0D, 0

str_footer:
        .byte $0D
        .byte " UP/DN/LT/RT   R=RESET  N=NEXT", $0D, 0

str_win:
        .byte $0D, " LEVEL CLEARED! PRESS A KEY", $0D, 0

; --- Level data ---
; Format: w, h, row_offset, col_offset, then w*h ASCII bytes

; Level 1: minimal teaching push (5x3)
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

; --- Microban I #1..#20 by David W. Skinner ---

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

; --- Microban I #21..#44 (text version extras up to 4K) ---

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
