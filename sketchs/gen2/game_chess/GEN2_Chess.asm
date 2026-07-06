; =============================================================================
; GEN2_Chess.asm -- graphical Chess for the Apple-1 + Uncle Bernie GEN2 HGR card
; VERHILLE Arnaud - 2026
; =============================================================================
; HGR (280x192 artifact-colour) front-end for the shared chess engine
; (dev/lib/games/chess/chess_engine.asm), the sibling of TMS_Chess.asm. The
; engine is platform-agnostic; this file is the HGR renderer + keyboard-cursor
; game loop + Apple-1 text UI. Same game, same controls -- only the board
; drawing changes (HGR bytes instead of TMS Mode-2 cells).
;
; Board look (per the reference image): blue / white checkerboard, one side
; ORANGE, the other BLACK. The whole board is emitted in the bit-7=1 NTSC group
; (blue/orange family); white squares use $FF (white in EITHER group) so orange
; piece pixels coexist with a white background in the same byte. Per piece:
;   WHITE side -> ORANGE (clear the piece pixels, then stamp the orange pattern)
;   BLACK side -> BLACK  (just clear the piece pixels)
; The cursor / selection are BITMAP overlays (no hardware sprites -- the Apple 1
; has none): a persistent black frame marks the selected square, blinking black
; corner ticks mark the cursor (human turn only).
;
; Geometry: 8x8 board, square = 3 bytes (21 px) x 21 scanlines, top-left at
; byte col 0 / scanline 8. Board = byte cols 0..23 (168 px); cols 24..39
; (112 px) are reserved for the future coord/move panel (milestone 2b).
;
; Controls (Apple-1 keyboard, forced uppercase):
;   I/J/K/L  move cursor        SPACE or RETURN  pick source / target
;   ESC      cancel selection   M  cycle mode     N  new game
;   U        undo last move     P  toggle AI strategy (FAST 1-ply / STRONG 2-ply)
; Modes (M cycles): HVH, WAI (white=you/black=AI), BAI, AVA (auto self-play).
;
; Build: dual-bank GEN2 (apple1_gen2_chess.cfg) -- code in the $E000 high bank
; + data in the $0280 low bank, the $2000-$3FFF framebuffer untouched. Links
; chess_engine.o. Entry: E000R.
; =============================================================================

.include "apple1.inc"
.include "chess_common.inc"

; --- geometry -------------------------------------------------------------
BOARD_Y0   = 8
SQH        = 21         ; square height in scanlines
PIECE_YOFF = 3          ; centre the 16-row piece in the 21-row square
BLINK_HI   = $40        ; cursor blink half-period (poll-loop iters, hi byte)
; Board shifted 1 byte right (byte cols 1..24) so col 0 holds the rank digit.
; --- right text panel (byte cols 25..39 = 15 chars) ------------------------
RCOL0      = 25         ; panel left byte column (x = 175)
PANEL_W    = 15         ; panel width in 7-px chars
MLIST_TOP  = 2          ; first panel text row used by the move list
MLIST_BOT  = 24         ; one past the last usable row (row*8 = scanline)
FILEY      = 179        ; scanline of the file-letter row (below the board)

; --- HGR artifact bytes (bit 7 = 1 group throughout the board) ------------
WHITE_BYTE = $FF        ; all pixels on -> white regardless of group
BLUE_EV    = $D5        ; blue, even byte column ($55 | $80)
BLUE_OD    = $AA        ; blue, odd  byte column ($2A | $80)
ORNG_EV    = $AA        ; orange, even byte column ($2A | $80)
ORNG_OD    = $D5        ; orange, odd  byte column ($55 | $80)

; ---------------------------------------------------------------------------
; Zero page. tmp/tmp2 MUST be $00/$01 (the engine .importzp them). Declared
; first so the layout starts at $00; the engine's own ZEROPAGE .res block
; concatenates after these.
; ---------------------------------------------------------------------------
.segment "ZEROPAGE"
tmp:       .res 1       ; $00
tmp2:      .res 1       ; $01
sptr_lo:   .res 1       ; string ptr for puts_a1
sptr_hi:   .res 1
ptr_lo:    .res 1       ; scanline base pointer (hgr_clear + blit)
ptr_hi:    .res 1
mptr_lo:   .res 1       ; current mask row pointer
mptr_hi:   .res 1
.exportzp tmp, tmp2

