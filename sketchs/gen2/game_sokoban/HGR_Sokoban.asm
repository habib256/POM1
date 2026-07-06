; =============================================
; HGR SOKOBAN - GEN2 Color Graphics Card
; VERHILLE Arnaud - 2026
; Classic push-boxes puzzle game
; =============================================
; Assemble with cc65:
;   Build: make
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
; In POM1: plug GEN2 card, File > Load Memory (HGR_Sokoban.txt),
; then type E000R in Woz Monitor.
; =============================================

; --- Apple 1 I/O ---
.include "apple1.inc"

; --- GEN2 soft-switch equates (GEN2_PAGE1 for the V-blank poll below).
;     Include-guarded, so the trailing gen2_init.asm re-include is a no-op. ---
.include "gen2.inc"

; --- Game constants ---
NCOLS   = 20
NROWS   = 12
NUM_LEVELS = 45            ; trimmed from 72 to fit DATA_LOW (3.2 KB low bank)
                           ; on the Parmigiani 8 KB dual-bank — levels
                           ; 46..72 (sokoban_levels_ext.inc) dropped.

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

; --- Boot-time + title/help/HUD scratch (zp at $00A0, free after init) ---
.segment "ZPSCRATCH": zeropage
tbl_lo:          .res 1
tbl_hi:          .res 1
title_ix:        .res 1
title_glyph:     .res 1
title_col_start: .res 1
title_scanline:  .res 1
big_byte0:       .res 1
big_byte1:       .res 1
hud_base_sl:     .res 1

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
        JSR gen2_hgr_init
        ; Graphical splash on the GEN2 framebuffer (visible while the
        ; Apple-1 text screen shows the credits + layout prompt).
        JSR clear_hgr
        JSR draw_title

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
        JSR draw_hud

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
        CMP #'H'
        BEQ key_help
        JMP move_loop                   ; ignore unknown keys

key_undo:
        JSR execute_undo
        JMP move_loop

key_help:
        JSR draw_help
        JSR wait_key                    ; any key dismisses
        ; Repaint the playfield (clears help screen) and restore the HUD.
        JSR clear_hgr
        JSR render_all
        JSR draw_hud
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

        ; Level complete — splash on the HGR screen, summary on the
        ; Apple-1 text screen. wait_key holds both until any key.
        LDA #<str_win
        LDX #>str_win
        JSR print_str_ax
        JSR draw_success
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
        JSR draw_tile_raw               ; bulk redraw: no per-tile V-blank wait
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
; wait_vbl: block until shortly after V-blank begins, so the framebuffer
; writes that follow land while the beam is off-screen — this is what
; kills the shearing when a tile/HUD is redrawn mid-frame.
;
; Polls HST0 (bit 7) at GEN2_PAGE1. The read toggles the addressed switch,
; but the game already runs on page 1, so polling PAGE1 is a no-op on the
; latch. OR of two back-to-back samples (bus accesses land ~4 cycles apart)
; masks the 3-cycle colour-burst notch that would otherwise read 0 inside
; V-blank. A blank that outlives an H-blank's 25 cycles is the V-blank.
; Clobbers A only. Coarse copy of gen2_waitvbl — full derivation in
; dev/lib/gen2/gen2_sync.asm + doc/GEN2_RELEASE.md.
; =============================================
wait_vbl:
@live:  LDA GEN2_PAGE1                  ; wait for live scan (HST0 = 0)
        ORA GEN2_PAGE1
        BMI @live
@blank: LDA GEN2_PAGE1                  ; wait for the next blanking edge
        ORA GEN2_PAGE1
        BPL @blank
        ; 26 idle cycles: an H-blank (25 cycles) is over by now, so a
        ; sample that still reads 1 can only be the V-blank.
        .repeat 13
        NOP
        .endrep
        LDA GEN2_PAGE1
        ORA GEN2_PAGE1
        BPL @live                       ; was just an H-blank — scan next line
        RTS                             ; V-blank: safe to write the framebuffer

