; ============================================================================
; sid_init.asm -- silence the SID and set sane defaults for voice 1
; ============================================================================
;   sid_init -- write zeros to all 29 visible registers (silences any
;               leftover state from a prior program), then set master
;               volume = $0F (max) and a generic ADSR for voice 1.
;               Useful as a one-shot startup before ANY note is played.
;               Clobbers A, X. Y preserved.
;
; ZP usage: none.
;
; Caller responsibility: nothing — sid.inc is included by this module.
; ============================================================================

.include "sid.inc"

.segment "CODE"

sid_init:
        ; Zero $C800..$C81C (29 visible registers). The chip mirrors past
        ; offset $1F, so 29 writes cover everything user code can reach.
        LDX     #29
        LDA     #$00
@zr:    DEX
        STA     SID_BASE,X      ; first iter writes $C81C; last writes $C800
        BNE     @zr             ; DEX set Z when X reached 0

        ; Master volume = $0F (max), no filter routing.
        LDA     #$0F
        STA     SID_VOLUME

        ; Voice 1 default ADSR: A=0 D=8 S=$F R=8 (snappy attack,
        ; sustained tone). Caller can override before triggering.
        LDA     #$08
        STA     SID_V1_AD       ; A=0, D=8
        LDA     #$F8
        STA     SID_V1_SR       ; S=$F, R=8
        RTS