; ---------------------------------------------------------------------------
; Engine imports. Switch out of ZEROPAGE first so these default to absolute.
; ---------------------------------------------------------------------------
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
old_cur:    .res 1      ; previous cursor square (redraw on move)
sel_sq:     .res 1      ; selected source ($FF = none)
sel_active: .res 1      ; 0/1
piece_code: .res 1      ; raw board byte for dsq
play_mode:  .res 1      ; 0 HvH / 1 WAI / 2 BAI / 3 AvA
movecount:  .res 1      ; AI half-move counter (AvA cap)
game_result:.res 1      ; 1 wmate / 2 bmate / 3 stale / 4 draw
cursor_on:  .res 1      ; 1 = show the selection cursor (human turn only)
blink_vis:  .res 1      ; cursor blink phase: 1 = ticks shown, 0 = hidden
blink_lo:   .res 1      ; blink timer (poll-loop iterations)
blink_hi:   .res 1
turn_ai:    .res 1      ; 1 = side to move is the AI (this loop iteration)
printed_side: .res 1    ; last side we printed an Apple-1 turn line for ($FF=none)
seed_acc:   .res 1      ; free-running counter -> AI RNG entropy (key timing)
move_row:   .res 1      ; next right-panel row for the move list (2b)
hist_n:     .res 1      ; move-list history depth (2b)
; --- HGR draw_square scratch ---
frank:      .res 1      ; rank 0..7 (0 = white home)
ffile:      .res 1      ; file 0..7
topsl:      .res 1      ; top scanline of the square
bcol:       .res 1      ; left byte column of the square
c0:         .res 1      ; square fill bytes (3 columns)
c1:         .res 1
c2:         .res 1
o0:         .res 1      ; orange piece bytes (3 columns)
o1:         .res 1
o2:         .res 1
is_org:     .res 1      ; 1 = orange piece, 0 = black piece
prow:       .res 1      ; piece blit row 0..15
yy:         .res 1      ; fill row 0..20
maskbits:   .res 1
scratch:    .res 1
save_x:     .res 1      ; X saved across putc_hgr (clobbers X)
; --- HGR text panel ---
tx_col:     .res 1      ; glyph blit byte column
tx_sl:      .res 1      ; glyph blit top scanline
; move-list entry being printed + a 2-deep history (wrapped-column continuity)
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
        LDA #2
        STA move_row
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
        LDA #1
        STA blink_vis
        JSR gen2_hgr_init_clear  ; HGR + PAGE1 + blank the framebuffer
        JSR draw_board
        JSR draw_coords          ; rank digits (left) + file letters (bottom)
        JSR a1_choose_mode       ; Apple-1 presentation + keyboard mode select

game_loop:
        JSR check_terminal      ; shows result + waits N (-> new_game) if over
        JSR update_status       ; side + mode at the top of the right panel
        JSR side_is_ai          ; A = 1 (AI) / 0 (human)
        STA turn_ai
        JSR a1_turn_status      ; "X IS THINKING" / "X TO PLAY" on the Apple-1
        LDA turn_ai             ; cursor visible only on a human turn
        EOR #1
        STA cursor_on
        LDA turn_ai
        BEQ human_turn
        ; AI's turn: erase the cursor ticks so nothing blinks while it thinks
        LDA #0
        STA blink_vis
        LDA cur_sq
        STA dsq
        JSR draw_square
        JMP ai_turn

; ---------------------------------------------------------------------------
; Human turn: show the blinking cursor, poll the keyboard (non-blocking so the
;   cursor keeps blinking), then dispatch the key.
; ---------------------------------------------------------------------------
human_turn:
        LDA #1
        STA blink_vis           ; cursor visible on entry
        LDA cur_sq
        STA dsq
        JSR draw_square
        LDA #0
        STA blink_lo
        STA blink_hi
@wait:  JSR poll_key
        BNE @got                ; a key is ready
        INC blink_lo            ; else tick the blink timer
        BNE @wait
        INC blink_hi
        LDA blink_hi
        CMP #BLINK_HI
        BCC @wait
        LDA #0                  ; half-period elapsed -> toggle cursor
        STA blink_hi
        LDA blink_vis
        EOR #1
        STA blink_vis
        LDA cur_sq
        STA dsq
        JSR draw_square
        JMP @wait
