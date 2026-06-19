; HELLO WORLD - GEN2 HIRES with the Beautiful Boot 8x8 font.
; DevBench target: Uncle Bernie GEN2 HGR (asm), entry $E000.

.include "gen2.inc"

TOP_ROW = 88
START_X = 42
STRIDE  = 18

.zeropage
cur_x:   .res 1
cur_y:   .res 1
ptr_lo:  .res 1
ptr_hi:  .res 1
src_lo:  .res 1
src_hi:  .res 1
gx:      .res 1
px:      .res 1
line:    .res 1
chidx:   .res 1
rowbits: .res 1
tmp:     .res 1

.code

start:
    bit GEN2_TEXTOFF
    bit GEN2_HIRES
    bit GEN2_PAGE1
    bit GEN2_MIXOFF
    jsr clear_hgr
    lda #START_X
    sta gx
    lda #$00
    sta chidx

next_ch:
    ldx chidx
    lda message,x
    beq done
    and #$7F
    sta tmp
    lda #$00
    sta src_hi
    lda tmp
    asl a
    rol src_hi
    asl a
    rol src_hi
    asl a
    rol src_hi
    clc
    adc #<HGR_BBFont
    sta src_lo
    lda src_hi
    adc #>HGR_BBFont
    sta src_hi
    lda #$00
    sta line

rowloop:
    lda line
    asl a
    clc
    adc #TOP_ROW
    sta cur_y
    jsr draw_row
    inc cur_y
    jsr draw_row
    inc line
    lda line
    cmp #$08
    bne rowloop
    lda gx
    clc
    adc #STRIDE
    sta gx
    inc chidx
    jmp next_ch

done:
    jmp *

draw_row:
    ldy line
    lda (src_lo),y
    sta rowbits
    lda gx
    sta px
    ldx #$08
bitloop:
    lsr rowbits
    bcc skip_pixel
    lda px
    sta cur_x
    jsr plot_pixel
    inc cur_x
    jsr plot_pixel
skip_pixel:
    lda px
    clc
    adc #$02
    sta px
    dex
    bne bitloop
    rts

message:
    .byte "HELLO WORLD", 0

.include "bbfont_cp437.inc"
.include "hgr_tables.inc"

