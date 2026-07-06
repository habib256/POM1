; =============================================================================
; TMS_Chess.asm -- graphical Chess for the Apple-1 + P-LAB TMS9918 Graphic Card
; VERHILLE Arnaud - 2026
; =============================================================================
; Graphical (Mode-2 bitmap) front-end for the shared chess engine
; (dev/lib/games/chess/chess_engine.asm). The engine is platform-agnostic;
; this file is the TMS9918 renderer + keyboard-cursor game loop, the sibling
; of the text-mode sketchs/apple1/game_chess/Chess.asm.
;
; Board: 8x8 squares x 24x24 px = 192x192, shifted LEFT. Screen layout:
;   col 0 (x=0..7)     rank digits 1..8
;   cols 1..24         the board (x=8..199)
;   cols 25..31 (x=200..255)  right panel: "side mode" status + the move list
;                             ("W G2G4" / "B G7G5" ...), one half-move per row.
; Each piece is a 16x16 line-art pattern from dev/lib/tms9918/sprites_chess.asm,
; CENTRED in its 24x24 square (4-px margin): since 4 is a sub-cell offset the
; piece straddles the 8x8 cell grid, so draw_square composes each square's
; 3x3 cells in RAM (compose_piece) before blitting them. Pieces are recoloured
; per side via the Mode-2 colour table (white = $0F ink, black = $01 ink). The
; TMS9918's 4-sprites-per-line limit makes a full rank impossible in hardware
; sprites, so the board is background bitmap cells; cursor/selection are shown
; by recolouring the square (no sprites needed).
;
; Controls (Apple-1 keyboard, forced uppercase):
;   I/J/K/L  move cursor        SPACE or RETURN  pick source / target
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
; .ifndef-guarded so a build can override any of them with `-D NAME=$xx`.
.ifndef WHITE_INK
WHITE_INK = $0F         ; white pieces
.endif
.ifndef BLACK_INK
BLACK_INK = $01         ; black pieces
.endif
.ifndef LIGHT_SQ
LIGHT_SQ  = $03         ; light square (light green)
.endif
.ifndef DARK_SQ
DARK_SQ   = $0C         ; dark square  (dark green)
.endif
.ifndef CUR_COL
CUR_COL   = $0B         ; cursor highlight (light yellow)
.endif
.ifndef SEL_COL
SEL_COL   = $0D         ; selected-source highlight (magenta)
.endif

; --- board geometry: 24x24-px squares (3x3 TMS cells), 192x192 board -------
; Layout: rank digit (1 cell) | board (24 cells) | move list (7 cells).
SQ        = 24          ; square size in pixels (= 3 cells)
BOARD_X0  = 8           ; board shifted left; x=0 column holds the rank digits
BOARD_Y0  = 0           ; fills the full 192-px height
PIECE_OFF = 4           ; (24-16)/2 -- centre the 16x16 piece in its square
RMARGIN_X = 208         ; right panel column (1-cell gap off the board): moves
MLIST_TOP = 2           ; first right-panel row used by the move list
MLIST_BOT = 24          ; one past the last usable row

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
sq_px:      .res 1      ; left-column pixel X of dsq
sq_py:      .res 1      ; top-row pixel Y of dsq
scr:        .res 1      ; misc scratch
scr2:       .res 1
scr3:       .res 1
scr4:       .res 1
c0v:        .res 1      ; compose_piece: the 3 shifted cell bytes for a row
c1v:        .res 1
c2v:        .res 1
save_x:     .res 1
save_y:     .res 1
save_x2:    .res 1
save_y2:    .res 1
move_row:   .res 1      ; next right-panel row for the move list
cursor_on:  .res 1      ; 1 = show the selection cursor (human turn only)
turn_ai:    .res 1      ; 1 = side to move is the AI (this loop iteration)
printed_side: .res 1    ; last side we printed an Apple-1 turn line for ($FF=none)
cbuf:       .res 72     ; 3x3 cells x 8 bytes = one composed square's patterns

; ===========================================================================
.segment "CODE"
; ===========================================================================

main:
        JSR init_board
        LDA #0
        STA plot_mode
        STA sel_active
        STA movecount
        LDA #2
        STA move_row            ; move list starts at right-panel row 2
        LDA #$FF
        STA sel_sq
        LDA #$14                ; cursor starts on e2 (file 4, rank 1)
        STA cur_sq
        LDA #1
        STA ai_strategy         ; STRONG (2-ply) default
        LDA #$FF
        STA printed_side
        LDA #0
        STA cursor_on           ; no cursor during the startup menu
        JSR init_vdp_g2
        JSR draw_board
        JSR draw_coords         ; rank numbers (left of the board)
        JSR a1_choose_mode      ; Apple-1 presentation + keyboard mode select