; =============================================
; draw_tile: draw 14x16 tile at (draw_row, draw_col), synced to V-blank so
; the write never tears. A single tile (16 scanlines, ~500 cycles) always
; fits inside the ~4200-cycle V-blank window. Used by the interactive
; move/undo paths; render_all uses draw_tile_raw to avoid one V-blank wait
; per tile (240 tiles would stall a whole level-load).
; Input: A = tile type (0-6)
; =============================================
draw_tile:
        PHA                             ; wait_vbl clobbers A — save tile type
        JSR wait_vbl
        PLA
draw_tile_raw:
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
        JSR draw_hud

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
        JSR draw_hud

        ; Single-step undo only: clear the undo latch.
        LDA #$00
        STA undo_avail
        STA had_push
        RTS

; =============================================
; (show_level was removed — the on-screen "L:NN" in the bottom-right
;  HGR HUD makes the Apple-1 text echo redundant.)
; =============================================
; draw_hud: render "MV:NNN" in the top-left (tile row 0, cells 0-5)
; and "L:NN" in the bottom-right (tile row 11, cells 16-19). Called
; on level load and after every change to `moves`; the bottom display
; updates on every call but its value only changes when the level
; changes, so the redundant writes are harmless.
; =============================================
; Glyph indices into hud_font (see data below):
HUD_G_M  = 10
HUD_G_V  = 11
HUD_G_CL = 12
HUD_G_L  = 22

draw_hud:
        ; Sync once: the top HUD (scanline 0) is written first, inside the
        ; V-blank, before the beam relights line 0; the bottom HUD (scanline
        ; 176) is written far ahead of the beam. One wait covers both — no
        ; per-cell wait (10 cells would cost 10 frames).
        JSR wait_vbl
        ; --- Top-left: MV:NNN at scanline 0 ---
        LDA #$00
        STA hud_base_sl
        LDA #HUD_G_M
        LDX #$00
        JSR draw_hud_cell
        LDA #HUD_G_V
        LDX #$01
        JSR draw_hud_cell
        LDA #HUD_G_CL
        LDX #$02
        JSR draw_hud_cell

        ; Hundreds
        LDA moves
        LDX #$00
@h100:  CMP #100
        BCC @h100d
        SBC #100
        INX
        JMP @h100
@h100d: PHA                             ; save remainder
        TXA
        LDX #$03
        JSR draw_hud_cell
        PLA

        ; Tens
        LDX #$00
@t10:   CMP #$0A
        BCC @t10d
        SBC #$0A
        INX
        JMP @t10
@t10d:  PHA                             ; save ones remainder
        TXA
        LDX #$04
        JSR draw_hud_cell
        PLA

        ; Ones
        LDX #$05
        JSR draw_hud_cell

        ; --- Bottom-right: L:NN at tile row 11 (scanline 176) ---
        LDA #176
        STA hud_base_sl
        LDA #HUD_G_L
        LDX #$10                        ; cell 16
        JSR draw_hud_cell
        LDA #HUD_G_CL
        LDX #$11
        JSR draw_hud_cell

        ; 2-digit level number (level_idx is 0-based; display 1-based)
        LDA level_idx
        CLC
        ADC #$01
        LDX #$00
@lt10:  CMP #$0A
        BCC @lt10d
        SBC #$0A
        INX
        JMP @lt10
@lt10d: PHA                             ; save ones
        TXA
        LDX #$12
        JSR draw_hud_cell
        PLA
        LDX #$13
        JMP draw_hud_cell               ; tail-call

; =============================================
; draw_hud_cell: write one 14x16 HUD cell. Glyph in the left byte of
; the cell, right byte blank; top 8 scanlines carry the glyph rows,
; bottom 8 are blanked so the underlying tile doesn't poke through.
; The cell's top scanline is `hud_base_sl` (set by caller: 0 for the
; top HUD, 176 for the bottom HUD).
; Input: A = glyph index, X = cell column (0..19)
; Clobbers: A, X, Y, temp, temp2, ptr_lo, ptr_hi
; =============================================
draw_hud_cell:
        STX temp                        ; temp = cell column
        ASL A
        ASL A
        ASL A                           ; A = glyph_idx * 8
        STA temp2                       ; temp2 = glyph base offset
        LDX #$00                        ; X = scanline offset 0..15
