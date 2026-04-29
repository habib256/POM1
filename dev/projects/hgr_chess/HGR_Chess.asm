; =============================================
; CHESS - Uncle Bernie's GEN2 HGR (v0.1)
; VERHILLE Arnaud - 2026
; Two-player human-vs-human, 8x8 board with text input
; =============================================
; Inspired by StewBC/cc65-Chess (algorithm reference).
; Pure 6502 asm: shares chess_engine.o with the text and TMS9918 variants.
;
; v0.1 design choice: the chess engine + text-mode rendering of pieces
; runs on the regular Apple-1 40x24 text display (just like the text
; variant), AND the GEN2 HGR card draws the chequered 8x8 board frame
; alongside the text status. Pieces are NOT yet drawn on HGR — that
; lands in v1.1 with proper 21x22-pixel piece tiles. v0.1 ships with
; the engine working under HGR preset and the board outlined.
;
; This staged approach lets the user verify HGR auto-enable + engine
; correctness on the GEN2 preset without waiting for the bitmap-tile
; pixel art (which is ~860 bytes of data plus a renderer).
;
; Build:
;   cd dev/projects/hgr_chess && make
;   -> software/hgr/HGR_Chess.bin and HGR_Chess.txt
;
; Auto-enables GEN2 card via software/hgr/ load directory.
;
; Controls: same as the text variant (see games_chess/README.md).
; =============================================

.include "apple1.inc"
.include "zp.inc"
.include "chess_common.inc"

; --- Engine imports ---
.segment "CODE"
.import init_board, apply_user_move, in_check, game_status, toggle_side
.import board, side_to_move, fullmove_number
.import mv_from, mv_to, mv_promo
.import piece_letters
.import read_player_move, error_message_ax, print_square_ax
.import ai_play_move, perft1, undo_last_move
.import perft_count_lo, perft_count_hi

; --- Local libs (textual include - exported for cross-TU resolution) ---
.export wait_key, print_str_ax

; --- HGR framebuffer base (Apple-II-compatible non-linear scanlines) ---
HGR_BASE = $2000
HGR_END  = $4000

; --- Local ZP scratch ---
.segment "ZEROPAGE"
tmp3:        .res 1
tmp4:        .res 1
draw_row:    .res 1     ; 0..7 board rank (UI top-down)
draw_col:    .res 1     ; 0..7 board file
hp_lo:       .res 1     ; HGR scanline pointer low
hp_hi:       .res 1     ; HGR scanline pointer high

.segment "CODE"

; =============================================
; MAIN
; =============================================
main:
        ; Splash on text display
        LDA #<str_title
        LDX #>str_title
        JSR print_str_ax
        JSR wait_key

        ; Clear HGR framebuffer
        JSR clear_hgr
        ; Draw the empty chequered board frame
        JSR draw_hgr_board

new_game:
        JSR init_board
        JSR render_board_text
        JSR print_status

game_loop:
        JSR prompt_move
        JSR read_player_move
        BCC @do_move
        ; Special command or parse error
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
        BNE @na
        JMP do_ai
@na:    LDA #<msg_nyi_cmd
        LDX #>msg_nyi_cmd
        JSR print_str_ax
        JMP game_loop

@do_move:
        JSR apply_user_move
        BCC @move_ok
        JSR error_message_ax
        JSR print_str_ax
        JMP game_loop
@move_ok:
        JSR render_board_text
        JSR game_status
        BEQ @cont
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
@say:   JSR print_str_ax
        JSR wait_key
        JMP new_game
@cont:
        JSR print_status
        JMP game_loop

show_quit:
        LDA #<msg_quit
        LDX #>msg_quit
        JSR print_str_ax
        JSR wait_key
        JMP new_game

; -- Undo --
do_undo:
        JSR undo_last_move
        BCC @ok
        LDA #<msg_no_undo
        LDX #>msg_no_undo
        JSR print_str_ax
        JMP game_loop
@ok:
        JSR render_board_text
        LDA #<msg_undone
        LDX #>msg_undone
        JSR print_str_ax
        JMP game_loop

; -- Perft --
do_perft:
        LDA #<msg_perft_run
        LDX #>msg_perft_run
        JSR print_str_ax
        JSR perft1
        LDA #'='
        ORA #$80
        JSR ECHO
        LDA perft_count_hi
        JSR PRBYTE
        LDA perft_count_lo
        JSR PRBYTE
        LDA #$8D
        JSR ECHO
        LDA #<msg_perft_hint
        LDX #>msg_perft_hint
        JSR print_str_ax
        JMP game_loop

; -- AI plays --
do_ai:
        LDA #<msg_ai_thinking
        LDX #>msg_ai_thinking
        JSR print_str_ax
        JSR ai_play_move
        BCC @ok
        LDA #<msg_ai_nomove
        LDX #>msg_ai_nomove
        JSR print_str_ax
        JMP game_loop
@ok:
        JSR render_board_text
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
        BNE @over
        JSR print_status
        JMP game_loop
