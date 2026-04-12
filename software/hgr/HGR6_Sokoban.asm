; =============================================
; HGR SOKOBAN - GEN2 Color Graphics Card
; VERHILLE Arnaud - 2026
; Classic push-boxes puzzle game
; =============================================
; Assemble with cc65:
;   ca65 -o build/HGR6_Sokoban.o software/hgr/HGR6_Sokoban.asm
;   ld65 -C software/hgr/apple1_gen2.cfg -o build/HGR6_Sokoban.bin build/HGR6_Sokoban.o
;
; Controls (uppercase — Apple 1 forces uppercase):
;   W/S/A/D  - move up/down/left/right
;   R        - reset current level
;   N        - skip to next level
;
; 14x16 byte-aligned tiles, 20 cols x 12 rows playfield.
; Uses hgr_lo/hgr_hi tables from hgr_tables.inc for scanline
; addresses (handles Apple II interleave automatically).
;
; State grid at $4000 (one byte per tile cell, 240 cells total).
; Delta rendering: each move only redraws 2-4 affected tiles.
;
; In POM1: plug GEN2 card, File > Load Memory (HGR6_Sokoban.txt),
; then type 280R in Woz Monitor.
; =============================================

; --- Apple 1 I/O ---
ECHO    = $FFEF
KBD     = $D010
KBDCR   = $D011

; --- Game constants ---
NCOLS   = 20
NROWS   = 12
NUM_LEVELS = 23

; --- Tile types ---
TILE_FLOOR         = 0
TILE_WALL          = 1
TILE_TARGET        = 2
TILE_BOX           = 3
TILE_BOX_TARGET    = 4
TILE_PLAYER        = 5
TILE_PLAYER_TARGET = 6

; --- State grid: 240 bytes at $4000 (page-aligned) ---
STATE_GRID = $4000

; --- Zero page ---
.zeropage
temp:        .res 1  ; $00
temp2:       .res 1  ; $01
ptr_lo:      .res 1  ; $02  HGR dest pointer (also used by hgr_tables.inc clear_hgr)
ptr_hi:      .res 1  ; $03
src_lo:      .res 1  ; $04  tile bitmap source pointer
src_hi:      .res 1  ; $05
sptr_lo:     .res 1  ; $06  scratch pointer (level data)
sptr_hi:     .res 1  ; $07
level_idx:   .res 1  ; $08
player_row:  .res 1  ; $09
player_col:  .res 1  ; $0A
lvl_w:       .res 1  ; $0B
lvl_h:       .res 1  ; $0C
row_offset:  .res 1  ; $0D
col_offset:  .res 1  ; $0E
new_row:     .res 1  ; $0F
new_col:     .res 1  ; $10
box_row:     .res 1  ; $11
box_col:     .res 1  ; $12
dir_dy:      .res 1  ; $13  (signed: -1, 0, 1)
dir_dx:      .res 1  ; $14
draw_row:    .res 1  ; $15
draw_col:    .res 1  ; $16
str_lo:      .res 1  ; $17
str_hi:      .res 1  ; $18
; Variables required by hgr_tables.inc routines (declared even if unused)
cur_x:       .res 1  ; $19
cur_y:       .res 1  ; $1A
mul_tmp:     .res 1  ; $1B
mul_res0:    .res 1  ; $1C

; --- Code at $0280 ---
.code

; =============================================
; MAIN
; =============================================
main:
        ; Title on Apple 1 screen
        LDA #<str_title
        LDX #>str_title
        JSR print_str_ax

        ; Wait for any key
        JSR wait_key

        ; Start at level 0
        LDA #$00
        STA level_idx

game_loop:
        JSR init_level
        JSR clear_hgr
        JSR render_all
        JSR show_level

move_loop:
        JSR wait_key
        ; A = ASCII char, bit 7 stripped

        CMP #'W'
        BEQ key_up
        CMP #'S'
        BEQ key_down
        CMP #'A'
        BEQ key_left
        CMP #'D'
        BEQ key_right
        CMP #'R'
        BEQ key_reset
        CMP #'N'
        BEQ key_next
        JMP move_loop                   ; ignore unknown keys

key_up:
        LDA #$FF                        ; dy = -1
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
        LDA #$FF                        ; dx = -1
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
        BEQ move_loop_j                 ; blocked, no move

        ; Check win condition
        JSR check_win
        CMP #$00
        BEQ move_loop_j                 ; not won yet

        ; Level complete
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
; init_level: parse level into state grid at $4000
; =============================================
init_level:
        ; Point sptr at level data: levels[level_idx]
        LDX level_idx
        LDA level_ptrs_lo,X
        STA sptr_lo
        LDA level_ptrs_hi,X
        STA sptr_hi

        ; Read 4-byte header: w, h, row_off, col_off
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

        ; sptr += 4 (advance past header)
        CLC
        LDA sptr_lo
        ADC #$04
        STA sptr_lo
        LDA sptr_hi
        ADC #$00
        STA sptr_hi

        ; Clear state grid: 240 bytes at $4000 = TILE_FLOOR
        LDY #$00
        TYA