game_loop:
        JSR check_terminal      ; shows result + waits N (-> new_game) if over
        JSR update_status       ; side + mode at the top of the right panel
        JSR side_is_ai          ; A = 1 (AI) / 0 (human)
        STA turn_ai
        JSR a1_turn_status      ; "X IS THINKING" / "X TO PLAY" on the Apple-1
        LDA turn_ai             ; cursor visible only on a human turn
        EOR #1
        STA cursor_on
        LDA cur_sq
        STA dsq
        JSR draw_square         ; reflect cursor visibility on the cursor square
        LDA turn_ai
        BEQ human_turn
        JMP ai_turn

; ---------------------------------------------------------------------------
; Human turn: block on a key, dispatch.
; ---------------------------------------------------------------------------
human_turn:
        JSR wait_key
        CMP #'I'                ; I = up
        BNE @n1
        JSR cur_up
        JMP game_loop
@n1:    CMP #'K'                ; K = down
        BNE @n2
        JSR cur_down
        JMP game_loop
@n2:    CMP #'J'                ; J = left
        BNE @n3
        JSR cur_left
        JMP game_loop
@n3:    CMP #'L'                ; L = right
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
        JSR print_move_tms      ; log the move in the right panel
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
        LDA #$FF
        STA printed_side        ; force a fresh turn line for the new mode
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
        LDA #$FF
        STA printed_side
        LDA #0
        STA cursor_on
        JSR draw_board
        JSR clear_movelist      ; blank the right panel + reset move_row
        JSR a1_choose_mode      ; re-present + re-select mode for the new game
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
        JSR a1_result
@wdraw: JSR wait_key
        CMP #'N'
        BNE @wdraw
        JMP new_game
@poll:  JSR poll_key
        BEQ @go
        CMP #'N'
        BNE @pm
        JMP new_game
@pm:    CMP #'M'
        BNE @go
        JMP cycle_mode
@go:    JSR ai_play_move
        BCS @loop               ; no move (terminal caught next iteration)
        INC movecount
        JSR draw_board
        JSR print_move_tms      ; log the move in the right panel
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
        JSR a1_result
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
; draw_square: render the 24x24 square in `dsq` -- pixel position, base colour,
;   highlight, and (if occupied) the centred 16x16 piece composed into 3x3
;   cells. Clobbers A,X,Y.
; ---------------------------------------------------------------------------
draw_square:
        ; pixel X = BOARD_X0 + file*24  (col_x[file])
        LDA dsq
        AND #$07
        TAX
        LDA col_x,X
        CLC
        ADC #BOARD_X0
        STA sq_px
        ; pixel Y = BOARD_Y0 + (7-rank)*24  (col_x[7-rank])
        LDA dsq
        LSR
        LSR
        LSR
        LSR                     ; rank 0..7
        STA scr
        LDA #7
        SEC
        SBC scr                 ; drow = 7-rank
        TAX
        LDA col_x,X
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
        LDA cursor_on
        BEQ @bgdone             ; cursor hidden (AI turn / menu) -> no highlight
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
        ; empty: colour = bg in both nibbles, 9 solid cells
        LDA cellbg
        ASL
        ASL
        ASL
        ASL
        ORA cellbg
        STA cellcolor
        JMP draw_empty9
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
        JSR compose_piece
        JMP draw_cells9

; ---------------------------------------------------------------------------
; compose_piece: rasterise the 16x16 piece at base_lo/hi into cbuf (3x3 cells),
;   centred at pixel offset (PIECE_OFF, PIECE_OFF). The piece's 16 columns land
;   at square-x 4..19, so each row's bits split across 3 cells as:
;     c0 = hi>>4 ; c1 = (hi<<4)|(lo>>4) ; c2 = lo<<4     (hi/lo = the 2 col bytes)
;   Row pr lands at square-y pr+4 -> cell-row (pr+4)>>3, in-cell row (pr+4)&7.
;   Clobbers A,X; preserves nothing else of note (Y = piece row on exit=16).
; ---------------------------------------------------------------------------
compose_piece:
        ; mptr = base + 16 (the right-half column bytes)
        CLC
        LDA base_lo
        ADC #16
        STA mptr_lo
        LDA base_hi
        ADC #0
        STA mptr_hi
        ; zero cbuf (72 bytes)
        LDX #71
        LDA #0
