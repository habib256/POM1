; basicrt_float.s -- standalone binary32 (IEEE-754 single) software float for the
; native BASIC compiler's FLOATING-POINT phase. NO Applesoft ROM: this is the
; autonomous FP runtime. Storage is 4-byte little-endian IEEE binary32. Internally
; a value is unpacked to {sign, E (signed 16-bit), SG (24-bit significand in
; [2^23, 2^24))} so that value = SG * 2^E, then repacked.
;
; ABI (operands in the zero-page slots FA / FB, result in FA):
;   fp_fromint16  : FA.lo16 (signed int) -> FA (float)
;   fp_toint16    : FA (float) -> FA.lo16 (signed int, truncate toward zero)
;   fp_add/fp_sub : FA = FA +/- FB
;   fp_mul/fp_div : FA = FA */ FB
;   fp_cmp        : A = 0 (FA<FB) / 1 (==) / 2 (FA>FB)
;
; Tested against host `float` by tests/basic_float_runtime_test.cpp.

.setcpu "6502"
.export fp_fromint16, fp_toint16, fp_add, fp_sub, fp_mul, fp_div, fp_cmp
.exportzp FA, FB

.segment "ZEROPAGE"
FA:  .res 4          ; operand/result A (binary32)
FB:  .res 4          ; operand B

; unpacked A
asgn: .res 1         ; sign (0 or 1)
aexp: .res 2         ; E (signed 16-bit)
amnt: .res 4         ; significand (24-bit in amnt+0..+2, amnt+3 guard)
; unpacked B
bsgn: .res 1
bexp: .res 2
bmnt: .res 4
; result
rsgn: .res 1
rexp: .res 2
rmnt: .res 4
; scratch
fcnt: .res 1         ; shift counter
prod: .res 7         ; 48-bit product / dividend (+1 byte: intermediate carry)
dtmp: .res 4

.segment "CODE"

; ---- unpack: FA -> asgn/aexp/amnt (zero -> amnt all 0) ----------------------
unpackA:
        lda FA+3
        rol                     ; sign into carry
        lda #0
        rol
        sta asgn
        ; ef = (FA+3 << 1) | (FA+2 >> 7)
        lda FA+2
        asl                     ; bit7 -> carry
        lda FA+3
        rol                     ; A = (FA+3<<1)|carry = ef
        sta fcnt                ; ef
        beq @zero
        ; significand
        lda FA+0
        sta amnt+0
        lda FA+1
        sta amnt+1
        lda FA+2
        and #$7F
        ora #$80                ; implicit leading 1 at bit23
        sta amnt+2
        lda #0
        sta amnt+3
        ; E = ef - 150  (127 + 23)
        lda fcnt
        sec
        sbc #150
        sta aexp
        lda #$FF
        adc #0                  ; sign-extend (if borrow, hi=$FF else $00)
        ; ef in [1,255], ef-150 in [-149,105]; compute hi byte properly:
        ; redo with 16-bit
        lda fcnt
        sec
        sbc #150
        sta aexp
        lda #0
        sbc #0
        sta aexp+1
        rts
@zero:  lda #0
        sta amnt+0
        sta amnt+1
        sta amnt+2
        sta amnt+3
        sta aexp
        sta aexp+1
        rts

; ---- unpack FB -> bsgn/bexp/bmnt --------------------------------------------
unpackB:
        lda FB+3
        rol
        lda #0
        rol
        sta bsgn
        lda FB+2
        asl
        lda FB+3
        rol
        sta fcnt
        beq @zero
        lda FB+0
        sta bmnt+0
        lda FB+1
        sta bmnt+1
        lda FB+2
        and #$7F
        ora #$80
        sta bmnt+2
        lda #0
        sta bmnt+3
        lda fcnt
        sec
        sbc #150
        sta bexp
        lda #0
        sbc #0
        sta bexp+1
        rts
@zero:  lda #0
        sta bmnt+0
        sta bmnt+1
        sta bmnt+2
        sta bmnt+3
        sta bexp
        sta bexp+1
        rts

