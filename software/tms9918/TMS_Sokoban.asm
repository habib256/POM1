; =============================================
; SOKOBAN for P-LAB TMS9918 Graphic Card
; VERHILLE Arnaud - 2026
; Classic push-boxes puzzle
; Levels 4-47 = Microban I #1..#44 by David W. Skinner
; =============================================
; Assemble with cc65:
;   ca65 -o build/TMS_Sokoban.o software/tms9918/TMS_Sokoban.asm
;   ld65 -C software/games/apple1_sok_8k.cfg -o build/TMS_Sokoban.bin build/TMS_Sokoban.o
;
; Load in POM1 via File > Load Memory (TMS_Sokoban.txt), then 280R.
; The TMS9918 card must be enabled (Hardware menu).
;
; Display: 32x24 Graphics I mode. Each Sokoban tile = one 16x16
; block (a 2x2 grid of 8x8 chars). The 7 tile types each live in
; their own "colour group" (chars 0, 8, 16, 24, 32, 40, 48) so
; they pick up their own palette colour.
;
; Target: real Apple 1 stock 8K + TMS9918 card.
;   - STATE_GRID lives at $1F00 (192 B) via the STATEGRID segment.
;   - LEVEL_BUF lives at $0020 (128 B in ZP) via the LEVELBUF segment.
; RLE-compressed level data: see sokoban_levels.inc.
; Shared routines: see sokoban_common.inc.
; =============================================

; --- Apple 1 I/O ---
ECHO    = $FFEF
KBD     = $D010
KBDCR   = $D011

; --- TMS9918 I/O ---
VDP_DATA = $CC00
VDP_CTRL = $CC01

; --- Game constants ---
NCOLS   = 16
NROWS   = 12
NUM_LEVELS = 45                 ; 3 teaching + Microban I #1..#42

; --- Tile types ---
TILE_FLOOR         = 0
TILE_WALL          = 1
TILE_TARGET        = 2
TILE_BOX           = 3
TILE_BOX_TARGET    = 4
TILE_PLAYER        = 5
TILE_PLAYER_TARGET = 6

; --- Memory layout ---
; STATE_GRID (192 B, 16x12) and LEVEL_BUF (128 B) are now declared as
; linker segments below. STATE_GRID_LEN is consumed by check_win in
; sokoban_common.inc.
STATE_GRID_LEN = 192

.segment "LEVELBUF": zeropage
LEVEL_BUF:  .res 128            ; ZP segment → zp,X addressing

.segment "STATEGRID"
STATE_GRID: .res 192            ; BSS segment → abs,X addressing

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
; --- Undo state + move counter ---
prev_player_row: .res 1 ; $19
prev_player_col: .res 1 ; $1A
undo_avail:      .res 1 ; $1B  1 = execute_undo is valid, 0 = no move to undo
had_push:        .res 1 ; $1C  1 = last move was a push (box needs undo too)
moves:           .res 1 ; $1D  move counter (saturates at $FF)

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
        ; Reset undo state + move counter for the new level
        LDA #$00
        STA undo_avail
        STA had_push
        STA moves

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

        ; src = tile_patterns + X*32  (each tile is 4 glyphs x 8 bytes)
        TXA
        PHA
        ASL A
        ASL A
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
        CPY #$20                        ; 32 bytes per tile (4 glyphs)
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
; render_all: draw the whole 16x12 playfield by calling draw_cell
; Playfield fills the full 32x24 screen (each tile is 2x2 chars).
; =============================================
render_all:
        LDA #$00
        STA draw_row
@rowlp:
        LDA #$00
        STA draw_col