@sc:
        ; abs_scanline = hud_base_sl + X
        TXA
        CLC
        ADC hud_base_sl
        TAY
        LDA hgr_lo,Y
        STA ptr_lo
        LDA hgr_hi,Y
        STA ptr_hi

        ; Pick glyph byte (scanline offset 0..7) or blank (8..15)
        CPX #$08
        BCS @blank
        TXA
        CLC
        ADC temp2
        TAY
        LDA hud_font,Y
        JMP @write
@blank:
        LDA #$00
@write:
        PHA                             ; save byte to write
        LDA temp
        ASL A                           ; Y = cell_col * 2
        TAY
        PLA
        STA (ptr_lo),Y                  ; left byte
        INY
        LDA #$00
        STA (ptr_lo),Y                  ; right byte (always blank)

        INX
        CPX #$10
        BCC @sc
        RTS

; (hud_base_sl lives in ZPSCRATCH — one byte in zp makes the
;  draw_hud_cell inner loop slightly smaller.)

; --- HUD/title font: Beautiful Boot subset, GEN2 bit order (see include) ---
.include "bbfont_subset.inc"
hud_font = HGR_Sokoban_bbfont

; --- 2x-scale doubling tables for draw_big_glyph ---
; Each fat-font row byte's 7 pixels double to 14 HGR pixels.
; byte 0 output is a function of glyph bits 0..3 (b0,b0,b1,b1,b2,b2,b3).
; byte 1 output is a function of glyph bits 3..6 (b3,b4,b4,b5,b5,b6,b6).
; (bit 3 straddles the two output bytes; both tables sample it.)
double_lo:
        .byte $00, $03, $0C, $0F, $30, $33, $3C, $3F
        .byte $40, $43, $4C, $4F, $70, $73, $7C, $7F
double_hi:
        .byte $00, $01, $06, $07, $18, $19, $1E, $1F
        .byte $60, $61, $66, $67, $78, $79, $7E, $7F

; (big_byte0/big_byte1 also live in ZPSCRATCH.)

; =============================================
; Title / help / success screens — all table-driven.
; Each table entry is 5 bytes: str_lo, str_hi, byte_col, scanline,
; style ($00 = small, $01 = 2x big).
; The table ends with $FF in the str_lo slot.
; =============================================
draw_title:
        LDA #<title_table
        STA tbl_lo
        LDA #>title_table
        STA tbl_hi
        JMP draw_from_table

draw_help:
        JSR clear_hgr
        LDA #<help_table
        STA tbl_lo
        LDA #>help_table
        STA tbl_hi
        JMP draw_from_table

draw_success:
        JSR clear_hgr
        LDA #<success_table
        STA tbl_lo
        LDA #>success_table
        STA tbl_hi
        ; fall through into draw_from_table

draw_from_table:
@entry:
        LDY #$00
        LDA (tbl_lo),Y
        CMP #$FF
        BEQ @done
        STA sptr_lo
        INY
        LDA (tbl_lo),Y
        STA sptr_hi
        INY
        LDA (tbl_lo),Y
        STA title_col_start
        INY
        LDA (tbl_lo),Y
        STA title_scanline
        INY
        LDA (tbl_lo),Y                  ; style
        BEQ @small
        JSR draw_title_big_line
        JMP @next
@small:
        JSR draw_title_line
@next:
        LDA tbl_lo
        CLC
        ADC #$05
        STA tbl_lo
        BCC @entry
        INC tbl_hi
        JMP @entry
@done:
        RTS