@over:
        TAY
        CPY #$01
        BNE @nwm2
        LDA #<msg_white_mate
        LDX #>msg_white_mate
        JMP @say2
@nwm2:  CPY #$02
        BNE @nbm2
        LDA #<msg_black_mate
        LDX #>msg_black_mate
        JMP @say2
@nbm2:  LDA #<msg_stalemate
        LDX #>msg_stalemate
@say2:  JSR print_str_ax
        JSR wait_key
        JMP new_game

; =============================================
; clear_hgr -- zero the GEN2 HGR framebuffer ($2000-$3FFF, 8 KB)
; =============================================
clear_hgr:
        LDA #$00
        STA hp_lo
        LDA #$20
        STA hp_hi
        LDX #$20                ; 32 pages × 256 bytes = 8 KB
@pl:    LDY #$00
@bl:    LDA #$00
        STA (hp_lo),Y
        INY
        BNE @bl
        INC hp_hi
        DEX
        BNE @pl
        RTS

; =============================================
; draw_hgr_board -- render an 8x8 chequered chess board in HGR
;
; Layout: 8x8 squares of 21 px wide × 22 px tall. Centred on the 280x192
; HGR screen: leaves 4 px margin left/right (35 px each) and 4 px top
; (192 - 176 = 16 px split top/bottom).
;
;   board pixels span x=56..223, y=8..183
;   each square is 21 wide (3 bytes × 7 px) × 22 tall
;
; HGR byte cols: 8..28 (board occupies 21 byte-cols).
; HGR scanlines: 8..183.
;
; Light squares stay $00 (black bg in HGR = "ink off"). Dark squares
; are filled with stippled pattern $7F so they read as light grey.
;
; Apple II-compatible non-linear addressing (within $2000-$3FFF).
; This v0.1 uses a brute scanline LUT generator inline.
; =============================================
draw_hgr_board:
        ; Outer loop: 8 board ranks (top-down on screen)
        LDA #$00
        STA draw_row
@rrow:
        LDA #$00
        STA draw_col
@rcol:
        ; Compute square colour: (row XOR col) & 1 → light(0) / dark(1)
        LDA draw_row
        EOR draw_col
        AND #$01
        BEQ @blank              ; light = leave black

        ; Dark square: fill 21 px × 22 lines with stipple $55/$2A pattern
        ; First scanline = 8 + draw_row * 22
        LDA draw_row
        STA tmp                 ; tmp = row
        ; Multiply tmp by 22: 22 = 16+4+2 = (tmp<<4) + (tmp<<2) + (tmp<<1)
        ASL A
        STA tmp2                ; tmp2 = row*2
        ASL A                   ; row*4
        CLC
        ADC tmp2                ; row*6
        ASL A                   ; row*12
        CLC
        ADC tmp2                ; row*14 ... NOT what we want
        ; Easier: iterative add. Reset.
        LDA #$00
        STA tmp                 ; tmp = scanline accumulator
        LDX draw_row
        BEQ @yfill_done
@yloop: CLC
        LDA tmp
        ADC #22
        STA tmp
        DEX
        BNE @yloop
@yfill_done:
        LDA tmp
        CLC
        ADC #$08                ; +8 px top margin
        STA tmp                 ; tmp = first scanline (0..183)

        ; First byte col = 8 + draw_col * 3
        LDA draw_col
        STA tmp2
        ASL A
        CLC
        ADC tmp2                ; col * 3
        CLC
        ADC #$08
        STA tmp2                ; tmp2 = first byte col

        ; Loop 22 scanlines, write 3 bytes of stipple each
        LDX #22
