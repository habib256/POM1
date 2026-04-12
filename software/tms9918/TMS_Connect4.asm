; =============================================
; CONNECT 4 - P-LAB TMS9918 Graphic Card
; VERHILLE Arnaud - 2026
; Two-player drop-piece game, 7x6 grid
; =============================================
; Assemble:
;   ca65 -o build/TMS_Connect4.o software/tms9918/TMS_Connect4.asm
;   ld65 -C software/hgr/apple1_gen2.cfg -o build/TMS_Connect4.bin build/TMS_Connect4.o
;
; Load via File > Load Memory, 280R.  Enable the TMS9918 card.
;
; Graphics I mode, 32x24 chars.  Each Connect 4 slot uses a *4x4*
; block of characters = 32x32 pixels.  The 7x6 board becomes
; 28x24 name-table cells = 224x192 pixels — it fills the screen
; vertically with a 2-column margin on each side.
; Pattern is a 32x32 disc (filled circle) split into 16 glyphs.
; Three colour groups share the shape: empty (black hole on blue),
; red piece, yellow piece — all on a continuous blue board.
; =============================================

ECHO     = $FFEF
KBD      = $D010
KBDCR    = $D011
VDP_DATA = $CC00
VDP_CTRL = $CC01

NCOLS   = 7
NROWS   = 6
NCELLS  = 42

TILE_EMPTY  = 0
TILE_RED    = 1
TILE_YELLOW = 2

GRID_BASE = $4000

; --- Zero page ---
.zeropage
temp:          .res 1
temp2:         .res 1
temp3:         .res 1
temp4:         .res 1
src_lo:        .res 1
src_hi:        .res 1
str_lo:        .res 1
str_hi:        .res 1
current_player:.res 1
move_count:    .res 1
winner:        .res 1
last_row:      .res 1
last_col:      .res 1
row_cnt:       .res 1
col_cnt:       .res 1
draw_row:      .res 1
draw_col:      .res 1

.code

; =============================================
; MAIN
; =============================================
main:
        LDA #<str_title
        LDX #>str_title
        JSR print_str_ax
        JSR wait_key

        JSR init_vdp

new_game:
        LDY #$00
        TYA
@clr:   STA GRID_BASE,Y
        INY
        CPY #NCELLS
        BNE @clr

        LDA #TILE_RED
        STA current_player
        LDA #$00
        STA move_count
        STA winner

        JSR render_all
        JSR print_status

move_loop:
        JSR wait_key

        CMP #'R'
        BNE @not_r
        JMP new_game
@not_r:
        CMP #'1'
        BCC move_loop
        CMP #'8'
        BCS move_loop
        SEC
        SBC #'1'

        JSR drop_piece
        CMP #$00
        BEQ move_loop

        LDA last_row
        STA draw_row
        LDA last_col
        STA draw_col
        LDA current_player
        JSR draw_cell

        INC move_count

        JSR check_win
        STA winner
        CMP #$00
        BNE @game_won

        LDA move_count
        CMP #NCELLS
        BCS @draw

        LDA current_player
        EOR #$03
        STA current_player
        JSR print_status
        JMP move_loop

@game_won:
        LDA winner
        CMP #TILE_RED
        BNE @y
        LDA #<str_red_wins
        LDX #>str_red_wins
        JMP @show
@y:
        LDA #<str_yellow_wins
        LDX #>str_yellow_wins
@show:
        JSR print_str_ax
        JSR wait_key
        JMP new_game

@draw:
        LDA #<str_draw
        LDX #>str_draw
        JSR print_str_ax
        JSR wait_key
        JMP new_game

; =============================================
; drop_piece / check_win / check_4_at_x
; =============================================
drop_piece:
        STA last_col
        LDX #NROWS-1
@f:     LDA row_x7,X
        CLC
        ADC last_col
        TAY
        LDA GRID_BASE,Y
        BEQ @place
        DEX
        BPL @f
        LDA #$00
        RTS
@place:
        STX last_row
        LDA current_player
        STA GRID_BASE,Y
        LDA #$01
        RTS

check_win:
        LDA #$01
        STA temp4
        LDA #$00
        STA row_cnt
@h_row:
        LDA #$00
        STA col_cnt
