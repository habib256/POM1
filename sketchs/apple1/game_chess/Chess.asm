; =============================================
; CHESS (text mode) for Apple 1
; VERHILLE Arnaud - 2026
; Two-player human-vs-human, 8x8 board, ASCII rendering
; =============================================
; Inspired by StewBC/cc65-Chess (algorithm reference).
; Apple-1 implementation: pure 6502 asm, board in 0x88 mailbox.
;
; Engine lives in dev/lib/games/chess/chess_engine.asm (linked separately).
; This file is the text-mode renderer + game loop.
;
; Build:
;   cd dev/projects/apple1/game_chess && make
;   -> software/Apple-1 games/Chess.{bin,txt}
;
; Run in POM1: File > Load Memory > Chess.txt, then 280R.
;
; Move format: type "E2E4" + RETURN.  Promotion: "E7E8Q" (Q/R/B/N).
; Special at MOVE? prompt: Q quit, U undo (v0.2+), D depth toggle (v0.3+),
; T self-test (v0.2+), P perft (v0.2+).
; =============================================

.include "apple1.inc"
.include "zp.inc"
.include "chess_common.inc"

; --- Local ZP scratch (renderer-private). Declared early so they land
; in the standard zero-page segment, before we switch to CODE. ---
.segment "ZEROPAGE"
tmp3:      .res 1
tmp4:      .res 1
play_mode: .res 1   ; 0 = HvH, 1 = W=Hum/B=AI, 2 = W=AI/B=Hum, 3 = AvA
last_from: .res 1   ; previous move's from-square ($88 = no highlight yet)
last_to:   .res 1   ; previous move's to-square
list_col:  .res 1   ; cell counter for do_list_moves wrapping (0..7)

; Switch back to CODE segment so .import directives below pick up the
; default absolute address size.
.segment "CODE"

; --- Engine imports ---
.import init_board, apply_user_move, in_check, game_status, toggle_side
.import board, side_to_move, fullmove_number
.import mv_from, mv_to, mv_promo, mv_flags
.import piece_letters
.import read_player_move, error_message_ax, print_square_ax
.import ai_play_move, perft1, undo_last_move
.import perft_count_lo, perft_count_hi
.import undo_avail
.import ai_strategy
.import is_pseudo_legal, make_move, unmake_move
.importzp ce_piece

; --- wait_key and print_str_ax are textually included below (kbd.asm +
;     print.asm). We .export them so the separately-assembled
;     chess_text_io.o can link against the local copies.
.export wait_key, print_str_ax

.code

main:
        ; Splash screen
        LDA #<str_title
        LDX #>str_title
        JSR print_str_ax
        JSR wait_key
        ; Sentinel "no move yet" so render_board doesn't paint a stray '*'.
        LDA #$88
        STA last_from
        STA last_to
        ; Fall through to restart_game (mode prompt + init).

; restart_game -- prompt user for mode, then start a fresh game.
; Entry point for "new game after game-over" so the player can switch
; mode between games. cycle_mode (M command at MOVE? prompt) is the
; mid-game shortcut and stays unchanged.
restart_game:
        JSR choose_mode
        ; fall through

new_game:
        JSR init_board
        ; Reset the last-move highlight every time we start (or restart) a
        ; game. Without this the first render of the new game would paint
        ; '*' on the squares of the previous game's final move, since
        ; last_from/last_to live in this variant's ZP and init_board only
        ; touches the engine's board[]/state slots.
        LDA #$88
        STA last_from
        STA last_to

game_loop:
        JSR render_board
        JSR print_status

        ; --- Auto-AI for designated side based on play_mode ---
        ; play_mode: 0=HvH 1=W-hum/B-AI 2=W-AI/B-hum 3=A-vs-A
        LDA play_mode
        BEQ @human_input
        CMP #$03
        BEQ @auto_ai_play       ; A-vs-A always AI
        ; play_mode = 1 or 2: AI plays one specific side
        ; mode 1 → AI plays BLACK (when side_to_move != 0)
        ; mode 2 → AI plays WHITE (when side_to_move == 0)
        LDA side_to_move
        BNE @side_black
        ; side is white. AI plays only if mode == 2
        LDA play_mode
        CMP #$02
        BEQ @auto_ai_play
        JMP @human_input
