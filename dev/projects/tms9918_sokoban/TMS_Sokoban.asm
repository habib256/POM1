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
        .import tms9918_pad12  ; silicon-strict pad12-v3 (helper from tms9918_pad.asm)
ECHO    = $FFEF
KBD     = $D010
KBDCR   = $D011

; --- TMS9918 I/O (VDP_DATA / VDP_CTRL + WAIT_VBLANK macro) ---
.include "tms9918.inc"

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
        ; Bring up the TMS9918 first so the graphical title is visible
        ; while the Apple-1 text screen shows the credits + prompt.
        JSR init_vdp
        JSR draw_title_tms

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
        JSR draw_hud

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
        CMP #'H'
        BEQ key_help
        JMP move_loop

key_undo:
        JSR execute_undo
        JMP move_loop

key_help:
        JSR draw_help_tms
        JSR wait_key
        JSR render_all                  ; repaint the playfield
        JSR draw_hud                    ; repaint the two-corner HUD
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
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

        ; Victory — splash on the VDP, summary on the Apple-1 text
        ; screen. wait_key holds the display until any key.
        LDA #<str_win
        LDX #>str_win
        JSR print_str_ax
        JSR draw_success_tms
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
        ; The register loop must survive entry with R1 already display-ON
        ; (e.g. re-launching from Wozmon after another game left $C2 in
        ; R1). The auto-patcher injects JSR pad40 intra-pair (between
        ; ORA #$80 and STA VDP_CTRL cmd) and inter-iter (loop-back
        ; detection drops a pad before BNE @regloop). Once iter 1 (X=1)
        ; commits R1 with bit 6 cleared, the gate drops to 16c for the
        ; rest of init.
        LDX #$00
@regloop:
        LDA vdp_regs,X
        CPX #1
        BNE @reg_store
        AND #$BF                        ; force R1 display=OFF for this pass
@reg_store:
        STA VDP_CTRL
        TXA
        ORA #$80                        ; register write flag
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STA VDP_CTRL
        INX
        CPX #$08
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @regloop

        ; --- Load 7 tile patterns at pattern-table offsets 0, 64, 128, ... ---
        LDX #$00
@patloop:
        ; Set VRAM write addr = $00{vram_lo tbl}
        LDA tile_vram_lo,X
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
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
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BCC @pb

        PLA
        TAX
        INX
        CPX #$07
        BNE @patloop

        ; --- Upload HUD + title glyph patterns at VRAM $01C0 ---
        ; (chars 56..94 = 39 glyphs x 8 = 312 bytes; two blocks because
        ; the loop counter is 8-bit.)
        LDA #$C0
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$41                        ; $01 | $40
        STA VDP_CTRL
        LDX #$00
@hudpat1:
        LDA hud_patterns,X
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STA VDP_DATA
        INX
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @hudpat1                    ; writes 256 bytes
        LDX #$00
@hudpat2:
        LDA hud_patterns+256,X
        STA VDP_DATA
        INX
        CPX #56                         ; remaining 312-256 = 56 bytes
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BCC @hudpat2

        ; --- Load 12 colour bytes at colour table $2000 (groups 0..11) ---
        ; Groups 0..6 = tile colours. Groups 7..11 cover chars 56..95
        ; (HUD + title letters, all white).
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$60                        ; $20 | $40 (write flag)
        STA VDP_CTRL
        LDX #$00
@colloop:
        LDA tile_colors,X
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STA VDP_DATA
        INX
        CPX #$0C
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @colloop

        ; --- Clear name table ($1800, 768 bytes = char 0 = blank floor) ---
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$58                        ; $18 | $40
        STA VDP_CTRL
        LDX #$03                        ; 3 pages
        LDA #$00
@np:    LDY #$00
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
@nb:    STA VDP_DATA
        INY
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @nb
        DEX
        BNE @np

        ; --- Disable sprites: first sprite Y = $D0 stops the chain ---
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$5B                        ; $1B | $40
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$D0
        STA VDP_DATA

        ; --- Final: re-arm R1 with display ON. Display stays OFF until the
        ;     cmd byte commits — threshold = 2c through both STAs, no pad
        ;     needed inline. The caller's next VDP write picks up 16c gating.
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA vdp_regs+1
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$81
        STA VDP_CTRL
        RTS

