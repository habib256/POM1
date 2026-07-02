; ============================================================================
; t06_math_vectors.s — micro-test: m6502/math.asm arg/tmp window semantics
; ============================================================================
; GUARDS: the shared-ZP argument-window contract (the bug class that let
;   Maze3D's cell_index_xy `tmp` clobber survive for months): math.asm owns
;   no state — every routine reads/writes the CALLER's tmp/tmp2/arg_*/prod_*
;   slots. These vectors pin, for each routine, exactly which slots are
;   consumed and where results land:
;     signed_sin      : in tmp:tmp2 (angle), out A            — 4 quadrants
;     mod360_tmp      : in/out tmp:tmp2 (725 -> 5)
;     mul_dist_by_signed: in arg_lo + A(signed), out prod_lo/hi (sign extend),
;                         clobbers tmp/arg2_* (100 * +64 /64 ; 100 * -64 /64)
;     div_arg_by_10   : in/out arg_lo:arg_hi, remainder in A  (12345 -> 1234 r5)
;     roll_lfsr       : in/out lfsr_lo/hi ($0001 -> $B400 tap kicks in)
;   A routine that starts using a different scratch slot, or returns through
;   a different register, changes a mailbox byte.
;
; POM1-LIB-MICRO-TEST
; LIBS: m6502/math.asm
; CFG: micro.cfg
; PRESET: 1
; LOAD: 0300
; RUN: 0300
; STEPS: 4000
; EXPECT: 0F00 A5 00 40 20 C0 05 00 64 00 9C FF D2 04 05 00 B4
; ============================================================================

.include "apple1.inc"

.import signed_sin, mod360_tmp, mul_dist_by_signed, div_arg_by_10, roll_lfsr

.segment "ZEROPAGE"
tmp:      .res 1
tmp2:     .res 1
arg_lo:   .res 1
arg_hi:   .res 1
arg2_lo:  .res 1
arg2_hi:  .res 1
th_lo:    .res 1
th_hi:    .res 1
.exportzp tmp, tmp2, arg_lo, arg_hi, arg2_lo, arg2_hi, th_lo, th_hi

.segment "BSS"
prod_lo:   .res 1
prod_hi:   .res 1
sign_flag: .res 1
lfsr_lo:   .res 1
lfsr_hi:   .res 1
.export prod_lo, prod_hi, sign_flag, lfsr_lo, lfsr_hi

MB = $0F00

.segment "CODE"
main:
        APPLE1_PREAMBLE

        ; --- signed_sin: 0 -> 0, 90 -> +64, 30 -> +32, 270 -> -64 ($C0) -----
        LDA     #0
        STA     tmp
        STA     tmp2
        JSR     signed_sin
        STA     MB+1            ; 00
        LDA     #90
        STA     tmp
        LDA     #0
        STA     tmp2
        JSR     signed_sin
        STA     MB+2            ; $40
        LDA     #30
        STA     tmp
        LDA     #0
        STA     tmp2
        JSR     signed_sin
        STA     MB+3            ; $20
        LDA     #<270
        STA     tmp
        LDA     #>270
        STA     tmp2
        JSR     signed_sin
        STA     MB+4            ; $C0 (-64)

        ; --- mod360_tmp: 725 -> 5 -------------------------------------------
        LDA     #<725
        STA     tmp
        LDA     #>725
        STA     tmp2
        JSR     mod360_tmp
        LDA     tmp
        STA     MB+5            ; 05
        LDA     tmp2
        STA     MB+6            ; 00

        ; --- mul_dist_by_signed: 100 * +64 / 64 = +100 -----------------------
        LDA     #100
        STA     arg_lo
        LDA     #64
        JSR     mul_dist_by_signed
        LDA     prod_lo
        STA     MB+7            ; $64 (100)
        LDA     prod_hi
        STA     MB+8            ; 00

        ; --- mul_dist_by_signed: 100 * -64 / 64 = -100 -----------------------
        LDA     #100
        STA     arg_lo
        LDA     #<-64
        JSR     mul_dist_by_signed
        LDA     prod_lo
        STA     MB+9            ; $9C (-100 low)
        LDA     prod_hi
        STA     MB+10           ; $FF (sign extension)

        ; --- div_arg_by_10: 12345 -> 1234 remainder 5 ------------------------
        LDA     #<12345
        STA     arg_lo
        LDA     #>12345
        STA     arg_hi
        JSR     div_arg_by_10
        STA     MB+13           ; remainder 05 (returned in A)
        LDA     arg_lo
        STA     MB+11           ; $D2
        LDA     arg_hi
        STA     MB+12           ; $04

        ; --- roll_lfsr: $0001 -> shifted-out 1 XORs $B4 into hi --------------
        LDA     #$01
        STA     lfsr_lo
        LDA     #$00
        STA     lfsr_hi
        JSR     roll_lfsr
        LDA     lfsr_lo
        STA     MB+14           ; 00
        LDA     lfsr_hi
        STA     MB+15           ; $B4

        LDA     #$A5
        STA     MB
spin:   JMP     spin
