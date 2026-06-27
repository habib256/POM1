; ============================================================================
; math.asm -- fixed-point trig + LFSR + decimal output for Apple-1 + TMS9918
;             programs. Reusable wherever 16-bit signed/unsigned helpers,
;             sine/cosine, angle normalisation, RNG, or decimal output are
;             needed.
;
; Public symbols:
;   roll_lfsr           -- advance 16-bit Galois LFSR ($B400 taps)
;   print_decimal       -- print arg_lo:arg_hi as unsigned decimal (1..5 dig)
;   div_arg_by_10       -- arg_lo:hi /= 10 unsigned, A = remainder
;   mod360_arg          -- arg_lo:hi mod 360
;   mod360_tmp          -- tmp:tmp2  mod 360
;   norm360             -- th_lo:th_hi mod 360
;   signed_sin          -- A = signed_sin(tmp:tmp2) * 64,  range -64..+64
;   mul_dist_by_signed  -- prod_lo:prod_hi = (arg_lo * A) / 64, signed
;   negate_prod         -- prod_lo:prod_hi = -prod_lo:prod_hi (2's comp)
;
; This module owns no state of its own. All scratch / argument / result
; slots live in the caller's ZP and BSS (declared in TMS_Logo.asm) and are
; consumed via .importzp / .import below.
; ============================================================================

.include "apple1.inc"

; --- imported ZP slots (defined in TMS_Logo.asm) ---------------------------
.importzp tmp, tmp2
.importzp arg_lo, arg_hi, arg2_lo, arg2_hi
.importzp th_lo, th_hi

; --- imported BSS slots (defined in TMS_Logo.asm LINEBUF) ------------------
.import prod_lo, prod_hi, sign_flag, lfsr_lo, lfsr_hi

; --- exported routines -----------------------------------------------------
.export roll_lfsr
.export print_decimal, div_arg_by_10
.export mod360_arg, mod360_tmp, norm360
.export signed_sin, mul_dist_by_signed, negate_prod

; ============================================================================
.segment "CODE"
; ============================================================================

; ----------------------------------------------------------------------------
; roll_lfsr: advance the 16-bit Galois LFSR (taps $B400 -> bits 16,14,13,11).
;   Standard right-shift Galois variant: shift the 16-bit value right by 1;
;   if the bit shifted out (= old bit 0 of lfsr_lo) was 1, XOR taps into hi.
;
;   Intentional duplicate of lib/m6502/prng16.asm:prng16 (same $B400
;   polynomial). Kept distinct because the two consumers have different state
;   placement contracts: roll_lfsr's lfsr_lo/hi lives in BSS (TMS_Logo's
;   LINEBUF), prng16's prng_lo/hi lives in ZEROPAGE (arcade games' tight
;   loops). Merging would require migrating TMS_Logo's seed slots to ZP —
;   gain is 12 bytes ROM + 4 bytes state, not worth the test surface today.
; ----------------------------------------------------------------------------
roll_lfsr:
        LSR lfsr_hi          ; bit 0 of lfsr_hi -> C
        ROR lfsr_lo          ; lfsr_lo: C -> bit 7, bit 0 -> C (the shifted-out bit)
        BCC @done            ; if shifted-out bit was 0, no XOR
        LDA lfsr_hi
        EOR #$B4
        STA lfsr_hi
@done:  RTS

; ----------------------------------------------------------------------------
; print_decimal: print arg_lo:hi as unsigned decimal (1..5 digits, no padding).
; ----------------------------------------------------------------------------
print_decimal:
        ; If value is zero, print "0" and return.
        LDA arg_lo
        ORA arg_hi
        BNE @nz
        LDA #'0'
        ORA #$80
        JSR ECHO
        RTS
@nz:    ; Extract digits via repeated div by 10, push, then pop to print.
        LDY #0                ; digit count (X is clobbered by div_arg_by_10)
@dig:   JSR div_arg_by_10     ; arg /= 10, A = remainder
        PHA
        INY
        LDA arg_lo
        ORA arg_hi
        BNE @dig
@out:   PLA
        ORA #'0'
        ORA #$80
        JSR ECHO
        DEY
        BNE @out
        RTS

