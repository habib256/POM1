; =============================================
; HGR 28x28 SMILEY SHOWCASE (smiley.inc)
; GEN2 Color Graphics Card
; =============================================
; Glyphes 28x28 : grille 14x14 agrandie x2 + renfort blanc HGR.
; 6 x 4 octets = 24 octets par rangée ; marge col 2 → tient dans 40.
;
; ca65 / ld65 (+ dump Woz, depuis la racine du depot):
;   ca65 -I software/hgr -o build/HGR9_Smiley16Show.o software/hgr/HGR9_Smiley16Show.asm
;   ld65 -C software/hgr/apple1_gen2.cfg -o build/HGR9_Smiley16Show.bin build/HGR9_Smiley16Show.o
;   python3 software/hgr/emit_HGR9_Smiley16Show_txt.py
;
; POM1: carte GEN2, File > Load Memory (HGR9_Smiley16Show.txt), puis 280R.
; =============================================

ECHO    = $FFEF
KBDCR   = $D011
KBD     = $D010

.zeropage
            .res 2
cur_x:      .res 1
cur_y:      .res 1
ptr_lo:     .res 1
ptr_hi:     .res 1
fpl:        .res 1
fph:        .res 1
hcol:       .res 1
gline:      .res 1
sm_idx:     .res 1
mul_tmp:    .res 1
mul_res0:   .res 1

.code

main:
        LDA #<str_title
        LDX #>str_title
        JSR print_str_ax

        JSR clear_hgr

        LDA #82 ; (192-28)/2
        STA cur_y
        LDA #2
        STA hcol

        LDA #$00
        STA sm_idx

@row: LDA sm_idx
        JSR draw_smiley

        LDA hcol
        CLC
        ADC #5                  ; 4 octets + 1 espace
        STA hcol

        INC sm_idx
        LDA sm_idx
        CMP #HGR_SMILEY_GLYPH_COUNT
        BCC @row

        LDA #<str_footer
        LDX #>str_footer
        JSR print_str_ax

@wait:  LDA KBDCR
        BPL @wait
        LDA KBD
        RTS

; --- A = index 0..5, cur_y, hcol ---
draw_smiley:
        TAY
        LDA smiley_off_lo,Y
        CLC
        ADC #<hgr_smiley28_font
        STA fpl
        LDA smiley_off_hi,Y
        ADC #>hgr_smiley28_font
        STA fph

        LDY #$00
        STY gline

@line:  LDY gline
        TYA
        CLC
        ADC cur_y
        TAX
        LDA hgr_lo,X
        STA ptr_lo
        LDA hgr_hi,X
        STA ptr_hi

        LDY #3
        LDA (fpl),Y
        PHA
        DEY
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
        STA (ptr_lo),Y
        INY
        PLA
        STA (ptr_lo),Y
        INY
        PLA
        STA (ptr_lo),Y
        INY
        PLA
        STA (ptr_lo),Y

        LDA fpl
        CLC
        ADC #$04
        STA fpl
        BCC @incf
        INC fph
@incf:  INC gline
        LDA gline
        CMP #$1C                  ; 28
        BCC @line
        RTS

smiley_off_lo:
        .byte 0, $70, $E0, $50, $C0, $30
smiley_off_hi:
        .byte 0, 0, 0, 1, 1, 2

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

str_title:
        .byte $0D
        .byte " * HGR SMILEY 14x14 X2 GEN2 *", $0D
        .byte " DOUBLE + BRIDGE WHITE", $0D, 0

str_footer:
        .byte $0D
        .byte " GRIN LAUGH WINK NEUTRAL SAD SURPRISED - KEY", $0D, 0

.include "smiley.inc"
.include "hgr_tables.inc"