; =============================================
; render_all: draw the whole 16x12 playfield by calling draw_cell
; Playfield fills the full 32x24 screen (each tile is 2x2 chars).
; =============================================
render_all:
        ; Sync to VBlank before the full-playfield rebuild burst.
        WAIT_VBLANK
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
        ; NOTE: no WAIT_VBLANK here — draw_cell is the inner routine
        ; called 192 times in render_all's loop (16 cols x 12 rows).
        ; A per-cell vblank wait would stall a level redraw to ~3 s.
        ; render_all already syncs once at its entry. Per-move single-
        ; cell repaints (execute_move / execute_undo) are 2-3 calls
        ; that complete in ~100 us — fast enough to not visibly tear.
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
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA temp2
        CLC
        ADC #$18
        ORA #$40
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STX VDP_DATA                    ; TL = base+0
        INX
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
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
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA temp2
        CLC
        ADC #$18
        ORA #$40
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STX VDP_DATA                    ; BL = base+2
        INX
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
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
        JSR draw_hud

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
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR draw_cell

        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA prev_player_row
        STA player_row
        LDA prev_player_col
        STA player_col

        LDA moves
        BEQ @no_dec
        DEC moves
@no_dec:
        JSR draw_hud

        LDA #$00
        STA undo_avail
        STA had_push
        RTS

; =============================================
; draw_hud: render the two-corner HUD into the VDP name table.
;   - Top-left  (row 0 cols 0-5)   : "MV:NNN"
;   - Bottom-right (row 23 cols 28-31) : "L:NN"
; Row 1 cols 0-5 and row 22 cols 28-31 are also cleared to char 0
; (floor) so the "other half" of the 2x2 tile underneath doesn't poke
; through. Called after render_all and after every change to `moves`.
; The L:NN value follows `level_idx+1` and is redundantly rewritten on
; every move — cheap, keeps the logic flat.
; =============================================
HUD_C_M   = 56
HUD_C_V   = 57
HUD_C_CL  = 58
HUD_C_D0  = 59                          ; '0'..'9' at chars 59..68
HUD_C_L   = 78                          ; 'L' glyph char (from title set)

draw_hud:
        ; --- Row 0: "MV:HTU" at VRAM $1800 ---
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$58                        ; $18 | $40
        STA VDP_CTRL

        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #HUD_C_M
        STA VDP_DATA
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #HUD_C_V
        STA VDP_DATA
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #HUD_C_CL
        STA VDP_DATA

        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA moves
        LDX #$00
@h100:  CMP #100
        BCC @h100d
        SBC #100
        INX
        JMP @h100
@h100d: STA temp                        ; remainder
        TXA
        CLC
        ADC #HUD_C_D0
        STA VDP_DATA                    ; hundreds char

        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA temp
        LDX #$00
@t10:   CMP #$0A
        BCC @t10d
        SBC #$0A
        INX
        JMP @t10
@t10d:  STA temp                        ; ones remainder
        TXA
        CLC
        ADC #HUD_C_D0
        STA VDP_DATA                    ; tens char

        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA temp
        CLC
        ADC #HUD_C_D0
        STA VDP_DATA                    ; ones char

        ; --- Row 1 cols 0-5: blank floor (char 0) to hide underlying BL/BR ---
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$20                        ; $1800 + 32 = $1820 low byte
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$58
        STA VDP_CTRL
        LDX #$06
        LDA #$00
@clr_tl:
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STA VDP_DATA
        DEX
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @clr_tl

        ; --- Row 23 cols 28-31: "L:NN" at VRAM $1AFC ---
        LDA #$FC
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$5A                        ; $1A | $40
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #HUD_C_L
        STA VDP_DATA
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #HUD_C_CL
        STA VDP_DATA

        ; level_idx is 0-based; display as 1-based 2-digit decimal.
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA level_idx
        CLC
        ADC #$01
        LDX #$00
@lt10:  CMP #$0A
        BCC @lt10d
        SBC #$0A
        INX
        JMP @lt10