@side_black:
        ; side is black. AI plays only if mode == 1
        LDA play_mode
        CMP #$01
        BEQ @auto_ai_play
        JMP @human_input
@auto_ai_play:
        JMP do_ai

@human_input:
        ; Read user move
        JSR prompt_move
        JSR read_player_move
        BCC @do_move
        ; Carry set: special command or parse error.
        ; A = $01 Q quit, $02 U undo, $03 malformed, $04 D AI strategy toggle,
        ;     $05 C cursor (TMS/HGR only), $06 P perft, $07 A AI plays,
        ;     $08 M cycle mode, $09 H hint, $0A L list legal moves
        CMP #$01
        BNE @nq
        JMP show_quit
@nq:    CMP #$02
        BNE @nu
        JMP do_undo
@nu:    CMP #$06
        BNE @np
        JMP do_perft
@np:    CMP #$07
        BNE @nai
        JMP do_ai
@nai:   CMP #$08
        BNE @nm
        JMP cycle_mode
@nm:    CMP #$04
        BNE @nd
        JMP do_strategy
@nd:    CMP #$09
        BNE @nh
        JMP do_hint
@nh:    CMP #$0A
        BNE @nl
        JMP do_list_moves
@nl:    CMP #$03
        BNE @other_special
        ; Malformed input — re-prompt
        LDA #<msg_bad_input
        LDX #>msg_bad_input
        JSR print_str_ax
        JMP game_loop
@other_special:
        ; Cursor mode: TMS9918/HGR variants only.
        LDA #<msg_nyi_cmd
        LDX #>msg_nyi_cmd
        JSR print_str_ax
        JMP game_loop

@do_move:
        JSR apply_user_move
        BCC @move_ok
        ; Illegal move: print friendly error.
        JSR error_message_ax
        JSR print_str_ax
        JMP game_loop
@move_ok:
        ; Record for last-move highlight on next render.
        LDA mv_from
        STA last_from
        LDA mv_to
        STA last_to
        ; Check terminal state.
        JSR game_status
        BNE @gameover_user
        JMP game_loop
@gameover_user:

        ; Game over — render final board, announce reason, prompt N/H.
        JSR render_board
        TAY                     ; Y = status code
        JMP game_over_announce

show_quit:
        LDA #<msg_quit
        LDX #>msg_quit
        JSR print_str_ax
        JMP game_over_prompt    ; offer N=new game / H=help

; -- Cycle play mode: 0 → 1 → 2 → 3 → 0 → ... --
cycle_mode:
        INC play_mode
        LDA play_mode
        CMP #$04
        BCC @ok
        LDA #$00
        STA play_mode
@ok:
        ; Print friendly mode label
        LDA play_mode
        BNE @nm0
        LDA #<msg_mode_hvh
        LDX #>msg_mode_hvh
        JMP @say
@nm0:   CMP #$01
        BNE @nm1
        LDA #<msg_mode_wai
        LDX #>msg_mode_wai
        JMP @say
@nm1:   CMP #$02
        BNE @nm2
        LDA #<msg_mode_bai
        LDX #>msg_mode_bai
        JMP @say
@nm2:   LDA #<msg_mode_ava
        LDX #>msg_mode_ava
@say:   JSR print_str_ax
        JMP game_loop

; =============================================
; do_undo -- restore the previous position (single level)
; =============================================
do_undo:
        JSR undo_last_move
        BCC @ok
        LDA #<msg_no_undo
        LDX #>msg_no_undo
        JSR print_str_ax
        JMP game_loop
@ok:    ; Last-move highlight no longer reflects current state.
        LDA #$88
        STA last_from
        STA last_to
        LDA #<msg_undone
        LDX #>msg_undone
        JSR print_str_ax
        JMP game_loop

; =============================================
; do_perft -- count legal moves at current position (depth 1)
; =============================================
do_perft:
        LDA #<msg_perft_run
        LDX #>msg_perft_run
        JSR print_str_ax
        JSR perft1
        ; Print 16-bit count as decimal-ish (just hex for simplicity)
        LDA #'='
        ORA #$80
        JSR ECHO
        LDA perft_count_hi
        JSR PRBYTE
        LDA perft_count_lo
        JSR PRBYTE
        LDA #' '
        ORA #$80
        JSR ECHO
        LDA #'('
        ORA #$80
        JSR ECHO
        LDA #'H'
        ORA #$80
        JSR ECHO
        LDA #'E'
        ORA #$80
        JSR ECHO
        LDA #'X'
        ORA #$80
        JSR ECHO
        LDA #')'
        ORA #$80
        JSR ECHO
        LDA #$8D
        JSR ECHO
        ; Hint expected value for the initial position
        LDA #<msg_perft_hint
        LDX #>msg_perft_hint
        JSR print_str_ax
        JMP game_loop

