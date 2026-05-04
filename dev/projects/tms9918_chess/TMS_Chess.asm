; =============================================
; CHESS - P-LAB TMS9918 Graphic Card (v0.1)
; VERHILLE Arnaud - 2026
; Two-player human-vs-human, 8x8 board, 16x16-pixel pieces
; =============================================
; Inspired by StewBC/cc65-Chess (algorithm reference).
; Pure 6502 asm: shares chess_engine.o with the text and HGR variants.
;
; Build:
;   cd dev/projects/tms9918_chess && make
;   -> software/tms9918/TMS_Chess.bin and TMS_Chess.txt
;
; Display:
;   Each board square = 2x2 chars (16x16 px) → 16x16 cells of board,
;   placed at name-table cols 8..23, rows 4..19. Plenty of room for
;   labels (cols 5..7 for ranks, cols 24..31 for status).
;
;   Char-code allocation (256 patterns, 32 colour groups of 8 chars):
;     0    invisible (background, colour group 0 = $11 black-on-black)
;     8-9  light empty square (group 1)
;     16-17 dark empty square (group 2)
;     32-55 white pieces (groups 4-6) - 6 pieces × 4 chars = 24 chars
;     64-87 black pieces (groups 8-10)
;     128+ unused
;
;   For v0.1 the 4 chars per piece are arranged as a 2x2 cell:
;     +---+---+
;     | TL| TR|    TL = top-left glyph cell, etc.
;     +---+---+
;     | BL| BR|
;     +---+---+
;   Pieces are simple 16x16 pixel art (filled silhouette + initial
;   letter underneath). Black pieces are the same shape with inverted
;   colour (white-on-dark vs dark-on-light via colour groups).
;
; Controls (text input via Apple-1 keyboard, board appears on TMS9918):
;   E2E4 + RET   move
;   E7E8Q + RET  promote
;   Q            quit
; (Cursor mode 'C' planned for v0.3)
; =============================================

        .import tms9918_pad12  ; silicon-strict pad16 (helper from tms9918_pad.asm)
.include "apple1.inc"
.include "zp.inc"
.include "tms9918.inc"
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

; --- TMS9918 driver imports ---
.import init_vdp_g1, disable_sprites, clear_name_table
.import vdp_set_write, vdp_upload_a, name_at_rc, print_at_rc
.importzp vdp_lo, vdp_hi, vdp_src_lo, vdp_src_hi, vdp_row, vdp_col

; --- Local libs (textual include - exported for cross-TU resolution) ---
.export wait_key, print_str_ax

; --- Local ZP scratch ---
.segment "ZEROPAGE"
draw_row:    .res 1     ; 0..7 board rank
draw_col:    .res 1     ; 0..7 board file
draw_piece:  .res 1     ; piece byte at draw target

.segment "CODE"

; =============================================
; MAIN
; =============================================
main:
        ; Splash screen on text display first (before VDP init)
        LDA #<str_title
        LDX #>str_title
        JSR print_str_ax
        JSR wait_key

        JSR init_vdp_g1
        JSR upload_chess_patterns
        JSR upload_chess_colors
        JSR clear_name_table
        JSR draw_static_decor

new_game:
        JSR init_board
        JSR render_all_cells
        JSR print_status

game_loop:
        JSR prompt_move
        JSR read_player_move
        BCC @do_move
        ; Special command or parse error.
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
        ; Re-render only the two affected squares for speed.
        LDA mv_from
        STA draw_row_col_arg
        JSR render_one_cell_from_sq
        LDA mv_to
        STA draw_row_col_arg
        JSR render_one_cell_from_sq

        JSR game_status
        BEQ @cont
        ; Game over.
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
        ; Re-render full board (cheap on TMS9918)
        JSR render_all_cells
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
        ; Re-render full board after AI move
        JSR render_all_cells
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
; upload_chess_patterns -- write 8x8 patterns for empty squares + 12 pieces
; into the TMS9918 pattern table.
;
; Layout in pattern table:
;   chars 0..7    : invisible (chars 0-7 = colour group 0 = invisible)
;   chars 8..15   : light empty square (group 1) — uses chars 8 and 9
;   chars 16..23  : dark empty square (group 2)  — uses chars 16 and 17
;   chars 32..55  : white pieces (groups 4-6) — 6 pieces × 4 chars (2x2)
;   chars 64..87  : black pieces (groups 8-10) — 6 pieces × 4 chars
;
; Total chars defined: 1 (empty light) + 1 (empty dark) + 24 (white) + 24 (black) = 50.
; Pattern bytes uploaded: 50 × 8 = 400 bytes.
; =============================================
upload_chess_patterns:
        ; Set VDP write address to $0000 (start of pattern table).
        LDA #$00
        STA vdp_lo
        STA vdp_hi
        JSR vdp_set_write

        ; Write 32 bytes of zero (chars 0-3, all invisible).
        LDX #32