; ----------------------------------------------------------------------------
; div_arg_by_10: arg_lo:hi /= 10 (unsigned), returns remainder in A.
; ----------------------------------------------------------------------------
div_arg_by_10:
        LDA #0
        STA tmp               ; remainder
        LDX #16
@d:     ASL arg_lo
        ROL arg_hi
        ROL tmp
        LDA tmp
        CMP #10
        BCC @ns
        SBC #10
        STA tmp
        INC arg_lo            ; bit-0 of quotient
@ns:    DEX
        BNE @d
        LDA tmp
        RTS

; ----------------------------------------------------------------------------
; mod360_arg: clamp arg_lo/hi into [0..359] by repeated subtraction.
; ----------------------------------------------------------------------------
mod360_arg:
@l:     LDA arg_hi
        CMP #>360
        BCC @done
        BNE @sub
        LDA arg_lo
        CMP #<360
        BCC @done
@sub:   SEC
        LDA arg_lo
        SBC #<360
        STA arg_lo
        LDA arg_hi
        SBC #>360
        STA arg_hi
        JMP @l
@done:  RTS

; ----------------------------------------------------------------------------
; norm360: reduce th_lo/hi mod 360 (after addition that may have grown).
; ----------------------------------------------------------------------------
norm360:
@l:     LDA th_hi
        CMP #>360
        BCC @done
        BNE @sub
        LDA th_lo
        CMP #<360
        BCC @done
@sub:   SEC
        LDA th_lo
        SBC #<360
        STA th_lo
        LDA th_hi
        SBC #>360
        STA th_hi
        JMP @l
@done:  RTS

; ----------------------------------------------------------------------------
; mod360_tmp: reduce tmp:tmp2 modulo 360.
; ----------------------------------------------------------------------------
mod360_tmp:
@l:     LDA tmp2
        CMP #>360
        BCC @done
        BNE @sub
        LDA tmp
        CMP #<360
        BCC @done
@sub:   SEC
        LDA tmp
        SBC #<360
        STA tmp
        LDA tmp2
        SBC #>360
        STA tmp2
        JMP @l
@done:  RTS

; ============================================================================
; signed_sin: input  tmp:tmp2 = angle in [0..359]
;             output A = signed_sin*64  in [-64..+64]
;
;   if a <=  90:  +sin_q[a]
;   if a <= 180:  +sin_q[180 - a]
;   if a <= 270:  -sin_q[a - 180]
;   else       :  -sin_q[360 - a]
; ============================================================================
signed_sin:
        LDA tmp2
        BNE @hi               ; angle >= 256 -> in (256..359] => last quadrant
        LDA tmp
        CMP #91
        BCC @q1               ; 0..90
        CMP #181
        BCC @q2               ; 91..180
        ; 181..255 -> q3
@q3:    SEC
        LDA tmp
        SBC #180
        TAX
        LDA sin_q,X
        EOR #$FF
        CLC
        ADC #1
        RTS
@q1:    LDX tmp
        LDA sin_q,X
        RTS
@q2:    SEC
        LDA #180
        SBC tmp
        TAX
        LDA sin_q,X
        RTS
@hi:    ; tmp2 = 1, tmp in 0..103 -> angle 256..359
        ; q3 covers 181..270 (i.e. tmp2=1, tmp 0..14)
        ; q4 covers 271..359 (i.e. tmp2=1, tmp 15..103)
        LDA tmp
        CMP #15               ; 256+15 = 271
        BCC @hi_q3
        ; q4: -sin_q[360 - angle] = -sin_q[360 - (256+tmp)] = -sin_q[104 - tmp]
@hi_q4: SEC
        LDA #104
        SBC tmp
        TAX
        LDA sin_q,X
        EOR #$FF
        CLC
        ADC #1
        RTS
@hi_q3: ; 256..270: -sin_q[angle - 180] = -sin_q[(256+tmp) - 180] = -sin_q[76+tmp]
        CLC
        LDA #76
        ADC tmp
        TAX
        LDA sin_q,X
        EOR #$FF
        CLC
        ADC #1
        RTS