@got:   CMP #'I'                ; I = up
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
        JSR draw_square         ; show selection frame
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
        ; legal: clear selection + cursor (the human's turn is over), full
        ; board redraw (capture/castle/e.p.)
        LDA #0
        STA sel_active
        STA cursor_on           ; hide the cursor immediately after the move
        LDA #$FF
        STA sel_sq
        JSR draw_board
        JSR print_move_hgr      ; log the move (2b)
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
        JSR clear_movelist      ; 2b
        JSR a1_choose_mode      ; re-present + re-select mode for the new game
        JMP game_loop

; ---------------------------------------------------------------------------
; AI turn. AvA auto-plays (capped); single-AI modes move once then return to
; the loop. Non-blocking poll lets N/M interrupt an AvA game.
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
        JSR print_move_hgr      ; 2b
@loop:  JMP game_loop

; ---------------------------------------------------------------------------
; side_is_ai: A = 1 if the side to move is the AI, else 0.
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
        LDA #1
        STA blink_vis           ; keep the cursor visible right after a move
        LDA old_cur
        STA dsq
        JSR draw_square         ; erase ticks on the vacated square
        LDA cur_sq
        STA dsq
        JSR draw_square         ; draw ticks on the new square
cur_ret:
        RTS

; ===========================================================================
; HGR renderer
; ===========================================================================
; draw_board: paint all 64 squares.
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
; draw_square: fill square `dsq` (blue/white) + piece (orange/black) + the
;   selection frame / blinking cursor ticks. Clobbers A,X,Y.
; ---------------------------------------------------------------------------
draw_square:
        ; frank = sq/8-ish from 0x88: file = dsq & 7, rank = dsq >> 4
        LDA dsq
        AND #$07
        STA ffile
        LDA dsq
        LSR
        LSR
        LSR
        LSR
        STA frank
        LDX frank
        LDA topsl_tab,X
        STA topsl
        LDX ffile
        LDA bcol_tab,X
        STA bcol

        ; square colour: (frank + ffile) & 1 -> 0 = dark(blue), 1 = light(white)
        LDA frank
        CLC
        ADC ffile
        AND #1
        BEQ @dark
        LDA #WHITE_BYTE
        STA c0
        STA c1
        STA c2
        JMP @orange_bytes
@dark:
        LDA ffile
        AND #1
        BEQ @dfe
        LDA #BLUE_OD
        STA c0
        LDA #BLUE_EV
        STA c1
        LDA #BLUE_OD
        STA c2
        JMP @orange_bytes
@dfe:
        LDA #BLUE_EV
        STA c0
        LDA #BLUE_OD
        STA c1
        LDA #BLUE_EV
        STA c2

@orange_bytes:
        LDA ffile
        AND #1
        BEQ @ofe
        LDA #ORNG_OD
        STA o0
        LDA #ORNG_EV
        STA o1
        LDA #ORNG_OD
        STA o2
        JMP @fill
@ofe:
        LDA #ORNG_EV
        STA o0
        LDA #ORNG_OD
        STA o1
        LDA #ORNG_EV
        STA o2

@fill:
        LDA #0
        STA yy
@frow:
        LDA topsl
        CLC
        ADC yy
        TAX
        LDA hgr_lo,X
        STA ptr_lo
        LDA hgr_hi,X
        STA ptr_hi
        LDY bcol
        LDA c0
        STA (ptr_lo),Y
        INY
        LDA c1
        STA (ptr_lo),Y
        INY
        LDA c2
        STA (ptr_lo),Y
        INC yy
        LDA yy
        CMP #SQH
        BNE @frow

        ; piece?
        LDX dsq
        JSR piece_at
        BNE @haspiece           ; occupied
        JMP @overlays           ; empty (out of branch range)
@haspiece:
        STA piece_code
        AND #COLOR_MASK
        BNE @black
        LDA #1                  ; white -> orange
        STA is_org
        JMP @sel
@black:
        LDA #0
        STA is_org
@sel:
        LDA piece_code
        AND #PIECE_MASK
        SEC
        SBC #1
        TAX
        LDA mask_lo,X
        STA mptr_lo
        LDA mask_hi,X
        STA mptr_hi

        LDA #0
        STA prow
@prow:
        LDA topsl
        CLC
        ADC #PIECE_YOFF
        ADC prow
        TAX
        LDA hgr_lo,X
        STA ptr_lo
        LDA hgr_hi,X
        STA ptr_hi
        LDX #0                  ; column 0..2
@pcol:
        TXA
        TAY
        LDA (mptr_lo),Y
        STA maskbits
        TXA
        CLC
        ADC bcol
        TAY
        LDA maskbits
        EOR #$FF
        AND (ptr_lo),Y
        STA scratch
        LDA is_org
        BEQ @pst
        LDA o0,X
        AND maskbits
        ORA scratch
        STA scratch
@pst:
        LDA scratch
        STA (ptr_lo),Y
        INX
        CPX #3
        BNE @pcol
        LDA mptr_lo
        CLC
        ADC #3
        STA mptr_lo
        BCC @nomc
        INC mptr_hi
@nomc:
        INC prow
        LDA prow
        CMP #16
        BNE @prow

@overlays:
        ; selection frame (persistent, on the selected source square)
        LDA sel_active
        BEQ @nosel
        LDA dsq
        CMP sel_sq
        BNE @nosel
        JSR draw_sel_frame
@nosel:
        ; blinking cursor ticks (human turn only)
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
; draw_cursor_ticks: black 3x3 corner ticks (clear pixels; bit 7 preserved so
;   the surrounding square keeps its NTSC group). Rows 0..2 and 18..20.
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
        AND #$F8                ; clear px 0..2 (left tick)
        STA (ptr_lo),Y
        LDY bcol
        INY
        INY
        LDA (ptr_lo),Y
        AND #$8F                ; clear px 18..20 (right tick, byte2 bits 4..6)
        STA (ptr_lo),Y
        INX
        CPX #6
        BNE @t
        RTS

; ---------------------------------------------------------------------------
; draw_sel_frame: black 1px rectangle border around the square.
; ---------------------------------------------------------------------------
draw_sel_frame:
        ; top (row 0) + bottom (row 20): full black line across the 3 bytes
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
        LDA #0
        STA (ptr_lo),Y
        INY
        STA (ptr_lo),Y
        INY
        STA (ptr_lo),Y
        INX
        CPX #2
        BNE @tb
        ; left edge (px0) + right edge (px20) rows 1..19
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
        AND #$FE                ; clear px 0 (left edge, keep bit 7)
        STA (ptr_lo),Y
        LDY bcol
        INY
        INY
        LDA (ptr_lo),Y
        AND #$BF                ; clear px 20 (byte2 bit 6, keep bit 7)
        STA (ptr_lo),Y
        INX
        CPX #20
        BNE @sd
        RTS

; ---------------------------------------------------------------------------
; Text panel stubs (implemented in milestone 2b -- HGR move list + coords).
; ---------------------------------------------------------------------------
print_move_hgr:
clear_movelist:
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
; game-over announcement (the board lives on the HGR screen).
; ---------------------------------------------------------------------------
; cout: print A (7-bit) to the Apple-1 display via the Woz DSP handshake.
cout:
        ORA #$80
@w:     BIT DSP
        BMI @w
        STA DSP
        RTS

; puts_a1: print the NUL-terminated string at (sptr) (16-bit ptr increment).
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

; a1_choose_mode: present the game + read the play mode (1..4) from the keyboard.
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
@spin:  INC seed_acc            ; free-running while we wait for a key
        LDA KBDCR
        BPL @spin
        LDA KBD
        AND #$7F
        CMP #'1'
        BCC @ask
        CMP #'5'
        BCS @ask
        SEC
        SBC #'1'                ; '1'..'4' -> 0..3
        STA play_mode
        LDA ai_rng              ; fold the timing entropy into the AI RNG
        EOR seed_acc
        BNE @seeded
        LDA #$AC                ; never leave it 0 (the LFSR would jam)
@seeded:
        STA ai_rng
        LDX play_mode           ; echo the chosen mode label
        LDA a1_modelbl_lo,X
        STA sptr_lo
        LDA a1_modelbl_hi,X
        STA sptr_hi
        JMP puts_a1             ; tail

; a1_result: announce the game result + "PRESS N" on the Apple-1.
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

; a1_turn_status: announce whose turn it is + a CHECK flag. Prints only when
;   the side to move changed.
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
        JSR puts_a1
        JSR in_check            ; A nonzero if side_to_move's king is attacked
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
;   topsl = BOARD_Y0 + (7 - rank) * 21
topsl_tab:
        .byte 155, 134, 113, 92, 71, 50, 29, 8
; left byte column per file = 1 + file*3 (board shifted 1 byte right so col 0
; holds the rank digit)
bcol_tab:
        .byte 1, 4, 7, 10, 13, 16, 19, 22
; corner-tick rows / frame edge rows (offsets from topsl)
tickrows:  .byte 0, 1, 2, 18, 19, 20
framerows: .byte 0, 20

; mask pointers indexed by (type-1): pawn,knight,bishop,rook,queen,king
mask_lo:
        .byte <chess_pawn_mask, <chess_knight_mask, <chess_bishop_mask
        .byte <chess_rook_mask, <chess_queen_mask, <chess_king_mask
mask_hi:
        .byte >chess_pawn_mask, >chess_knight_mask, >chess_bishop_mask
        .byte >chess_rook_mask, >chess_queen_mask, >chess_king_mask

; --- Apple-1 screen strings (DSP output; no font needed) --------------------
a1_splash:
        .byte $0D
        .byte "APPLE-1 CHESS  V0.7  (HGR)", $0D
        .byte "BY VERHILLE ARNAUD, 2026", $0D
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

; ---------------------------------------------------------------------------
; Self-contained HGR library includes (data + init; the engine is a separate
; .o linked in, but these have no external symbols we define).
; ---------------------------------------------------------------------------
.include "sprites/chess_hgr_masks.asm"   ; chess_<piece>_mask silhouettes
.include "hgr_clear.asm"                  ; clear_hgr (uses ptr_lo/ptr_hi)
.include "hgr_scanline.inc"               ; hgr_lo / hgr_hi base tables
.include "gen2_init.asm"                  ; gen2_hgr_init_clear / gen2_text_restore