@collp:
        LDX draw_row
        LDA row_x16,X
        CLC
        ADC draw_col
        TAX
        LDA STATE_GRID,X
        JSR draw_cell

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
; draw_cell: draw one 16x16 Sokoban tile as a 2x2 name-table block
; Input: A = tile type (0..6), draw_row, draw_col
; Layout in name table: top-left at (draw_row*2, draw_col*2)
;   VRAM addr of TL = $1800 + (draw_row*2)*32 + (draw_col*2)
;                   = $1800 + draw_row*64 + draw_col*2
; Each tile occupies 4 consecutive character codes (TL, TR, BL, BR),
; starting at base = tile * 8 (so tiles live at chars 0, 8, 16, ..., 48).
; =============================================
draw_cell:
        ; base_char = tile * 8  (also the colour-group trick: chars 0, 8, ...)
        ASL A
        ASL A
        ASL A
        TAX                             ; X = base char code

        ; Compute name-table offset = draw_row*64 + draw_col*2
        LDY draw_row
        LDA row_x64_lo,Y
        STA temp
        LDA row_x64_hi,Y
        STA temp2

        LDA draw_col
        ASL A
        CLC
        ADC temp
        STA temp
        LDA temp2
        ADC #$00
        STA temp2

        ; --- Top name row: write TL, TR ---
        LDA temp
        STA VDP_CTRL
        LDA temp2
        CLC
        ADC #$18
        ORA #$40
        STA VDP_CTRL
        STX VDP_DATA                    ; TL = base+0
        INX
        STX VDP_DATA                    ; TR = base+1
        INX

        ; Advance offset by 32 (next name row)
        CLC
        LDA temp
        ADC #$20
        STA temp
        LDA temp2
        ADC #$00
        STA temp2

        ; --- Bottom name row: write BL, BR ---
        LDA temp
        STA VDP_CTRL
        LDA temp2
        CLC
        ADC #$18
        ORA #$40
        STA VDP_CTRL
        STX VDP_DATA                    ; BL = base+2
        INX
        STX VDP_DATA                    ; BR = base+3
        RTS

; =============================================
; init_level: RLE-expand + parse level into state grid
; The stored row_offset/col_offset (targeting a 20x12 grid) are
; discarded here and recomputed for the 16x12 TMS playfield.
; =============================================
init_level:
        JSR load_level                  ; fills LEVEL_BUF, sets lvl_w/h (+ hdr offsets)

        ; Recompute centring for the 16x12 screen
        LDA #NROWS
        SEC
        SBC lvl_h
        LSR A
        STA row_offset
        LDA #NCOLS
        SEC
        SBC lvl_w
        LSR A
        STA col_offset

        ; Clear 192-byte state grid
        LDY #$00
        TYA
@clr:   STA STATE_GRID,Y
        INY
        CPY #192
        BNE @clr

        LDA #$00
        STA temp                        ; parse_row
        STA sptr_lo                     ; flat LEVEL_BUF index (sptr is free after load_level)
@rowlp:
        LDY #$00                        ; parse_col
@collp:
        LDX sptr_lo
        LDA LEVEL_BUF,X
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
        LDA row_x16,X
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
        LDA row_x16,X
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
        ; Plain walk: clear had_push (push paths set it to 1 below).
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
        LDA row_x16,X
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
        JMP @box_drawn
@push_target:
        LDA #TILE_BOX_TARGET
        STA STATE_GRID,X
        LDA #$01
        STA had_push
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
        LDA row_x16,X
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
        LDA row_x16,X
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
@blocked:
        LDA #$00
        RTS

; =============================================
; execute_undo: reverse the last successful move (single-step).
; No-op if undo_avail = 0. Updates the state grid and delta-redraws
; the 2-3 affected tiles.
; =============================================
execute_undo:
        LDA undo_avail
        BNE @do_undo
        RTS
@do_undo:
        ; If the last move was a push, the box sits at 2*player - prev_player.
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
        LDA row_x16,X
        CLC
        ADC box_col
        TAX
        LDA STATE_GRID,X
        JSR leave_tile                  ; 3->0, 4->2
        STA STATE_GRID,X
        LDA box_row
        STA draw_row
        LDA box_col
        STA draw_col
        LDA STATE_GRID,X
        JSR draw_cell
@skip_box:

        ; Remove player from current cell
        LDX player_row
        LDA row_x16,X
        CLC
        ADC player_col
        TAX
        LDA STATE_GRID,X
        JSR leave_tile                  ; 5->0, 6->2
        STA STATE_GRID,X

        ; If had_push, drop a box into the cell the player is vacating
        LDA had_push
        BEQ @draw_cur
        LDA STATE_GRID,X
        JSR enter_as_box                ; 0->3, 2->4
        STA STATE_GRID,X
@draw_cur:
        LDA player_row
        STA draw_row
        LDA player_col
        STA draw_col
        LDA STATE_GRID,X
        JSR draw_cell

        ; Put the player back at prev_player
        LDX prev_player_row
        LDA row_x16,X
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
        JSR draw_cell

        LDA prev_player_row
        STA player_row
        LDA prev_player_col
        STA player_col

        LDA moves
        BEQ @no_dec
        DEC moves
@no_dec:

        LDA #$00
        STA undo_avail
        STA had_push
        RTS

; =============================================
; Shared routines + tile-state tables
; =============================================
.include "../games/sokoban_common.inc"

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

; --- row * 16 (state grid row stride) for rows 0..11 ---
row_x16:
        .byte   0,  16,  32,  48,  64,  80,  96, 112
        .byte 128, 144, 160, 176

