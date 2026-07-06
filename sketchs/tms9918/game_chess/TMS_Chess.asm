; =============================================================================
; TMS_Chess.asm -- graphical Chess for the Apple-1 + P-LAB TMS9918 Graphic Card
; VERHILLE Arnaud - 2026
; =============================================================================
; Graphical (Mode-2 bitmap) front-end for the shared chess engine
; (dev/lib/games/chess/chess_engine.asm). The engine is platform-agnostic;
; this file is the TMS9918 renderer + keyboard-cursor game loop, the sibling
; of the text-mode sketchs/apple1/game_chess/Chess.asm.
;
; Board: 8x8 squares x 16x16 px = 128x128, centred in the 256x192 bitmap.
; Each piece is a 2x2-cell (16x16) monochrome silhouette from
; dev/lib/tms9918/sprites_chess.asm, recoloured per side via the Mode-2
; colour table (white = $0F ink, black = $01 ink). The TMS9918's 4-sprites-
; per-line limit makes a full rank impossible in hardware sprites, so the
; board is drawn as background bitmap cells; the cursor/selection are shown
; by recolouring the square (no sprites needed).
;
; Controls (Apple-1 keyboard, forced uppercase):
;   W/A/S/D  move cursor        SPACE or RETURN  pick source / target
;   ESC      cancel selection   M  cycle mode     N  new game
;   U        undo last move     P  toggle AI strategy (FAST 1-ply / STRONG 2-ply)
; Modes (M cycles): HVH, WAI (white=you/black=AI), BAI, AVA (auto self-play).
;
; Build (CodeTank 16k, run-in-place at $4000, needs the charmap font):
;   assembled by tools/build_codetank_rom.py (GAME7) with -D CODETANK_BUILD,
;   linking chess_engine + tms9918m2 + text_bitmap + sprites_chess + pad.
; Entry: 4000R.
; =============================================================================

        .import tms9918_pad18   ; silicon-strict pad18-v4 (tms9918_pad.asm)
.include "apple1.inc"
.include "tms9918.inc"
.include "chess_common.inc"

; --- colour equates (TMS9918 palette index 0..15) --------------------------
WHITE_INK = $0F         ; white pieces
BLACK_INK = $01         ; black pieces
LIGHT_SQ  = $03         ; light square (light green)
DARK_SQ   = $0C         ; dark square  (dark green)
CUR_COL   = $0B         ; cursor highlight (light yellow)
SEL_COL   = $0D         ; selected-source highlight (magenta)

; --- board geometry (top-left pixel of the 8x8 board) ----------------------
BOARD_X0 = 64           ; (256-128)/2
BOARD_Y0 = 32           ; (192-128)/2

; ---------------------------------------------------------------------------
; Zero page. tmp/tmp2 MUST be $00/$01 (engine + tms9918m2 + text_bitmap all
; .importzp them). Declared first so the layout starts at $00; the driver's
; and engine's own ZEROPAGE .res blocks concatenate after these.
; ---------------------------------------------------------------------------
.segment "ZEROPAGE"
tmp:       .res 1       ; $00
tmp2:      .res 1       ; $01
mptr_lo:   .res 1       ; $02  blit source ptr (also text_bitmap glyph ptr)
mptr_hi:   .res 1       ; $03
sptr_lo:   .res 1       ; $04  string ptr for puts
sptr_hi:   .res 1       ; $05
base_lo:   .res 1       ; $06  piece-pattern base for the current square
base_hi:   .res 1       ; $07
.exportzp tmp, tmp2, mptr_lo, mptr_hi

; Imported ZP from the Mode-2 driver.
.importzp pix_x, pix_y, pix_addr_lo, pix_addr_hi, pen_color

; ---------------------------------------------------------------------------
; Engine + library imports. Switch out of ZEROPAGE first so these default to
; absolute (an .import inheriting the ZEROPAGE segment mis-sizes them as zp).
; ---------------------------------------------------------------------------
.segment "CODE"
.import init_board, apply_user_move, ai_play_move, game_status, in_check
.import piece_at, undo_last_move
.import side_to_move, mv_from, mv_to, mv_promo, ai_strategy
.import init_vdp_g2, calc_pix_addr, vdp_set_write
.import chess_pawn_pat, chess_knight_pat, chess_bishop_pat
.import chess_rook_pat, chess_queen_pat, chess_king_pat
.ifdef CODETANK_BUILD
.import text_blit_glyph
.endif

; Driver needs plot_mode (referenced by its unused plot_set/line_xy paths).
.export plot_mode

