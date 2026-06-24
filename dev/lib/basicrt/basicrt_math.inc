; basicrt_math.inc -- card-agnostic 16-bit integer runtime for the native BASIC
; compiler. The including file must define (in ZEROPAGE): rt_a, rt_b (ABI), and
; the scratch m_prod, m_rem (2 bytes each) + m_sign (1 byte). CODE segment.

; Routines gated per feature flag (-D RT_xxx) so the build assembles only what the
; compiled program imports.

.if .defined(RT_PRINT) .or .defined(RT_PUTC) .or .defined(RT_PRINTCR)
.export rt_print, rt_printcr, rt_putc

ECHO := $FFEF           ; WOZ Monitor char output (present in POM1 at $FF00)

; rt_putc: A = char -> Apple-1 terminal (waits for DSP, writes $D012).
rt_putc:
        ora #$80
        jmp ECHO

; rt_printcr: newline.
rt_printcr:
        lda #$0D
        jmp rt_putc

; rt_print: print rt_a as a signed decimal integer.
rt_print:
        lda rt_a+1
        bpl @u
        lda #'-'
        jsr rt_putc
        sec
        lda #0
        sbc rt_a
        sta rt_a
        lda #0
        sbc rt_a+1
        sta rt_a+1
@u:     lda #$FF                ; stack sentinel
        pha
@dl:    jsr @div10              ; rt_a /= 10, A = remainder 0..9
        ora #'0'
        pha
        lda rt_a
        ora rt_a+1
        bne @dl
@pl:    pla
        cmp #$FF
        beq @end
        jsr rt_putc
        jmp @pl
@end:   rts
@div10:
        lda #0
        sta m_rem
        ldx #16
@l:     asl rt_a
        rol rt_a+1
        rol m_rem
        lda m_rem
        cmp #10
        bcc @s
        sbc #10
        sta m_rem
        inc rt_a
@s:     dex
        bne @l
        lda m_rem
        rts
.endif  ; PRINT/PUTC/PRINTCR

.ifdef RT_MUL
.export rt_mul
; rt_mul: rt_a = rt_a * rt_b  (low 16 bits; correct for signed two's complement).
rt_mul:
        lda #0
        sta m_prod
        sta m_prod+1
        ldx #16
@l:     lsr rt_b+1
        ror rt_b
        bcc @noadd
        clc
        lda m_prod
        adc rt_a
        sta m_prod
        lda m_prod+1
        adc rt_a+1
        sta m_prod+1
@noadd: asl rt_a
        rol rt_a+1
        dex
        bne @l
        lda m_prod
        sta rt_a
        lda m_prod+1
        sta rt_a+1
        rts

.endif  ; RT_MUL

.ifdef RT_DIV
.export rt_div
; rt_div: rt_a = rt_a / rt_b  (signed, truncate toward zero).
rt_div:
        lda rt_a+1
        eor rt_b+1
        sta m_sign
        lda rt_a+1
        bpl @aok
        sec
        lda #0
        sbc rt_a
        sta rt_a
        lda #0
        sbc rt_a+1
        sta rt_a+1
@aok:   lda rt_b+1
        bpl @bok
        sec
        lda #0
        sbc rt_b
        sta rt_b
        lda #0
        sbc rt_b+1
        sta rt_b+1
@bok:   lda #0
        sta m_rem
        sta m_rem+1
        ldx #16
@dl:    asl rt_a
        rol rt_a+1
        rol m_rem
        rol m_rem+1
        sec
        lda m_rem
        sbc rt_b
        tay
        lda m_rem+1
        sbc rt_b+1
        bcc @skip
        sta m_rem+1
        sty m_rem
        inc rt_a
@skip:  dex
        bne @dl
        lda m_sign
        bpl @done
        sec
        lda #0
        sbc rt_a
        sta rt_a
        lda #0
        sbc rt_a+1
        sta rt_a+1
@done:  rts

.endif  ; RT_DIV

.ifdef RT_CMP16
.export rt_cmp16
; rt_cmp16: signed compare rt_a vs rt_b -> A = 0 (a<b) / 1 (a==b) / 2 (a>b).
rt_cmp16:
        lda rt_a
        cmp rt_b
        bne @ne
        lda rt_a+1
        cmp rt_b+1
        bne @ne
        lda #1
        rts
@ne:    sec
        lda rt_a
        sbc rt_b
        lda rt_a+1
        sbc rt_b+1
        bvc @nov
        eor #$80
@nov:   bmi @less
        lda #2
        rts
@less:  lda #0
        rts
.endif  ; RT_CMP16