; --- Screen tables (entries = str_lo, str_hi, byte_col, scanline, style) ---
title_table:
        .byte <title_sokoban, >title_sokoban, $0D, $08, $01    ; big
        .byte <title_apple,   >title_apple,   $0D, $30, $00
        .byte <title_card,    >title_card,    $03, $40, $00    ; "GEN2 UNCLE BERNIE"
        .byte <title_author,  >title_author,  $02, $60, $00
        ; SELECT KEYBOARD; blank $78-$8F; then 1 QWERTY (WASD) / 2 AZERTY (ZQSD)
        .byte <title_kb_hint, >title_kb_hint, $05, $70, $00
        .byte <title_qwerty,  >title_qwerty,  $05, $90, $00
        .byte <title_azerty,  >title_azerty,  $05, $A0, $00
        .byte <title_h_help,  >title_h_help,  $0E, $B8, $00    ; "H HELP" reminder
        .byte $FF

help_table:
        .byte <help_big_title, >help_big_title, $10, $10, $01  ; big HELP
        .byte <help_qwerty,    >help_qwerty,    $07, $30, $00
        .byte <help_azerty,    >help_azerty,    $07, $40, $00
        .byte <help_u,         >help_u,         $0E, $58, $00
        .byte <help_r,         >help_r,         $0D, $68, $00
        .byte <help_n,         >help_n,         $0E, $78, $00
        .byte <help_h,         >help_h,         $0E, $88, $00
        .byte <help_back,      >help_back,      $08, $A0, $00
        .byte $FF

success_table:
        .byte <title_success,    >title_success,    $0D, 56, $01
        .byte <title_press_key,  >title_press_key,  $09, 96, $00
        .byte $FF

; =============================================
; draw_title_line: walk the string at (sptr_lo/hi), stamping each
; glyph at byte_col = title_col_start + ix*2 on scanline
; title_scanline. Terminated by $FF.
; =============================================
draw_title_line:
        LDA #$00
        STA title_ix
@lp:
        LDY title_ix
        LDA (sptr_lo),Y
        CMP #$FF
        BEQ @done
        STA title_glyph
        TYA
        ASL A
        CLC
        ADC title_col_start
        TAX
        LDA title_glyph
        LDY title_scanline
        JSR draw_title_glyph
        INC title_ix
        JMP @lp
@done:
        RTS

; =============================================
; draw_title_big_line: same iteration, 2x-scale glyphs (14 px wide,
; 16 scanlines tall). Big-glyph cells are 2 byte cols wide and packed
; back-to-back, so step = ix*2.
; =============================================
draw_title_big_line:
        LDA #$00
        STA title_ix
@lp:
        LDY title_ix
        LDA (sptr_lo),Y
        CMP #$FF
        BEQ @done
        STA title_glyph
        TYA
        ASL A                           ; *2 bytes per big glyph
        CLC
        ADC title_col_start
        TAX
        LDA title_glyph
        LDY title_scanline
        JSR draw_big_glyph
        INC title_ix
        JMP @lp
@done:
        RTS

; set_hud_font_ptr: src_lo/hi = hud_font + (glyph_idx * 8), idx*8 in 16 bits.
; Input: A = glyph index. Clobbers A, X. (Callers save their X in temp first.)
; =============================================
set_hud_font_ptr:
        LDX #$00
        STX src_hi
        ASL A
        ROL src_hi
        ASL A
        ROL src_hi
        ASL A
        ROL src_hi
        CLC
        ADC #<hud_font
        STA src_lo
        LDA src_hi
        ADC #>hud_font
        STA src_hi
        RTS