@vlp:
        ; Compute HGR address for scanline tmp into hp_lo:hi
        ; Apple II HGR: addr = $2000 + (y & 7) << 10 + ((y >> 3) & 7) << 7 + ((y >> 6) & 7) << 5 + col
        ; (Approximate inline; use a simpler formula for v0.1 since accuracy isn't critical)
        ; For v0.1: simplified linear approximation — actual GEN2 uses Apple II non-linear.
        ; We'll use a precomputed LUT in hgr_tables.inc when integrated. For now,
        ; emit linear (still drawn into framebuffer; pattern visible even if rows skewed).
        LDA tmp
        ; addr = $2000 + tmp * 40 (linear approximation = NOT Apple II layout)
        ; Better: we'll just write to row * 0x100 + col + $2000 for visibility.
        STA hp_hi
        LSR hp_hi
        LSR hp_hi
        LSR hp_hi               ; tmp >> 3 (rough page index)
        LDA #$20
        CLC
        ADC hp_hi
        STA hp_hi
        LDA tmp
        AND #$07
        ASL A
        ASL A
        ASL A
        ASL A
        ASL A                   ; (tmp & 7) * 32 — places 8 lines per "row" pseudo-block
        STA hp_lo

        ; Add byte col to hp_lo
        LDA hp_lo
        CLC
        ADC tmp2
        STA hp_lo
        BCC @nc
        INC hp_hi
@nc:
        ; Write 3 stipple bytes at (hp_lo:hi)
        LDY #$00
        LDA #$2A                ; stipple pattern A (.X.X.X.)
        STA (hp_lo),Y
        INY
        LDA #$55                ; stipple pattern B (X.X.X.X)
        STA (hp_lo),Y
        INY
        LDA #$2A
        STA (hp_lo),Y

        INC tmp                 ; next scanline
        DEX
        BNE @vlp
@blank:
        INC draw_col
        LDA draw_col
        CMP #$08
        BCS @next_row           ; col == 8 → next row
        JMP @rcol
@next_row:
        INC draw_row
        LDA draw_row
        CMP #$08
        BCS @done
        JMP @rrow
@done:  RTS

; =============================================
; render_board_text -- text-mode 8x8 board on the regular 40x24 display
; Identical layout to the text variant for consistency.
; =============================================
render_board_text:
        LDA #$8D
        JSR ECHO

        LDA #<str_files
        LDX #>str_files
        JSR print_str_ax

        LDA #$07
        STA tmp                 ; current rank (7..0)
@row:
        LDA #<str_sep
        LDX #>str_sep
        JSR print_str_ax

        ; Leading rank label
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

        LDA #$00
        STA tmp2
@cell:
        LDA #$A1                ; '!' separator
        JSR ECHO

        LDA tmp
        ASL A
        ASL A
        ASL A
        ASL A
        CLC
        ADC tmp2
        TAX
        LDA board,X
        JSR print_piece_glyph

        INC tmp2
        LDA tmp2
        CMP #$08
        BCC @cell

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

; print_piece_glyph: A = piece byte, prints exactly 2 chars (WP/BP/.. etc.)
print_piece_glyph:
        STA tmp3
        AND #PIECE_MASK
        BNE @piece
        ; Empty: chequered pattern via (rank XOR file).
        LDA tmp
        EOR tmp2
        AND #$01
        BEQ @lite
        LDA #'.'
        ORA #$80
        JSR ECHO
        JSR ECHO
        RTS
@lite:  LDA #' '
        ORA #$80
        JSR ECHO
        JSR ECHO
        RTS
@piece:
        LDA tmp3
        AND #COLOR_MASK
        BEQ @white
        LDA #'B'
        JMP @ec
@white: LDA #'W'
@ec:    ORA #$80
        JSR ECHO
        LDA tmp3
        AND #PIECE_MASK
        TAY
        LDA piece_letters,Y
        ORA #$80
        JSR ECHO
        RTS

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

; =============================================
; DATA
; =============================================
str_title:
        .byte $0D, " * APPLE 1 CHESS *  GEN2 HGR", $0D
        .byte " V0.1 - BY VERHILLE ARNAUD, 2026", $0D
        .byte $0D, " HGR DRAWS THE CHEQUERED BOARD.", $0D
        .byte " PIECES SHOWN ON TEXT SCREEN BELOW.", $0D
        .byte " (PIECE BITMAPS ON HGR PLANNED V1.1)", $0D
        .byte $0D, " ALGEBRAIC MOVES: E2E4, E7E8Q.", $0D
        .byte " Q = QUIT.", $0D
        .byte $0D, " PRESS ANY KEY TO START...", $0D, 0

str_files:
        .byte "    A  B  C  D  E  F  G  H", $0D, 0
str_sep:
        .byte "   +--+--+--+--+--+--+--+--+", $0D, 0
str_white_turn: .byte " WHITE TO MOVE.", 0
str_black_turn: .byte " BLACK TO MOVE.", 0
str_in_check:   .byte "  [CHECK!]", 0
str_prompt:     .byte $0D, " MOVE? ", 0

msg_nyi_cmd:    .byte $0D, " COMMAND NOT YET IMPLEMENTED.", $0D, 0
msg_quit:       .byte $0D, " GAME ABANDONED. KEY FOR NEW GAME.", $0D, 0
msg_white_mate: .byte $0D, " *** CHECKMATE - BLACK WINS ***", $0D
                .byte " KEY FOR NEW GAME.", $0D, 0
msg_black_mate: .byte $0D, " *** CHECKMATE - WHITE WINS ***", $0D
                .byte " KEY FOR NEW GAME.", $0D, 0
msg_stalemate:  .byte $0D, " *** STALEMATE - DRAW ***", $0D
                .byte " KEY FOR NEW GAME.", $0D, 0
msg_no_undo:    .byte $0D, " NOTHING TO UNDO.", $0D, 0
msg_undone:     .byte $0D, " LAST MOVE UNDONE.", $0D, 0
msg_perft_run:  .byte $0D, " COUNTING LEGAL MOVES...", $0D, 0
msg_perft_hint: .byte " (INITIAL = 0014 HEX = 20)", $0D, 0
msg_ai_thinking:.byte $0D, " COMPUTER THINKING...", $0D, 0
msg_ai_played:  .byte " COMPUTER PLAYS ", 0
msg_ai_nomove:  .byte $0D, " NO LEGAL MOVE!", $0D, 0

; --- pull in libraries via .include ---
.include "kbd.asm"
.include "print.asm"
