; =============================================
; HGR SOKOBAN - GEN2 Color Graphics Card
; VERHILLE Arnaud - 2026
; Classic push-boxes puzzle game
; =============================================
; Assemble with cc65:
;   ca65 -o build/HGR6_Sokoban.o software/hgr/HGR6_Sokoban.asm
;   ld65 -C software/games/apple1_sok_hgr.cfg -o build/HGR6_Sokoban.bin build/HGR6_Sokoban.o
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
; Level data is RLE compressed (sokoban_levels.inc +
; sokoban_levels_ext.inc) and shared plumbing lives in
; sokoban_common.inc.
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
NUM_LEVELS = 72

; --- Tile types ---
TILE_FLOOR         = 0
TILE_WALL          = 1
TILE_TARGET        = 2
TILE_BOX           = 3
TILE_BOX_TARGET    = 4
TILE_PLAYER        = 5
TILE_PLAYER_TARGET = 6

; --- Memory layout ---
; STATE_GRID (240 B, 20x12) and LEVEL_BUF (128 B) live in linker-managed
; segments — see software/games/apple1_sok_hgr.cfg for the addresses.
; Target layout on real hardware: 8 KB main DRAM + GEN2 card (framebuffer
; $2000-$3FFF). STATE_GRID at $1F00, LEVEL_BUF at $0020 (ZP).
STATE_GRID_LEN = 240

.segment "LEVELBUF": zeropage
LEVEL_BUF:  .res 128            ; zp,X addressing, max level is 117 B