@z:     STA cbuf,X
        DEX
        BPL @z
        LDY #0                  ; piece row 0..15
@pr:    LDA (base_lo),Y         ; hi = cols 0-7
        STA scr
        LDA (mptr_lo),Y         ; lo = cols 8-15
        STA scr2
        LDA scr                 ; c0 = hi >> 4
        LSR
        LSR
        LSR
        LSR
        STA c0v
        LDA scr2                ; c2 = lo << 4
        ASL
        ASL
        ASL
        ASL
        STA c2v
        LDA scr                 ; c1 = (hi<<4) | (lo>>4)
        ASL
        ASL
        ASL
        ASL
        STA c1v
        LDA scr2
        LSR
        LSR
        LSR
        LSR
        ORA c1v
        STA c1v
        ; sy = pr+4 ; cr = sy>>3 ; ri = sy&7 ; idx = cr*24 + ri
        TYA
        CLC
        ADC #PIECE_OFF
        STA scr3
        AND #7
        STA scr4                ; ri
        LDA scr3
        LSR
        LSR
        LSR                     ; cr 0..2
        TAX
        LDA col_x,X             ; cr*24 (col_x[0,1,2] = 0,24,48)
        CLC
        ADC scr4
        TAX                     ; idx into cbuf for cell column 0
        LDA c0v
        STA cbuf,X
        LDA c1v
        STA cbuf+8,X            ; cell column 1
        LDA c2v
        STA cbuf+16,X           ; cell column 2
        INY
        CPY #16
        BNE @pr
        RTS

; draw_cells9: blit the 9 composed cells (cbuf) of the current square.
draw_cells9:
        LDX #0
@c:     LDA sq_px
        CLC
        ADC cell9_dx,X
        STA pix_x
        LDA sq_py
        CLC
        ADC cell9_dy,X
        STA pix_y
        TXA
        ASL
        ASL
        ASL                     ; X*8 = cbuf cell offset
        CLC
        ADC #<cbuf
        STA mptr_lo
        LDA #>cbuf
        ADC #0
        STA mptr_hi
        STX save_x
        JSR blit_cell
        LDX save_x
        INX
        CPX #9
        BNE @c
        RTS

; draw_empty9: blit 9 solid-background cells (pattern 0, colour = cellcolor).
draw_empty9:
        LDX #0
@c:     LDA sq_px
        CLC
        ADC cell9_dx,X
        STA pix_x
        LDA sq_py
        CLC
        ADC cell9_dy,X
        STA pix_y
        LDA #<zeros8
        STA mptr_lo
        LDA #>zeros8
        STA mptr_hi
        STX save_x
        JSR blit_cell
        LDX save_x
        INX
        CPX #9
        BNE @c
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
; Apple-1 character-screen output: startup presentation + keyboard menu +
; game-over announcement (the moves/board live on the TMS card).
; ---------------------------------------------------------------------------
; cout: print A (7-bit) to the Apple-1 display via the Woz DSP handshake
;   (bit 7 = busy). Bounded wait so it can never hang. Clobbers A,X; keeps Y.
cout:
        ORA #$80                ; the display wants bit 7 set
        LDX #$60
@w:     BIT DSP
        BPL @rdy                ; bit 7 = 0 -> ready
        DEX
        BNE @w
@rdy:   STA DSP
        RTS

; puts_a1: print the NUL-terminated string at (sptr) to the Apple-1 display.
puts_a1:
        LDY #0
@l:     LDA (sptr_lo),Y
        BEQ @done
        JSR cout                ; preserves Y
        INY
        BNE @l
@done:  RTS

; a1_choose_mode: present the game + read the play mode (1..4) from the
;   keyboard. Blocks until a valid key; sets play_mode + echoes the label.
a1_choose_mode:
        LDA #<a1_splash
        STA sptr_lo
        LDA #>a1_splash
        STA sptr_hi
        JSR puts_a1
@ask:   LDA #<a1_modeprompt
        STA sptr_lo
        LDA #>a1_modeprompt
        STA sptr_hi
        JSR puts_a1
        JSR wait_key
        CMP #'1'
        BCC @ask
        CMP #'5'
        BCS @ask
        SEC
        SBC #'1'                ; '1'..'4' -> 0..3
        STA play_mode
        LDX play_mode           ; echo the chosen mode label
        LDA a1_modelbl_lo,X
        STA sptr_lo
        LDA a1_modelbl_hi,X
        STA sptr_hi
        JMP puts_a1             ; tail