@z1:    LDA #$00
        STA VDP_DATA
        DEX
        BNE @z1

        ; Skip to char 8 (already at $40 = char 8).
        ; Upload light-square pattern at chars 8..15 (we use 8 only).
        ; Light square: dotted pattern $00 / $00 / ... (just empty)
        LDX #64                 ; chars 8..15 = 8 chars × 8 bytes
@l1:    LDA #$00
        JSR     tms9918_pad12   ; +12c silicon-strict pad16 (back-to-back VDP store)
        STA VDP_DATA
        DEX
        BNE @l1

        ; Dark square at chars 16..23.
        ; Pattern: subtle stippling so dark squares are visible.
        LDX #8
@dpat:
        LDA #$AA                ; row pattern: bit pattern X.X.X.X.
        JSR     tms9918_pad12   ; +12c silicon-strict pad16 (back-to-back VDP store)
        STA VDP_DATA
        JSR     tms9918_pad12   ; +12c silicon-strict pad16 (before LDA #imm bridge)
        LDA #$55                ; row pattern: .X.X.X.X
        STA VDP_DATA
        DEX
        BNE @dpat
        ; That writes 16 bytes for 2 chars; need 64 bytes total for 8 chars.
        LDX #6 * 8              ; remaining 6 chars × 8 bytes = 48 bytes
@d2:    LDA #$00
        JSR     tms9918_pad12   ; +12c silicon-strict pad16 (back-to-back VDP store)
        STA VDP_DATA
        DEX
        BNE @d2

        ; Now at char 24. Skip to char 32 (white pieces).
        LDX #8 * 8              ; 8 chars × 8 bytes = 64 bytes of zeros
@sk:    LDA #$00
        JSR     tms9918_pad12   ; +12c silicon-strict pad16 (back-to-back VDP store)
        STA VDP_DATA
        DEX
        BNE @sk

        ; --- White pieces at chars 32..55 (24 chars = 192 bytes) ---
        ; Each piece = 4 chars (2x2 = 16x16 px), uploaded directly.
        ; piece_glyphs_white is a 24×8 = 192 byte block, all in order.
        LDA #<piece_glyphs_white
        STA vdp_src_lo
        LDA #>piece_glyphs_white
        STA vdp_src_hi
        ; Upload 192 bytes. vdp_upload_a takes A=count (max 256).
        LDA #192
        JSR vdp_upload_a

        ; Skip chars 56..63 (8 chars = 64 bytes).
        LDX #64
@sk2:   LDA #$00
        STA VDP_DATA
        DEX
        BNE @sk2

        ; --- Black pieces at chars 64..87 ---
        ; Use the same shapes as white pieces (just different colours).
        LDA #<piece_glyphs_white
        STA vdp_src_lo
        LDA #>piece_glyphs_white
        STA vdp_src_hi
        LDA #192
        JSR vdp_upload_a

        RTS

; =============================================
; upload_chess_colors -- write the 32-entry colour table at VRAM $2000.
; One byte per group of 8 chars (fg<<4 | bg).
;
; TMS9918 colour codes: 1=black, 2=med green, 3=lt green, 4=dk blue,
; 5=lt blue, 6=dk red, 7=cyan, 8=med red, 9=lt red, 10=dk yellow,
; 11=lt yellow, 12=dk green, 13=magenta, 14=grey, 15=white.
; =============================================
upload_chess_colors:
        LDA #$00
        STA vdp_lo
        LDA #$20                ; colour table = $2000
        STA vdp_hi
        JSR vdp_set_write
        LDX #$00
@cl:    LDA color_table,X
        STA VDP_DATA
        INX
        CPX #$20                ; 32 entries
        BNE @cl
        RTS

color_table:
        .byte $11               ; group  0: invisible (black on black)
        .byte $E1               ; group  1: light square (grey on black)
        .byte $1E               ; group  2: dark  square (black on grey)
        .byte $11               ; group  3: unused
        .byte $FE               ; group  4: white piece on light (white on grey)
        .byte $F1               ; group  5: white piece on dark (white on black)
        .byte $FE               ; group  6: white piece (continuation)
        .byte $11               ; group  7: unused
        .byte $1E               ; group  8: black piece on light (black on grey)
        .byte $1F               ; group  9: black piece on dark (black on white)
        .byte $1E               ; group 10: black piece (continuation)
        .byte $F1               ; group 11..31: white-on-black for status text
        .byte $F1, $F1, $F1, $F1, $F1, $F1, $F1, $F1
        .byte $F1, $F1, $F1, $F1, $F1, $F1, $F1, $F1
        .byte $F1, $F1, $F1, $F1

; =============================================
; draw_static_decor -- print rank labels (1..8) and file labels (A..H)
; into the name table around the board area.
;
; Board area: cols 8..23 (16 cols), rows 4..19 (16 rows). Each board
; square is 2x2 chars; 8 squares × 2 = 16 cells per dimension.
;
; Rank labels: col 6, rows 4, 6, 8, 10, 12, 14, 16, 18 (top of each square)
;   Wait: 8 ranks × 2 rows each = 16 rows, starting at row 4.
;   Rank N (N=8..1) tops at row 4 + (8-N)*2.
; File labels: row 21, cols 8.5..22.5 → centre of each 2-col square is at
;   col 8 + 2*file + 0 (use left col).
; =============================================
draw_static_decor:
        ; Print rank digits 1..8 in left margin (col 5).
        LDX #0                  ; X = 0..7 (file index for rank digit row)
@rk:
        ; Rank N=8-X (so X=0 → rank 8 → top of board).
        ; Position: col = 5, row = 4 + X*2 + 0 (centre of 2-row cell would be 4+X*2+0 or 4+X*2+1; pick top)
        LDA #5
        STA vdp_col
        TXA
        ASL A
        CLC
        ADC #5                  ; row = 5 + X*2 (centre of 2-row cell is row 4+X*2 + 0 or 1)
        STA vdp_row
        LDA #'8'
        SEC
        SBC tmp_x_save
        ; tmp_x_save = X, computed below
        ; Actually simpler: rank digit = '0' + (8 - X)
        TXA
        STA tmp_x_save
        LDA #'0'+8
        SEC
        SBC tmp_x_save
        JSR print_at_rc
        INC tmp_x_save
        LDX tmp_x_save
        CPX #8
        BCC @rk

        ; Print file letters A..H at row 21, every 2 cols starting col 8.
        LDX #0
@fl:
        LDA #21
        STA vdp_row
        TXA
        ASL A
        CLC
        ADC #8                  ; col = 8 + X*2
        STA vdp_col
        TXA
        CLC
        ADC #'A'
        JSR print_at_rc
        INX
        CPX #8
        BCC @fl
        RTS

tmp_x_save: .byte 0
draw_row_col_arg: .byte 0       ; 0x88 square passed to render_one_cell_from_sq

; =============================================
; render_all_cells -- redraw the entire 8x8 board
; =============================================
render_all_cells:
        LDA #$00
        STA draw_row
@rlp:
        LDA #$00
        STA draw_col
@clp:
        JSR render_one_cell
        INC draw_col
        LDA draw_col
        CMP #$08
        BCC @clp
        INC draw_row
        LDA draw_row
        CMP #$08
        BCC @rlp
        RTS

; render_one_cell_from_sq: extract (row, col) from the 0x88 square in
; draw_row_col_arg, then call render_one_cell.
render_one_cell_from_sq:
        LDA draw_row_col_arg
        AND #$07
        STA draw_col
        LDA draw_row_col_arg
        LSR A
        LSR A
        LSR A
        LSR A
        AND #$07
        STA draw_row
        ; fall through

; =============================================
; render_one_cell -- draw one 2x2-char board square
; Input: draw_row (0..7), draw_col (0..7)
; Reads board[rank*16 + file] to get piece byte.
;
; Char codes for a 2x2 cell:
;   - empty light:  chars 8, 9, 10, 11 (all from group 1) but we just
;     use chars 8..9 for the 2 cols, so the 2x2 = 8 9 / 8 9.
;     Simpler: a single char id repeated 4 times.
;   - empty dark:   char 16 (or 17) repeated 4 times.
;   - white piece:  chars 32 + (piece-1)*4 + 0..3 in 2x2 order:
;       TL=base, TR=base+1, BL=base+2, BR=base+3
;   - black piece:  chars 64 + (piece-1)*4 + 0..3
;
; Light square = (rank XOR file) parity 0 (rank+file even)
; Dark  square = parity 1
; =============================================
render_one_cell:
        ; Compute board[] index = rank*16 + file
        LDA draw_row
        ASL A
        ASL A
        ASL A
        ASL A
        CLC
        ADC draw_col
        TAX
        LDA board,X
        STA draw_piece

        ; Compute name-table position: row = 4 + draw_row*2 (top-down).
        ; Chess rank 1 is bottom; we drew with rank 8 at top of board.
        ; draw_row = 0 → rank 1 → name-table row = 4 + (7-0)*2 = 18.
        ; Generic: nt_row = 4 + (7 - draw_row) * 2
        LDA #$07
        SEC
        SBC draw_row
        ASL A
        CLC
        ADC #$04
        STA vdp_row             ; top row of this square
        ; nt_col = 8 + draw_col * 2
        LDA draw_col
        ASL A
        CLC
        ADC #$08
        STA vdp_col

        ; Pick base char code based on piece (or empty + parity).
        LDA draw_piece
        BNE @piece
        ; Empty: parity = (draw_row XOR draw_col) & 1
        LDA draw_row
        EOR draw_col
        AND #$01
        BEQ @lite
        LDA #16                 ; dark square base
        JMP @do
@lite:  LDA #8                  ; light square base
        JMP @do
@piece:
        STA tmp                 ; full byte
        AND #PIECE_MASK
        SEC
        SBC #$01                ; piece type 0..5 (PNBRQK indexed 0..5)
        ASL A
        ASL A                   ; A = (type-1) * 4 = char offset
        STA tmp2
        LDA tmp
        AND #COLOR_MASK
        BEQ @wp
        ; Black piece: base = 64
        LDA tmp2
        CLC
        ADC #64
        JMP @do
@wp:    ; White piece: base = 32
        LDA tmp2
        CLC
        ADC #32
@do:
        ; A = top-left char code. Write 2x2 to name table.
        STA tmp                 ; base char
        ; --- Top row: write base, base+1 ---
        PHA
        JSR name_at_rc
        JSR vdp_set_write
        LDA tmp
        STA VDP_DATA
        ; The TMS9918 auto-increments VRAM addr after each write.
        CLC
        ADC #$01
        JSR     tms9918_pad12   ; +12c silicon-strict pad16 (back-to-back VDP store)
        STA VDP_DATA
        PLA

        ; --- Bottom row: vdp_row + 1, write base+2, base+3 (or same for empty) ---
        INC vdp_row
        JSR name_at_rc
        JSR vdp_set_write
        LDA draw_piece
        BEQ @e_b                ; empty: use same chars as top
        LDA tmp
        CLC
        ADC #$02
        JSR     tms9918_pad12   ; +12c silicon-strict pad16 (back-to-back VDP store)
        STA VDP_DATA
        CLC
        ADC #$01
        JSR     tms9918_pad12   ; +12c silicon-strict pad16 (back-to-back VDP store)
        STA VDP_DATA
        RTS
@e_b:
        LDA tmp
        STA VDP_DATA
        CLC
        ADC #$01
        JSR     tms9918_pad12   ; +12c silicon-strict pad16 (back-to-back VDP store)
        STA VDP_DATA
        RTS

; =============================================
; print_status -- on the Apple-1 text display (40-col)
; The TMS9918 board persists; status text is below on the regular screen.
; =============================================
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
; DATA — piece glyphs (16x16 each, expressed as 4 chars of 8x8)
; Layout per piece: TL TR BL BR (each 8 bytes), so 4 × 8 = 32 bytes / piece.
; 6 pieces × 32 = 192 bytes total (piece_glyphs_white).
;
; Piece order matches PIECE_PAWN..PIECE_KING (1..6):
;   pawn, knight, bishop, rook, queen, king.
;
; Glyphs are simple silhouettes (filled shapes in white = bit set);
; the colour group at name-table time decides if they read as white-on-X
; or black-on-X.
; =============================================
piece_glyphs_white:
        ; --- PAWN (16x16) ---
        ; Top-left, top-right (rows 0..7), then bottom-left, bottom-right.
        .byte $00,$00,$00,$00,$00,$00,$01,$03         ; pawn TL row 0..7
        .byte $00,$00,$00,$00,$00,$00,$80,$C0         ; pawn TR row 0..7
        .byte $07,$07,$0E,$1F,$3F,$7F,$FF,$FF         ; pawn BL row 8..15
        .byte $E0,$E0,$70,$F8,$FC,$FE,$FF,$FF         ; pawn BR row 8..15

        ; --- KNIGHT (16x16) ---
        .byte $00,$01,$03,$07,$0F,$1F,$1F,$3F
        .byte $00,$80,$C0,$E0,$F0,$F8,$F8,$FC
        .byte $7F,$7F,$3F,$1F,$3F,$7F,$FF,$FF
        .byte $FE,$FE,$FC,$F8,$FC,$FE,$FF,$FF

        ; --- BISHOP (16x16) ---
        .byte $00,$01,$03,$07,$03,$01,$03,$07
        .byte $00,$80,$C0,$E0,$C0,$80,$C0,$E0
        .byte $0F,$1F,$3F,$1F,$3F,$7F,$FF,$FF
        .byte $F0,$F8,$FC,$F8,$FC,$FE,$FF,$FF

        ; --- ROOK (16x16) ---
        .byte $00,$5F,$5F,$5F,$5F,$3F,$3F,$3F
        .byte $00,$FA,$FA,$FA,$FA,$FC,$FC,$FC
        .byte $3F,$3F,$3F,$3F,$3F,$7F,$FF,$FF
        .byte $FC,$FC,$FC,$FC,$FC,$FE,$FF,$FF

        ; --- QUEEN (16x16) ---
        .byte $00,$11,$1B,$0F,$1F,$3F,$7F,$7F
        .byte $00,$88,$D8,$F0,$F8,$FC,$FE,$FE
        .byte $7F,$3F,$1F,$3F,$7F,$7F,$FF,$FF
        .byte $FE,$FC,$F8,$FC,$FE,$FE,$FF,$FF

        ; --- KING (16x16) ---
        .byte $00,$03,$07,$0F,$1F,$1F,$3F,$7F
        .byte $00,$C0,$E0,$F0,$F8,$F8,$FC,$FE
        .byte $7F,$3F,$1F,$3F,$7F,$7F,$FF,$FF
        .byte $FE,$FC,$F8,$FC,$FE,$FE,$FF,$FF

; --- Strings ---
str_title:
        .byte $0D, " * APPLE 1 CHESS *  TMS9918", $0D
        .byte " V0.1 - BY VERHILLE ARNAUD, 2026", $0D
        .byte $0D, " GRAPHIC BOARD APPEARS ON TMS9918.", $0D
        .byte " STATUS LINE STAYS ON TEXT SCREEN.", $0D
        .byte $0D, " ALGEBRAIC MOVES: E2E4 E7E5 ...", $0D
        .byte " PROMOTE: E7E8Q.  Q AT PROMPT QUITS.", $0D
        .byte $0D, " EN-PASSANT, CASTLING, AI: V0.2+.", $0D
        .byte $0D, " PRESS ANY KEY TO START...", $0D, 0

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