; ---------------------------------------------------------------------------
; BSS scratch. Uninitialised on a cartridge (no crt0) -- main/new_game seed
; every field explicitly.
; ---------------------------------------------------------------------------
.segment "BSS"
plot_mode:  .res 1
dsq:        .res 1      ; square being drawn (0x88)
cur_sq:     .res 1      ; cursor square (0x88)
old_cur:    .res 1      ; previous cursor square (redraw on move)
sel_sq:     .res 1      ; selected source ($FF = none)
sel_active: .res 1      ; 0/1
cellbg:     .res 1      ; current cell background colour 0..15
cellcolor:  .res 1      ; packed (fg<<4)|bg
piece_code: .res 1      ; raw board byte for dsq
play_mode:  .res 1      ; 0 HvH / 1 WAI / 2 BAI / 3 AvA
movecount:  .res 1      ; AI half-move counter (AvA cap)
game_result:.res 1      ; 1 wmate / 2 bmate / 3 stale / 4 draw
use_off:    .res 1      ; draw_quads: 1 = piece offsets, 0 = empty
sq_px:      .res 1      ; left-column pixel X of dsq
sq_py:      .res 1      ; top-row pixel Y of dsq
scr:        .res 1      ; misc scratch
save_x:     .res 1
save_y:     .res 1
save_x2:    .res 1
save_y2:    .res 1

; ===========================================================================
.segment "CODE"
; ===========================================================================

main:
        JSR init_board
        LDA #0
        STA plot_mode
        STA sel_active
        STA movecount
        LDA #$FF
        STA sel_sq
        LDA #$14                ; cursor starts on e2 (file 4, rank 1)
        STA cur_sq
        LDA #0
        STA play_mode           ; HvH
        LDA #1
        STA ai_strategy         ; STRONG (2-ply) default
        JSR init_vdp_g2
        JSR draw_board
        JSR draw_help

game_loop:
        JSR check_terminal      ; shows result + waits N (-> new_game) if over
        JSR update_status
        JSR side_is_ai
        BEQ human_turn
        JMP ai_turn

; ---------------------------------------------------------------------------
; Human turn: block on a key, dispatch.
; ---------------------------------------------------------------------------
human_turn:
        JSR wait_key
        CMP #'W'
        BNE @n1
        JSR cur_up
        JMP game_loop
@n1:    CMP #'S'
        BNE @n2
        JSR cur_down
        JMP game_loop
@n2:    CMP #'A'
        BNE @n3
        JSR cur_left
        JMP game_loop
@n3:    CMP #'D'
        BNE @n4
        JSR cur_right
        JMP game_loop
@n4:    CMP #' '
        BNE @n5
        JMP do_confirm
@n5:    CMP #$0D
        BNE @n6
        JMP do_confirm
@n6:    CMP #$1B                ; ESC
        BNE @n7
        JMP do_deselect
@n7:    CMP #'M'
        BNE @k1
        JMP cycle_mode
@k1:    CMP #'N'
        BNE @k2
        JMP new_game
@k2:    CMP #'U'
        BNE @k3
        JMP do_undo
@k3:    CMP #'P'
        BNE @k4
        JMP toggle_strategy
@k4:    JMP game_loop           ; unknown key

; ---------------------------------------------------------------------------
; Confirm (SPACE/RETURN): select a source, or attempt sel -> cur.
; ---------------------------------------------------------------------------
do_confirm:
        LDA sel_active
        BNE @have
        ; no selection yet: select cur if it holds a piece of the side to move
        LDX cur_sq
        JSR piece_at
        STA piece_code
        AND #PIECE_MASK
        BEQ @ret                ; empty square
        LDA piece_code
        AND #COLOR_BLACK
        CMP side_to_move
        BNE @ret                ; not our piece
        LDA cur_sq
        STA sel_sq
        LDA #1
        STA sel_active
        LDA cur_sq
        STA dsq
        JSR draw_square         ; show selection highlight
@ret:   JMP game_loop
@have:  ; a source is selected
        LDA cur_sq
        CMP sel_sq
        BEQ do_deselect         ; clicking source again cancels
        LDA sel_sq
        STA mv_from
        LDA cur_sq
        STA mv_to
        LDA #0
        STA mv_promo            ; auto-queen on promotion
        JSR apply_user_move
        BCS do_deselect         ; illegal -> just cancel selection
        ; legal: clear selection, full board redraw (capture/castle/e.p.)
        LDA #0
        STA sel_active
        LDA #$FF
        STA sel_sq
        JSR draw_board
        JMP game_loop