@clr:   STA STATE_GRID,Y
        INY
        CPY #240
        BNE @clr

        ; Parse level data
        LDA #$00
        STA temp                        ; temp = parse_row (0..lvl_h-1)
@rowlp:
        LDY #$00                        ; Y = parse_col (0..lvl_w-1)
@collp:
        ; Read ASCII char at (sptr)[Y]
        LDA (sptr_lo),Y
        JSR ascii_to_tile               ; A = tile type
        PHA                             ; save tile type

        ; Record player position if player tile
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

        ; cell_index = (temp+row_offset)*20 + (Y+col_offset)
        LDA temp
        CLC
        ADC row_offset
        TAX
        LDA row_x20,X
        STA temp2                       ; temp2 = row * 20
        TYA
        CLC
        ADC col_offset
        CLC
        ADC temp2
        TAX                             ; X = cell index
        PLA                             ; restore tile type
        STA STATE_GRID,X

        INY
        CPY lvl_w
        BCC @collp

        ; Row done: advance sptr by lvl_w
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
        BCS @init_done
        JMP @rowlp
@init_done:
        RTS

; =============================================
; ascii_to_tile: map ASCII char to tile type
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
; render_all: draw all 240 tiles
; =============================================
render_all:
        LDA #$00
        STA draw_row
@rowlp: LDA #$00
        STA draw_col
@collp:
        LDX draw_row
        LDA row_x20,X
        CLC
        ADC draw_col
        TAX
        LDA STATE_GRID,X
        JSR draw_tile
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
; draw_tile: draw 14x16 tile at (draw_row, draw_col)
; Input: A = tile type (0-6)
; =============================================
draw_tile:
        ; src = tile_bitmaps + A * 32 (A max = 6, A*32 max = 192, fits in byte)
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

        ; byte offset into HGR line = draw_col * 2
        LDA draw_col
        ASL A
        STA temp                        ; temp = byte offset

        ; start scanline = draw_row * 16
        LDA draw_row
        ASL A
        ASL A
        ASL A
        ASL A
        STA temp2                       ; temp2 = start scanline

        ; Loop 16 scanlines
        LDX #$00
@scan:
        ; y = temp2 + X
        TXA
        CLC
        ADC temp2
        TAY                             ; Y = scanline (0-191)

        LDA hgr_lo,Y
        CLC
        ADC temp
        STA ptr_lo
        LDA hgr_hi,Y
        ADC #$00                        ; + carry
        STA ptr_hi

        ; Copy 2 bytes from (src) to (ptr)
        LDY #$00
        LDA (src_lo),Y
        STA (ptr_lo),Y
        INY
        LDA (src_lo),Y
        STA (ptr_lo),Y

        ; src += 2
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
; execute_move: try to move player
; Reads: player_row/col, dir_dy/dx
; Returns: A=0 blocked, A!=0 moved
; Updates state grid and redraws affected tiles (delta render)
; =============================================
execute_move:
        ; new = player + dir
        LDA player_row
        CLC
        ADC dir_dy
        STA new_row
        LDA player_col
        CLC
        ADC dir_dx
        STA new_col

        ; Bounds check (treat as unsigned: wrap-around gives high value, caught by CMP)
        LDA new_row
        CMP #NROWS
        BCS @blk_tr
        LDA new_col
        CMP #NCOLS
        BCS @blk_tr

        ; Get tile at new position
        LDX new_row
        LDA row_x20,X
        CLC
        ADC new_col
        TAX
        LDA STATE_GRID,X

        ; Dispatch
        CMP #TILE_WALL
        BEQ @blk_tr
        CMP #TILE_BOX
        BEQ @try_push
        CMP #TILE_BOX_TARGET
        BEQ @try_push
        JMP @simple_move                ; floor or target

@blk_tr:
        JMP @blocked

@try_push:
        ; box_dest = new + dir
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

        ; Get tile at box destination
        LDX box_row
        LDA row_x20,X
        CLC
        ADC box_col
        TAX                             ; X = box_dest cell index
        LDA STATE_GRID,X

        CMP #TILE_FLOOR
        BEQ @push_floor
        CMP #TILE_TARGET
        BEQ @push_target
        JMP @blocked_bot