; =============================================
; draw_big_glyph: render a glyph at 2x horizontal AND vertical scale
; into the HGR framebuffer, using the fat-font data plus the
; double_lo/double_hi lookup tables.
;   Input : A = glyph index, X = byte_col (0..36), Y = start scanline.
;   Output: 2 bytes wide × 16 scanlines tall.
;   Clobbers: A, X, Y, temp, temp2, src_lo, src_hi, ptr_lo, ptr_hi,
;             big_byte0, big_byte1 (scratch).
; Font base = hud_font + (glyph_idx*8) with idx*8 in 16 bits (32*8=256),
; then each row via (src_lo),Y — fixes W/Z/C/X and row fetches past $FF.
; =============================================
draw_big_glyph:
        STX temp                        ; byte_col
        STY temp2                       ; start_scanline
        JSR set_hud_font_ptr
        LDY #$00
        STY title_glyph                 ; font row 0..7 in title_glyph
@row:
        LDY title_glyph
        LDA (src_lo),Y
        PHA

        AND #$0F
        TAX
        LDA double_lo,X
        STA big_byte0                   ; pixels 0-6 of doubled row

        PLA
        LSR A
        LSR A
        LSR A
        AND #$0F
        TAX
        LDA double_hi,X
        STA big_byte1                   ; pixels 7-13 of doubled row

        ; Absolute scanline = temp2 + Y*2
        TYA
        ASL A
        CLC
        ADC temp2
        TAX                             ; X = scanline

        ; Two back-to-back scanlines get the same doubled pixels.
        LDA hgr_lo,X
        CLC
        ADC temp
        STA ptr_lo
        LDA hgr_hi,X
        ADC #$00
        STA ptr_hi
        LDY #$00
        LDA big_byte0
        STA (ptr_lo),Y
        INY
        LDA big_byte1
        STA (ptr_lo),Y

        INX                             ; next scanline
        LDA hgr_lo,X
        CLC
        ADC temp
        STA ptr_lo
        LDA hgr_hi,X
        ADC #$00
        STA ptr_hi
        LDY #$00
        LDA big_byte0
        STA (ptr_lo),Y
        INY
        LDA big_byte1
        STA (ptr_lo),Y

        LDY title_glyph
        INY
        STY title_glyph
        CPY #$08
        BCC @row
        RTS

; =============================================
; draw_title_glyph: write 8 scanlines of one glyph into the HGR
; framebuffer at (byte_col, start_scanline). Single byte wide (7
; visible pixels). Assumes the background scanlines are already zero.
; Input:  A = glyph index, X = byte_col (0..39), Y = start_scanline.
; Clobbers: A, X, Y, temp, temp2, src_lo, src_hi, ptr_lo, ptr_hi,
;           title_glyph (row temp, same contract as draw_big_glyph).
; Reads font via (src_lo),Y; pointer from set_hud_font_ptr.
; =============================================
draw_title_glyph:
        STX temp                        ; temp = byte_col
        STY temp2                       ; temp2 = start_scanline
        JSR set_hud_font_ptr
        LDY #$00
@sc:
        STY title_glyph
        LDA (src_lo),Y
        PHA
        LDY title_glyph
        TYA
        CLC
        ADC temp2
        TAX
        LDA hgr_lo,X
        CLC
        ADC temp
        STA ptr_lo
        LDA hgr_hi,X
        ADC #$00
        STA ptr_hi
        PLA
        LDY #$00
        STA (ptr_lo),Y
        LDY title_glyph
        INY
        STY title_glyph
        CPY #$08
        BCC @sc
        RTS

; --- Title / help / success strings (glyph-index encoded, $FF terminated) ---
; Glyph index table:
;   0..9 = '0'..'9'
;   10=M  11=V  12=:  13=S  14=O  15=K  16=B  17=A  18=N  19=P
;   20=R  21=E  22=L  23=space  24=G  25=H  26=T  27=D  28=Y  29=I
;   30=U  31=Q  32=W  33=Z  34=C  35=X  36=(  37=)
title_kb_hint:
        ; SELECT KEYBOARD (15 glyphs x 2 cols = 30; col 5 centres in 40)
        .byte 13,21,22,21,34,26,23,15,21,28,16,14,17,20,27, $FF
title_sokoban:
        ; S  O  K  O  B  A  N
        .byte 13,14,15,14,16,17,18, $FF
