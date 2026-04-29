; =============================================
; CHESS (text mode) for Apple 1
; VERHILLE Arnaud - 2026
; Two-player human-vs-human, 8x8 board, ASCII rendering
; =============================================
; Inspired by StewBC/cc65-Chess (algorithm reference).
; Apple-1 implementation: pure 6502 asm, board in 0x88 mailbox.
;
; Engine lives in dev/lib/chess/chess_engine.asm (linked separately).
; This file is the text-mode renderer + game loop.
;
; Build:
;   cd dev/projects/games_chess && make
;   -> software/games/Chess.bin and Chess.txt
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
tmp3:    .res 1
tmp4:    .res 1
play_mode: .res 1   ; 0 = HvH, 1 = W=Hum/B=AI, 2 = W=AI/B=Hum, 3 = AvA

; Switch back to CODE segment so .import directives below pick up the
; default absolute address size.
.segment "CODE"

; --- Engine imports ---
.import init_board, apply_user_move, in_check, game_status, toggle_side
.import board, side_to_move, fullmove_number
.import mv_from, mv_to, mv_promo
.import piece_letters
.import read_player_move, error_message_ax, print_square_ax
.import ai_play_move, perft1, undo_last_move
.import perft_count_lo, perft_count_hi
.import undo_avail

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
        LDA #$00
        STA play_mode

new_game:
        JSR init_board

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
        ; A = $01 Q quit, $02 U undo, $03 malformed, $04 D depth/AI,
        ;     $05 C cursor (ignored in text variant), $06 P perft, $07 A AI plays
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
        BNE @na
        JMP cycle_mode
@na:    CMP #$03
        BNE @other_special
        ; Malformed input — re-prompt
        LDA #<msg_bad_input
        LDX #>msg_bad_input
        JSR print_str_ax
        JMP game_loop
@other_special:
        ; Cursor / depth: TMS9918/HGR variants only or not yet wired.
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
        ; Check terminal state.
        JSR game_status
        BEQ game_loop

        ; Game over — render final board, announce result, wait, restart.
        JSR render_board
        TAY                     ; Y = status code
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
@nbm:   ; stalemate
        LDA #<msg_stalemate
        LDX #>msg_stalemate
@say:   JSR print_str_ax
        JSR wait_key
        JMP new_game

show_quit:
        LDA #<msg_quit
        LDX #>msg_quit
        JSR print_str_ax
        JSR wait_key
        JMP new_game

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
        ASL A                   ; index × 2 (string ptr table)
        TAY
        LDA mode_strs,Y
        TAX
        INY
        LDA mode_strs,Y
        TYA
        LDA mode_strs-1,Y       ; reload low byte (Y is now odd)
        ; Simpler: read directly
        LDA play_mode
        CMP #$00
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
@ok:
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
        ; Print which move it played, then check terminal state
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
@say:   JSR render_board
        JSR print_str_ax
        JSR wait_key
        JMP new_game