@lt10d: STA temp                        ; ones remainder
        TXA
        CLC
        ADC #HUD_C_D0
        STA VDP_DATA                    ; tens
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA temp
        CLC
        ADC #HUD_C_D0
        STA VDP_DATA                    ; ones

        ; --- Row 22 cols 28-31: blank to hide top-half of row-11 tiles ---
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$DC
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$5A
        STA VDP_CTRL
        LDX #$04
        LDA #$00
@clr_br:
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STA VDP_DATA
        DEX
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @clr_br
        RTS

; =============================================
; draw_title_tms: splash screen shown at boot on the TMS9918 while
; the Apple-1 text screen prints credits + layout prompt. Writes six
; centred lines of info to the name table (SOKOBAN / APPLE 1 /
; TMS9918 VDP / BY VERHILLE ARNAUD / keyboard hints). Each line is a
; raw-char-code string; draw_title_tms_line sets the VRAM write
; address then emits chars until $FF. The title is wiped by the first
; render_all in game_loop (every cell rewritten with tile chars).
; =============================================
draw_title_tms:
        ; "SOKOBAN" row 3 col 12 -> $186C
        LDA #<title_sokoban_tms
        STA sptr_lo
        LDA #>title_sokoban_tms
        STA sptr_hi
        LDA #$6C
        LDX #$58
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR draw_title_tms_line

        ; "APPLE 1" at row 7 col 12   -> $18EC
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #<title_apple_tms
        STA sptr_lo
        LDA #>title_apple_tms
        STA sptr_hi
        LDA #$EC
        LDX #$58
        JSR draw_title_tms_line

        ; "TMS9918 VDP" at row 8 col 10 -> $190A
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #<title_card_tms
        STA sptr_lo
        LDA #>title_card_tms
        STA sptr_hi
        LDA #$0A
        LDX #$59                        ; $19 | $40
        JSR draw_title_tms_line

        ; "BY VERHILLE ARNAUD" at row 11 col 7 -> $1967
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #<title_author_tms
        STA sptr_lo
        LDA #>title_author_tms
        STA sptr_hi
        LDA #$67
        LDX #$59
        JSR draw_title_tms_line

        ; "SELECT KEYBOARD" row 12 col 8 -> $1988 (15 chars)
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #<title_select_kb_tms
        STA sptr_lo
        LDA #>title_select_kb_tms
        STA sptr_hi
        LDA #$88
        LDX #$59
        JSR draw_title_tms_line

        ; "1 QWERTY (WASD)" row 15 col 8 -> $19E8
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #<title_qwerty_tms
        STA sptr_lo
        LDA #>title_qwerty_tms
        STA sptr_hi
        LDA #$E8
        LDX #$59
        JSR draw_title_tms_line

        ; "2 AZERTY (ZQSD)" row 16 col 8 -> $1A08
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #<title_azerty_tms
        STA sptr_lo
        LDA #>title_azerty_tms
        STA sptr_hi
        LDA #$08
        LDX #$5A
        JSR draw_title_tms_line

        ; "H HELP" row 20 col 12 -> $1A8C
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #<title_h_help_tms
        STA sptr_lo
        LDA #>title_h_help_tms
        STA sptr_hi
        LDA #$8C
        LDX #$5A
        JMP draw_title_tms_line         ; tail-call

; =============================================
; draw_help_tms: show a key reference in the name table. Clears then
; lists every key the game responds to. Caller waits for any key to
; dismiss and calls render_all + draw_hud to restore the playfield.
; =============================================
draw_help_tms:
        ; Clear name table (3 × 256 bytes of char 0).
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$58
        STA VDP_CTRL
        LDX #$03
        LDA #$00