title_apple:
        ; A  P  P  L  E  _  1
        .byte 17,19,19,22,21,23, 1, $FF
title_card:
        ; G  E  N  2  _  U  N  C  L  E  _  B  E  R  N  I  E
        .byte 24,21,18, 2,23,30,18,34,22,21,23,16,21,20,18,29,21, $FF
title_author:
        ; B  Y  _  V  E  R  H  I  L  L  E  _  A  R  N  A  U  D
        .byte 16,28,23,11,21,20,25,29,22,22,21,23,17,20,18,17,30,27, $FF
title_qwerty:
        ; 1  _  QWERTY  _  ( WASD )
        .byte  1,23,31,32,21,20,26,28,23,36,32,17,13,27,37, $FF
title_azerty:
        ; 2  _  AZERTY  _  ( ZQSD )
        .byte  2,23,17,33,21,20,26,28,23,36,33,31,13,27,37, $FF
title_h_help:
        ; H  _  H  E  L  P
        .byte 25,23,25,21,22,19, $FF
title_success:
        ; S  U  C  C  E  S  S
        .byte 13,30,34,34,21,13,13, $FF
title_press_key:
        ; P  R  E  S  S  _  A  _  K  E  Y
        .byte 19,20,21,13,13,23,17,23,15,21,28, $FF

; --- Help strings ---
help_big_title:
        ; H  E  L  P
        .byte 25,21,22,19, $FF
help_qwerty:
        ; QWERTY  ( WASD )
        .byte 31,32,21,20,26,28,23,36,32,17,13,27,37, $FF
help_azerty:
        ; AZERTY  ( ZQSD )
        .byte 17,33,21,20,26,28,23,36,33,31,13,27,37, $FF
help_u:
        ; U  _  U  N  D  O
        .byte 30,23,30,18,27,14, $FF
help_r:
        ; R  _  R  E  S  E  T
        .byte 20,23,20,21,13,21,26, $FF
help_n:
        ; N  _  N  E  X  T
        .byte 18,23,18,21,35,26, $FF
help_h:
        ; H  _  H  E  L  P
        .byte 25,23,25,21,22,19, $FF
help_back:
        ; A  N  Y  _  K  E  Y  _  B  A  C  K
        .byte 17,18,28,23,15,21,28,23,16,17,34,15, $FF

; (title_ix/title_glyph/title_col_start/title_scanline now live in
;  the ZPSCRATCH segment — see the .zeropage block up top.)

; =============================================
; Shared routines + tile-state tables
; =============================================
.include "sokoban_common.inc"

; =============================================
; DATA
; =============================================

; --- row * 20 for rows 0..11 ---
row_x20:
        .byte   0,  20,  40,  60,  80, 100, 120, 140
        .byte 160, 180, 200, 220

; --- Tile bitmaps (7 tiles x 32 bytes = 224 bytes) ---
; Each tile: 16 scanlines, 2 bytes per scanline
;
; Everything from here to EOF lives in the low bank ($0280-$0EFF) under
; the dual-bank Parmigiani layout — this is where the bulk data goes so
; the high bank ($E000-$EFFF) stays free for the program code.
.segment "DATA_LOW"

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
.include "sokoban_levels.inc"
; sokoban_levels_ext.inc (levels 46..72) dropped: would push DATA_LOW past
; the 3.2 KB low-bank ceiling on the Parmigiani 8 KB dual-bank.

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

; --- Strings ---
str_title:
        .byte $0D, " SOKOBAN + GEN2 HGR", $0D
        .byte " MICROBAN I 45LV  V.ARNAUD 26", $0D
        .byte " PUSH BOXES ON TARGETS  U R N", $0D, 0

str_layout:
        .byte $0D, " KEYBOARD: 1 QWERTY  2 AZERTY", $0D, 0

str_win:
        .byte $0D, " CLEARED! KEY=NEXT", $0D, 0

.include "hgr_tables.inc"
.include "multiply.asm"
.include "gen2_init.asm"