@h_col:
        LDX row_cnt
        LDA row_x7,X
        CLC
        ADC col_cnt
        TAX
        JSR check_4_at_x
        CMP #$00
        BNE @win_tr
        INC col_cnt
        LDA col_cnt
        CMP #$04
        BCC @h_col
        INC row_cnt
        LDA row_cnt
        CMP #$06
        BCC @h_row

        JMP @v_start
@win_tr:
        JMP @winner
@v_start:
        LDA #$07
        STA temp4
        LDA #$00
        STA row_cnt
@v_row:
        LDA #$00
        STA col_cnt
@v_col:
        LDX row_cnt
        LDA row_x7,X
        CLC
        ADC col_cnt
        TAX
        JSR check_4_at_x
        CMP #$00
        BNE @win_tr
        INC col_cnt
        LDA col_cnt
        CMP #$07
        BCC @v_col
        INC row_cnt
        LDA row_cnt
        CMP #$03
        BCC @v_row

        LDA #$08
        STA temp4
        LDA #$00
        STA row_cnt
@d1_row:
        LDA #$00
        STA col_cnt
@d1_col:
        LDX row_cnt
        LDA row_x7,X
        CLC
        ADC col_cnt
        TAX
        JSR check_4_at_x
        CMP #$00
        BNE @win_tr
        INC col_cnt
        LDA col_cnt
        CMP #$04
        BCC @d1_col
        INC row_cnt
        LDA row_cnt
        CMP #$03
        BCC @d1_row

        LDA #$06
        STA temp4
        LDA #$00
        STA row_cnt
@d2_row:
        LDA #$03
        STA col_cnt
@d2_col:
        LDX row_cnt
        LDA row_x7,X
        CLC
        ADC col_cnt
        TAX
        JSR check_4_at_x
        CMP #$00
        BNE @win_tr
        INC col_cnt
        LDA col_cnt
        CMP #$07
        BCC @d2_col
        INC row_cnt
        LDA row_cnt
        CMP #$03
        BCC @d2_row

        LDA #$00
@winner:
        RTS

check_4_at_x:
        LDA GRID_BASE,X
        BEQ @no
        STA temp
        TXA
        CLC
        ADC temp4
        TAX
        LDA GRID_BASE,X
        CMP temp
        BNE @no
        TXA
        CLC
        ADC temp4
        TAX
        LDA GRID_BASE,X
        CMP temp
        BNE @no
        TXA
        CLC
        ADC temp4
        TAX
        LDA GRID_BASE,X
        CMP temp
        BNE @no
        LDA temp
        RTS
@no:
        LDA #$00
        RTS

; =============================================
; init_vdp: set up TMS9918 and upload patterns, colours, clear name table
; Uses 16 glyphs per piece type (4x4 chars) to make 32x32-pixel pieces.
; Chars 8..23  = empty slot   (groups 1-2, colour $14 black-on-blue)
; Chars 24..39 = red piece    (groups 3-4, colour $84 red-on-blue)
; Chars 40..55 = yellow piece (groups 5-6, colour $B4 yellow-on-blue)
; Chars 0..7   = outside board (group 0, colour $11 invisible)
; =============================================
init_vdp:
        LDX #$00
@regloop:
        LDA vdp_regs,X
        STA VDP_CTRL
        TXA
        ORA #$80
        STA VDP_CTRL
        INX
        CPX #$08
        BNE @regloop

        ; Upload the 128-byte disc pattern 3 times:
        ; empty at char  8 → VRAM $0040
        ; red   at char 24 → VRAM $00C0
        ; yellow at char 40 → VRAM $0140
        LDA #$40
        STA VDP_CTRL
        LDA #$40
        STA VDP_CTRL
        JSR upload_pattern

        LDA #$C0
        STA VDP_CTRL
        LDA #$40
        STA VDP_CTRL
        JSR upload_pattern

        LDA #$40
        STA VDP_CTRL
        LDA #$41
        STA VDP_CTRL
        JSR upload_pattern

        ; Colour table: 7 entries at $2000
        LDA #$00
        STA VDP_CTRL
        LDA #$60
        STA VDP_CTRL
        LDX #$00