@np:    LDY #$00
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
@nb:    STA VDP_DATA
        INY
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @nb
        DEX
        BNE @np

        ; "HELP" row 3 col 14 -> $186E
        LDA #<help_title_tms
        STA sptr_lo
        LDA #>help_title_tms
        STA sptr_hi
        LDA #$6E
        LDX #$58
        JSR draw_title_tms_line

        ; "QWERTY (WASD)" row 6 col 10 -> $18CA (align with command rows)
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #<help_qwerty_tms
        STA sptr_lo
        LDA #>help_qwerty_tms
        STA sptr_hi
        LDA #$CA
        LDX #$58
        JSR draw_title_tms_line

        ; "AZERTY (ZQSD)" row 7 col 10 -> $18EA
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #<help_azerty_tms
        STA sptr_lo
        LDA #>help_azerty_tms
        STA sptr_hi
        LDA #$EA
        LDX #$58
        JSR draw_title_tms_line

        ; "U UNDO" row 10 col 10 (align with R/N/H — key in col 10, verb from 12)
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #<help_u_tms
        STA sptr_lo
        LDA #>help_u_tms
        STA sptr_hi
        LDA #$4A
        LDX #$59
        JSR draw_title_tms_line

        ; "R RESET" row 11 col 10 -> $196A
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #<help_r_tms
        STA sptr_lo
        LDA #>help_r_tms
        STA sptr_hi
        LDA #$6A
        LDX #$59
        JSR draw_title_tms_line

        ; "N NEXT" row 12 col 10 -> $198A
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #<help_n_tms
        STA sptr_lo
        LDA #>help_n_tms
        STA sptr_hi
        LDA #$8A
        LDX #$59
        JSR draw_title_tms_line

        ; "H HELP" row 13 col 10 -> $19AA
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #<help_h_tms
        STA sptr_lo
        LDA #>help_h_tms
        STA sptr_hi
        LDA #$AA
        LDX #$59
        JSR draw_title_tms_line

        ; "ANY KEY BACK" at row 20 col 10 -> $1A8A
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #<help_back_tms
        STA sptr_lo
        LDA #>help_back_tms
        STA sptr_hi
        LDA #$8A
        LDX #$5A
        JMP draw_title_tms_line         ; tail-call

; draw_title_tms_line: program VRAM write addr (A = low, X = high|$40)
; then emit raw char codes from (sptr_lo/hi) until $FF.
draw_title_tms_line:
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STX VDP_CTRL
        LDY #$00
@lp:    LDA (sptr_lo),Y
        CMP #$FF
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BEQ @done
        STA VDP_DATA
        INY
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JMP @lp
@done:  RTS

; =============================================
; draw_success_tms: level-complete splash. Clears the whole name table
; (768 char-0 cells = all black), then writes "SUCCESS" and
; "PRESS A KEY" centred on rows 10 and 13. The next render_all in
; game_loop rewrites the playfield.
; =============================================
draw_success_tms:
        ; Clear name table — 3 × 256 bytes of char 0 at $1800.
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$58                        ; $18 | $40
        STA VDP_CTRL
        LDX #$03
        LDA #$00
@np:    LDY #$00
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
@nb:    STA VDP_DATA
        INY
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @nb
        DEX
        BNE @np

        ; "SUCCESS" row 10 col 12 -> $194C
        LDA #<title_success_tms
        STA sptr_lo
        LDA #>title_success_tms
        STA sptr_hi
        LDA #$4C
        LDX #$59                        ; $19 | $40
        JSR draw_title_tms_line

        ; "PRESS A KEY" row 13 col 10 -> $19AA
        LDA #<title_press_key_tms
        STA sptr_lo
        LDA #>title_press_key_tms
        STA sptr_hi
        LDA #$AA
        LDX #$59
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JMP draw_title_tms_line         ; tail-call

; --- TMS title strings (raw TMS char codes, $FF terminated) ---
; Char assignments:
;   56=M  57=V  58=:  59..68='0'..'9'  69=S  70=O  71=K  72=B  73=A
;   74=N  75=P  76=R  77=E  78=L  79=space  80=G  81=H  82=T  83=D
;   84=Y  85=I  86=U  87=Q  88=W  89=Z  90=C  91=X  92=F  93=(  94=)
title_select_kb_tms:
        ; SELECT KEYBOARD
        .byte 69,77,78,77,90,82,79,71,77,84,72,70,73,76,83, $FF
title_sokoban_tms:
        ; S  O  K  O  B  A  N
        .byte 69,70,71,70,72,73,74, $FF
title_apple_tms:
        ; A  P  P  L  E  _  1
        .byte 73,75,75,78,77,79,60, $FF
title_card_tms:
        ; T  M  S  9  9  1  8  _  V  D  P
        .byte 82,56,69,68,68,60,67,79,57,83,75, $FF
title_author_tms:
        ; B  Y  _  V  E  R  H  I  L  L  E  _  A  R  N  A  U  D
        .byte 72,84,79,57,77,76,81,85,78,78,77,79,73,76,74,73,86,83, $FF
