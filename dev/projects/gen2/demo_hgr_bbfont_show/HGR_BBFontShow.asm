; =============================================
; HGR CP437 FONT SHOWCASE (bbfont_cp437.inc)
; GEN2 Color Graphics Card
; VERHILLE Arnaud - 2026
; =============================================
; Displays all 256 code points from fonts/font_codepage_437_8x8.png
; (linear CP437 order, index = IBM code point).
;
; Assemble with cc65:
;   Build: make
;
; In POM1: enable GEN2, File > Load Memory (HGR_BBFontShow.txt),
; then type E000R in Woz Monitor.
;
; Grid: 16 columns × 16 rows. Each cell 14×16 px (7-bit glyph + pad byte).
; Row pitch 10 scanlines (fits 192 lines).
; Loop exit: gidx wraps 255→0 (8-bit), so use BNE after INC, not CMP #256.
; =============================================

.include "apple1.inc"

; --- Zero page ---
.zeropage
            .res 2
cur_x:      .res 1
cur_y:      .res 1
ptr_lo:     .res 1
ptr_hi:     .res 1
gidx:       .res 1
cellcol:    .res 1
fpl:        .res 1
fph:        .res 1
cline:      .res 1
row_tmp:    .res 1
mul_tmp:    .res 1
mul_res0:   .res 1

.code

main:
        LDA #<str_title
        LDX #>str_title
        JSR print_str_ax

        JSR clear_hgr

        LDA #$00
        STA gidx

@show:  LDA gidx
        AND #$0F
        STA cellcol             ; column 0..15
        LDA gidx
        LSR A
        LSR A
        LSR A
        LSR A
        TAX                     ; row 0..15
        LDA row_y_base,X
        STA cur_y

        LDA gidx
        LDX cellcol
        JSR draw_glyph_cell

        INC gidx
        BNE @show               ; 256 glyphs: stop when gidx wraps 255→0

        LDA #<str_footer
        LDX #>str_footer
        JSR print_str_ax

@wait:  LDA KBDCR
        BPL @wait
        LDA KBD
        RTS

; --- print_str_ax — promoted to dev/lib/apple1/print.asm (Tier 2 mutualization). ---
.include "print.asm"

; --- Draw one glyph: A = CP437 index 0..255, X = column 0..15, cur_y = top ---
draw_glyph_cell:
        STA gidx
        STX cellcol

        LDA #$00
        STA fph
        LDA gidx
        ASL A
        ROL fph
        ASL A
        ROL fph
        ASL A
        ROL fph
        CLC
        ADC #<HGR_BBFont
        STA fpl
        LDA fph
        ADC #>HGR_BBFont
        STA fph

        LDX #$00
@sc:    STX cline
        TXA
        CLC
        ADC cur_y
        TAY
        LDA hgr_lo,Y
        STA ptr_lo
        LDA hgr_hi,Y
        STA ptr_hi

        LDY cline
        CPY #$08
        BCS @blank
        LDA (fpl),Y
        JMP @writ
@blank: LDA #$00
@writ:  PHA
        LDA cellcol
        ASL A
        TAY
        PLA
        STA (ptr_lo),Y
        INY
        LDA #$00
        STA (ptr_lo),Y

        LDX cline
        INX
        CPX #$10
        BCC @sc
        RTS

; --- Top scanline for each of 16 rows (pitch 10, starts at line 4) ---
row_y_base:
        .byte   4,  14,  24,  34,  44,  54,  64,  74
        .byte  84,  94, 104, 114, 124, 134, 144, 154

str_title:
        .byte $0D
        .byte " * HGR CP437 GEN2 *", $0D
        .byte " 256 CHARS (PNG ORDER 0-FF)", $0D, 0

str_footer:
        .byte $0D
        .byte " CP437 IBM PC ORDER — ANY KEY", $0D, 0

.include "bbfont_cp437.inc"
.include "hgr_tables.inc"
.include "multiply.asm"