@colloop:
        LDA tile_colors,X
        STA VDP_DATA
        INX
        CPX #$07
        BCC @colloop

        ; Clear name table (768 bytes) to char 0
        LDA #$00
        STA VDP_CTRL
        LDA #$58
        STA VDP_CTRL
        LDX #$03
        LDA #$00
@np:    LDY #$00
@nb:    STA VDP_DATA
        INY
        BNE @nb
        DEX
        BNE @np

        ; Disable sprites
        LDA #$00
        STA VDP_CTRL
        LDA #$5B
        STA VDP_CTRL
        LDA #$D0
        STA VDP_DATA
        RTS

; Upload 128 pattern bytes from disc_pattern to VDP_DATA
upload_pattern:
        LDA #<disc_pattern
        STA src_lo
        LDA #>disc_pattern
        STA src_hi
        LDY #$00
@lp:    LDA (src_lo),Y
        STA VDP_DATA
        INY
        CPY #$80
        BCC @lp
        RTS

; =============================================
; render_all: draw the whole board via repeated draw_cell calls
; =============================================
render_all:
        LDA #$00
        STA draw_row
@rlp:
        LDA #$00
        STA draw_col
@clp:
        LDX draw_row
        LDA row_x7,X
        CLC
        ADC draw_col
        TAX
        LDA GRID_BASE,X
        JSR draw_cell
        INC draw_col
        LDA draw_col
        CMP #NCOLS
        BCC @clp
        INC draw_row
        LDA draw_row
        CMP #NROWS
        BCC @rlp
        RTS

; =============================================
; draw_cell: paint one 4x4-char board slot
; Input: A = state (0, 1, 2), draw_row (0..5), draw_col (0..6)
;
; Name-table layout: 2-col margin, 28-col board, 2-col margin.
; Top-left glyph of cell (br, bc) is at name row br*4, name col 2+bc*4
; → VRAM addr = $1800 + (br*4)*32 + (2+bc*4)
;             = $1802 + br*128 + bc*4
;
; Base char code per state:  8, 24, 40  =>  8 + state*16
; The 16 glyphs are written in row-major order: TL,T1,T2,TR,ML,...,BR
; =============================================
draw_cell:
        ; base_char = 8 + state*16
        ASL A
        ASL A
        ASL A
        ASL A
        CLC
        ADC #$08
        TAX                             ; X = current char code

        ; name-table offset = 2 + draw_row*128 + draw_col*4
        LDY draw_row
        LDA row_x128_lo,Y
        CLC
        ADC #$02
        STA temp
        LDA row_x128_hi,Y
        ADC #$00
        STA temp2

        LDA draw_col
        ASL A
        ASL A
        CLC
        ADC temp
        STA temp
        LDA temp2
        ADC #$00
        STA temp2

        ; Loop 4 super-rows (Y counts down from 4)
        LDY #$04
@row:
        ; Set VDP write addr to $1800 + temp
        LDA temp
        STA VDP_CTRL
        LDA temp2
        CLC
        ADC #$18
        ORA #$40
        STA VDP_CTRL

        ; Write 4 consecutive char codes
        STX VDP_DATA
        INX
        STX VDP_DATA
        INX
        STX VDP_DATA
        INX
        STX VDP_DATA
        INX

        ; Advance temp by 32 for next name-table row
        CLC
        LDA temp
        ADC #$20
        STA temp
        LDA temp2
        ADC #$00
        STA temp2

        DEY
        BNE @row
        RTS

; =============================================
; print_status
; =============================================
print_status:
        LDA current_player
        CMP #TILE_RED
        BNE @y
        LDA #<str_red_turn
        LDX #>str_red_turn
        JMP @do
@y:
        LDA #<str_yellow_turn
        LDX #>str_yellow_turn
@do:
        JSR print_str_ax
        RTS

wait_key:
@wk:    LDA KBDCR
        BPL @wk
        LDA KBD
        AND #$7F
        RTS

print_str_ax:
        STA str_lo
        STX str_hi
        LDY #$00
@lp:    LDA (str_lo),Y
        BEQ @dn
        ORA #$80
        JSR ECHO
        INY
        BNE @lp
@dn:    RTS

; =============================================
; DATA
; =============================================

row_x7:
        .byte 0, 7, 14, 21, 28, 35

