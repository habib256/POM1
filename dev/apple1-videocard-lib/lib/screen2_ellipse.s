;
; screen2_ellipse.s — axis-aligned ellipse outline (64-segment polyline)
; 8×8 unsigned multiply (shift-and-add, same idea as dev/lib/m6502/multiply.asm).
; Calls C screen2_line for each edge.
;
.importzp       sp
.import         pusha, incsp4, decsp3
.import         _screen2_line
.export         _screen2_ellipse_rect

.segment        "RODATA"
ell_cos_tab:
        .byte   127,126,125,122,117,112,106,98,90,81,71,60,49,37,25,12
        .byte   0,244,231,219,207,196,185,175,166,158,150,144,139,134,131,130
        .byte   129,130,131,134,139,144,150,158,166,175,185,196,207,219,231,244
        .byte   0,12,25,37,49,60,71,81,90,98,106,112,117,122,125,126

ell_sin_tab:
        .byte   0,12,25,37,49,60,71,81,90,98,106,112,117,122,125,126,127
        .byte   126,125,122,117,112,106,98,90,81,71,60,49,37,25,12,0
        .byte   244,231,219,207,196,185,175,166,158,150,144,139,134,131,130
        .byte   129,130,131,134,139,144,150,158,166,175,185,196,207,219,231,244

.segment        "BSS"
ell_x0:         .res    1
ell_y0:         .res    1
ell_x1:         .res    1
ell_y1:         .res    1
ell_xc:         .res    1
ell_yc:         .res    1
ell_rx:         .res    1
ell_ry:         .res    1
ell_px:         .res    1
ell_py:         .res    1
ell_prevx:      .res    1
ell_prevy:      .res    1
ell_fstx:       .res    1
ell_fsty:       .res    1
ell_i:          .res    1
ell_mt:         .res    1
ell_mr:         .res    1
ell_acc0:       .res    1
ell_acc1:       .res    1
ell_dx0:        .res    1
ell_dx1:        .res    1
ell_dy0:        .res    1
ell_dy1:        .res    1
ell_t0:         .res    1
ell_t1:         .res    1

.segment        "CODE"

; A×X → ell_acc0 (lo), ell_acc1 (hi), unsigned
mul8u:
        sta     ell_mt
        stx     ell_mr
        lda     #0
        ldx     #8
        lsr     ell_mr
@ml:    bcc     @mn
        clc
        adc     ell_mt
@mn:    ror     a
        ror     ell_mr
        dex
        bne     @ml
        tax
        lda     ell_mr
        sta     ell_acc0
        stx     ell_acc1
        rts

shr7_16:
        ldy     #7
@s7:    lsr     ell_acc1
        ror     ell_acc0
        dey
        bne     @s7
        rts

neg16acc:
        sec
        lda     #0
        sbc     ell_acc0
        sta     ell_acc0
        lda     #0
        sbc     ell_acc1
        sta     ell_acc1
        rts

; cos byte in A, use ell_rx → signed delta in ell_dx0/1, ÷128
scale_rx:
        sta     ell_t0
        lda     ell_t0
        bpl     @pc
        eor     #$FF
        clc
        adc     #1
@pc:    tax
        lda     ell_rx
        jsr     mul8u
        jsr     shr7_16
        lda     ell_t0
        bpl     @dn
        jsr     neg16acc
@dn:    lda     ell_acc0
        sta     ell_dx0
        lda     ell_acc1
        sta     ell_dx1
        rts

; sin byte in A, use ell_ry → ell_dy0/1
scale_ry:
        sta     ell_t0
        lda     ell_t0
        bpl     @pc
        eor     #$FF
        clc
        adc     #1
@pc:    tax
        lda     ell_ry
        jsr     mul8u
        jsr     shr7_16
        lda     ell_t0
        bpl     @dn
        jsr     neg16acc
@dn:    lda     ell_acc0
        sta     ell_dy0
        lda     ell_acc1
        sta     ell_dy1
        rts