; =============================================
; render_board -- print the 8x8 board with file/rank labels
;
; Layout (each cell is 2 chars wide → 3 chars per cell with separator):
;
;     A  B  C  D  E  F  G  H
;    +--+--+--+--+--+--+--+--+
;  8 !BR!BN!BB!BQ!BK!BB!BN!BR! 8
;    +--+--+--+--+--+--+--+--+
;  7 !BP!BP!BP!BP!BP!BP!BP!BP! 7
;    +--+--+--+--+--+--+--+--+
;    ... ranks 6..3 with `..` for dark empty cells, `  ` for light ...
;    +--+--+--+--+--+--+--+--+
;  2 !WP!WP!WP!WP!WP!WP!WP!WP! 2
;    +--+--+--+--+--+--+--+--+
;  1 !WR!WN!WB!WQ!WK!WB!WN!WR! 1
;    +--+--+--+--+--+--+--+--+
;     A  B  C  D  E  F  G  H
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
        LDA #$8D
        JSR ECHO

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

        ; Leading rank label + space ("  8 " etc.)
        LDA #' '
        ORA #$80
        JSR ECHO
        LDA tmp
        CLC
        ADC #'1'
        ORA #$80
        JSR ECHO
        LDA #' '
        ORA #$80
        JSR ECHO

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
        LDA board,X
        JSR print_piece_glyph2  ; prints exactly 2 chars

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

        LDA #<str_files
        LDX #>str_files
        JSR print_str_ax
        RTS

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
; DATA
; =============================================
str_title:
        .byte $0D, " * APPLE 1 CHESS *  TEXT MODE V0.4", $0D
        .byte " BY VERHILLE ARNAUD, 2026", $0D
        .byte $0D
        .byte " ALGEBRAIC NOTATION (NO RETURN):", $0D
        .byte "   E2E4  G1F3  E7E8Q  E1G1 ...", $0D
        .byte $0D
        .byte " COMMANDS AT PROMPT:", $0D
        .byte "   Q  QUIT     U  UNDO LAST MOVE", $0D
        .byte "   A  AI PLAYS THIS TURN (1 PLY)", $0D
        .byte "   M  CYCLE MODE (HVH/WAI/BAI/AVA)", $0D
        .byte "   P  COUNT LEGAL MOVES (PERFT)", $0D
        .byte $0D, " PRESS ANY KEY TO START...", $0D, 0

str_files:
        .byte "    A  B  C  D  E  F  G  H", $0D, 0

str_sep:
        .byte "   +--+--+--+--+--+--+--+--+", $0D, 0

str_white_turn:
        .byte " WHITE TO MOVE.", 0
str_black_turn:
        .byte " BLACK TO MOVE.", 0
str_in_check:
        .byte "  [CHECK!]", 0
str_prompt:
        .byte $0D, " MOVE? ", 0

msg_bad_input:
        .byte $0D, " INVALID INPUT FORMAT.", $0D, 0
msg_nyi_cmd:
        .byte $0D, " COMMAND NOT YET IMPLEMENTED.", $0D, 0
msg_no_undo:
        .byte $0D, " NOTHING TO UNDO.", $0D, 0
msg_undone:
        .byte $0D, " LAST MOVE UNDONE.", $0D, 0
msg_perft_run:
        .byte $0D, " COUNTING LEGAL MOVES...", $0D, 0
msg_perft_hint:
        .byte " (INITIAL POSITION = 0014 HEX = 20)", $0D, 0
msg_ai_thinking:
        .byte $0D, " COMPUTER THINKING...", $0D, 0
msg_ai_played:
        .byte " COMPUTER PLAYS ", 0
msg_ai_nomove:
        .byte $0D, " NO LEGAL MOVE!", $0D, 0
msg_mode_hvh:
        .byte $0D, " MODE: HUMAN VS HUMAN", $0D, 0
msg_mode_wai:
        .byte $0D, " MODE: HUMAN (W) VS COMPUTER (B)", $0D, 0
msg_mode_bai:
        .byte $0D, " MODE: COMPUTER (W) VS HUMAN (B)", $0D, 0
msg_mode_ava:
        .byte $0D, " MODE: COMPUTER (W) VS COMPUTER (B)", $0D, 0
mode_strs: .byte 0, 0, 0, 0, 0, 0, 0, 0   ; placeholder (unused)
msg_quit:
        .byte $0D, " GAME ABANDONED.", $0D
        .byte " PRESS A KEY FOR NEW GAME.", $0D, 0
msg_white_mate:
        .byte $0D, " *** CHECKMATE - BLACK WINS! ***", $0D
        .byte " PRESS A KEY FOR NEW GAME.", $0D, 0
msg_black_mate:
        .byte $0D, " *** CHECKMATE - WHITE WINS! ***", $0D
        .byte " PRESS A KEY FOR NEW GAME.", $0D, 0
msg_stalemate:
        .byte $0D, " *** STALEMATE - DRAW. ***", $0D
        .byte " PRESS A KEY FOR NEW GAME.", $0D, 0

; --- pull in libraries via .include (Tier 2 mutualisation) ---
.include "kbd.asm"
.include "print.asm"