; ============================================================================
; mul_dist_by_signed: multiply arg_lo (unsigned 0..255) by A (signed -64..+64),
;                     divide by 64, sign-extend, store result in prod_lo/prod_hi.
;
;   abs_v = |A|
;   uprod = arg_lo * abs_v             (16-bit unsigned, max 255*64=16320)
;   result = uprod / 64                (range 0..255)
;   apply sign -> prod_lo/prod_hi signed.
; ============================================================================
mul_dist_by_signed:
        ; record sign
        STA tmp               ; signed value
        LDA #0
        STA sign_flag
        LDA tmp
        BPL @abs_done
        LDA tmp
        EOR #$FF
        CLC
        ADC #1
        STA tmp               ; tmp = abs(value)
        LDA #1
        STA sign_flag
@abs_done:
.ifdef LOGO_GEN2
        ; 16x8 unsigned multiply: (arg_hi:arg_lo) * tmp -> 16-bit arg2_hi:arg2_lo,
        ; so a GEN2 FD step can exceed 255 px and span the full 280 width. With
        ; the distance clamped to <= 511 and tmp <= 64, the product <= 32704
        ; fits 16 bits. MSB-first left-shift add.
        LDA #0
        STA arg2_lo
        STA arg2_hi
        LDX #8
@m:     ASL arg2_lo
        ROL arg2_hi
        ASL tmp               ; multiplier bit (MSB first) -> C
        BCC @noadd
        CLC
        LDA arg2_lo
        ADC arg_lo
        STA arg2_lo
        LDA arg2_hi
        ADC arg_hi
        STA arg2_hi
@noadd: DEX
        BNE @m
        ; divide by 64 (16-bit; result may exceed 255 now)
        LDX #6
@shr:   LSR arg2_hi
        ROR arg2_lo
        DEX
        BNE @shr
        LDA arg2_lo
        STA prod_lo
        LDA arg2_hi
        STA prod_hi
.else
        ; 8x8 unsigned multiply: arg_lo * tmp -> 16-bit in arg2_hi:arg2_lo (scratch)
        LDA #0
        STA arg2_hi
        STA arg2_lo
        LDX #8
@m:     LSR tmp               ; bit -> C
        BCC @noadd
        CLC
        LDA arg2_hi
        ADC arg_lo
        STA arg2_hi
@noadd: ROR arg2_hi
        ROR arg2_lo
        DEX
        BNE @m
        ; uprod is in arg2_hi:arg2_lo. Divide by 64 = shift right by 6 across
        ; the 16-bit value. Result fits in 8 bits since uprod <= 16320.
        LDX #6
@shr:   LSR arg2_hi
        ROR arg2_lo
        DEX
        BNE @shr
        LDA arg2_lo
        STA prod_lo
        LDA #0
        STA prod_hi
.endif
        ; apply sign
        LDA sign_flag
        BEQ @done
        ; negate prod
        SEC
        LDA #0
        SBC prod_lo
        STA prod_lo
        LDA #0
        SBC prod_hi
        STA prod_hi
@done:  RTS

negate_prod:
        SEC
        LDA #0
        SBC prod_lo
        STA prod_lo
        LDA #0
        SBC prod_hi
        STA prod_hi
        RTS

; ============================================================================
; sine table: sin_q[i] = round(sin(i degrees) * 64), for i in 0..90.
;             Worst-case index across signed_sin's 5 paths is 90 (q1 a=90 or
;             @hi_q3 tmp=14 → 76+14). Was historically padded to 104 entries;
;             the 14 trailing zeros saved no path (audited 2026-06-18). If a
;             future caller needs indices > 90, re-extend here AND update the
;             range comment in signed_sin.
; ============================================================================
sin_q:
        ; 0  1  2  3  4  5  6  7  8  9
        .byte  0, 1, 2, 3, 4, 6, 7, 8, 9,10
        .byte 11,12,13,14,16,17,18,19,20,21
        .byte 22,23,24,25,26,27,28,29,30,31
        .byte 32,33,34,35,36,37,38,39,40,41
        .byte 42,42,43,44,45,45,46,47,48,48
        .byte 49,50,50,51,52,52,53,54,54,55
        .byte 55,56,56,57,57,58,58,59,59,60
        .byte 60,60,61,61,61,62,62,62,63,63
        .byte 63,63,63,64,64,64,64,64,64,64
        .byte 64                                ; index 90 (sin(90°)=1 → 64)
