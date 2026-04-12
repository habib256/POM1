; =============================================
; SOKOBAN (text mode) for Apple 1
; VERHILLE Arnaud - 2026
; Classic push-boxes puzzle
; Levels 4-23 = Microban I by David W. Skinner
; =============================================
; Assemble with cc65:
;   ca65 -o build/Sokoban.o software/games/Sokoban.asm
;   ld65 -C software/hgr/apple1_gen2.cfg -o build/Sokoban.bin build/Sokoban.o
; (uses the GEN2 linker config for its 7552-byte CODE budget,
;  even though no HGR card is required to run this text version.)
;
; Load in POM1 via File > Load Memory (Sokoban.txt),
; then type 280R in Woz Monitor.
;
; Classic Sokoban ASCII rendering on the 40x24 text display:
;   #  wall       .  target        $  box
;   *  box-on-target   @  player   +  player-on-target
; 20x12 playfield, 47 levels.  No HGR required.
;
; Shared code lives in sokoban_common.inc and level data (RLE
; compressed) in sokoban_levels.inc.
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

; --- Memory layout ---
; 20x12 = 240 bytes of state at $4000, RLE scratch buffer just above.
STATE_GRID     = $4000
STATE_GRID_LEN = 240
LEVEL_BUF      = $4100          ; 240-byte decompression scratch

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
; --- Undo state + move counter ---
prev_player_row: .res 1 ; $17  player_row before the last successful move
prev_player_col: .res 1 ; $18
undo_avail:    .res 1   ; $19  1 = execute_undo is valid, 0 = no move to undo
had_push:      .res 1   ; $1A  1 = last move was a push (box needs undo too)
moves:         .res 1   ; $1B  move counter (0..255, saturates at $FF for display)

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
        ; Reset undo state and move counter for the new level
        LDA #$00
        STA undo_avail
        STA had_push
        STA moves
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
        CMP #'U'
        BEQ key_undo
        JMP move_loop

key_undo:
        JSR execute_undo
        JSR render_screen
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
; init_level: expand RLE level, then populate state grid at $4000
; from LEVEL_BUF, applying row_offset / col_offset for centring.
; =============================================
init_level:
        JSR load_level                  ; common: fills LEVEL_BUF, sets lvl_w/h + offsets

        ; Clear 240-byte state grid
        LDY #$00
        TYA
@clr:   STA STATE_GRID,Y
        INY
        CPY #240
        BNE @clr

        LDA #$00
        STA temp                        ; temp = parse_row
        STA sptr_lo                     ; sptr_lo = flat idx into LEVEL_BUF (reused)
@rowlp:
        LDY #$00                        ; Y = parse_col
@collp:
        LDX sptr_lo
        LDA LEVEL_BUF,X
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

        INC sptr_lo
        INY
        CPY lvl_w
        BCC @collp

        INC temp
        LDA temp
        CMP lvl_h
        BCS @done
        JMP @rowlp
@done:  RTS

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
        ; Simple move (no push): clear had_push before proceeding
        ; (push paths set had_push=1 themselves and JMP @simple_move)
        LDA #$00
        STA had_push
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
        LDA #$01
        STA had_push
        JMP @simple_move
@push_target:
        LDA #TILE_BOX_TARGET
        STA STATE_GRID,X
        LDA #$01
        STA had_push
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

        ; Save undo state BEFORE overwriting player_row/col
        ; (only reached on successful move; @blocked returns without touching undo state)
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
@blocked:
        LDA #$00
        RTS

; =============================================
; execute_undo: reverse the last successful move
; Uses prev_player_row/col, had_push, undo_avail. No-op if !undo_avail.
; Modifies state grid only (caller calls render_screen).
; =============================================
execute_undo:
        LDA undo_avail
        BEQ @no_undo

        ; If there was a push, pull the box back first.
        ; Current box position = (player_row + dy, player_col + dx) where
        ;   dy = player_row - prev_player_row, dx = player_col - prev_player_col
        ; So box_row = 2*player_row - prev_player_row, etc.
        LDA had_push
        BEQ @skip_box

        ; box_row/col = 2*player - prev_player
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

        ; Remove box from box_row/col
        LDX box_row
        LDA row_x20,X
        CLC
        ADC box_col
        TAX
        LDA STATE_GRID,X
        JSR leave_tile                  ; (3)→(0), (4)→(2)
        STA STATE_GRID,X
@skip_box:

        ; Remove player from current position
        LDX player_row
        LDA row_x20,X
        CLC
        ADC player_col
        TAX
        LDA STATE_GRID,X
        JSR leave_tile                  ; (5)→(0), (6)→(2)
        STA STATE_GRID,X

        ; If had_push, place a box at the current player cell (reverse the push)
        LDA had_push
        BEQ @skip_place_box
        LDA STATE_GRID,X                ; X still = current player cell idx
        JSR enter_as_box                ; (0)→(3), (2)→(4)
        STA STATE_GRID,X
@skip_place_box:

        ; Put player back at prev_player position
        LDX prev_player_row
        LDA row_x20,X
        CLC
        ADC prev_player_col
        TAX
        LDA STATE_GRID,X
        JSR enter_player                ; (0)→(5), (2)→(6)
        STA STATE_GRID,X

        ; Restore player coords
        LDA prev_player_row
        STA player_row
        LDA prev_player_col
        STA player_col

        ; Decrement move counter (unless it's already 0)
        LDA moves
        BEQ @no_dec
        DEC moves
@no_dec:

        ; Clear undo (only single-step undo supported)
        LDA #$00
        STA undo_avail
        STA had_push

@no_undo:
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

        ; Blank line + "MOVES: NNN" + controls hint + CR
        LDA #<str_moves_prefix
        LDX #>str_moves_prefix
        JSR print_str_ax
        LDA moves
        JSR print_3_digits
        LDA #<str_footer
        LDX #>str_footer
        JSR print_str_ax
        RTS

; =============================================
; print_3_digits: print A as three decimal digits (000..255)
; =============================================
print_3_digits:
        LDX #$00
@h:     CMP #100
        BCC @hd
        SBC #100
        INX
        JMP @h
@hd:
        PHA
        TXA
        ORA #'0'
        ORA #$80
        JSR ECHO
        PLA

        LDX #$00
@t:     CMP #10
        BCC @td
        SBC #10
        INX
        JMP @t
@td:
        PHA
        TXA
        ORA #'0'
        ORA #$80
        JSR ECHO
        PLA

        ORA #'0'
        ORA #$80
        JSR ECHO
        RTS

; =============================================
; Shared routines + tile-state tables
; =============================================
.include "sokoban_common.inc"

; =============================================
; DATA
; =============================================

row_x20:
        .byte   0,  20,  40,  60,  80, 100, 120, 140
        .byte 160, 180, 200, 220

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

str_moves_prefix:
        .byte $0D, " MOVES: ", 0

str_footer:
        .byte "   U=UNDO  R=RESET  N=NEXT", $0D, 0

str_win:
        .byte $0D, " LEVEL CLEARED! PRESS A KEY", $0D, 0

; --- Level data (RLE compressed) ---
.include "sokoban_levels.inc"

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