; row * 128 for rows 0..5 (one 16-bit entry per row)
row_x128_lo:
        .byte $00, $80, $00, $80, $00, $80
row_x128_hi:
        .byte $00, $00, $01, $01, $02, $02

; VDP register setup (Graphics I, standard layout)
vdp_regs:
        .byte $00, $C0, $06, $80, $00, $36, $07, $01

; --- Disc pattern (32x32 pixels split into 16 x 8x8 glyphs) ---
; The disc is a 3x-scaled-up 8x8 classic disc shape:
;   8x8 base:  ..XXXX..    ->  rows 0-3:  cols 8-23  on
;              .XXXXXX.         rows 4-7:  cols 4-27  on
;              XXXXXXXX         rows 8-23: cols 0-31  on (full)
;              XXXXXXXX         rows 24-27:cols 4-27  on
;              XXXXXXXX         rows 28-31:cols 8-23  on
;              XXXXXXXX
;              .XXXXXX.
;              ..XXXX..
;
; Glyphs are written in row-major order (super-row, super-col):
;   TL  T1  T2  TR     (super-row 0, super-cols 0..3 — rows 0-7)
;   ML  M1  M2  MR     (super-row 1 — rows 8-15)
;   NL  N1  N2  NR     (super-row 2 — rows 16-23)
;   BL  B1  B2  BR     (super-row 3 — rows 24-31)
disc_pattern:
        ; Super-row 0 (rows 0-7): top arc, narrow
        .byte $00,$00,$00,$00,$0F,$0F,$0F,$0F       ; TL (byte col 0)
        .byte $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF       ; T1 (byte col 1)
        .byte $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF       ; T2 (byte col 2)
        .byte $00,$00,$00,$00,$F0,$F0,$F0,$F0       ; TR (byte col 3)
        ; Super-row 1 (rows 8-15): solid fill
        .byte $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF       ; ML
        .byte $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF       ; M1
        .byte $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF       ; M2
        .byte $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF       ; MR
        ; Super-row 2 (rows 16-23): solid fill
        .byte $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF       ; NL
        .byte $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF       ; N1
        .byte $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF       ; N2
        .byte $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF       ; NR
        ; Super-row 3 (rows 24-31): bottom arc
        .byte $0F,$0F,$0F,$0F,$00,$00,$00,$00       ; BL
        .byte $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF       ; B1
        .byte $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF       ; B2
        .byte $F0,$F0,$F0,$F0,$00,$00,$00,$00       ; BR

; --- Colour groups (7 entries, fg<<4 | bg) ---
tile_colors:
        .byte $11       ; Group 0 outside       fg=1 black, bg=1 black (invisible)
        .byte $14       ; Group 1 empty slot    fg=1 black hole, bg=4 blue
        .byte $14       ; Group 2 empty cont.   same (for BR glyph at char 16)
        .byte $84       ; Group 3 red piece     fg=8 medium red, bg=4 blue
        .byte $84       ; Group 4 red cont.     same (for BR glyph at char 32)
        .byte $B4       ; Group 5 yellow piece  fg=11 light yellow, bg=4 blue
        .byte $B4       ; Group 6 yellow cont.  same (for BR glyph at char 48)

; --- Strings ---
str_title:
        .byte $0D, " * CONNECT 4 *  TMS9918", $0D
        .byte " BY VERHILLE ARNAUD, 2026", $0D
        .byte $0D
        .byte " 32x32-PIXEL PIECES ON BLUE", $0D
        .byte " BOARD.  DROP WITH 1-7.", $0D
        .byte " ALIGN 4 TO WIN.  R=RESTART.", $0D
        .byte $0D, " PRESS ANY KEY TO START...", $0D, 0

str_red_turn:
        .byte $0D, " RED'S TURN - 1-7 TO DROP", $0D, 0
str_yellow_turn:
        .byte $0D, " YELLOW'S TURN - 1-7 TO DROP", $0D, 0
str_red_wins:
        .byte $0D, " **** RED WINS! **** KEY TO RESTART", $0D, 0
str_yellow_wins:
        .byte $0D, " *** YELLOW WINS! *** KEY TO RESTART", $0D, 0
str_draw:
        .byte $0D, " *** DRAW. *** KEY TO RESTART", $0D, 0