@push_floor:
        LDA #TILE_BOX
        STA STATE_GRID,X
        JMP @box_done
@push_target:
        LDA #TILE_BOX_TARGET
        STA STATE_GRID,X
@box_done:
        ; Redraw box destination tile
        LDA box_row
        STA draw_row
        LDA box_col
        STA draw_col
        LDA STATE_GRID,X
        JSR draw_tile
        ; Fall through to simple move to move player onto new_pos (where box was)

@simple_move:
        ; --- Leave old player cell ---
        LDX player_row
        LDA row_x20,X
        CLC
        ADC player_col
        TAX                             ; X = old player cell index
        LDA STATE_GRID,X
        JSR leave_tile                  ; A = 0 (floor) or 2 (target)
        STA STATE_GRID,X
        LDA player_row
        STA draw_row
        LDA player_col
        STA draw_col
        LDA STATE_GRID,X
        JSR draw_tile

        ; --- Enter new player cell ---
        LDX new_row
        LDA row_x20,X
        CLC
        ADC new_col
        TAX                             ; X = new cell index
        LDA STATE_GRID,X

        ; If state at new_pos is box/box_target (push case), strip box first
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
        JSR enter_player                ; A = 5 or 6
        STA STATE_GRID,X

        LDA new_row
        STA draw_row
        LDA new_col
        STA draw_col
        LDA STATE_GRID,X
        JSR draw_tile

        ; Update player position
        LDA new_row
        STA player_row
        LDA new_col
        STA player_col

        LDA #$01
        RTS

@blocked_bot:
@blocked:
        LDA #$00
        RTS

; =============================================
; leave_tile: cell occupant (player/box) leaves
; Input: A = current tile (3,4,5,6)
; Returns: A = 0 or 2 (floor or target underneath)
; =============================================
leave_tile:
        TAX
        LDA leave_tbl,X
        RTS

; =============================================
; enter_player: player enters empty cell
; Input: A = current tile (0 or 2)
; Returns: A = 5 or 6
; =============================================
enter_player:
        TAX
        LDA enter_player_tbl,X
        RTS

; =============================================
; check_win: all targets filled?
; Returns: A=1 win, A=0 not yet
; =============================================
check_win:
        LDY #$00
@loop:  LDA STATE_GRID,Y
        CMP #TILE_TARGET
        BEQ @no
        INY
        CPY #240
        BNE @loop
        LDA #$01
        RTS
@no:    LDA #$00
        RTS

; =============================================
; show_level: display "LEVEL NN" on Apple 1 (2-digit)
; =============================================
show_level:
        LDA #<str_level
        LDX #>str_level
        JSR print_str_ax
        ; level_num = level_idx + 1
        LDA level_idx
        CLC
        ADC #$01
        ; Divide by 10 via repeated subtraction (X = tens, A = ones)
        LDX #$00
@div:   CMP #$0A
        BCC @ddone
        SBC #$0A                        ; carry set from CMP
        INX
        JMP @div
@ddone:
        PHA                             ; save ones digit
        TXA
        ORA #'0'
        ORA #$80
        JSR ECHO                        ; tens digit
        PLA
        ORA #'0'
        ORA #$80
        JSR ECHO                        ; ones digit
        LDA #$8D                        ; CR
        JSR ECHO
        RTS

; =============================================
; wait_key: wait for keypress, return ASCII in A (bit 7 stripped)
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

; --- row * 20 for rows 0..11 ---
row_x20:
        .byte   0,  20,  40,  60,  80, 100, 120, 140
        .byte 160, 180, 200, 220

; --- Tile state transition tables ---
; leave_tbl[current] = state after occupant leaves
;   3 (box-floor)   -> 0 (floor)
;   4 (box-target)  -> 2 (target)
;   5 (player-flr)  -> 0
;   6 (player-tgt)  -> 2
leave_tbl:
        .byte 0, 1, 2, 0, 2, 0, 2

; enter_player_tbl[current] = state after player enters
;   0 (floor)  -> 5
;   2 (target) -> 6
enter_player_tbl:
        .byte 5, 0, 6, 0, 0, 0, 0

; --- Tile bitmaps (7 tiles x 32 bytes = 224 bytes) ---
; Each tile: 16 scanlines, 2 bytes per scanline
tile_bitmaps:
; Tile 0: FLOOR (all black)
        .byte $00,$00, $00,$00, $00,$00, $00,$00
        .byte $00,$00, $00,$00, $00,$00, $00,$00
        .byte $00,$00, $00,$00, $00,$00, $00,$00
        .byte $00,$00, $00,$00, $00,$00, $00,$00
