; ============================================================================
; multiply.asm -- 6502 unsigned multiply primitives (shift-and-add)
; ============================================================================
; Promoted out of `dev/lib/hgr/hgr_tables.inc` (where it was wedged among the
; HGR pixel tables). Two reasons for the move:
;   (1) Multiply is generic 6502, not HGR-specific — it belongs in m6502/.
;   (2) Old layout forced every hgr_tables.inc consumer to also declare
;       mul_tmp / mul_res0 (HGR_Sierpinski, HGR_TestCard previously broke).
;
; Reserves its own ZP slot pair (mul_tmp, mul_res0) via `.ifndef` guard so
; ZP-tight callers can alias their own slots before the .include:
;
;     mul_tmp  = my_tmp
;     mul_res0 = my_res
;     .include "multiply.asm"
;
;   umul8 -- unsigned 8x8 -> 16-bit shift-and-add multiply.
;            Input:  A = multiplicand, X = multiplier.
;            Output: A = product low byte, X = product high byte.
;            Caller-visible state: mul_tmp / mul_res0 trashed.
;            Reference: 6502.org canonical implementation.
;
;   umul4 -- unsigned 4x4 -> 8-bit shift-and-add multiply.
;            Input:  A = multiplicand (0..15), X = multiplier (0..15).
;            Output: A = product (0..225 — fits in one byte since 15*15
;                    = 225 < 256).
;            Caller-visible state: mul_tmp / mul_res0 trashed.
;
;            Half the cost of umul8 when both operands are known to fit
;            in 4 bits — slope arithmetic, 0..F-bound table indices, the
;            shadowcaster's slope cross-multiplications, percentage math.
;            Pre-shifts the multiplier into the high nybble so the inner
;            loop only needs 4 iterations instead of 8.
;
; Projects building 16x16 multipliers on top of umul8 (e.g. HGR4 Mandelbrot)
; declare their own extra accumulator slots (mul_res1..3) on top of these
; two — those stay project-local since the build-up logic is project-specific.
; ============================================================================

.ifndef mul_tmp
.segment "ZEROPAGE"
mul_tmp:        .res 1
mul_res0:       .res 1
.endif

.segment "CODE"

umul8:
        STA     mul_tmp
        STX     mul_res0
        LDA     #$00
        LDX     #8
        LSR     mul_res0        ; extract first multiplier bit
@uml:   BCC     @noad
        CLC
        ADC     mul_tmp
@noad:  ROR     A               ; shift result high byte
        ROR     mul_res0        ; shift result low + next multiplier bit -> carry
        DEX
        BNE     @uml
        TAX                     ; X = high byte
        LDA     mul_res0        ; A = low byte
        RTS

umul4:
        STA     mul_tmp         ; multiplicand 0..15
        TXA                     ; A = multiplier 0..15 (low nybble)
        ASL
        ASL
        ASL
        ASL                     ; multiplier shifted to high nybble
        STA     mul_res0
        LDA     #0
        LDX     #4
@u4lp:  ASL                     ; result <<= 1 (MSB-first accumulation)
        ASL     mul_res0        ; multiplier MSB -> carry
        BCC     @u4noad
        CLC
        ADC     mul_tmp
@u4noad:
        DEX
        BNE     @u4lp
        RTS