; a1_result: announce the game result (game_result) + "PRESS N" on the Apple-1.
a1_result:
        LDX game_result
        LDA overstr_lo,X
        STA sptr_lo
        LDA overstr_hi,X
        STA sptr_hi
        JSR puts_a1
        LDA #<a1_pressn
        STA sptr_lo
        LDA #>a1_pressn
        STA sptr_hi
        JMP puts_a1             ; tail

; a1_turn_status: on the Apple-1, announce whose turn it is -- "X IS THINKING"
;   when that side is the AI, "X TO PLAY - YOUR MOVE" when it is the human.
;   Prints only when the side to move changed (caller sets turn_ai = side_is_ai).
a1_turn_status:
        LDA side_to_move
        CMP printed_side
        BEQ @done
        STA printed_side
        LDA turn_ai
        BNE @ai
        LDA side_to_move        ; human to move
        BNE @hb
        LDA #<a1_wt_play
        LDX #>a1_wt_play
        JMP @pr
@hb:    LDA #<a1_bl_play
        LDX #>a1_bl_play
        JMP @pr
@ai:    LDA side_to_move        ; AI to move
        BNE @ab
        LDA #<a1_wt_think
        LDX #>a1_wt_think
        JMP @pr
@ab:    LDA #<a1_bl_think
        LDX #>a1_bl_think
@pr:    STA sptr_lo
        STX sptr_hi
        JMP puts_a1
@done:  RTS

; ---------------------------------------------------------------------------
; Status / text overlays (need the charmap font -> CODETANK_BUILD only).
; ---------------------------------------------------------------------------
; update_status: side to move + mode at the very top of the right panel
;   (row 0). The move list fills the rows below (from MLIST_TOP down).
update_status:
.ifdef CODETANK_BUILD
        LDA #WHITE_INK
        STA pen_color
        LDA #RMARGIN_X
        STA pix_x
        LDA #0
        STA pix_y
        LDA side_to_move
        BNE @black
        LDA #'W'
        JMP @ps
@black: LDA #'B'
@ps:    JSR putc_tms
        LDA #' '
        JSR putc_tms
        LDX play_mode
        LDA modestr_lo,X
        STA sptr_lo
        LDA modestr_hi,X
        STA sptr_hi
        JSR puts
.endif
        RTS

; putc_tms: blit glyph A at pix_x/pix_y then advance pix_x by 8 (one cell).
putc_tms:
.ifdef CODETANK_BUILD
        JSR text_blit_glyph
        LDA pix_x
        CLC
        ADC #8
        STA pix_x
.endif
        RTS

; print_sq_tms: print 0x88 square A as "FR" (file letter + rank digit).
print_sq_tms:
.ifdef CODETANK_BUILD
        PHA
        AND #$07
        CLC
        ADC #'A'
        JSR putc_tms
        PLA
        LSR
        LSR
        LSR
        LSR
        CLC
        ADC #'1'
        JSR putc_tms
.endif
        RTS

; print_move_tms: log the last move (mv_from,mv_to) in the right panel as
;   "s FROMTO" (s = the side that just moved). Advances/wraps move_row.
print_move_tms:
.ifdef CODETANK_BUILD
        LDA #WHITE_INK
        STA pen_color
        LDA #RMARGIN_X
        STA pix_x
        LDA move_row
        ASL
        ASL
        ASL                     ; row*8
        STA pix_y
        LDA side_to_move        ; already toggled -> the OTHER side just moved
        BNE @wm
        LDA #'B'
        JMP @ps
@wm:    LDA #'W'
@ps:    JSR putc_tms
        LDA #' '
        JSR putc_tms
        LDA mv_from
        JSR print_sq_tms
        LDA mv_to
        JSR print_sq_tms
        INC move_row
        LDA move_row
        CMP #MLIST_BOT
        BCC @done
        JSR clear_movelist      ; column full -> blank + restart at top
@done:
.endif
        RTS

; clear_movelist: blank right-panel rows [MLIST_TOP, MLIST_BOT), reset move_row.
clear_movelist:
.ifdef CODETANK_BUILD
        LDA #WHITE_INK
        STA pen_color
        LDX #MLIST_TOP
@row:   LDA #RMARGIN_X
        STA pix_x
        TXA
        ASL
        ASL
        ASL                     ; row*8
        STA pix_y
        LDY #6                  ; 6 cells wide (x=208..255)
@col:   STX save_x
        STY save_y2
        LDA #' '
        JSR putc_tms
        LDX save_x
        LDY save_y2
        DEY
        BNE @col
        INX
        CPX #MLIST_BOT
        BNE @row
        LDA #MLIST_TOP
        STA move_row
