; =============================================
; HGR NTSC COLOR TEST CARD
; GEN2 Color Graphics Card
; VERHILLE Arnaud - 2026
; Displays all 6 NTSC artifact colors
; =============================================
; Assemble with cc65:
;   ca65 -o build/HGR3_TestCard.o software/hgr/HGR3_TestCard.asm
;   ld65 -C software/hgr/apple1_gen2.cfg -o build/HGR3_TestCard.bin build/HGR3_TestCard.o
;
; In POM1: plug GEN2 card, File > Load Memory (HGR3_TestCard.txt)
; then type 280R in Woz Monitor.
;
; Shows 8 vertical bands (5 byte columns each):
;   BLACK | VIOLET | GREEN | WHITE | BLUE | ORANGE | WHITE | BLACK
;
; NTSC artifact color is determined by pixel screen-X
; parity and the byte's bit 7 (color group selector).
; Same byte values produce different colors at
; different column positions — this is the essence
; of Apple II NTSC artifact color.
; =============================================

; --- Apple 1 I/O ---
ECHO    = $FFEF
KBDCR   = $D011
KBD     = $D010

; --- Zero page variables ---
.zeropage
ptr_lo:     .res 1      ; $00
ptr_hi:     .res 1      ; $01
scanline:   .res 1      ; $02
temp:       .res 1      ; $03

; --- Code at $0280 ---
.code

; =============================================
; MAIN
; =============================================
main:
        LDA #<str_title
        LDX #>str_title
        JSR print_str_ax

        JSR fill_test_card

        LDA #<str_legend
        LDX #>str_legend
        JSR print_str_ax

        ; Wait for keypress
@wait:  LDA KBDCR
        BPL @wait
        LDA KBD
        RTS                 ; return to Woz Monitor

; =============================================
; FILL TEST CARD: write color bars to all 192 scanlines
; =============================================
fill_test_card:
        LDA #$00
        STA scanline

@line:  ; Compute scanline base address
        JSR calc_scanline

        ; Copy 40-byte color pattern to this scanline
        LDY #39
@col:   LDA color_table,Y
        STA (ptr_lo),Y
        DEY
        BPL @col

        INC scanline
        LDA scanline
        CMP #192
        BNE @line
        RTS

; =============================================
; CALC SCANLINE: base addr for 'scanline' variable
; addr = $2000 + (y%8)*$400 + ((y/8)%8)*$80 + (y/64)*$28
; =============================================
calc_scanline:
        LDA #$00
        STA ptr_lo
        LDA #$20
        STA ptr_hi

        ; (y % 8) * $0400
        LDA scanline
        AND #$07
        ASL A
        ASL A
        CLC
        ADC ptr_hi
        STA ptr_hi

        ; ((y / 8) % 8) * $80
        LDA scanline
        LSR A
        LSR A
        LSR A
        AND #$07
        LSR A               ; A = val/2, carry = val bit 0
        TAX
        LDA #$00
        BCC @no80
        LDA #$80
@no80:  CLC
        ADC ptr_lo
        STA ptr_lo
        TXA
        ADC ptr_hi
        STA ptr_hi

        ; Group offset
        LDA scanline
        CMP #128
        BCS @g2
        CMP #64
        BCS @g1
        RTS
@g1:    LDA ptr_lo
        CLC
        ADC #$28
        STA ptr_lo
        BCC @gd
        INC ptr_hi
@gd:    RTS
@g2:    LDA ptr_lo
        CLC
        ADC #$50
        STA ptr_lo
        BCC @gd2
        INC ptr_hi
@gd2:   RTS

; =============================================
; Print null-terminated string (A=lo, X=hi)
; =============================================
print_str_ax:
        STA ptr_lo
        STX ptr_hi
        LDY #$00
@lp:    LDA (ptr_lo),Y
        BEQ @dn
        ORA #$80
        JSR ECHO
        INY
        BNE @lp
@dn:    RTS

; =============================================
; DATA: 40-byte color bar pattern
; =============================================
; Each band is 5 byte columns (35 pixels) wide.
; Alternating-pixel patterns ($55/$2A) produce
; isolated dots that the GEN2 renders as NTSC
; artifact colors. The color depends on whether
; the pixel's screen-X position is even or odd.

color_table:
        ; Cols  0- 4: BLACK
        .byte $00, $00, $00, $00, $00
        ; Cols  5- 9: VIOLET (group 0, even screen X)
        .byte $2A, $55, $2A, $55, $2A
        ; Cols 10-14: GREEN  (group 0, odd screen X)
        .byte $2A, $55, $2A, $55, $2A
        ; Cols 15-19: WHITE  (group 0, all pixels on)
        .byte $7F, $7F, $7F, $7F, $7F
        ; Cols 20-24: BLUE   (group 1, even screen X)
        .byte $D5, $AA, $D5, $AA, $D5
        ; Cols 25-29: ORANGE (group 1, odd screen X)
        .byte $D5, $AA, $D5, $AA, $D5
        ; Cols 30-34: WHITE  (group 1, all pixels on)
        .byte $FF, $FF, $FF, $FF, $FF
        ; Cols 35-39: BLACK
        .byte $00, $00, $00, $00, $00

; =============================================
; STRINGS
; =============================================
str_title:
        .byte $0D, " * HGR NTSC COLOR TEST *", $0D
        .byte " GEN2 COLOR GRAPHICS CARD", $0D, 0

str_legend:
        .byte $0D
        .byte " BLK VIO GRN WHT BLU ORG WHT BLK", $0D
        .byte " --- --- --- --- --- --- --- ---", $0D
        .byte " GRP0         GRP0 GRP1         GRP1", $0D
        .byte $0D
        .byte " PRESS ANY KEY TO EXIT", $0D, 0