; ---- pack rsgn/rexp/rmnt -> FA  (rmnt normalized to [2^23,2^24) or zero) -----
; Assumes rmnt+3 == 0 and bit23 (rmnt+2 bit7) set, unless value is zero.
packA:
        lda rmnt+0
        ora rmnt+1
        ora rmnt+2
        ora rmnt+3
        bne @nz
        ; zero
        lda #0
        sta FA+0
        sta FA+1
        sta FA+2
        sta FA+3
        rts
@nz:    ; ef = E + 150
        lda rexp
        clc
        adc #150
        sta fcnt                ; ef low
        lda rexp+1
        adc #0                  ; ef high (should be 0)
        bmi @under              ; ef negative -> underflow to 0
        bne @over               ; ef >= 256 -> overflow
        lda fcnt
        beq @under              ; ef == 0 -> underflow
        cmp #$FF
        bcs @over               ; ef >= 255 -> overflow (clamp)
        ; assemble: FA0=m0, FA1=m1, FA2=(m2&0x7F)|((ef&1)<<7), FA3=(sign<<7)|(ef>>1)
        lda rmnt+0
        sta FA+0
        lda rmnt+1
        sta FA+1
        lda fcnt
        lsr                     ; ef>>1 -> A, bit0 -> carry
        pha                     ; save ef>>1
        lda rmnt+2
        and #$7F
        bcc @noset
        ora #$80
@noset: sta FA+2
        pla                     ; ef>>1
        ldx rsgn
        beq @possign
        ora #$80
@possign:
        sta FA+3
        rts
@under: lda #0
        sta FA+0
        sta FA+1
        sta FA+2
        sta FA+3
        rts
@over:  ; clamp to largest finite: ef=254, frac=0x7FFFFF
        lda #$FF
        sta FA+0
        sta FA+1
        lda #$7F
        ora #$80                ; ef=254 -> bit0=0; (254>>1=127) so FA2 bit7 = ef&1 =0
        ; frac high = 0x7F, ef&1 = 0 -> FA2 = 0x7F
        lda #$7F
        sta FA+2
        lda #$7F                ; ef>>1 = 127
        ldx rsgn
        beq @op2
        ora #$80
@op2:   sta FA+3
        rts

; ---- fp_fromint16: signed 16-bit in FA+0/FA+1 -> FA float -------------------
fp_fromint16:
        lda FA+1
        bpl @pos
        ; negate
        sec
        lda #0
        sbc FA+0
        sta FA+0
        lda #0
        sbc FA+1
        sta FA+1
        lda #1
        sta rsgn
        jmp @mag
@pos:   lda #0
        sta rsgn
@mag:   lda FA+0
        sta rmnt+0
        lda FA+1
        sta rmnt+1
        lda #0
        sta rmnt+2
        sta rmnt+3
        ; zero?
        lda rmnt+0
        ora rmnt+1
        bne @nz
        jmp packA               ; zero
@nz:    ; E starts at 0 (value = rmnt as integer); normalize rmnt to [2^23,2^24)
        lda #0
        sta rexp
        sta rexp+1
@norm:  ; while bit23 (rmnt+2 bit7) == 0: shift left, E--
        lda rmnt+2
        and #$80
        bne @done
        asl rmnt+0
        rol rmnt+1
        rol rmnt+2
        ; E--
        lda rexp
        sec
        sbc #1
        sta rexp
        lda rexp+1
        sbc #0
        sta rexp+1
        jmp @norm
@done:  jmp packA

; ---- fp_toint16: FA float -> signed 16-bit in FA+0/FA+1 (truncate) ----------
fp_toint16:
        jsr unpackA
        ; zero?
        lda amnt+0
        ora amnt+1
        ora amnt+2
        bne @nz
        lda #0
        sta FA+0
        sta FA+1
        rts