.endif
        RTS

; draw_coords: rank numbers 1..8 down the left margin (static, drawn once).
;   File letters are dropped -- the full-height board leaves no bottom row.
draw_coords:
.ifdef CODETANK_BUILD
        LDA #$0E                ; grey
        STA pen_color
        LDX #0                  ; rank 0..7 (rank 8 at top)
@r:     LDA #0                  ; leftmost column, just left of the board
        STA pix_x
        TXA
        EOR #$07                ; drow = 7-rank
        TAY
        LDA col_x,Y             ; drow*24
        CLC
        ADC #8                  ; centre digit in the 24-px row
        STA pix_y
        TXA
        CLC
        ADC #'1'
        STX save_x
        JSR text_blit_glyph
        LDX save_x
        INX
        CPX #8
        BNE @r
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

; col_x[i] = i*24 -- board file/rank pixel offsets AND cell-row*24 for compose.
col_x:     .byte 0, 24, 48, 72, 96, 120, 144, 168
; the 9 cells of a 24x24 square, in (row-major) blit order.
cell9_dx:  .byte 0, 8, 16, 0, 8, 16, 0, 8, 16
cell9_dy:  .byte 0, 0, 0, 8, 8, 8, 16, 16, 16

piece_ptrs_lo:
        .byte <chess_pawn_pat, <chess_knight_pat, <chess_bishop_pat
        .byte <chess_rook_pat, <chess_queen_pat, <chess_king_pat
piece_ptrs_hi:
        .byte >chess_pawn_pat, >chess_knight_pat, >chess_bishop_pat
        .byte >chess_rook_pat, >chess_queen_pat, >chess_king_pat

; --- Apple-1 screen strings (DSP output; no font needed, always present) ----
; $0D = CR (cout sets bit 7 -> $8D). Lines <= 40 chars (Apple-1 screen width).
a1_splash:
        .byte "APPLE-1 CHESS  V0.6", $0D
        .byte "BY VERHILLE ARNAUD, 2026", $0D
        .byte $0D
        .byte "PLAY ON THE TMS9918 SCREEN:", $0D
        .byte " IJKL = MOVE THE CURSOR", $0D
        .byte " SPACE = SELECT SQUARE", $0D
        .byte " M MODE  N NEW  U UNDO  P AI", $0D
        .byte $0D, 0
a1_modeprompt:
        .byte "MODE? 1=HVH 2=WAI 3=BAI 4=AVA: ", 0
a1_pressn:
        .byte $0D, "PRESS N FOR A NEW GAME", $0D, 0

a1_wt_play:  .byte "WHITE TO PLAY - YOUR MOVE", $0D, 0
a1_bl_play:  .byte "BLACK TO PLAY - YOUR MOVE", $0D, 0
a1_wt_think: .byte "WHITE IS THINKING...", $0D, 0
a1_bl_think: .byte "BLACK IS THINKING...", $0D, 0

a1_mode_hvh: .byte $0D, " MODE: HUMAN VS HUMAN", $0D, 0
a1_mode_wai: .byte $0D, " MODE: HUMAN(W) VS AI(B)", $0D, 0
a1_mode_bai: .byte $0D, " MODE: AI(W) VS HUMAN(B)", $0D, 0
a1_mode_ava: .byte $0D, " MODE: AI(W) VS AI(B)", $0D, 0
a1_modelbl_lo: .byte <a1_mode_hvh, <a1_mode_wai, <a1_mode_bai, <a1_mode_ava
a1_modelbl_hi: .byte >a1_mode_hvh, >a1_mode_wai, >a1_mode_bai, >a1_mode_ava

o_bwin: .byte $0D, " CHECKMATE - BLACK WINS", $0D, 0
o_wwin: .byte $0D, " CHECKMATE - WHITE WINS", $0D, 0
o_stale:.byte $0D, " STALEMATE - DRAW", $0D, 0
o_draw: .byte $0D, " DRAW", $0D, 0
overstr_lo: .byte 0, <o_bwin, <o_wwin, <o_stale, <o_draw
overstr_hi: .byte 0, >o_bwin, >o_wwin, >o_stale, >o_draw

.ifdef CODETANK_BUILD
m_hvh:  .byte "HVH", 0
m_wai:  .byte "WAI", 0
m_bai:  .byte "BAI", 0
m_ava:  .byte "AVA", 0
modestr_lo: .byte <m_hvh, <m_wai, <m_bai, <m_ava
modestr_hi: .byte >m_hvh, >m_wai, >m_bai, >m_ava
.endif
