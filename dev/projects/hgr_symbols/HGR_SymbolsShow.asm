; =============================================================================
; HGR_SymbolsShow.asm  --  display the 23 SCROLL-O-SPRITES "Symbols" sprites
; on Uncle Bernie's GEN2 Color Graphics Card
; -----------------------------------------------------------------------------
; Source data: dev/lib/tms9918/sprites_symbols.asm  (23 x 16x16 TMS9918 sprites)
; Pre-rendered for GEN2 HGR by _gen_sprites_symbols_hgr.py into
; sprites_symbols_hgr.inc (48 B per sprite, 16 rows x 3 bytes).
;
; Layout: 6 columns x 4 rows grid (24 slots, 23 used). Each slot is
; 4 HGR bytes (3 sprite + 1 gap) wide, 22 scanlines tall (16 sprite + 6 gap).
; Grid centred horizontally; top margin = 36 px.
;
; Build (from this directory):
;   make
;
; Run (POM1 with GEN2 enabled, preset that includes HGR):
;   File > Load Memory > software/hgr/HGR_SymbolsShow.txt   then:   280R
; =============================================================================

ECHO    = $FFEF
KBDCR   = $D011
KBD     = $D010

GRID_COLS    = 6
GRID_ROWS    = 4
GRID_X0      = 8                    ; HGR byte column of left edge
GRID_Y0      = 36                   ; top scanline of first row
SLOT_X_PITCH = 4                    ; bytes per slot horizontally (3 sprite + 1 gap)
SLOT_Y_PITCH = 22                   ; scanlines per slot (16 sprite + 6 gap)

.zeropage
            .res 2                  ; leave $00..$01 reserved
cur_x:      .res 1
cur_y:      .res 1
ptr_lo:     .res 1                  ; HGR scanline pointer
ptr_hi:     .res 1
fpl:        .res 1                  ; sprite data pointer
fph:        .res 1
hcol:       .res 1                  ; current slot's left HGR byte
top_y:      .res 1                  ; current slot's top scanline
gline:      .res 1                  ; current row within sprite (0..15)
sm_idx:     .res 1                  ; sprite index (0..22)
slot_col:   .res 1                  ; current slot column (0..GRID_COLS-1)
slot_row:   .res 1                  ; current slot row (0..GRID_ROWS-1)

.code

main:
        LDA #<str_title
        LDX #>str_title
        JSR print_str_ax

        JSR clear_hgr

        ; --- iterate slots row-major; stop after HGR_SYMBOL_GLYPH_COUNT ---
        LDA #0
        STA sm_idx
        STA slot_row
@row_lp:
        LDA #0
        STA slot_col
@col_lp:
        ; compute hcol = GRID_X0 + slot_col * SLOT_X_PITCH
        LDA slot_col
        ASL                         ; *2
        ASL                         ; *4
        CLC
        ADC #GRID_X0
        STA hcol

        ; compute top_y = GRID_Y0 + slot_row * SLOT_Y_PITCH (22 = 16 + 4 + 2)
        LDA slot_row
        ASL                         ; *2
        STA top_y                   ; tmp = row*2
        ASL                         ; row*4
        ASL                         ; row*8
        ASL                         ; row*16
        CLC
        ADC top_y                   ; row*16 + row*2 = row*18
        CLC
        ADC top_y
        CLC
        ADC top_y                   ; row*18 + 2*row*2 = row*22
        CLC
        ADC #GRID_Y0
        STA top_y

        ; draw current sprite
        LDA sm_idx
        JSR draw_symbol

        ; advance sprite index, stop when all rendered
        INC sm_idx
        LDA sm_idx
        CMP #HGR_SYMBOL_GLYPH_COUNT
        BCS @done

        ; advance slot col / row
        INC slot_col
        LDA slot_col
        CMP #GRID_COLS
        BCC @col_lp
        INC slot_row
        LDA slot_row
        CMP #GRID_ROWS
        BCC @row_lp

@done:
        LDA #<str_footer
        LDX #>str_footer
        JSR print_str_ax

@wait:
        LDA KBDCR
        BPL @wait
        LDA KBD
        RTS


; -----------------------------------------------------------------------------
; draw_symbol -- A = sprite index (0..22). Reads hcol, top_y from zp.
;   Reads 16 rows x 3 bytes from hgr_symbols_data + idx*48 and stores them
;   at (hgr_row[top_y + r] + hcol .. + hcol + 2) for r in 0..15.
; Clobbers: A, X, Y, ptr_lo/hi, fpl/fph, gline.
; -----------------------------------------------------------------------------
draw_symbol:
        ; fp = hgr_symbols_data + A*48 = A*32 + A*16
        ;   = (A << 5) + (A << 4)
        TAX
        STX gline                   ; reuse gline as scratch for low partial

        ; Build A*48 in 16 bits: low = (A*48) & $FF, high = (A*48) >> 8
        ; Easiest: multiply via repeated addition (idx <= 22, so 22*48 = 1056).
        LDA #0
        STA fpl
        STA fph
        TXA                         ; A = idx
        BEQ @off_done               ; idx 0 -> offset 0
@add_lp:
        LDA fpl
        CLC
        ADC #48
        STA fpl
        BCC @no_carry
        INC fph
@no_carry:
        DEX
        BNE @add_lp
@off_done:
        ; fp += hgr_symbols_data
        LDA fpl
        CLC
        ADC #<hgr_symbols_data
        STA fpl
        LDA fph
        ADC #>hgr_symbols_data
        STA fph

        LDA #0
        STA gline
@line:
        LDA gline
        CLC
        ADC top_y
        TAX
        LDA hgr_lo,X
        STA ptr_lo
        LDA hgr_hi,X
        STA ptr_hi

        ; read 3 sprite bytes via Y=0/1/2, push, then write at hcol/+1/+2
        LDY #2
        LDA (fpl),Y
        PHA
        DEY
        LDA (fpl),Y
        PHA
        DEY
        LDA (fpl),Y
        PHA

        LDY hcol
        PLA
        STA (ptr_lo),Y              ; byte 0 (cols 0..6)
        INY
        PLA
        STA (ptr_lo),Y              ; byte 1 (cols 7..13)
        INY
        PLA
        STA (ptr_lo),Y              ; byte 2 (cols 14..15)

        ; advance sprite pointer by 3 (next row)
        LDA fpl
        CLC
        ADC #3
        STA fpl
        BCC @no_carry2
        INC fph
@no_carry2:
        INC gline
        LDA gline
        CMP #16
        BCC @line
        RTS


; -----------------------------------------------------------------------------
; print_str_ax (mutualised) and HGR scanline LUTs.
; -----------------------------------------------------------------------------
.include "print.asm"

str_title:
        .byte $0D
        .byte " * SCROLL-O-SPRITES SYMBOLS ON GEN2 *", $0D
        .byte " 23 sprites x 16x16 -- Quale CC-BY-3.0", $0D, 0

str_footer:
        .byte $0D
        .byte " AT  ARROW UP NE  PLUS  X  HEART  STAR", $0D
        .byte " TARGET  MOON  EYE  WARNING  NOTE  FIRE", $0D
        .byte " SPARKLE  DROP  LIGHTNING  SPIRAL", $0D
        .byte " POTION  ZZZ  SKULL  SWORDS  SHIELD", $0D
        .byte " HOURGLASS         press any key", $0D, 0

.include "sprites_symbols_hgr.inc"
.include "hgr_tables.inc"