@nz:    ; integer = SG >> (-E)   (E is negative for values ~1..2^16)
        ; if E >= 0: shift left (may overflow; clamp not handled -> wrap)
        lda aexp+1
        bmi @negE               ; E < 0
        ; E >= 0: shift amnt left E times
        ldx aexp
@shl:   beq @signapply
        asl amnt+0
        rol amnt+1
        rol amnt+2
        dex
        jmp @shl
@negE:  ; shift right by (-E). -E = 0 - E
        sec
        lda #0
        sbc aexp
        tax                     ; X = (-E) low byte (E in [-149..]) fits 8-bit for our range
        ; if -E >= 24 result is 0
        cpx #24
        bcc @shr
        lda #0
        sta amnt+0
        sta amnt+1
        jmp @signapply
@shr:   cpx #0
        beq @signapply
@shrl:  lsr amnt+2
        ror amnt+1
        ror amnt+0
        dex
        bne @shrl
@signapply:
        lda amnt+0
        sta FA+0
        lda amnt+1
        sta FA+1
        lda asgn
        beq @done
        sec
        lda #0
        sbc FA+0
        sta FA+0
        lda #0
        sbc FA+1
        sta FA+1
@done:  rts

; ---- fp_sub: FA = FA - FB  (flip FB sign, add) ------------------------------
fp_sub:
        lda FB+3
        eor #$80
        sta FB+3
        ; fall through to fp_add

; ---- fp_add: FA = FA + FB ---------------------------------------------------
fp_add:
        jsr unpackA
        jsr unpackB
        ; A zero -> result = B
        lda amnt+0
        ora amnt+1
        ora amnt+2
        bne @aok
        ; copy B into FA
        lda FB+0
        sta FA+0
        lda FB+1
        sta FA+1
        lda FB+2
        sta FA+2
        lda FB+3
        sta FA+3
        rts
@aok:   lda bmnt+0
        ora bmnt+1
        ora bmnt+2
        bne @bok
        rts                     ; B zero -> FA unchanged
@bok:   ; align exponents: shift the smaller-exponent mantissa right
        ; compare aexp vs bexp (16-bit signed)
        lda aexp+1
        cmp bexp+1
        bne @cmphi
        lda aexp
        cmp bexp
        beq @aligned
        bcs @ash                ; aexp > bexp -> shift B
        jmp @bsh
@cmphi: bvc @nov
        eor #$80
@nov:   bmi @bsh                ; aexp < bexp -> shift A (handled in @bsh path? no)
        ; aexp > bexp -> shift B down to aexp
@ash:   ; diff = aexp - bexp (fits 8-bit for sane inputs)
        sec
        lda aexp
        sbc bexp
        tax
        cpx #24
        bcc @ashl
        ; B negligible -> result = A
        jmp @resultA
@ashl:  cpx #0
        beq @useA
@ashlp: lsr bmnt+2
        ror bmnt+1
        ror bmnt+0
        dex
        bne @ashlp
@useA:  ; result exponent = aexp
        lda aexp
        sta rexp
        lda aexp+1
        sta rexp+1
        jmp @addsub
@bsh:   ; aexp < bexp -> shift A down to bexp
        sec
        lda bexp
        sbc aexp
        tax
        cpx #24
        bcc @bshl
        jmp @resultB
@bshl:  cpx #0
        beq @useB
@bshlp: lsr amnt+2
        ror amnt+1
        ror amnt+0
        dex
        bne @bshlp
@useB:  lda bexp
        sta rexp
        lda bexp+1
        sta rexp+1
        jmp @addsub
@aligned:
        lda aexp
        sta rexp
        lda aexp+1
        sta rexp+1
@addsub:
        ; signs equal -> add magnitudes; differ -> subtract
        lda asgn
        cmp bsgn
        bne @diff
        ; add
        clc
        lda amnt+0
        adc bmnt+0
        sta rmnt+0
        lda amnt+1
        adc bmnt+1
        sta rmnt+1
        lda amnt+2
        adc bmnt+2
        sta rmnt+2
        lda #0
        adc #0
        sta rmnt+3              ; carry (bit24)
        lda asgn
        sta rsgn
        ; normalize right if rmnt+3 set or rmnt+2 bit7 with overflow (>=2^24)