title_qwerty_tms:
        ; 1  _  QWERTY  _  ( WASD )
        .byte 60,79,87,88,77,76,82,84,79,93,88,73,69,83,94, $FF
title_azerty_tms:
        ; 2  _  AZERTY  _  ( ZQSD )
        .byte 61,79,73,89,77,76,82,84,79,93,89,87,69,83,94, $FF
title_success_tms:
        ; S  U  C  C  E  S  S
        .byte 69,86,90,90,77,69,69, $FF
title_press_key_tms:
        ; P  R  E  S  S  _  A  _  K  E  Y
        .byte 75,76,77,69,69,79,73,79,71,77,84, $FF
title_h_help_tms:
        ; H  _  H  E  L  P
        .byte 81,79,81,77,78,75, $FF

; --- Help-screen strings (TMS raw char codes) ---
help_title_tms:
        ; H  E  L  P
        .byte 81,77,78,75, $FF
help_qwerty_tms:
        ; QWERTY  ( WASD )
        .byte 87,88,77,76,82,84,79,93,88,73,69,83,94, $FF
help_azerty_tms:
        ; AZERTY  ( ZQSD )
        .byte 73,89,77,76,82,84,79,93,89,87,69,83,94, $FF
help_u_tms:
        ; U  _  U  N  D  O
        .byte 86,79,86,74,83,70, $FF
help_r_tms:
        ; R  _  R  E  S  E  T
        .byte 76,79,76,77,69,77,82, $FF
help_n_tms:
        ; N  _  N  E  X  T
        .byte 74,79,74,77,91,82, $FF
help_h_tms:
        ; H  _  H  E  L  P
        .byte 81,79,81,77,78,75, $FF
help_back_tms:
        ; A  N  Y  _  K  E  Y  _  B  A  C  K
        .byte 73,74,84,79,71,77,84,79,72,73,90,71, $FF

; =============================================
; Shared routines + tile-state tables
; =============================================
.include "sokoban_common.inc"

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
; Entries 0..6 = tile types. Entries 7..9 colour the HUD + title glyphs
; at chars 56..79 (three 8-char colour groups of white-on-black).
tile_colors:
        .byte $11       ; Tile 0 floor         fg=1  black  (invisible)
        .byte $E1       ; Tile 1 wall          fg=14 grey
        .byte $81       ; Tile 2 target        fg=8  medium red
        .byte $A1       ; Tile 3 box           fg=10 dark yellow
        .byte $31       ; Tile 4 box+target    fg=3  light green
        .byte $51       ; Tile 5 player        fg=5  light blue
        .byte $21       ; Tile 6 player+target fg=2  medium green
        .byte $F1       ; Group 7  chars 56-63  fg=15 white
        .byte $F1       ; Group 8  chars 64-71  fg=15 white
        .byte $F1       ; Group 9  chars 72-79  fg=15 white
        .byte $F1       ; Group 10 chars 80-87  fg=15 white
        .byte $F1       ; Group 11 chars 88-95  fg=15 white

