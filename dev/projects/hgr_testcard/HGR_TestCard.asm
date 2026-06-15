; =============================================
; HGR NTSC COLOR TEST CARD
; GEN2 Color Graphics Card
; VERHILLE Arnaud - 2026
; Displays all 6 NTSC artifact colors
; =============================================
; Assemble with cc65:
;   ca65 -o build/HGR_TestCard.o software/hgr/HGR_TestCard.asm
;   ld65 -C software/hgr/apple1_gen2.cfg -o build/HGR_TestCard.bin build/HGR_TestCard.o
;
; In POM1: plug GEN2 card, File > Load Memory (HGR_TestCard.txt)
; then type E000R in Woz Monitor.
;
; Shows 8 vertical bands (5 byte columns each):
;   BLACK | VIOLET | GREEN | WHITE | BLUE | ORANGE | WHITE | BLACK
; =============================================

; --- Apple 1 I/O ---
.include "apple1.inc"

; --- Zero page variables ---
.zeropage
            .res 2      ; $00-$01 (unused, keep aligned with hgr_tables.inc)
cur_x:      .res 1      ; $02  (required by hgr_tables.inc)
cur_y:      .res 1      ; $03  (used as scanline counter here)
ptr_lo:     .res 1      ; $04  (required by hgr_tables.inc)
ptr_hi:     .res 1      ; $05  (required by hgr_tables.inc)

; --- Code at $E000 ---
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

@wait:  LDA KBDCR
        BPL @wait
        LDA KBD
        RTS

; =============================================
; FILL TEST CARD: write color bars to all 192 scanlines
; Uses hgr_lo/hgr_hi tables from hgr_tables.inc
; =============================================
fill_test_card:
        LDA #$00
        STA cur_y

@line:  LDX cur_y
        LDA hgr_lo,X
        STA ptr_lo
        LDA hgr_hi,X
        STA ptr_hi

        LDY #39
@col:   LDA color_table,Y
        STA (ptr_lo),Y
        DEY
        BPL @col

        INC cur_y
        LDA cur_y
        CMP #192
        BNE @line
        RTS

; =============================================
; print_str_ax — promoted to dev/lib/apple1/print.asm (Tier 2 mutualization).
; =============================================
.include "print.asm"

; =============================================
; DATA: 40-byte color bar pattern
; =============================================
color_table:
        .byte $00, $00, $00, $00, $00           ; cols  0- 4: BLACK
        .byte $2A, $55, $2A, $55, $2A           ; cols  5- 9: VIOLET
        .byte $2A, $55, $2A, $55, $2A           ; cols 10-14: GREEN
        .byte $7F, $7F, $7F, $7F, $7F           ; cols 15-19: WHITE (group 0)
        .byte $D5, $AA, $D5, $AA, $D5           ; cols 20-24: BLUE
        .byte $D5, $AA, $D5, $AA, $D5           ; cols 25-29: ORANGE
        .byte $FF, $FF, $FF, $FF, $FF           ; cols 30-34: WHITE (group 1)
        .byte $00, $00, $00, $00, $00           ; cols 35-39: BLACK

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

; =============================================
; HGR TABLES (shared include — only hgr_lo/hgr_hi used here)
; =============================================
.include "hgr_tables.inc"