@narsh: lda rmnt+3
        bne @rsh
        lda rmnt+2
        and #$80
        bne @adone              ; bit23 set, in range
        ; (shouldn't underflow when adding two normalized; but guard)
        jmp @adone
@rsh:   lsr rmnt+3
        ror rmnt+2
        ror rmnt+1
        ror rmnt+0
        inc rexp
        bne @narsh
        inc rexp+1
        jmp @narsh
@adone: jmp packA
@diff:  ; subtract smaller magnitude from larger; pick sign of larger
        ; compare amnt vs bmnt (24-bit)
        lda amnt+2
        cmp bmnt+2
        bne @dc
        lda amnt+1
        cmp bmnt+1
        bne @dc
        lda amnt+0
        cmp bmnt+0
@dc:    bcs @ag                 ; amnt >= bmnt
        ; bmnt > amnt: r = bmnt - amnt, sign = bsgn
        sec
        lda bmnt+0
        sbc amnt+0
        sta rmnt+0
        lda bmnt+1
        sbc amnt+1
        sta rmnt+1
        lda bmnt+2
        sbc amnt+2
        sta rmnt+2
        lda bsgn
        sta rsgn
        jmp @dnorm
@ag:    sec
        lda amnt+0
        sbc bmnt+0
        sta rmnt+0
        lda amnt+1
        sbc bmnt+1
        sta rmnt+1
        lda amnt+2
        sbc bmnt+2
        sta rmnt+2
        lda asgn
        sta rsgn
@dnorm: ; result zero?
        lda rmnt+0
        ora rmnt+1
        ora rmnt+2
        bne @dn2
        lda #0                  ; zero
        sta rmnt+3
        jmp packA
@dn2:   ; normalize left until bit23 set
        lda rmnt+2
        and #$80
        bne @ddone
        asl rmnt+0
        rol rmnt+1
        rol rmnt+2
        lda rexp
        sec
        sbc #1
        sta rexp
        lda rexp+1
        sbc #0
        sta rexp+1
        jmp @dn2
@ddone: lda #0
        sta rmnt+3
        jmp packA
@resultA:
        rts                     ; FA already holds A
@resultB:
        lda FB+0
        sta FA+0
        lda FB+1
        sta FA+1
        lda FB+2
        sta FA+2
        lda FB+3
        sta FA+3
        rts

; ---- fp_mul: FA = FA * FB ---------------------------------------------------
fp_mul:
        jsr unpackA
        jsr unpackB
        lda amnt+0
        ora amnt+1
        ora amnt+2
        beq @zero
        lda bmnt+0
        ora bmnt+1
        ora bmnt+2
        bne @go
@zero:  lda #0
        sta FA+0
        sta FA+1
        sta FA+2
        sta FA+3
        rts
@go:    lda asgn
        eor bsgn
        sta rsgn
        clc                     ; rexp = aexp + bexp
        lda aexp
        adc bexp
        sta rexp
        lda aexp+1
        adc bexp+1
        sta rexp+1
        ldx #0                  ; prod(48+1) = amnt * bmnt
        lda #0
@clr:   sta prod,x
        inx
        cpx #7
        bne @clr
        ldx #24
@ml:    lsr bmnt+2
        ror bmnt+1
        ror bmnt+0
        bcc @noadd
        clc
        lda prod+3
        adc amnt+0
        sta prod+3
        lda prod+4
        adc amnt+1
        sta prod+4
        lda prod+5
        adc amnt+2
        sta prod+5
        lda prod+6              ; capture carry out of bit47 (bit48)
        adc #0
        sta prod+6
@noadd: lsr prod+6
        ror prod+5
        ror prod+4
        ror prod+3
        ror prod+2
        ror prod+1
        ror prod+0
        dex
        bne @ml
        ; P = amnt*bmnt in prod[0..5], in [2^46,2^48).
        ; bit47 set -> SGc = P>>24 (=prod[3..5]), Ec+=24; else SGc = P>>23, Ec+=23.
        lda prod+5
        bmi @big
        asl prod+2              ; P>>23: shift prod[2..5] left once
        rol prod+3
        rol prod+4
        rol prod+5
        clc
        lda rexp
        adc #23
        sta rexp
        lda rexp+1
        adc #0
        sta rexp+1
        jmp @take
@big:   clc
        lda rexp
        adc #24
        sta rexp
        lda rexp+1
        adc #0
        sta rexp+1
@take:  lda prod+3
        sta rmnt+0
        lda prod+4
        sta rmnt+1
        lda prod+5
        sta rmnt+2
        lda #0
        sta rmnt+3
        jmp packA

; ---- fp_div: FA = FA / FB ---------------------------------------------------
fp_div:
        jsr unpackA
        jsr unpackB
        lda amnt+0
        ora amnt+1
        ora amnt+2
        bne @aok
        lda #0                  ; A zero -> 0
        sta FA+0
        sta FA+1
        sta FA+2
        sta FA+3
        rts
@aok:   lda bmnt+0
        ora bmnt+1
        ora bmnt+2
        bne @go
        lda asgn                ; B zero -> overflow clamp
        eor bsgn
        sta rsgn
        lda #$7F
        sta rexp
        lda #1
        sta rexp+1
        lda #$80
        sta rmnt+2
        lda #0
        sta rmnt+0
        sta rmnt+1
        sta rmnt+3
        jmp packA
@go:    lda asgn
        eor bsgn
        sta rsgn
        sec                     ; rexp = aexp - bexp - 24
        lda aexp
        sbc bexp
        sta rexp
        lda aexp+1
        sbc bexp+1
        sta rexp+1
        sec
        lda rexp
        sbc #24
        sta rexp
        lda rexp+1
        sbc #0
        sta rexp+1
        ; dividend = amnt << 24 in prod[0..5]; quotient Q=(amnt<<24)/bmnt (<2^25)
        lda #0
        sta prod+0
        sta prod+1
        sta prod+2
        sta dtmp+0
        sta dtmp+1
        sta dtmp+2
        sta dtmp+3
        lda amnt+0
        sta prod+3
        lda amnt+1
        sta prod+4
        lda amnt+2
        sta prod+5
        ldx #48                 ; full 48-bit restoring division
@dl:    asl prod+0
        rol prod+1
        rol prod+2
        rol prod+3
        rol prod+4
        rol prod+5
        rol dtmp+0
        rol dtmp+1
        rol dtmp+2
        rol dtmp+3
        lda dtmp+3              ; rem >= divisor?
        bne @sub                ; rem >= 2^24 > divisor
        lda dtmp+2
        cmp bmnt+2
        bcc @q0
        bne @sub
        lda dtmp+1
        cmp bmnt+1
        bcc @q0
        bne @sub
        lda dtmp+0
        cmp bmnt+0
        bcc @q0
@sub:   sec                     ; rem -= divisor (32-bit)
        lda dtmp+0
        sbc bmnt+0
        sta dtmp+0
        lda dtmp+1
        sbc bmnt+1
        sta dtmp+1
        lda dtmp+2
        sbc bmnt+2
        sta dtmp+2
        lda dtmp+3
        sbc #0
        sta dtmp+3
        inc prod+0              ; quotient bit = 1 (just-shifted bit0 is 0)
@q0:    dex
        bne @dl
        ; Q (<=25 bits) in prod[0..2] + bit24 in prod+3 bit0
        lda prod+3
        and #1
        beq @noov
        lsr prod+2              ; Q >= 2^24 -> SGc = Q>>1 ; Ec++
        ror prod+1
        ror prod+0
        lda prod+2
        ora #$80
        sta prod+2
        inc rexp
        bne @noov
        inc rexp+1
@noov:  lda prod+0
        sta rmnt+0
        lda prod+1
        sta rmnt+1
        lda prod+2
        sta rmnt+2
        lda #0
        sta rmnt+3
        jmp packA

; ---- fp_cmp: A = 0 (FA<FB) / 1 (==) / 2 (FA>FB) -----------------------------
fp_cmp:
        ; compute FA - FB into FA (clobbers), then classify sign/zero.
        jsr fp_sub
        lda FA+0
        ora FA+1
        ora FA+2
        ; FA+3 low 7 bits are exp/frac; if all mantissa+exp zero -> zero
        ldx FA+3
        stx fcnt
        lda fcnt
        and #$7F
        ora FA+0
        ora FA+1
        ora FA+2
        bne @nz
        lda #1                  ; equal
        rts
@nz:    lda FA+3
        bmi @less               ; sign set -> result negative -> FA<FB
        lda #2
        rts
@less:  lda #0
        rts

; ============================================================================
; Transcendentals -- built on the fp_* core above. Gated per feature (-D FP_xxx)
; so a program that doesn't use them never links them.
; ============================================================================
.if .defined(FP_SQRT) .or .defined(FP_SIN) .or .defined(FP_INT)
.segment "ZEROPAGE"
sA: .res 4              ; scratch float a / argument
sY: .res 4              ; scratch float y
sR: .res 4              ; scratch float (sin: reduced angle / accumulators)
sR2: .res 4
.segment "CODE"
.endif

; ---- fp_int: FA = truncate-toward-zero(FA)  (Applesoft INT for >= 0) ---------
.ifdef FP_INT
.export fp_int
fp_int:
        jsr unpackA
        lda amnt+0
        ora amnt+1
        ora amnt+2
        bne @nz
@zero:  lda #0
        sta FA+0
        sta FA+1
        sta FA+2
        sta FA+3
        rts
@nz:    lda aexp+1
        bpl @keep               ; E >= 0 -> already integer
        sec                     ; k = -E
        lda #0
        sbc aexp
        sta fcnt
        cmp #24
        bcs @zero               ; |x| < 1 -> 0
        ldx fcnt                ; clear low k bits: >>k then <<k
@sr:    lsr amnt+2
        ror amnt+1
        ror amnt+0
        dex
        bne @sr
        ldx fcnt
@sl:    asl amnt+0
        rol amnt+1
        rol amnt+2
        dex
        bne @sl
        lda amnt+0
        ora amnt+1
        ora amnt+2
        beq @zero
        lda asgn
        sta rsgn
        lda aexp
        sta rexp
        lda aexp+1
        sta rexp+1
        lda amnt+0
        sta rmnt+0
        lda amnt+1
        sta rmnt+1
        lda amnt+2
        sta rmnt+2
        lda #0
        sta rmnt+3
        jmp packA
@keep:  rts
.endif

; ---- fp_sqrt: FA = sqrt(FA)  (Newton-Raphson; <=0 -> 0) ----------------------
.ifdef FP_SQRT
.export fp_sqrt
fp_sqrt:
        lda FA+3
        bmi @z0                 ; negative -> 0
        lda FA+0
        ora FA+1
        ora FA+2
        ora FA+3
        bne @go
@z0:    lda #0
        sta FA+0
        sta FA+1
        sta FA+2
        sta FA+3
        rts
@go:    ldx #3                  ; sA = a
@cpa:   lda FA,x
        sta sA,x
        dex
        bpl @cpa
        ; initial guess y0: ef_y0 = (ef + 127) >> 1, keep mantissa
        lda FA+2
        asl
        lda FA+3
        rol                     ; A = ef
        clc
        adc #127
        ror                     ; A = (ef+127)>>1 = ef_y0
        sta fcnt
        lsr
        sta FA+3                ; sign 0 | ef_y0>>1
        lda FA+2
        and #$7F
        sta FA+2
        lda fcnt
        and #1
        beq @it0
        lda FA+2
        ora #$80
        sta FA+2
@it0:   ldx #5                  ; 5 Newton iterations: y = (y + a/y) * 0.5
@it:    lda FA+0
        sta sY+0
        lda FA+1
        sta sY+1
        lda FA+2
        sta sY+2
        lda FA+3
        sta sY+3
        ; FA = a / y
        lda sA+0
        sta FA+0
        lda sA+1
        sta FA+1
        lda sA+2
        sta FA+2
        lda sA+3
        sta FA+3
        lda sY+0
        sta FB+0
        lda sY+1
        sta FB+1
        lda sY+2
        sta FB+2
        lda sY+3
        sta FB+3
        txa
        pha
        jsr fp_div
        ; FA = FA + y
        lda sY+0
        sta FB+0
        lda sY+1
        sta FB+1
        lda sY+2
        sta FB+2
        lda sY+3
        sta FB+3
        jsr fp_add
        ; FA = FA * 0.5
        lda #$00
        sta FB+0
        sta FB+1
        sta FB+2
        lda #$3F
        sta FB+3
        jsr fp_mul
        pla
        tax
        dex
        bne @it
        rts
.endif

; ---- fp_sin: FA = sin(FA) radians ------------------------------------------
; Range-reduce to [-pi/2, pi/2] then a 4-term Taylor series. Args within ~+-5000
; (k = round(x/2pi) must fit 16 bits). Built entirely on fp_add/sub/mul/cmp.
.ifdef FP_SIN
.macro LDF reg, b0,b1,b2,b3
        lda #b0
        sta reg+0
        lda #b1
        sta reg+1
        lda #b2
        sta reg+2
        lda #b3
        sta reg+3
.endmacro
.macro CPF dst, src
        lda src+0
        sta dst+0
        lda src+1
        sta dst+1
        lda src+2
        sta dst+2
        lda src+3
        sta dst+3
.endmacro
.export fp_sin
fp_sin:
        CPF sA, FA                      ; sA = x
        LDF FB, $83,$F9,$22,$3E         ; 1/2pi
        jsr fp_mul                      ; FA = x/2pi
        lda FA+3                        ; FB = copysign(0.5, FA)
        and #$80
        ora #$3F
        sta FB+3
        lda #0
        sta FB+0
        sta FB+1
        sta FB+2
        jsr fp_add
        jsr fp_toint16                  ; round(x/2pi)
        jsr fp_fromint16                ; FA = k
        LDF FB, $DB,$0F,$C9,$40         ; 2pi
        jsr fp_mul                      ; FA = k*2pi
        CPF FB, FA
        CPF FA, sA                      ; FA = x
        jsr fp_sub                      ; FA = r in [-pi,pi]
        CPF sR, FA
        LDF FB, $DB,$0F,$C9,$3F         ; pi/2
        jsr fp_cmp
        cmp #2
        bne @lo
        LDF FA, $DB,$0F,$49,$40         ; pi
        CPF FB, sR
        jsr fp_sub                      ; pi - r
        jmp @red
@lo:    CPF FA, sR
        LDF FB, $DB,$0F,$C9,$BF         ; -pi/2
        jsr fp_cmp
        cmp #0
        bne @keepr
        LDF FA, $DB,$0F,$49,$C0         ; -pi
        CPF FB, sR
        jsr fp_sub                      ; -pi - r
        jmp @red
@keepr: CPF FA, sR
@red:   CPF sR, FA                      ; sR = reduced r
        CPF FB, sR
        jsr fp_mul                      ; FA = r^2
        CPF sR2, FA
        LDF FA, $01,$0D,$50,$B9         ; -1/5040
        CPF FB, sR2
        jsr fp_mul
        LDF FB, $89,$88,$08,$3C         ; +1/120
        jsr fp_add
        CPF FB, sR2
        jsr fp_mul
        LDF FB, $AB,$AA,$2A,$BE         ; -1/6
        jsr fp_add
        CPF FB, sR2
        jsr fp_mul
        LDF FB, $00,$00,$80,$3F         ; +1
        jsr fp_add
        CPF FB, sR
        jsr fp_mul                      ; FA = sin(x)
        rts
.endif