; Tile 1: WALL (all white via adjacent-pixel NTSC rule)
        .byte $7F,$7F, $7F,$7F, $7F,$7F, $7F,$7F
        .byte $7F,$7F, $7F,$7F, $7F,$7F, $7F,$7F
        .byte $7F,$7F, $7F,$7F, $7F,$7F, $7F,$7F
        .byte $7F,$7F, $7F,$7F, $7F,$7F, $7F,$7F
; Tile 2: TARGET (4x4 white dot in center)
; Center dot: bits 5,6 of byte 0 ($60) + bits 0,1 of byte 1 ($03)
        .byte $00,$00, $00,$00, $00,$00, $00,$00
        .byte $00,$00, $00,$00, $60,$03, $60,$03
        .byte $60,$03, $60,$03, $00,$00, $00,$00
        .byte $00,$00, $00,$00, $00,$00, $00,$00
; Tile 3: BOX (outlined 10x12 rectangle, 2-px border)
; Top/bottom: bits 2-6 byte0 ($7C) + bits 0-4 byte1 ($1F)
; Sides:      bits 2,3 byte0 ($0C) + bits 3,4 byte1 ($18)
        .byte $00,$00, $00,$00, $7C,$1F, $7C,$1F
        .byte $0C,$18, $0C,$18, $0C,$18, $0C,$18
        .byte $0C,$18, $0C,$18, $0C,$18, $0C,$18
        .byte $7C,$1F, $7C,$1F, $00,$00, $00,$00
; Tile 4: BOX ON TARGET (filled 10x12 rectangle)
        .byte $00,$00, $00,$00, $7C,$1F, $7C,$1F
        .byte $7C,$1F, $7C,$1F, $7C,$1F, $7C,$1F
        .byte $7C,$1F, $7C,$1F, $7C,$1F, $7C,$1F
        .byte $7C,$1F, $7C,$1F, $00,$00, $00,$00
; Tile 5: PLAYER (plus/cross shape)
; Vertical bar: cols 5-8 = $60,$03
; Horizontal bar (rows 6-9): cols 2-11 = $7C,$1F
        .byte $00,$00, $00,$00, $60,$03, $60,$03
        .byte $60,$03, $60,$03, $7C,$1F, $7C,$1F
        .byte $7C,$1F, $7C,$1F, $60,$03, $60,$03
        .byte $60,$03, $60,$03, $00,$00, $00,$00
; Tile 6: PLAYER ON TARGET (cross with extra "shoulders" rows 4,5,10,11)
; $6C = $60 | $0C (vertical bar + side dots on left)
; $1B = $03 | $18 (vertical bar + side dots on right)
        .byte $00,$00, $00,$00, $60,$03, $60,$03
        .byte $6C,$1B, $6C,$1B, $7C,$1F, $7C,$1F
        .byte $7C,$1F, $7C,$1F, $6C,$1B, $6C,$1B
        .byte $60,$03, $60,$03, $00,$00, $00,$00

; --- Levels ---
; Format: 4-byte header (w, h, row_offset, col_offset) then w*h ASCII bytes
; Sokoban chars: # wall, . target, $ box, @ player, * box-on-target, + player-on-target

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

; --- Microban collection by David W. Skinner (classic 2000) ---
; Levels 4-23 = Microban I #1..#20 (progressive difficulty)

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

level_ptrs_lo:
        .byte <level1, <level2, <level3
        .byte <level4, <level5, <level6, <level7, <level8
        .byte <level9, <level10, <level11, <level12, <level13
        .byte <level14, <level15, <level16, <level17, <level18
        .byte <level19, <level20, <level21, <level22, <level23
level_ptrs_hi:
        .byte >level1, >level2, >level3
        .byte >level4, >level5, >level6, >level7, >level8
        .byte >level9, >level10, >level11, >level12, >level13
        .byte >level14, >level15, >level16, >level17, >level18
        .byte >level19, >level20, >level21, >level22, >level23

; --- Strings (ASCII, bit 7 added by print_str_ax) ---
str_title:
        .byte $0D, " * SOKOBAN *", $0D
        .byte " GEN2 COLOR GRAPHICS CARD", $0D
        .byte " W=UP S=DOWN A=LEFT D=RIGHT", $0D
        .byte " R=RESET  N=NEXT LEVEL", $0D
        .byte $0D, " PRESS ANY KEY...", $0D, 0

str_level:
        .byte $0D, " LEVEL ", 0

str_win:
        .byte $0D, " LEVEL CLEARED!", $0D
        .byte " ANY KEY = NEXT LEVEL", 0

.include "hgr_tables.inc"
