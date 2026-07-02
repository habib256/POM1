; ============================================================================
; t03_pad18_transparency.s — micro-test: tms9918_pad18 register+flag contract
; ============================================================================
; GUARDS: the pinned INVARIANT in tms9918_pad.asm — pad18 must be flag- AND
;   register-transparent (NOP-only filler). Hot loops place JSR tms9918_pad18
;   BETWEEN a CPX/CMP and its branch (sprite_triangle, tms9918_text) and fill
;   loops keep the fill byte in A across the pad (basicrt lr_fill). A future
;   pad body containing any LDA/LDX/LDY/TAX/compare would silently break
;   those consumers; here it flips a mailbox byte instead.
;
; POM1-LIB-MICRO-TEST
; LIBS: tms9918/tms9918_pad.asm
; CFG: micro.cfg
; PRESET: 1
; LOAD: 0300
; RUN: 0300
; STEPS: 500
; EXPECT: 0F00 A5 5A C3 3C 35 35 01 01 01
; ============================================================================

.include "apple1.inc"

.import tms9918_pad18

MB = $0F00

.segment "CODE"
main:
        APPLE1_PREAMBLE

        ; --- A/X/Y + P survival across the pad ------------------------------
        LDX     #$C3
        LDY     #$3C
        CLV                     ; pin V=0 so the pushed P bytes are exact
        SEC                     ; pin C=1
        LDA     #$5A            ; last: pins N=0 Z=0
        PHP                     ; P before (I=1 from SEI, B+bit5 set by PHP -> $35)
        JSR     tms9918_pad18
        PHP                     ; P after — must be identical
        STA     MB+1            ; A  = $5A
        STX     MB+2            ; X  = $C3
        STY     MB+3            ; Y  = $3C
        PLA
        STA     MB+5            ; P after  = $35
        PLA
        STA     MB+4            ; P before = $35

        ; --- Z survives a compare split by the pad (the CPX/branch idiom) ---
        LDX     #5
        CPX     #5              ; Z=1, C=1
        JSR     tms9918_pad18
        BNE     @z_fail
        LDA     #$01
        STA     MB+6            ; Z survived
@z_fail:
        LDX     #5
        CPX     #5              ; C=1
        JSR     tms9918_pad18
        BCC     @c_fail
        LDA     #$01
        STA     MB+7            ; C survived
@c_fail:
        LDX     #5
        CPX     #6              ; C=0 (borrow)
        JSR     tms9918_pad18
        BCS     @b_fail
        LDA     #$01
        STA     MB+8            ; clear-C survived
@b_fail:

        LDA     #$A5
        STA     MB
spin:   JMP     spin