; --- row * 64 (name-table stride: each tile is 2 name rows) ---
row_x64_lo:
        .byte $00, $40, $80, $C0, $00, $40, $80, $C0
        .byte $00, $40, $80, $C0
row_x64_hi:
        .byte $00, $00, $00, $00, $01, $01, $01, $01
        .byte $02, $02, $02, $02

; --- Tile patterns (7 tiles x 32 bytes = 224 bytes) ---
; Each tile is a 16x16 sprite split into 4 glyphs of 8x8:
;   TL (rows 0-7,  left byte)   TR (rows 0-7,  right byte)
;   BL (rows 8-15, left byte)   BR (rows 8-15, right byte)
; Designed natively at 16x16 for more detail than a 2x scale-up.
tile_patterns:
        ; --- Tile 0 FLOOR: all black ---
        .byte $00,$00,$00,$00,$00,$00,$00,$00   ; TL
        .byte $00,$00,$00,$00,$00,$00,$00,$00   ; TR
        .byte $00,$00,$00,$00,$00,$00,$00,$00   ; BL
        .byte $00,$00,$00,$00,$00,$00,$00,$00   ; BR

        ; --- Tile 1 WALL: fully solid (grey) ---
        .byte $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF   ; TL
        .byte $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF   ; TR
        .byte $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF   ; BL
        .byte $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF   ; BR

        ; --- Tile 2 TARGET: 8x8 bullseye disc centred (red) ---
        ;   rows 3-12 form a disc of radius ~4
        .byte $00,$00,$00,$03,$07,$07,$0F,$0F   ; TL
        .byte $00,$00,$00,$C0,$E0,$E0,$F0,$F0   ; TR
        .byte $0F,$0F,$07,$07,$03,$00,$00,$00   ; BL
        .byte $F0,$F0,$E0,$E0,$C0,$00,$00,$00   ; BR

        ; --- Tile 3 BOX: wooden crate, 16x16 outline with X diagonal ---
        .byte $FF,$80,$A0,$90,$88,$84,$82,$81   ; TL
        .byte $FF,$01,$05,$09,$11,$21,$41,$81   ; TR
        .byte $81,$82,$84,$88,$90,$A0,$80,$FF   ; BL
        .byte $81,$41,$21,$11,$09,$05,$01,$FF   ; BR

        ; --- Tile 4 BOX-ON-TARGET: solid 12x12 block centred (green) ---
        .byte $00,$00,$3F,$3F,$3F,$3F,$3F,$3F   ; TL
        .byte $00,$00,$FC,$FC,$FC,$FC,$FC,$FC   ; TR
        .byte $3F,$3F,$3F,$3F,$3F,$3F,$00,$00   ; BL
        .byte $FC,$FC,$FC,$FC,$FC,$FC,$00,$00   ; BR

        ; --- Tile 5 PLAYER: humanoid (blue) ---
        .byte $03,$07,$0F,$0C,$0C,$0F,$07,$01   ; TL
        .byte $C0,$E0,$F0,$30,$30,$F0,$E0,$80   ; TR
        .byte $1F,$3F,$CF,$CF,$07,$0C,$0C,$1C   ; BL
        .byte $F8,$FC,$F3,$F3,$E0,$30,$30,$38   ; BR

        ; --- Tile 6 PLAYER-ON-TARGET: same figure (colour group differs) ---
        .byte $03,$07,$0F,$0C,$0C,$0F,$07,$01   ; TL
        .byte $C0,$E0,$F0,$30,$30,$F0,$E0,$80   ; TR
        .byte $1F,$3F,$CF,$CF,$07,$0C,$0C,$1C   ; BL
        .byte $F8,$FC,$F3,$F3,$E0,$30,$30,$38   ; BR

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
        .byte " LEVELS 4-45: MICROBAN I", $0D
        .byte " CLASSIC SET BY D.W. SKINNER", $0D
        .byte $0D
        .byte " PUSH ALL BOXES ONTO TARGETS.", $0D
        .byte " BOXES CAN ONLY BE PUSHED!", $0D
        .byte $0D
        .byte " U=UNDO  R=RESET  N=NEXT", $0D, 0

str_layout:
        .byte $0D, " KEYBOARD LAYOUT ?", $0D
        .byte "  1 = QWERTY  (W/A/S/D)", $0D
        .byte "  2 = AZERTY  (Z/Q/S/D)", $0D, 0

str_win:
        .byte $0D, " LEVEL CLEARED! PRESS A KEY", $0D, 0

; --- Level data (RLE compressed, shared with text + HGR variants) ---
.include "../games/sokoban_levels.inc"

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
