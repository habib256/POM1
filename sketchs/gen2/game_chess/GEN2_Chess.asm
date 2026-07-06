; =============================================================================
; GEN2_Chess.asm -- graphical Chess for the Apple-1 + Uncle Bernie GEN2 HGR card
; VERHILLE Arnaud - 2026
; =============================================================================
; HGR front-end for the shared chess engine (dev/lib/games/chess/
; chess_engine.asm), sibling of TMS_Chess.asm. The engine + game loop + Apple-1
; text UI are platform-agnostic; only the board drawing is HGR-specific.
;
; Board + pieces: adopted from Stefan Wessels' cc65-Chess (StewBC/cc65-Chess),
; Apple II port by Oliver Schmidt, piece art by Frank Gebhart. Each square is a
; 3-byte (21 px) x 22-row cell (origin byte col 2 / scanline 14). The board is
; a BLACK/WHITE checkerboard; a piece is a monochrome bitmap
; (dev/lib/gen2/sprites/chess_cc65_pieces) drawn with a COPY rop (EOR #$80, on
; black squares) or an INVERT rop (EOR #$FF, on white squares), the outline vs
; solid variant chosen (variant = blackWhite ^ isBlackPiece) so both sides read
; correctly on both square colours -- exactly cc65's platA2.c logic. The text
; panel (move list, coords, status) uses OUR bbfont subset (bbfont_ascii5f.inc).
;
; Cursor / selection are BITMAP overlays (no hardware sprites): a persistent
; XOR frame marks the selected square, blinking XOR corner ticks mark the
; cursor (human turn only) -- visible on black AND white squares.
;
; Controls (Apple-1 keyboard, forced uppercase):
;   I/J/K/L  move cursor        SPACE or RETURN  pick source / target
;   ESC      cancel selection   M  cycle mode     N  new game
;   U        undo last move     P  toggle AI strategy (FAST 1-ply / STRONG 2-ply)
; Modes (M cycles): HVH, WAI (white=you/black=AI), BAI, AVA (auto self-play).
;
; Build: single-region image at $6000 (above HGR page 2) on the 48 KB GEN2 HGR
; Color machine; links chess_engine.o. Entry: 6000R.
; =============================================================================

.include "apple1.inc"
.include "chess_common.inc"

; --- geometry (cc65-Chess layout) -----------------------------------------
PC_W       = 3          ; piece/square width in bytes (21 px)
PC_H       = 22         ; piece/square height in scanlines
BOARD_X0   = 2          ; left byte column of the board (col 0..1 = rank digit)
BOARD_Y0   = 14         ; top scanline of the board
BLINK_HI   = $40        ; cursor blink half-period (poll-loop iters, hi byte)
FILL_WHITE = $FF        ; white square / INVERT rop mask
FILL_BLACK = $80        ; black square / COPY rop mask (bit 7 = HGR palette)
; --- right text panel (bbfont) --------------------------------------------
RCOL0      = 27         ; panel left byte column (x = 189), past the board -- status
MLCOL      = 31         ; move-list left byte column (RCOL0 + 4 chars)
MLWIDTH    = 9          ; move-list width in chars (cols 31..39)
MLIST_TOP  = 2          ; first panel text row used by the move list
MLIST_BOT  = 23         ; one past the last usable row (row*8 = scanline)
COORDY     = 3          ; scanline of the file-letter row (above the board)

; ---------------------------------------------------------------------------
; Zero page. tmp/tmp2 MUST be $00/$01 (the engine .importzp them).
; ---------------------------------------------------------------------------
.segment "ZEROPAGE"
tmp:       .res 1       ; $00
tmp2:      .res 1       ; $01
sptr_lo:   .res 1       ; string ptr (Apple-1 puts + panel puts)
sptr_hi:   .res 1
ptr_lo:    .res 1       ; scanline base pointer (hgr_clear + blits)
ptr_hi:    .res 1
mptr_lo:   .res 1       ; piece bitmap / glyph source ptr
mptr_hi:   .res 1
.exportzp tmp, tmp2

.segment "CODE"
.import init_board, apply_user_move, ai_play_move, game_status, in_check
.import piece_at, undo_last_move
.import side_to_move, mv_from, mv_to, mv_promo, ai_strategy, ai_rng

; ---------------------------------------------------------------------------
; BSS scratch (uninitialised -- main/new_game seed every field explicitly).
; ---------------------------------------------------------------------------
.segment "BSS"
dsq:        .res 1      ; square being drawn (0x88)
cur_sq:     .res 1      ; cursor square (0x88)
old_cur:    .res 1      ; previous cursor square
sel_sq:     .res 1      ; selected source ($FF = none)
sel_active: .res 1
piece_code: .res 1
play_mode:  .res 1      ; 0 HvH / 1 WAI / 2 BAI / 3 AvA
movecount:  .res 1
game_result:.res 1
cursor_on:  .res 1
blink_vis:  .res 1
blink_lo:   .res 1
blink_hi:   .res 1
turn_ai:    .res 1
printed_side: .res 1
seed_acc:   .res 1
move_row:   .res 1
hist_n:     .res 1
; --- HGR renderer scratch ---
frank:      .res 1      ; rank 0..7 (0 = white home)
ffile:      .res 1      ; file 0..7
drow:       .res 1      ; display row 0..7 (0 = top = rank 8)
topsl:      .res 1      ; top scanline of the square
bcol:       .res 1      ; left byte column of the square
bw:         .res 1      ; 1 = white square, 0 = black square
sqval:      .res 1      ; white/black fill byte = COPY/INVERT rop mask
pvariant:   .res 1      ; piece bitmap variant (0 outline / 1 solid)
scratch:    .res 1
prow:       .res 1      ; blit row counter
save_x:     .res 1
; --- text panel ---
tx_col:     .res 1      ; glyph blit byte column
tx_sl:      .res 1      ; glyph blit top scanline
ml_side:    .res 1
ml_from:    .res 1
ml_to:      .res 1
cur_side:   .res 1
p1_side:    .res 1
p1_from:    .res 1
p1_to:      .res 1
p2_side:    .res 1
p2_from:    .res 1
p2_to:      .res 1

; ===========================================================================
.segment "CODE"
; ===========================================================================

main:
        JSR init_board
        LDA #0
        STA sel_active
        STA movecount
        STA hist_n
        STA cursor_on
        LDA #2
        STA move_row
        LDA #$FF
        STA sel_sq
        LDA #$14                ; cursor starts on e2 (file 4, rank 1)
        STA cur_sq
        LDA #1
        STA ai_strategy         ; STRONG (2-ply) default
        STA blink_vis
        LDA #$FF
        STA printed_side
        JSR gen2_hgr_init_clear  ; HGR + PAGE1 + blank the framebuffer
        JSR draw_board
        JSR draw_coords
        JSR a1_choose_mode

game_loop:
        JSR check_terminal
        JSR update_status
        JSR side_is_ai
        STA turn_ai
        JSR a1_turn_status
        LDA turn_ai
        EOR #1
        STA cursor_on
        LDA turn_ai
        BEQ human_turn
        LDA #0                  ; AI turn: erase the cursor ticks
        STA blink_vis
        LDA cur_sq
        STA dsq
        JSR draw_square
        JMP ai_turn

human_turn:
        LDA #1
        STA blink_vis
        LDA cur_sq
        STA dsq
        JSR draw_square
        LDA #0
        STA blink_lo
        STA blink_hi
@wait:  JSR poll_key
        BNE @got
        INC blink_lo
        BNE @wait
        INC blink_hi
        LDA blink_hi
        CMP #BLINK_HI
        BCC @wait
        LDA #0
        STA blink_hi
        LDA blink_vis
        EOR #1
        STA blink_vis
        LDA cur_sq
        STA dsq
        JSR draw_square
        JMP @wait
@got:   CMP #'I'
        BNE @n1
        JSR cur_up
        JMP game_loop
@n1:    CMP #'K'
        BNE @n2
        JSR cur_down
        JMP game_loop
@n2:    CMP #'J'
        BNE @n3
        JSR cur_left
        JMP game_loop
@n3:    CMP #'L'
        BNE @n4
        JSR cur_right
        JMP game_loop
@n4:    CMP #' '
        BNE @n5
        JMP do_confirm
@n5:    CMP #$0D
        BNE @n6
        JMP do_confirm
@n6:    CMP #$1B
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
@k4:    JMP game_loop

do_confirm:
        LDA sel_active
        BNE @have
        LDX cur_sq
        JSR piece_at
        STA piece_code
        AND #PIECE_MASK
        BEQ @ret
        LDA piece_code
        AND #COLOR_BLACK
        CMP side_to_move
        BNE @ret
        LDA cur_sq
        STA sel_sq
        LDA #1
        STA sel_active
        LDA cur_sq
        STA dsq
        JSR draw_square
@ret:   JMP game_loop
@have:  LDA cur_sq
        CMP sel_sq
        BEQ do_deselect
        LDA sel_sq
        STA mv_from
        LDA cur_sq
        STA mv_to
        LDA #0
        STA mv_promo
        JSR apply_user_move
        BCS do_deselect
        LDA #0
        STA sel_active
        STA cursor_on
        LDA #$FF
        STA sel_sq
        JSR draw_board
        JSR print_move_hgr
        JMP game_loop

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
        STA printed_side
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
        STA cursor_on
        STA hist_n
        LDA #$FF
        STA sel_sq
        LDA #$14
        STA cur_sq
        LDA #$FF
        STA printed_side
        LDA #2
        STA move_row
        JSR draw_board
        JSR clear_movelist
        JSR a1_choose_mode
        JMP game_loop

ai_turn:
        LDA play_mode
        CMP #3
        BNE @go
        LDA movecount
        CMP #200
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
        BCS @loop
        INC movecount
        JSR draw_board
        JSR print_move_hgr
@loop:  JMP game_loop

side_is_ai:
        LDA play_mode
        BEQ @human
        CMP #3
        BEQ @ai
        CMP #1
        BEQ @m1
        LDA side_to_move
        BEQ @ai
        BNE @human
@m1:    LDA side_to_move
        BNE @ai
@human: LDA #0
        RTS
@ai:    LDA #1
        RTS

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

cur_up:
        LDA cur_sq
        CMP #$70
        BCS cur_ret
        STA old_cur
        CLC
        ADC #$10
        STA cur_sq
        JMP cur_moved
cur_down:
        LDA cur_sq
        CMP #$10
        BCC cur_ret
        STA old_cur
        SEC
        SBC #$10
        STA cur_sq
        JMP cur_moved
cur_left:
        LDA cur_sq
        AND #$07
        BEQ cur_ret
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
        BEQ cur_ret
        LDA cur_sq
        STA old_cur
        CLC
        ADC #1
        STA cur_sq
cur_moved:
        LDA #1
        STA blink_vis
        LDA old_cur
        STA dsq
        JSR draw_square
        LDA cur_sq
        STA dsq
        JSR draw_square
cur_ret:
        RTS

; ===========================================================================
; HGR renderer (cc65-Chess board + pieces)
; ===========================================================================
draw_board:
        LDX #0                  ; rank 0..7
@r:     LDY #0                  ; file 0..7
@f:     TXA
        ASL
        ASL
        ASL
        ASL                     ; rank*16
        STA tmp
        TYA
        ORA tmp                 ; sq = rank*16 + file
        STA dsq
        STX tmp2
        TYA
        PHA
        JSR draw_square
        PLA
        TAY
        LDX tmp2
        INY
        CPY #8
        BNE @f
        INX
        CPX #8
        BNE @r
        RTS

; ---------------------------------------------------------------------------
; draw_square: paint square `dsq` -- black/white fill or piece bitmap (copy/
;   invert), then the selection frame / cursor ticks. Clobbers A,X,Y.
; ---------------------------------------------------------------------------
draw_square:
        LDA dsq
        AND #$07
        STA ffile
        LDA dsq
        LSR
        LSR
        LSR
        LSR
        STA frank
        LDA #7
        SEC
        SBC frank
        STA drow                ; display row (0 = top)
        ; topsl = BOARD_Y0 + (7-rank)*22   (topsl_tab indexed by frank)
        LDX frank
        LDA topsl_tab,X
        STA topsl
        ; bcol = BOARD_X0 + ffile*3
        LDX ffile
        LDA bcol_tab,X
        STA bcol
        ; blackWhite = !((ffile & 1) ^ (drow & 1))  -> bw (1 = white)
        LDA ffile
        AND #1
        STA tmp
        LDA drow
        AND #1
        EOR tmp
        EOR #1
        STA bw
        ; sqval = bw ? $FF : $80   (fill colour AND copy/invert rop mask)
        LDA #FILL_WHITE
        LDX bw
        BNE @haveval
        LDA #FILL_BLACK
@haveval:
        STA sqval
        ; occupied?
        LDX dsq
        JSR piece_at
        STA piece_code
        AND #PIECE_MASK
        BNE @occupied
        JSR fill_square         ; empty: solid black/white
        JMP @overlays
@occupied:
        ; The copy/invert rop is square-colour-dependent, so drawing the SAME
        ; bitmap on both square colours would flip a piece square-to-square.
        ; cc65's fix: variant = blackWhite ^ colourTerm -- the outline/solid
        ; variant compensates the rop flip. colourTerm = isWhite (our bit 7 =
        ; BLACK, inverted vs cc65) keeps the white side light (bottom).
        LDA piece_code
        AND #COLOR_BLACK        ; $80 if black piece
        BEQ @wpc
        LDA #0                  ; black piece
        JMP @haveblk
@wpc:   LDA #1                  ; white piece
@haveblk:
        EOR bw
        STA pvariant
        ; cc65 bitmap index = type2cc65[type]*2 + variant
        LDA piece_code
        AND #PIECE_MASK
        TAX
        LDA type2cc65,X
        ASL                     ; *2
        ORA pvariant
        TAX
        LDA cc65_piece_lo,X
        STA mptr_lo
        LDA cc65_piece_hi,X
        STA mptr_hi
        JSR blit_piece          ; eormask = sqval
@overlays:
        LDA sel_active
        BEQ @nosel
        LDA dsq
        CMP sel_sq
        BNE @nosel
        JSR draw_sel_frame
@nosel:
        LDA cursor_on
        BEQ @done
        LDA blink_vis
        BEQ @done
        LDA dsq
        CMP cur_sq
        BNE @done
        JSR draw_cursor_ticks
@done:
        RTS

; ---------------------------------------------------------------------------
; fill_square: fill the 3x22 square at bcol/topsl with sqval.
; ---------------------------------------------------------------------------
fill_square:
        LDA #0
        STA prow
@row:   LDA prow
        CLC
        ADC topsl
        TAX
        LDA hgr_lo,X
        STA ptr_lo
        LDA hgr_hi,X
        STA ptr_hi
        LDY bcol
        LDA sqval
        STA (ptr_lo),Y
        INY
        STA (ptr_lo),Y
        INY
        STA (ptr_lo),Y
        INC prow
        LDA prow
        CMP #PC_H
        BNE @row
        RTS

; ---------------------------------------------------------------------------
; blit_piece: draw the 3x22 bitmap at (mptr) into bcol/topsl, each source byte
;   EOR sqval (sqval = $80 copy / $FF invert), exactly cc65's ROP_CPY/ROP_INV.
; ---------------------------------------------------------------------------
blit_piece:
        LDA #0
        STA prow
@row:   LDA prow
        CLC
        ADC topsl
        TAX
        LDA hgr_lo,X
        STA ptr_lo
        LDA hgr_hi,X
        STA ptr_hi
        LDY #0                  ; source column 0..2
@col:   LDA (mptr_lo),Y
        EOR sqval
        STA scratch
        STY save_x
        TYA
        CLC
        ADC bcol
        TAY
        LDA scratch
        STA (ptr_lo),Y
        LDY save_x
        INY
        CPY #PC_W
        BNE @col
        LDA mptr_lo
        CLC
        ADC #PC_W
        STA mptr_lo
        BCC @nc
        INC mptr_hi
@nc:    INC prow
        LDA prow
        CMP #PC_H
        BNE @row
        RTS

; ---------------------------------------------------------------------------
; draw_cursor_ticks: XOR 3-px corner ticks (visible on black AND white).
; ---------------------------------------------------------------------------
draw_cursor_ticks:
        LDX #0
@t:     LDA tickrows,X
        CLC
        ADC topsl
        TAY
        LDA hgr_lo,Y
        STA ptr_lo
        LDA hgr_hi,Y
        STA ptr_hi
        LDY bcol
        LDA (ptr_lo),Y
        EOR #$07                ; toggle px 0..2 (left tick)
        STA (ptr_lo),Y
        LDY bcol
        INY
        INY
        LDA (ptr_lo),Y
        EOR #$70                ; toggle px 18..20 (right tick)
        STA (ptr_lo),Y
        INX
        CPX #6
        BNE @t
        RTS

; ---------------------------------------------------------------------------
; draw_sel_frame: XOR a 1-px frame around the square.
; ---------------------------------------------------------------------------
draw_sel_frame:
        LDX #0
@tb:    LDA framerows,X
        CLC
        ADC topsl
        TAY
        LDA hgr_lo,Y
        STA ptr_lo
        LDA hgr_hi,Y
        STA ptr_hi
        LDY bcol
        LDA (ptr_lo),Y
        EOR #$7F
        STA (ptr_lo),Y
        INY
        LDA (ptr_lo),Y
        EOR #$7F
        STA (ptr_lo),Y
        INY
        LDA (ptr_lo),Y
        EOR #$7F
        STA (ptr_lo),Y
        INX
        CPX #2
        BNE @tb
        LDX #1
@sd:    TXA
        CLC
        ADC topsl
        TAY
        LDA hgr_lo,Y
        STA ptr_lo
        LDA hgr_hi,Y
        STA ptr_hi
        LDY bcol
        LDA (ptr_lo),Y
        EOR #$01                ; left edge px 0
        STA (ptr_lo),Y
        LDY bcol
        INY
        INY
        LDA (ptr_lo),Y
        EOR #$40                ; right edge px 20
        STA (ptr_lo),Y
        INX
        CPX #21
        BNE @sd
        RTS

; ===========================================================================
; HGR text panel (OUR bbfont subset) -- move list, coords, status
; ===========================================================================
; putc_hgr: blit glyph A at tx_col/tx_sl, advance tx_col. Clobbers A,X,Y.
putc_hgr:
        SEC
        SBC #$20
        BCC @space
        CMP #64
        BCC @have
@space: LDA #0
@have:  STA tmp
        LDA #0
        STA tmp2
        ASL tmp
        ROL tmp2
        ASL tmp
        ROL tmp2
        ASL tmp
        ROL tmp2                ; index*8
        LDA tmp
        CLC
        ADC #<HGR_Font5F
        STA mptr_lo
        LDA tmp2
        ADC #>HGR_Font5F
        STA mptr_hi
        LDX #0                  ; glyph row 0..7
@r:     TXA
        CLC
        ADC tx_sl
        TAY
        LDA hgr_lo,Y
        STA ptr_lo
        LDA hgr_hi,Y
        STA ptr_hi
        TXA
        TAY
        LDA (mptr_lo),Y
        LDY tx_col
        STA (ptr_lo),Y
        INX
        CPX #8
        BNE @r
        INC tx_col
        RTS

; puts_hgr: print NUL-terminated (sptr) via putc_hgr at tx_col/tx_sl.
puts_hgr:
@l:     LDY #0
        LDA (sptr_lo),Y
        BEQ @d
        JSR putc_hgr
        INC sptr_lo
        BNE @l
        INC sptr_hi
        JMP @l
@d:     RTS

; print_sq_hgr: A = 0x88 square -> "FR" (file letter + rank digit).
print_sq_hgr:
        PHA
        AND #$07
        CLC
        ADC #'A'
        JSR putc_hgr
        PLA
        LSR
        LSR
        LSR
        LSR
        CLC
        ADC #'1'
        JSR putc_hgr
        RTS

; print_ml_entry: "s FROMTO" for (ml_side,ml_from,ml_to) at move_row.
print_ml_entry:
        LDA #MLCOL
        STA tx_col
        LDA move_row
        ASL
        ASL
        ASL
        STA tx_sl
        LDA ml_side
        JSR putc_hgr
        LDA #' '
        JSR putc_hgr
        LDA ml_from
        JSR print_sq_hgr
        LDA ml_to
        JSR print_sq_hgr
        INC move_row
        RTS

; print_move_hgr: log the move just made (mv_from,mv_to). Wraps the column when
;   full, re-printing the last two moves for continuity (2-deep history).
print_move_hgr:
        LDA side_to_move        ; side that just moved (already toggled)
        BNE @wm
        LDA #'B'
        JMP @sv
@wm:    LDA #'W'
@sv:    STA cur_side
        LDA move_row
        CMP #MLIST_BOT
        BCC @room
        JSR clear_movelist
        LDA hist_n
        CMP #2
        BCC @room
        LDA p2_side
        STA ml_side
        LDA p2_from
        STA ml_from
        LDA p2_to
        STA ml_to
        JSR print_ml_entry
        LDA p1_side
        STA ml_side
        LDA p1_from
        STA ml_from
        LDA p1_to
        STA ml_to
        JSR print_ml_entry
@room:  LDA cur_side
        STA ml_side
        LDA mv_from
        STA ml_from
        LDA mv_to
        STA ml_to
        JSR print_ml_entry
        LDA p1_side             ; shift history
        STA p2_side
        LDA p1_from
        STA p2_from
        LDA p1_to
        STA p2_to
        LDA cur_side
        STA p1_side
        LDA mv_from
        STA p1_from
        LDA mv_to
        STA p1_to
        LDA hist_n
        CMP #2
        BCS @done
        INC hist_n
@done:  RTS

; clear_movelist: blank panel rows [MLIST_TOP,MLIST_BOT), reset move_row.
clear_movelist:
        LDX #MLIST_TOP
@row:   LDA #MLCOL
        STA tx_col
        TXA
        ASL
        ASL
        ASL
        STA tx_sl
        STX save_x
        LDY #MLWIDTH            ; move-list width in chars (cols 31..39)
@col:   STY scratch
        LDA #' '
        JSR putc_hgr
        LDY scratch
        DEY
        BNE @col
        LDX save_x
        INX
        CPX #MLIST_BOT
        BNE @row
        LDA #MLIST_TOP
        STA move_row
        RTS

; update_status: side to move + mode at the top of the right panel (row 0).
update_status:
        LDA #MLCOL
        STA tx_col
        LDA #0
        STA tx_sl
        LDA side_to_move
        BNE @b
        LDA #'W'
        JMP @ps
@b:     LDA #'B'
@ps:    JSR putc_hgr
        LDA #' '
        JSR putc_hgr
        LDX play_mode
        LDA modestr_lo,X
        STA sptr_lo
        LDA modestr_hi,X
        STA sptr_hi
        JMP puts_hgr            ; tail

; draw_coords: rank digits (left column) + file letters (above the board).
draw_coords:
        LDX #0                  ; rank 0..7
@r:     LDA #0
        STA tx_col
        LDA topsl_tab,X
        CLC
        ADC #7
        STA tx_sl
        TXA
        CLC
        ADC #'1'
        STX save_x
        JSR putc_hgr
        LDX save_x
        INX
        CPX #8
        BNE @r
        LDX #0                  ; file 0..7
@f:     LDA bcol_tab,X
        CLC
        ADC #1                  ; centre the glyph in the 3-byte square
        STA tx_col
        LDA #COORDY
        STA tx_sl
        TXA
        CLC
        ADC #'A'
        STX save_x
        JSR putc_hgr
        LDX save_x
        INX
        CPX #8
        BNE @f
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

poll_key:
        LDA KBDCR
        BPL @none
        LDA KBD
        AND #$7F
        RTS
@none:  LDA #0
        RTS

; ---------------------------------------------------------------------------
; Apple-1 character-screen output.
; ---------------------------------------------------------------------------
cout:
        ORA #$80
@w:     BIT DSP
        BMI @w
        STA DSP
        RTS

puts_a1:
        LDY #0
@l:     LDA (sptr_lo),Y
        BEQ @done
        JSR cout
        INC sptr_lo
        BNE @l
        INC sptr_hi
        JMP @l
@done:  RTS

a1_choose_mode:
        LDA #<a1_splash
        STA sptr_lo
        LDA #>a1_splash
        STA sptr_hi
        JSR puts_a1
        LDA #0
        STA seed_acc
@ask:   LDA #<a1_modeprompt
        STA sptr_lo
        LDA #>a1_modeprompt
        STA sptr_hi
        JSR puts_a1
@spin:  INC seed_acc
        LDA KBDCR
        BPL @spin
        LDA KBD
        AND #$7F
        CMP #'1'
        BCC @ask
        CMP #'5'
        BCS @ask
        SEC
        SBC #'1'
        STA play_mode
        LDA ai_rng
        EOR seed_acc
        BNE @seeded
        LDA #$AC
@seeded:
        STA ai_rng
        LDX play_mode
        LDA a1_modelbl_lo,X
        STA sptr_lo
        LDA a1_modelbl_hi,X
        STA sptr_hi
        JMP puts_a1

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
        JMP puts_a1

a1_turn_status:
        LDA side_to_move
        CMP printed_side
        BEQ @done
        STA printed_side
        LDA turn_ai
        BNE @ai
        LDA side_to_move
        BNE @hb
        LDA #<a1_wt_play
        LDX #>a1_wt_play
        JMP @pr
@hb:    LDA #<a1_bl_play
        LDX #>a1_bl_play
        JMP @pr
@ai:    LDA side_to_move
        BNE @ab
        LDA #<a1_wt_think
        LDX #>a1_wt_think
        JMP @pr
@ab:    LDA #<a1_bl_think
        LDX #>a1_bl_think
@pr:    STA sptr_lo
        STX sptr_hi
        JSR puts_a1
        JSR in_check
        BEQ @done
        LDA #<a1_check
        STA sptr_lo
        LDA #>a1_check
        STA sptr_hi
        JSR puts_a1
@done:  RTS

; ---------------------------------------------------------------------------
; Read-only data / tables.
; ---------------------------------------------------------------------------
; top scanline per rank (rank 0 = white home, drawn at the BOTTOM):
;   topsl = BOARD_Y0 + (7-rank)*22
topsl_tab:
        .byte 168, 146, 124, 102, 80, 58, 36, 14
; left byte column per file = BOARD_X0 + file*3
bcol_tab:
        .byte 2, 5, 8, 11, 14, 17, 20, 23
; corner-tick rows / frame edge rows (offsets from topsl, 22-tall square)
tickrows:  .byte 0, 1, 2, 19, 20, 21
framerows: .byte 0, 21
; my piece type (1..6 = P,N,B,R,Q,K) -> cc65 bitmap index (ROOK=0,KNIGHT=1,
; BISHOP=2,QUEEN=3,KING=4,PAWN=5). Index 0 unused.
type2cc65:
        .byte 0, 5, 1, 2, 0, 3, 4

; --- Apple-1 screen strings -------------------------------------------------
a1_splash:
        .byte $0D
        .byte "APPLE-1 CHESS  V0.7  (HGR)", $0D
        .byte "BY VERHILLE ARNAUD, 2026", $0D
        .byte "PIECES: CC65-CHESS (WESSELS/", $0D
        .byte " SCHMIDT/GEBHART)", $0D
        .byte $0D
        .byte "BOARD + PLAY ON THE HGR SCREEN", $0D
        .byte " IJKL = MOVE THE CURSOR", $0D
        .byte " SPACE = SELECT / CONFIRM", $0D
        .byte " M=MODE N=NEW U=UNDO P=AI LVL", $0D
        .byte $0D
        .byte "CHOOSE A GAME MODE:", $0D
        .byte " 1 = TWO HUMAN PLAYERS", $0D
        .byte " 2 = YOU (WHITE) VS COMPUTER", $0D
        .byte " 3 = YOU (BLACK) VS COMPUTER", $0D
        .byte " 4 = COMPUTER VS COMPUTER", $0D
        .byte $0D, 0
a1_modeprompt:
        .byte "YOUR CHOICE (1-4)? ", 0
a1_pressn:
        .byte $0D, "PRESS N FOR A NEW GAME", $0D, 0

a1_wt_play:  .byte "WHITE TO PLAY - YOUR MOVE", $0D, 0
a1_bl_play:  .byte "BLACK TO PLAY - YOUR MOVE", $0D, 0
a1_wt_think: .byte "WHITE IS THINKING...", $0D, 0
a1_bl_think: .byte "BLACK IS THINKING...", $0D, 0
a1_check:    .byte " *** CHECK! ***", $0D, 0

a1_mode_hvh: .byte $0D, "MODE: TWO HUMAN PLAYERS", $0D, 0
a1_mode_wai: .byte $0D, "MODE: YOU (WHITE) VS COMPUTER", $0D, 0
a1_mode_bai: .byte $0D, "MODE: YOU (BLACK) VS COMPUTER", $0D, 0
a1_mode_ava: .byte $0D, "MODE: COMPUTER VS COMPUTER", $0D, 0
a1_modelbl_lo: .byte <a1_mode_hvh, <a1_mode_wai, <a1_mode_bai, <a1_mode_ava
a1_modelbl_hi: .byte >a1_mode_hvh, >a1_mode_wai, >a1_mode_bai, >a1_mode_ava

o_bwin: .byte $0D, " CHECKMATE - BLACK WINS", $0D, 0
o_wwin: .byte $0D, " CHECKMATE - WHITE WINS", $0D, 0
o_stale:.byte $0D, " STALEMATE - DRAW", $0D, 0
o_draw: .byte $0D, " DRAW", $0D, 0
overstr_lo: .byte 0, <o_bwin, <o_wwin, <o_stale, <o_draw
overstr_hi: .byte 0, >o_bwin, >o_wwin, >o_stale, >o_draw

; --- panel mode labels (bbfont) ---
m_hvh:  .byte "HVH", 0
m_wai:  .byte "WAI", 0
m_bai:  .byte "BAI", 0
m_ava:  .byte "AVA", 0
modestr_lo: .byte <m_hvh, <m_wai, <m_bai, <m_ava
modestr_hi: .byte >m_hvh, >m_wai, >m_bai, >m_ava

; ---------------------------------------------------------------------------
; Self-contained HGR library includes (data + init; the engine is a separate
; .o linked in).
; ---------------------------------------------------------------------------
.include "sprites/chess_cc65_pieces.asm"  ; cc65-Chess piece bitmaps
.include "bbfont_ascii5f.inc"             ; HGR_Font5F text glyphs ($20-$5F)
.include "hgr_clear.asm"                   ; clear_hgr (uses ptr_lo/ptr_hi)
.include "hgr_scanline.inc"                ; hgr_lo / hgr_hi base tables
.include "gen2_init.asm"                   ; gen2_hgr_init_clear
