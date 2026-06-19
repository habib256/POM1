; HELLO WORLD on the P-LAB TMS9918 (Graphics I).
; DevBench target: P-LAB TMS9918 Graphic Card (asm), CodeTank entry $4000.

VDAT = $CC00
VCTL = $CC01

.segment "CODE"

start:
    ldx #0
load_regs:
    lda regs,x
    sta VCTL
    txa
    ora #$80
    sta VCTL
    inx
    cpx #8
    bne load_regs

    lda #$00
    sta VCTL
    lda #$40
    sta VCTL
    lda #$00
    ldy #64
clear_outer:
    ldx #0
clear_inner:
    sta VDAT
    inx
    bne clear_inner
    dey
    bne clear_outer

    lda #$00
    sta VCTL
    lda #($1B | $40)
    sta VCTL
    lda #$D0
    sta VDAT

    lda #$00
    sta VCTL
    lda #$40
    sta VCTL
    ldx #0
load_font:
    lda font,x
    sta VDAT
    inx
    cpx #64
    bne load_font

    lda #$00
    sta VCTL
    lda #($20 | $40)
    sta VCTL
    lda #$F4
    ldx #32
load_color:
    sta VDAT
    dex
    bne load_color

    lda #$00
    sta VCTL
    lda #($18 | $40)
    sta VCTL
    ldx #0
load_message:
    lda message,x
    cmp #$FF
    beq enable_screen
    sta VDAT
    inx
    bne load_message

enable_screen:
    lda #$C0
    sta VCTL
    lda #($80 | 1)
    sta VCTL
done:
    jmp done

regs:
    .byte $00,$80,$06,$80,$00,$36,$07,$04

font:
    .byte $00,$00,$00,$00,$00,$00,$00,$00
    .byte $00,$44,$44,$7C,$44,$44,$44,$00
    .byte $00,$7C,$40,$78,$40,$40,$7C,$00
    .byte $00,$40,$40,$40,$40,$40,$7C,$00
    .byte $00,$38,$44,$44,$44,$44,$38,$00
    .byte $00,$44,$44,$44,$54,$54,$28,$00
    .byte $00,$78,$44,$44,$78,$48,$44,$00
    .byte $00,$78,$44,$44,$44,$44,$78,$00

message:
    .byte 1,2,3,3,4,0,5,4,6,3,7,$FF