; =============================================
; do_ai -- let the computer pick and play a move for the side to move
; =============================================
do_ai:
        LDA #<msg_ai_thinking
        LDX #>msg_ai_thinking
        JSR print_str_ax
        JSR ai_play_move
        BCC @ai_ok
        ; No move (mate or stalemate): game_status will detect on next loop
        LDA #<msg_ai_nomove
        LDX #>msg_ai_nomove
        JSR print_str_ax
        JMP game_loop
@ai_ok:
        ; Record for highlight, then announce the move.
        LDA mv_from
        STA last_from
        LDA mv_to
        STA last_to
        LDA #<msg_ai_played
        LDX #>msg_ai_played
        JSR print_str_ax
        LDA mv_from
        JSR print_square_ax
        LDA mv_to
        JSR print_square_ax
        LDA #$8D
        JSR ECHO
        JSR game_status
        BNE @gameover
        JMP game_loop
@gameover:
        ; Game over after AI move
        TAY
        JSR render_board
        JMP game_over_announce

; =============================================
; do_strategy -- toggle ai_strategy between NAIVE and SMART, announce.
; =============================================
do_strategy:
        LDA ai_strategy
        EOR #$01
        STA ai_strategy
        BNE @smart
        LDA #<msg_strat_naive
        LDX #>msg_strat_naive
        JMP @say
@smart: LDA #<msg_strat_smart
        LDX #>msg_strat_smart
@say:   JSR print_str_ax
        JMP game_loop

; =============================================
; do_hint -- have the AI propose a move without keeping it.
;
; Strategy: invoke ai_play_move (which applies + toggles side), capture the
; chosen mv_from/mv_to, then undo. Side effect: this overwrites whatever the
; player's previous undo state was — flagged in the README under "known
; limitations" because preserving it would require duplicating ai_play_move's
; ~80 lines.
; =============================================
do_hint:
        JSR ai_play_move
        BCC @ok
        LDA #<msg_no_hint
        LDX #>msg_no_hint
        JSR print_str_ax
        JMP game_loop