; ell_px = clamp(xc + signed16 in ell_dx*)
add_clamp_x:
        lda     #0
        sta     ell_t1
        lda     ell_xc
        sta     ell_t0
        clc
        lda     ell_t0
        adc     ell_dx0
        sta     ell_acc0
        lda     ell_t1
        adc     ell_dx1
        sta     ell_acc1
        lda     ell_acc1
        bmi     @z
        bne     @ff
        lda     ell_acc0
        jmp     @st
@z:     lda     #0
        beq     @st
@ff:    lda     #$FF
@st:    sta     ell_px
        rts

; ell_py = clamp(yc + ell_dy*)
add_clamp_y:
        lda     #0
        sta     ell_t1
        lda     ell_yc
        sta     ell_t0
        clc
        lda     ell_t0
        adc     ell_dy0
        sta     ell_acc0
        lda     ell_t1
        adc     ell_dy1
        sta     ell_acc1
        lda     ell_acc1
        bmi     @z
        bne     @ff
        lda     ell_acc0
        jmp     @st
@z:     lda     #0
        beq     @st
@ff:    lda     #$FF
@st:    sta     ell_py
        rts

; screen2_line(prevx, prevy, px, py)
do_line:
        jsr     decsp3
        lda     ell_prevx
        ldy     #2
        sta     (sp),y
        lda     ell_prevy
        ldy     #1
        sta     (sp),y
        lda     ell_px
        ldy     #0
        sta     (sp),y
        lda     ell_py
        jsr     _screen2_line
        rts

.proc   _screen2_ellipse_rect: near

        jsr     pusha

        ldy     #3
        lda     (sp),y
        sta     ell_x0
        dey
        lda     (sp),y
        sta     ell_y0
        dey
        lda     (sp),y
        sta     ell_x1
        dey
        lda     (sp),y
        sta     ell_y1

        ; xc = (x0+x1)/2
        lda     ell_x0
        clc
        adc     ell_x1
        sta     ell_t0
        lda     #0
        adc     #0
        sta     ell_t1
        lsr     ell_t1
        ror     ell_t0
        lda     ell_t0
        sta     ell_xc

        ; yc = (y0+y1)/2
        lda     ell_y0
        clc
        adc     ell_y1
        sta     ell_t0
        lda     #0
        adc     #0
        sta     ell_t1
        lsr     ell_t1
        ror     ell_t0
        lda     ell_t0
        sta     ell_yc

        ; rx = abs(x1-x0)/2
        lda     ell_x1
        sec
        sbc     ell_x0
        bcs     @rxp
        eor     #$FF
        clc
        adc     #1
@rxp:   lsr     a
        sta     ell_rx

        ; ry = abs(y1-y0)/2
        lda     ell_y1
        sec
        sbc     ell_y0
        bcs     @ryp
        eor     #$FF
        clc
        adc     #1
@ryp:   lsr     a
        sta     ell_ry

        lda     #0
        sta     ell_i

@loop:  ldx     ell_i
        lda     ell_cos_tab,x
        jsr     scale_rx
        lda     ell_sin_tab,x
        jsr     scale_ry
        jsr     add_clamp_x
        jsr     add_clamp_y

        lda     ell_i
        beq     @first

        jsr     do_line
        jmp     @adv

@first: lda     ell_px
        sta     ell_prevx
        sta     ell_fstx
        lda     ell_py
        sta     ell_prevy
        sta     ell_fsty

@adv:   lda     ell_px
        sta     ell_prevx
        lda     ell_py
        sta     ell_prevy

        inc     ell_i
        lda     ell_i
        cmp     #64
        bne     @loop

        ; close polygon: last point already in prev; draw to first
        lda     ell_fstx
        sta     ell_px
        lda     ell_fsty
        sta     ell_py
        jsr     do_line

        jmp     incsp4

.endproc