.segment "STATEGRID"
STATE_GRID: .res 240            ; abs,X addressing (20x12)

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
; Undo state (single-step). Sokoban never calls hgr_tables.inc's
; plot_pixel, so the cur_x/cur_y/mul_tmp/mul_res0 slots it would need
; are reclaimed here.
prev_player_row: .res 1 ; $19
prev_player_col: .res 1 ; $1A
undo_avail:      .res 1 ; $1B  1 = execute_undo is valid, 0 = no move to undo
had_push:        .res 1 ; $1C  1 = last move was a push (box needs undo too)
; Keyboard layout (set once at startup based on user choice)
key_up_code:   .res 1  ; $1D  'W' (QWERTY) or 'Z' (AZERTY)
key_left_code: .res 1  ; $1E  'A' (QWERTY) or 'Q' (AZERTY)
moves:         .res 1  ; $1F  move counter (saturates at $FF, shown next to level #)

; hgr_tables.inc references these for its (unused) plot_pixel routine —
; alias them onto the undo bytes so the assembler resolves the symbols.
; plot_pixel is never called, so the aliasing is harmless.
cur_x    = prev_player_row
cur_y    = prev_player_col
mul_tmp  = undo_avail
mul_res0 = had_push

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

        ; Prompt for keyboard layout
        LDA #<str_layout
        LDX #>str_layout
        JSR print_str_ax

@layout_wait:
        JSR wait_key
        CMP #'1'
        BEQ @qwerty
        CMP #'2'
        BEQ @azerty
        JMP @layout_wait                ; ignore other keys

@qwerty:
        LDA #'W'
        STA key_up_code
        LDA #'A'
        STA key_left_code
        JMP @layout_ok

@azerty:
        LDA #'Z'
        STA key_up_code
        LDA #'Q'
        STA key_left_code

@layout_ok:
        ; Start at level 0
        LDA #$00
        STA level_idx

game_loop:
        JSR init_level
        JSR clear_hgr
        JSR render_all
        ; Reset undo + move counter for the new level
        LDA #$00
        STA undo_avail
        STA had_push
        STA moves
        JSR show_level

move_loop:
        JSR wait_key
        ; A = ASCII char, bit 7 stripped

        CMP key_up_code                 ; W (QWERTY) or Z (AZERTY)
        BEQ key_up
        CMP #'S'
        BEQ key_down
        CMP key_left_code               ; A (QWERTY) or Q (AZERTY)
        BEQ key_left
        CMP #'D'
        BEQ key_right
        CMP #'R'
        BEQ key_reset
        CMP #'N'
        BEQ key_next
        CMP #'U'
        BEQ key_undo
        JMP move_loop                   ; ignore unknown keys

key_undo:
        JSR execute_undo
        JMP move_loop

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
; init_level: RLE-expand level into state grid at $4000
; =============================================
init_level:
        JSR load_level                  ; fills LEVEL_BUF, sets lvl_w/h + offsets

        ; Clear 240-byte state grid
        LDY #$00
        TYA
@clr:   STA STATE_GRID,Y
        INY
        CPY #240
        BNE @clr

        LDA #$00
        STA temp                        ; temp = parse_row
        STA sptr_lo                     ; reuse sptr_lo as flat LEVEL_BUF index
@rowlp:
        LDY #$00                        ; Y = parse_col
@collp:
        LDX sptr_lo
        LDA LEVEL_BUF,X
        JSR ascii_to_tile               ; A = tile type
        PHA                             ; save tile type

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

        INC sptr_lo
        INY
        CPY lvl_w
        BCC @collp

        INC temp
        LDA temp
        CMP lvl_h
        BCS @init_done
        JMP @rowlp
@init_done:
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
        ; Plain walk (no push): clear had_push; push paths set it to 1.
        LDA #$00
        STA had_push
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
        LDA #$01
        STA had_push
        JMP @box_done
@push_target:
        LDA #TILE_BOX_TARGET
        STA STATE_GRID,X
        LDA #$01
        STA had_push
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

        ; Save undo state BEFORE overwriting player_row/col
        LDA player_row
        STA prev_player_row
        LDA player_col
        STA prev_player_col
        LDA #$01
        STA undo_avail

        LDA new_row
        STA player_row
        LDA new_col
        STA player_col

        ; Increment move counter (saturate at $FF)
        INC moves
        BNE @no_sat
        LDA #$FF
        STA moves
@no_sat:

        LDA #$01
        RTS

@blocked_bot:
@blocked:
        LDA #$00
        RTS

; =============================================
; execute_undo: reverse the last successful move (single-step).
; No-op if undo_avail = 0.
; Updates the state grid AND delta-redraws the 2-3 affected tiles.
; =============================================
execute_undo:
        LDA undo_avail
        BNE @do_undo
        RTS
@do_undo:
        ; If last move was a push, the box sits at (player + dir) where
        ; dir = player - prev_player. So box = 2*player - prev_player.
        LDA had_push
        BEQ @skip_box

        LDA player_row
        ASL A
        SEC
        SBC prev_player_row
        STA box_row
        LDA player_col
        ASL A
        SEC
        SBC prev_player_col
        STA box_col

        LDX box_row
        LDA row_x20,X
        CLC
        ADC box_col
        TAX
        LDA STATE_GRID,X
        JSR leave_tile                  ; box vacates: 3->0, 4->2
        STA STATE_GRID,X
        LDA box_row
        STA draw_row
        LDA box_col
        STA draw_col
        LDA STATE_GRID,X
        JSR draw_tile
@skip_box:

        ; Remove player from its current cell
        LDX player_row
        LDA row_x20,X
        CLC
        ADC player_col
        TAX
        LDA STATE_GRID,X
        JSR leave_tile                  ; player vacates: 5->0, 6->2
        STA STATE_GRID,X

        ; If had_push, drop a box back where the player was standing
        LDA had_push
        BEQ @draw_cur
        LDA STATE_GRID,X                ; X = current player cell
        JSR enter_as_box                ; 0->3, 2->4
        STA STATE_GRID,X
@draw_cur:
        LDA player_row
        STA draw_row
        LDA player_col
        STA draw_col
        LDA STATE_GRID,X
        JSR draw_tile

        ; Put player back at prev_player
        LDX prev_player_row
        LDA row_x20,X
        CLC
        ADC prev_player_col
        TAX
        LDA STATE_GRID,X
        JSR enter_player                ; 0->5, 2->6
        STA STATE_GRID,X
        LDA prev_player_row
        STA draw_row
        LDA prev_player_col
        STA draw_col
        LDA STATE_GRID,X
        JSR draw_tile

        ; Restore player coords
        LDA prev_player_row
        STA player_row
        LDA prev_player_col
        STA player_col

        ; Decrement move counter (floor at 0)
        LDA moves
        BEQ @no_dec
        DEC moves
@no_dec:

        ; Single-step undo only: clear the undo latch.
        LDA #$00
        STA undo_avail
        STA had_push
        RTS

; =============================================
; show_level: display "LEVEL NN" on Apple 1 (2-digit)
; =============================================
show_level:
        LDA #<str_level
        LDX #>str_level
        JSR print_str_ax
        LDA level_idx
        CLC
        ADC #$01
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
; Shared routines + tile-state tables
; =============================================
.include "../games/sokoban_common.inc"

; =============================================
; DATA
; =============================================

; --- row * 20 for rows 0..11 ---
row_x20:
        .byte   0,  20,  40,  60,  80, 100, 120, 140
        .byte 160, 180, 200, 220

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
; Tile 5: PLAYER
        .byte $70,$01, $78,$03, $18,$03, $78,$03
        .byte $70,$01, $7C,$07, $7E,$0F, $70,$01
        .byte $70,$01, $78,$03, $0C,$06, $0C,$06
        .byte $0C,$06, $0E,$0E, $00,$00, $00,$00
; Tile 6: PLAYER ON TARGET — same figure + target line at row 14
        .byte $70,$01, $78,$03, $18,$03, $78,$03
        .byte $70,$01, $7C,$07, $7E,$0F, $70,$01
        .byte $70,$01, $78,$03, $0C,$06, $0C,$06
        .byte $0C,$06, $0E,$0E, $7C,$1F, $00,$00

; --- Level data (RLE compressed) ---
.include "../games/sokoban_levels.inc"
.include "../games/sokoban_levels_ext.inc"

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
        .byte <level46, <level47, <level48, <level49, <level50
        .byte <level51, <level52, <level53, <level54, <level55
        .byte <level56, <level57, <level58, <level59, <level60
        .byte <level61, <level62, <level63, <level64, <level65
        .byte <level66, <level67, <level68, <level69, <level70
        .byte <level71, <level72
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
        .byte >level46, >level47, >level48, >level49, >level50
        .byte >level51, >level52, >level53, >level54, >level55
        .byte >level56, >level57, >level58, >level59, >level60
        .byte >level61, >level62, >level63, >level64, >level65
        .byte >level66, >level67, >level68, >level69, >level70
        .byte >level71, >level72

; --- Strings ---
str_title:
        .byte $0D, " * SOKOBAN * ", $0D
        .byte " APPLE 1 + GEN2 COLOR CARD", $0D
        .byte " PORT BY VERHILLE ARNAUD, 2026", $0D
        .byte $0D
        .byte " LEVELS 4-72: MICROBAN I", $0D
        .byte " CLASSIC SET BY D.W. SKINNER", $0D
        .byte $0D
        .byte " PUSH ALL BOXES ONTO TARGETS.", $0D
        .byte " BOXES CAN ONLY BE PUSHED --", $0D
        .byte " NEVER PULLED! WATCH CORNERS!", $0D
        .byte $0D
        .byte " U=UNDO  R=RESET  N=NEXT", $0D, 0

str_layout:
        .byte $0D, " KEYBOARD LAYOUT ?", $0D
        .byte "  1 = QWERTY  (W/A/S/D)", $0D
        .byte "  2 = AZERTY  (Z/Q/S/D)", $0D, 0

str_level:
        .byte $0D, " LEVEL ", 0

str_win:
        .byte $0D, " LEVEL CLEARED!", $0D
        .byte " ANY KEY = NEXT LEVEL", 0

.include "hgr_tables.inc"