; --- HUD + title glyph patterns (8x8, uploaded to VRAM $01C0) ---
; 5-pixel-wide glyphs centred at columns 1..5 (MSB = leftmost pixel).
; Chars 56..68 are the HUD set (MV: + digits); chars 69..79 add the
; title letters S,O,K,B,A,N,P,R,E,L and one space glyph.
hud_patterns:
        ; char 56 'M'
        .byte $44, $6C, $54, $44, $44, $44, $44, $00
        ; char 57 'V'
        .byte $44, $44, $44, $44, $28, $28, $10, $00
        ; char 58 ':'
        .byte $00, $00, $10, $00, $00, $10, $00, $00
        ; char 59 '0'
        .byte $38, $44, $44, $44, $44, $44, $38, $00
        ; char 60 '1'
        .byte $10, $30, $10, $10, $10, $10, $38, $00
        ; char 61 '2'
        .byte $38, $44, $04, $08, $10, $20, $7C, $00
        ; char 62 '3'
        .byte $38, $44, $04, $38, $04, $44, $38, $00
        ; char 63 '4'
        .byte $44, $44, $44, $7C, $04, $04, $04, $00
        ; char 64 '5'
        .byte $7C, $40, $40, $78, $04, $44, $38, $00
        ; char 65 '6'
        .byte $18, $20, $40, $78, $44, $44, $38, $00
        ; char 66 '7'
        .byte $7C, $04, $08, $10, $20, $20, $20, $00
        ; char 67 '8'
        .byte $38, $44, $44, $38, $44, $44, $38, $00
        ; char 68 '9'
        .byte $38, $44, $44, $3C, $04, $08, $30, $00
        ; char 69 'S'
        .byte $38, $44, $40, $38, $04, $44, $38, $00
        ; char 70 'O' (distinct char from '0'; same silhouette)
        .byte $38, $44, $44, $44, $44, $44, $38, $00
        ; char 71 'K'
        .byte $44, $48, $50, $60, $50, $48, $44, $00
        ; char 72 'B'
        .byte $78, $44, $44, $78, $44, $44, $78, $00
        ; char 73 'A'
        .byte $38, $44, $44, $7C, $44, $44, $44, $00
        ; char 74 'N'
        .byte $44, $64, $54, $4C, $44, $44, $44, $00
        ; char 75 'P'
        .byte $78, $44, $44, $78, $40, $40, $40, $00
        ; char 76 'R'
        .byte $78, $44, $44, $78, $50, $48, $44, $00
        ; char 77 'E'
        .byte $7C, $40, $40, $78, $40, $40, $7C, $00
        ; char 78 'L'
        .byte $40, $40, $40, $40, $40, $40, $7C, $00
        ; char 79 ' ' (space)
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        ; char 80 'G'
        .byte $38, $44, $40, $4E, $44, $44, $38, $00
        ; char 81 'H'
        .byte $44, $44, $44, $7C, $44, $44, $44, $00
        ; char 82 'T'
        .byte $7C, $10, $10, $10, $10, $10, $10, $00
        ; char 83 'D'
        .byte $78, $44, $44, $44, $44, $44, $78, $00
        ; char 84 'Y'
        .byte $44, $44, $28, $10, $10, $10, $10, $00
        ; char 85 'I'
        .byte $38, $10, $10, $10, $10, $10, $38, $00
        ; char 86 'U'
        .byte $44, $44, $44, $44, $44, $44, $38, $00
        ; char 87 'Q'
        .byte $38, $44, $44, $44, $54, $48, $34, $00
        ; char 88 'W'
        .byte $44, $44, $44, $54, $54, $6C, $44, $00
        ; char 89 'Z'
        .byte $7C, $04, $08, $10, $20, $40, $7C, $00
        ; char 90 'C'
        .byte $38, $44, $40, $40, $40, $44, $38, $00
        ; char 91 'X'
        .byte $44, $44, $28, $10, $28, $44, $44, $00
        ; char 92 'F'
        .byte $7C, $40, $40, $78, $40, $40, $40, $00
        ; char 93 '(' — thin slim left paren (curve opens right)
        ;   ........  $00
        ;   ...X....  $10
        ;   ..X.....  $20
        ;   .X......  $40
        ;   .X......  $40
        ;   ..X.....  $20
        ;   ...X....  $10
        ;   ........  $00
        .byte $00, $10, $20, $40, $40, $20, $10, $00
        ; char 94 ')' — thin slim right paren (curve opens left, mirror of char 93)
        ;   ........  $00
        ;   ....X...  $08
        ;   .....X..  $04
        ;   ......X.  $02
        ;   ......X.  $02
        ;   .....X..  $04
        ;   ....X...  $08
        ;   ........  $00
        .byte $00, $08, $04, $02, $02, $04, $08, $00

; --- Strings (ASCII, high bit set by print_str_ax) ---
str_title:
        .byte $0D, " SOKOBAN + TMS9918", $0D
        .byte " MICROBAN I 45LV  V.ARNAUD 26", $0D
        .byte " PUSH BOXES ON TARGETS  U R N", $0D, 0

str_layout:
        .byte $0D, " KEYBOARD: 1 QWERTY  2 AZERTY", $0D, 0

str_win:
        .byte $0D, " CLEARED! KEY=NEXT", $0D, 0

; --- Level data (RLE compressed, shared with text + HGR variants) ---
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