@ok:
        ; ai_play_move applied the move. Capture mv_from/mv_to BEFORE undo.
        LDA mv_from
        STA tmp3
        LDA mv_to
        STA tmp4
        JSR undo_last_move
        ; Reset highlight (we don't want to cue the player on a move we
        ; haven't actually played).
        LDA #$88
        STA last_from
        STA last_to
        LDA #<msg_hint
        LDX #>msg_hint
        JSR print_str_ax
        LDA tmp3
        JSR print_square_ax
        LDA tmp4
        JSR print_square_ax
        LDA #$8D
        JSR ECHO
        JMP game_loop

; =============================================
; do_list_moves -- print every legal move for the side to move.
;
; Reuses the engine's is_pseudo_legal / make_move / in_check / unmake_move
; primitives (now exported via the lib/games/chess public API). Prints up to 32
; moves in a 5-char-wide grid (8 per line).
; =============================================
do_list_moves:
        LDA #<msg_list_header
        LDX #>msg_list_header
        JSR print_str_ax
        LDA #$00
        STA list_col            ; cells emitted on current line
        LDX #$00
@floop:
        TXA
        AND #OFFBOARD_MASK
        BEQ @on_board_f
        JMP @nextf
@on_board_f:
        LDA board,X
        BNE @has_p_f
        JMP @nextf
@has_p_f:
        AND #COLOR_MASK
        CMP side_to_move
        BEQ @same_c_f
        JMP @nextf
@same_c_f:
        STX mv_from
        LDA board,X
        STA ce_piece
        LDY #$00
@tloop:
        TYA
        AND #OFFBOARD_MASK
        BNE @nextt
        STY mv_to
        CPY mv_from
        BEQ @nextt
        LDA board,Y
        BEQ @ptest
        AND #COLOR_MASK
        CMP side_to_move
        BEQ @nextt
@ptest:
        LDA #$00
        STA mv_promo
        STA mv_flags
        TXA
        PHA                     ; preserve outer X across is_pseudo_legal/make/unmake
        TYA
        PHA
        JSR is_pseudo_legal
        BCS @undo_y
        JSR make_move
        JSR in_check
        BNE @bad
        ; Move is legal — print "FF-FF " using print_square_ax.
        LDA mv_from
        JSR print_square_ax
        LDA mv_to
        JSR print_square_ax
        LDA #' '
        ORA #$80
        JSR ECHO
        ; Wrap every 8 entries.
        INC list_col
        LDA list_col
        CMP #$08
        BCC @no_wrap
        LDA #$00
        STA list_col
        LDA #$8D
        JSR ECHO
@no_wrap:
        JSR unmake_move
        PLA
        TAY
        PLA
        TAX
        JMP @nextt
@bad:
        JSR unmake_move
@undo_y:
        PLA
        TAY
        PLA
        TAX
@nextt:
        INY
        BEQ @tloop_end
        JMP @tloop
@tloop_end:
@nextf:
        INX
        BEQ @floop_end
        JMP @floop
@floop_end:
        ; Trailing CR if the last line wasn't already terminated by the wrap.
        LDA list_col
        BEQ @done
        LDA #$8D
        JSR ECHO
@done:
        JMP game_loop

; =============================================
; choose_mode -- prompt the user to pick play_mode (1..4) before a
; new game starts. Loops until a key in '1'..'4' arrives. Echoes the
; chosen mode label so the player has visual confirmation.
; =============================================
choose_mode:
@ask:   LDA #<msg_mode_prompt
        LDX #>msg_mode_prompt
        JSR print_str_ax
        JSR wait_key            ; A = key, bit 7 already stripped
        CMP #'1'
        BCC @ask                ; below '1' → re-prompt
        CMP #'5'
        BCS @ask                ; '5' or above → re-prompt
        SEC
        SBC #'1'                ; map '1'..'4' → 0..3
        STA play_mode
        ; Echo selection (same labels as cycle_mode).
        LDA play_mode
        BNE @nm0
        LDA #<msg_mode_hvh
        LDX #>msg_mode_hvh
        JMP @say
@nm0:   CMP #$01
        BNE @nm1
        LDA #<msg_mode_wai
        LDX #>msg_mode_wai
        JMP @say
@nm1:   CMP #$02
        BNE @nm2
        LDA #<msg_mode_bai
        LDX #>msg_mode_bai
        JMP @say
@nm2:   LDA #<msg_mode_ava
        LDX #>msg_mode_ava
@say:   JMP print_str_ax        ; tail call — RTS in print_str_ax returns to caller

; =============================================
; game_over_announce -- Y = status code (1=white mated, 2=black mated,
; 3=stalemate). Prints the reason then falls through to game_over_prompt.
; Never returns to caller (tail-jumps into restart_game).
; =============================================
game_over_announce:
        CPY #$01
        BNE @nwm
        LDA #<msg_white_mate
        LDX #>msg_white_mate
        JMP @say
@nwm:   CPY #$02
        BNE @nbm
        LDA #<msg_black_mate
        LDX #>msg_black_mate
        JMP @say
@nbm:   LDA #<msg_stalemate
        LDX #>msg_stalemate
@say:   JSR print_str_ax
        ; fall through to game_over_prompt

; =============================================
; game_over_prompt -- offer "N = new game" or "H = help". H re-shows the
; splash help and re-prompts; any other key triggers a fresh game (with
; mode prompt). Never returns.
; =============================================
game_over_prompt:
@ask:   LDA #<msg_gameover_prompt
        LDX #>msg_gameover_prompt
        JSR print_str_ax
        JSR wait_key            ; A = key (bit 7 stripped)
        CMP #'H'
        BNE @notH
        ; Help: re-show the splash, wait for a key, then re-prompt.
        LDA #<str_title
        LDX #>str_title
        JSR print_str_ax
        JSR wait_key
        JMP @ask
@notH:  ; Any other key starts a new game (mode prompt + init_board).
        JMP restart_game

; =============================================
; render_board -- print the 8x8 board with file/rank labels
;
; Layout (each cell is 2 chars wide → 3 chars per cell with separator).
; Files A..H labelled once at the top; ranks 1..8 labelled on the right
; only (left-side rank column and bottom file row are intentionally
; omitted for a tighter display on the 40-col Apple-1 screen).
;
; A  B  C  D  E  F  G  H
;+--+--+--+--+--+--+--+--+    (files line has 1 leading space so
;                              letters land over cell content)
;!BR!BN!BB!BQ!BK!BB!BN!BR! 8
;+--+--+--+--+--+--+--+--+
;!BP!BP!BP!BP!BP!BP!BP!BP! 7
;+--+--+--+--+--+--+--+--+
;... ranks 6..3 with `..` for dark empty cells, `  ` for light ...
;+--+--+--+--+--+--+--+--+
;!WP!WP!WP!WP!WP!WP!WP!WP! 2
;+--+--+--+--+--+--+--+--+
;!WR!WN!WB!WQ!WK!WB!WN!WR! 1
;+--+--+--+--+--+--+--+--+
;
; Pieces are 2 chars: colour + piece. W = white, B = black.
;   WP white pawn   BP black pawn
;   WN white knight BN black knight
;   WB white bishop BB black bishop
;   WR white rook   BR black rook
;   WQ white queen  BQ black queen
;   WK white king   BK black king
; Empty cells: "  " (light square) or ".." (dark square).
; =============================================
render_board:
        ; Anti-scroll: Apple-1 has no cursor addressing, but emitting a
        ; full screen-height of CRs pushes any prior board entirely off
        ; the 24-line display. The new board lands at the top, period-
        ; authentic and easier to read than scroll-pollution.
        LDX #24
@scroll_lp:
        LDA #$8D
        JSR ECHO
        DEX
        BNE @scroll_lp

        LDA #<str_files
        LDX #>str_files
        JSR print_str_ax

        ; 8 rank rows, top-down (rank 8 first → 0x88 rank index 7 down to 0)
        LDA #$07
        STA tmp
@row:
        LDA #<str_sep
        LDX #>str_sep
        JSR print_str_ax

        ; No left margin — board is flush against col 0 to save display
        ; time on the slow Apple-1 video (every char ≈ 17 ms).

        ; Print 8 cells across files 0..7
        LDA #$00
        STA tmp2
@cell:
        LDA #$A1                ; '!' separator
        JSR ECHO

        ; Compute square index = rank*16 + file
        LDA tmp
        ASL A
        ASL A
        ASL A
        ASL A
        CLC
        ADC tmp2
        TAX
        ; Last-move highlight: if X == last_from or X == last_to, replace
        ; the first glyph char with '*'. Visible on the slow Apple-1 video
        ; without needing a real inverse-video attribute.
        CPX last_from
        BEQ @hilite
        CPX last_to
        BNE @no_hilite
@hilite:
        LDA board,X
        STA tmp3
        LDA #'*'
        ORA #$80
        JSR ECHO
        ; Second char: piece-type letter, or '.' for empty
        LDA tmp3
        AND #PIECE_MASK
        BNE @hp
        LDA #'.'
        ORA #$80
        JSR ECHO
        JMP @cell_done
@hp:
        TAY
        LDA piece_letters,Y
        ORA #$80
        JSR ECHO
        JMP @cell_done
@no_hilite:
        LDA board,X
        JSR print_piece_glyph2  ; prints exactly 2 chars
@cell_done:
        INC tmp2
        LDA tmp2
        CMP #$08
        BCC @cell

        ; Closing '!' + space + rank label + CR
        LDA #$A1
        JSR ECHO
        LDA #' '
        ORA #$80
        JSR ECHO
        LDA tmp
        CLC
        ADC #'1'
        ORA #$80
        JSR ECHO
        LDA #$8D
        JSR ECHO

        DEC tmp
        BPL @row

        LDA #<str_sep
        LDX #>str_sep
        JSR print_str_ax
        ; Material balance line directly under the board.
        JSR print_material_balance
        RTS

; ============================================================================
; print_material_balance -- one line summarising W vs B pawn-equivalents.
;
; Iterates the 0x88 board, sums white piece values minus black piece values
; using the engine's mat_simple table semantics inline (P=1, N/B=3, R=5, Q=9).
; Prints "MAT W:+N", "MAT B:+N", or "MAT  =" so the trailing "=" lands at the
; same column as the digits would.
; ============================================================================
print_material_balance:
        ; Compute white_mat - black_mat into tmp4 FIRST, before any output.
        ; If the balance is zero (initial position + every move pre-capture),
        ; emit nothing — saves a whole line per redraw on the slow Apple-1
        ; display (the "MAT =" line was the dominant complaint after the
        ; per-candidate "thinking" dots got removed).
        LDA #$00
        STA tmp4
        LDX #$00
@mloop:
        TXA
        AND #OFFBOARD_MASK
        BNE @mskip
        LDA board,X
        BEQ @mskip
        STA tmp3
        AND #PIECE_MASK
        TAY
        LDA mat_table,Y
        STA tmp
        LDA tmp3
        AND #COLOR_MASK
        BNE @sub
        CLC
        LDA tmp4
        ADC tmp
        STA tmp4
        JMP @mskip
@sub:
        SEC
        LDA tmp4
        SBC tmp
        STA tmp4
@mskip:
        INX
        BNE @mloop

        LDA tmp4
        BEQ @done                ; balance == 0 → no MAT line at all
        LDA #<str_mat_prefix
        LDX #>str_mat_prefix
        JSR print_str_ax
        LDA tmp4
        BPL @white_lead
        ; Black leads — print "B:+|N|"
        LDA #<str_b_plus
        LDX #>str_b_plus
        JSR print_str_ax
        SEC
        LDA #$00
        SBC tmp4                ; A = |tmp4|
        JSR print_byte_dec
        LDA #$8D
        JMP ECHO
@white_lead:
        LDA #<str_w_plus
        LDX #>str_w_plus
        JSR print_str_ax
        LDA tmp4
        JSR print_byte_dec
        LDA #$8D
        JMP ECHO
@done:
        RTS

; ============================================================================
; print_byte_dec -- A = unsigned 8-bit value 0..99; prints 1-2 ASCII digits.
; Ranges relevant to chess material: 0..39 typical, never above 89 in any
; reachable position, so 2-digit handling is enough.
; ============================================================================
print_byte_dec:
        STA tmp
        LDA #$00
        STA tmp2
@tens:  LDA tmp
        CMP #10
        BCC @done_tens
        SEC
        SBC #10
        STA tmp
        INC tmp2
        JMP @tens
@done_tens:
        LDA tmp2
        BEQ @units              ; suppress leading zero
        ORA #'0'
        ORA #$80
        JSR ECHO
@units: LDA tmp
        ORA #'0'
        ORA #$80
        JSR ECHO
        RTS

; Material values, indexed by PIECE_MASK code (0=NONE, 1=P, 2=N, ...).
mat_table:
        .byte 0, 1, 3, 3, 5, 9, 0, 0

; print_piece_glyph2: A = piece byte (colour | type), prints exactly 2 chars:
;   "WP" / "BP" / "WN" / ... / "WK" / "BK"
;   "  " (light empty) or ".." (dark empty)
print_piece_glyph2:
        STA tmp3
        AND #PIECE_MASK
        BNE @piece
        ; Empty: chequered pattern. tmp = rank, tmp2 = file.
        LDA tmp
        EOR tmp2
        AND #$01
        BEQ @light
        ; Dark: ".."
        LDA #'.'
        ORA #$80
        JSR ECHO
        JSR ECHO
        RTS
@light:
        ; Light: "  "
        LDA #' '
        ORA #$80
        JSR ECHO
        JSR ECHO
        RTS
@piece:
        ; Print colour letter then piece letter.
        LDA tmp3
        AND #COLOR_MASK
        BEQ @white
        LDA #'B'
        JMP @ec
@white: LDA #'W'
@ec:    ORA #$80
        JSR ECHO
        ; Piece type letter from piece_letters[piece_type].
        LDA tmp3
        AND #PIECE_MASK
        TAY
        LDA piece_letters,Y
        ORA #$80
        JSR ECHO
        RTS

; print_status: single-line side-to-move announce.
print_status:
        LDA #$8D
        JSR ECHO
        LDA side_to_move
        BEQ @w
        LDA #<str_black_turn
        LDX #>str_black_turn
        JMP @say
@w:     LDA #<str_white_turn
        LDX #>str_white_turn
@say:   JSR print_str_ax

        ; Show "[CHECK]" if applicable.
        JSR in_check
        BEQ @no_chk
        LDA #<str_in_check
        LDX #>str_in_check
        JSR print_str_ax
@no_chk:
        RTS

prompt_move:
        LDA #<str_prompt
        LDX #>str_prompt
        JSR print_str_ax
        RTS

; (tmp3 and tmp4 are declared near the top of this file, in ZEROPAGE.)

; =============================================
; DATA — split between CODE (small, hot strings) and ENGINE bank (large
; one-shot text). print_str_ax loads via 16-bit absolute pointer so data
; can live in either bank. The verbose splash + game-over text moves to
; the upper bank to keep CODELOW (3456 B) breathable.
; =============================================
str_files:
        .byte " A  B  C  D  E  F  G  H", $0D, 0
str_sep:
        .byte "+--+--+--+--+--+--+--+--+", $0D, 0
str_white_turn:
        .byte " WHITE TO MOVE.", 0
str_black_turn:
        .byte " BLACK TO MOVE.", 0
str_in_check:
        .byte "  [CHECK!]", 0
str_prompt:
        .byte $0D, " MOVE? ", 0
msg_bad_input:
        .byte $0D, " INVALID INPUT.", $0D, 0
msg_nyi_cmd:
        .byte $0D, " NOT IMPLEMENTED.", $0D, 0
msg_no_undo:
        .byte $0D, " NO UNDO.", $0D, 0
msg_undone:
        .byte $0D, " UNDONE.", $0D, 0
msg_perft_run:
        .byte $0D, " PERFT...", $0D, 0
msg_perft_hint:
        .byte " (INIT POS = 14H = 20)", $0D, 0
msg_ai_thinking:
        .byte $0D, " COMPUTER THINKING...", $0D, 0
msg_ai_played:
        .byte " COMPUTER PLAYS ", 0
msg_ai_nomove:
        .byte $0D, " NO LEGAL MOVE!", $0D, 0
msg_mode_hvh:
        .byte $0D, " MODE: HUMAN VS HUMAN", $0D, 0
msg_mode_wai:
        .byte $0D, " MODE: HUMAN(W) VS AI(B)", $0D, 0
msg_mode_bai:
        .byte $0D, " MODE: AI(W) VS HUMAN(B)", $0D, 0
msg_mode_ava:
        .byte $0D, " MODE: AI(W) VS AI(B)", $0D, 0
msg_quit:
        .byte $0D, " GAME ABANDONED.", $0D, 0
msg_gameover_prompt:
        .byte $0D, " N = NEW GAME    ? = HELP", $0D
        .byte " > ", 0

; --- Bulky strings move to the upper bank to free CODELOW ---
.segment "ENGINE"
str_title:
        .byte $0D, " * APPLE 1 CHESS *  V0.5", $0D
        .byte " BY VERHILLE ARNAUD, 2026", $0D
        .byte $0D
        .byte " MOVE: E2E4 G1F3 E7E8Q E1G1", $0D
        .byte " CMDS: Q U A M D H L P", $0D
        .byte "   Q QUIT  U UNDO  A AI MOVE", $0D
        .byte "   M MODE  D STRAT  H HINT", $0D
        .byte "   L LIST  P PERFT", $0D
        .byte $0D, " ANY KEY...", $0D, 0
msg_white_mate:
        .byte $0D, " CHECKMATE - BLACK WINS!", $0D, 0
msg_black_mate:
        .byte $0D, " CHECKMATE - WHITE WINS!", $0D, 0
msg_stalemate:
        .byte $0D, " STALEMATE - DRAW.", $0D, 0
msg_mode_prompt:
        .byte $0D, " MODE? 1=HVH 2=WAI 3=BAI 4=AVA: ", 0
msg_strat_naive:
        .byte $0D, " AI: NAIVE (MATERIAL ONLY)", $0D, 0
msg_strat_smart:
        .byte $0D, " AI: SMART (SEE + RANDOM)", $0D, 0
msg_hint:
        .byte $0D, " HINT: ", 0
msg_no_hint:
        .byte $0D, " NO HINT.", $0D, 0
msg_list_header:
        .byte $0D, " LEGAL MOVES:", $0D, 0
str_mat_prefix:
        .byte " MAT ", 0
str_w_plus:
        .byte "W:+", 0
str_b_plus:
        .byte "B:+", 0

; --- pull in libraries via .include (Tier 2 mutualisation) ---
; kbd.asm and print.asm declare their code in the default CODE segment, so
; switch back before including them.
.segment "CODE"
.include "kbd.asm"
.include "print.asm"