; Cancel the current selection (and redraw the freed square).
do_deselect:
        LDA #0
        STA sel_active
        LDA sel_sq
        STA old_cur
        LDA #$FF
        STA sel_sq
        LDA old_cur
        CMP #$FF
        BEQ @skip
        STA dsq
        JSR draw_square
@skip:  JMP game_loop

cycle_mode:
        LDA play_mode
        CLC
        ADC #1
        AND #3
        STA play_mode
        JMP game_loop

toggle_strategy:
        LDA ai_strategy
        EOR #1
        STA ai_strategy
        JMP game_loop

do_undo:
        JSR undo_last_move
        BCS @no
        JSR draw_board
@no:    JMP game_loop

new_game:
        JSR init_board
        LDA #0
        STA sel_active
        STA movecount
        LDA #$FF
        STA sel_sq
        LDA #$14
        STA cur_sq
        JSR draw_board
        JMP game_loop

; ---------------------------------------------------------------------------
; AI turn. AvA auto-plays (capped); single-AI modes move once then return to
; the loop (which will then be the human's turn). Non-blocking poll lets N/M
; interrupt an AvA game.
; ---------------------------------------------------------------------------
ai_turn:
        LDA play_mode
        CMP #3
        BNE @go
        LDA movecount
        CMP #200                ; AvA safety cap -> declared draw
        BCC @poll
        LDA #4
        STA game_result
        JSR show_gameover
@wdraw: JSR wait_key
        CMP #'N'
        BNE @wdraw
        JMP new_game
@poll:  JSR poll_key
        BEQ @go
        CMP #'N'
        BEQ new_game
        CMP #'M'
        BEQ cycle_mode
@go:    JSR ai_play_move
        BCS @loop               ; no move (terminal caught next iteration)
        INC movecount
        JSR draw_board
@loop:  JMP game_loop

; ---------------------------------------------------------------------------
; side_is_ai: A = 1 if the side to move is the AI, else 0. Preserves nothing.
; ---------------------------------------------------------------------------
side_is_ai:
        LDA play_mode
        BEQ @human              ; 0 HvH
        CMP #3
        BEQ @ai                 ; 3 AvA
        CMP #1
        BEQ @m1
        ; mode 2: white=AI, black=human
        LDA side_to_move
        BEQ @ai                 ; white -> AI
        BNE @human
@m1:    ; mode 1: white=human, black=AI
        LDA side_to_move
        BNE @ai                 ; black -> AI
@human: LDA #0
        RTS
@ai:    LDA #1
        RTS

; ---------------------------------------------------------------------------
; check_terminal: if game_status != 0, show the result, wait for N, restart.
; Returns (RTS) only when the game is ongoing.
; ---------------------------------------------------------------------------
check_terminal:
        JSR game_status
        BEQ @ok
        STA game_result
        JSR show_gameover
@wait:  JSR wait_key
        CMP #'N'
        BNE @wait
        JMP new_game
@ok:    RTS

; ---------------------------------------------------------------------------
; Cursor movement. Each redraws the vacated + new square.
; ---------------------------------------------------------------------------
cur_up:
        LDA cur_sq
        CMP #$70
        BCS cur_ret             ; already rank 8
        STA old_cur
        CLC
        ADC #$10
        STA cur_sq
        JMP cur_moved
cur_down:
        LDA cur_sq
        CMP #$10
        BCC cur_ret             ; already rank 1
        STA old_cur
        SEC
        SBC #$10
        STA cur_sq
        JMP cur_moved
cur_left:
        LDA cur_sq
        AND #$07
        BEQ cur_ret             ; file a
        LDA cur_sq
        STA old_cur
        SEC
        SBC #1
        STA cur_sq
        JMP cur_moved
cur_right:
        LDA cur_sq
        AND #$07
        CMP #$07
        BEQ cur_ret             ; file h
        LDA cur_sq
        STA old_cur
        CLC
        ADC #1
        STA cur_sq
cur_moved:
        LDA old_cur
        STA dsq
        JSR draw_square
        LDA cur_sq
        STA dsq
        JSR draw_square
cur_ret:
        RTS

; ---------------------------------------------------------------------------
; draw_board: paint all 64 squares.
; ---------------------------------------------------------------------------
draw_board:
        LDX #0                  ; rank 0..7
@r:     LDY #0                  ; file 0..7
@f:     TXA
        ASL
        ASL
        ASL
        ASL                     ; rank*16
        STA scr
        TYA
        ORA scr                 ; sq = rank*16 + file
        STA dsq
        STX save_x2
        STY save_y2
        JSR draw_square
        LDX save_x2
        LDY save_y2
        INY
        CPY #8
        BNE @f
        INX
        CPX #8
        BNE @r
        RTS

; ---------------------------------------------------------------------------
; draw_square: render the square in `dsq` (pixel pos, base colour, highlight,
; piece silhouette + colour). Clobbers A,X,Y.
; ---------------------------------------------------------------------------
draw_square:
        ; pixel X = BOARD_X0 + file*16
        LDA dsq
        AND #$07
        ASL
        ASL
        ASL
        ASL
        CLC
        ADC #BOARD_X0
        STA sq_px
        ; pixel Y = BOARD_Y0 + (7-rank)*16 = BOARD_Y0 + ($70 - rank*16)
        LDA dsq
        AND #$70
        STA scr
        LDA #$70
        SEC
        SBC scr
        CLC
        ADC #BOARD_Y0
        STA sq_py
        ; base colour by parity: (rank ^ file) & 1  (a1 = dark)
        LDA dsq
        LSR
        LSR
        LSR
        LSR
        EOR dsq
        AND #1
        BEQ @dark
        LDA #LIGHT_SQ
        JMP @havebg
@dark:  LDA #DARK_SQ
@havebg:
        STA cellbg
        ; highlight overrides (selected beats cursor)
        LDA dsq
        CMP sel_sq
        BNE @notsel
        LDA #SEL_COL
        STA cellbg
        JMP @bgdone
@notsel:
        LDA dsq
        CMP cur_sq
        BNE @bgdone
        LDA #CUR_COL
        STA cellbg
@bgdone:
        ; piece?
        LDX dsq
        JSR piece_at
        STA piece_code
        AND #PIECE_MASK
        BNE @occupied
        ; empty: colour = bg in both nibbles, pattern = zeros
        LDA cellbg
        ASL
        ASL
        ASL
        ASL
        ORA cellbg
        STA cellcolor
        LDA #<zeros8
        STA base_lo
        LDA #>zeros8
        STA base_hi
        LDA #0
        STA use_off
        JMP draw_quads
@occupied:
        TAY                     ; A = type 1..6
        DEY
        LDA piece_ptrs_lo,Y
        STA base_lo
        LDA piece_ptrs_hi,Y
        STA base_hi
        LDA piece_code
        AND #COLOR_BLACK
        BEQ @white
        LDA #BLACK_INK
        JMP @setfg
@white: LDA #WHITE_INK
@setfg: ASL
        ASL
        ASL
        ASL
        ORA cellbg
        STA cellcolor
        LDA #1
        STA use_off
        ; fall through to draw_quads

; draw_quads: blit the 4 cells of the current square. Uses base_lo/hi,
;   cellcolor, sq_px/sq_py, use_off.
draw_quads:
        LDX #0
@q:     LDA sq_px
        CLC
        ADC quad_dx,X
        STA pix_x
        LDA sq_py
        CLC
        ADC quad_dy,X
        STA pix_y
        LDA use_off
        BEQ @zoff
        LDA quad_off,X
        JMP @off
@zoff:  LDA #0
@off:   STX save_x
        JSR set_mptr_off
        JSR blit_cell
        LDX save_x
        INX
        CPX #4
        BNE @q
        RTS

; set_mptr_off: mptr = base + A. Clobbers A.
set_mptr_off:
        CLC
        ADC base_lo
        STA mptr_lo
        LDA base_hi
        ADC #0
        STA mptr_hi
        RTS

; ---------------------------------------------------------------------------
; blit_cell: write 8 pattern bytes from (mptr) then 8 colour bytes (cellcolor)
;   to the cell at (pix_x,pix_y). Mirrors text_bitmap.asm's VDP timing so it
;   is silicon-strict safe. Clobbers A,X,Y,pix_addr_lo/hi.
; ---------------------------------------------------------------------------
blit_cell:
        JSR calc_pix_addr
        JSR vdp_set_write
        LDY #0
@pat:   LDA (mptr_lo),Y
        STA VDP_DATA
        INY
        CPY #8
        JSR tms9918_pad18
        BNE @pat
        ; colour cell: re-prime at $2000 + same offset
        LDA pix_addr_lo
        STA VDP_CTRL
        JSR tms9918_pad18
        LDA pix_addr_hi
        ORA #$60                ; $20 colour base | $40 write
        STA VDP_CTRL
        JSR tms9918_pad18
        LDA cellcolor
        LDX #8
@col:   STA VDP_DATA
        JSR tms9918_pad18
        DEX
        BNE @col
        RTS

; ---------------------------------------------------------------------------
; Keyboard.
; ---------------------------------------------------------------------------
wait_key:
        LDA KBDCR
        BPL wait_key
        LDA KBD
        AND #$7F
        RTS

poll_key:                       ; A = key, or 0 if none pending
        LDA KBDCR
        BPL @none
        LDA KBD
        AND #$7F
        RTS
@none:  LDA #0
        RTS

; ---------------------------------------------------------------------------
; Status / text overlays (need the charmap font -> CODETANK_BUILD only).
; ---------------------------------------------------------------------------
update_status:
.ifdef CODETANK_BUILD
        LDA #WHITE_INK
        STA pen_color
        ; side to move
        LDA #16
        STA pix_x
        LDA #8
        STA pix_y
        LDA side_to_move
        BNE @black
        LDA #'W'
        JMP @ps
@black: LDA #'B'
@ps:    JSR text_blit_glyph
        ; mode name
        LDA #40
        STA pix_x
        LDA #8
        STA pix_y
        LDX play_mode
        LDA modestr_lo,X
        STA sptr_lo
        LDA modestr_hi,X
        STA sptr_hi
        JSR puts
        ; check indicator
        LDA #80
        STA pix_x
        LDA #8
        STA pix_y
        JSR in_check
        BEQ @clear
        LDA #<str_check
        STA sptr_lo
        LDA #>str_check
        STA sptr_hi
        JMP @pc
@clear: LDA #<str_blank5
        STA sptr_lo
        LDA #>str_blank5
        STA sptr_hi
@pc:    JSR puts
.endif
        RTS

draw_help:
.ifdef CODETANK_BUILD
        LDA #$0E                ; grey
        STA pen_color
        LDA #8
        STA pix_x
        LDA #176
        STA pix_y
        LDA #<str_help
        STA sptr_lo
        LDA #>str_help
        STA sptr_hi
        JSR puts
.endif
        RTS

show_gameover:
.ifdef CODETANK_BUILD
        LDA #WHITE_INK
        STA pen_color
        LDA #80
        STA pix_x
        LDA #80
        STA pix_y
        LDX game_result
        LDA overstr_lo,X
        STA sptr_lo
        LDA overstr_hi,X
        STA sptr_hi
        JSR puts
        LDA #80
        STA pix_x
        LDA #96
        STA pix_y
        LDA #<str_pressn
        STA sptr_lo
        LDA #>str_pressn
        STA sptr_hi
        JSR puts
.endif
        RTS

.ifdef CODETANK_BUILD
; puts: print the NUL-terminated string at (sptr) starting at pix_x/pix_y,
;   advancing 8 px per glyph. pen_color set by caller.
puts:
        LDY #0
@l:     LDA (sptr_lo),Y
        BEQ @done
        STY save_y
        JSR text_blit_glyph
        LDA pix_x
        CLC
        ADC #8
        STA pix_x
        LDY save_y
        INY
        BNE @l
@done:  RTS
.endif

; ---------------------------------------------------------------------------
; Read-only data.
; ---------------------------------------------------------------------------
zeros8:    .byte 0,0,0,0,0,0,0,0

quad_dx:   .byte 0, 8, 0, 8
quad_dy:   .byte 0, 0, 8, 8
quad_off:  .byte 0, 16, 8, 24   ; TL, TR, BL, BR sub-blocks of the 32-byte piece

piece_ptrs_lo:
        .byte <chess_pawn_pat, <chess_knight_pat, <chess_bishop_pat
        .byte <chess_rook_pat, <chess_queen_pat, <chess_king_pat
piece_ptrs_hi:
        .byte >chess_pawn_pat, >chess_knight_pat, >chess_bishop_pat
        .byte >chess_rook_pat, >chess_queen_pat, >chess_king_pat

.ifdef CODETANK_BUILD
m_hvh:  .byte "HVH", 0
m_wai:  .byte "WAI", 0
m_bai:  .byte "BAI", 0
m_ava:  .byte "AVA", 0
modestr_lo: .byte <m_hvh, <m_wai, <m_bai, <m_ava
modestr_hi: .byte >m_hvh, >m_wai, >m_bai, >m_ava

str_check:  .byte "CHECK", 0
str_blank5: .byte "     ", 0
str_help:   .byte "WASD=MOVE SPC=SEL M:MODE N:NEW", 0
str_pressn: .byte "PRESS N", 0

o_bwin: .byte "BLACK WINS", 0
o_wwin: .byte "WHITE WINS", 0
o_stale:.byte "STALEMATE ", 0
o_draw: .byte "DRAW      ", 0
overstr_lo: .byte 0, <o_bwin, <o_wwin, <o_stale, <o_draw
overstr_hi: .byte 0, >o_bwin, >o_wwin, >o_stale, >o_draw
.endif
