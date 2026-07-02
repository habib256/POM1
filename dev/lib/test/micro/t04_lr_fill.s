; ============================================================================
; t04_lr_fill.s — micro-test: basicrt_tms lr_fill / lr_setw / lr_setr
; ============================================================================
; GUARDS: lr_fill's documented reliance on pad18 A-transparency (the fill byte
;   stays in A across JSR tms9918_pad18 in the inner loop — the consumer side
;   of t03's contract: a non-transparent pad fills VRAM with garbage), the
;   page-count loop (X pages exactly: first/last byte in, neighbours out) and
;   the lr_setw/lr_setr address latches. basicrt_tms.s is an .include-style
;   lib whose lr_* symbols are module-local, so this driver textually includes
;   it (with RT_COLOR defined to arm the RT_LR lo-res section) exactly like a
;   real consumer program does.
;
; POM1-LIB-MICRO-TEST
; LIBS: tms9918/tms9918_pad.asm
; CFG: micro.cfg
; PRESET: 1
; LOAD: 0300
; RUN: 0300
; STEPS: 60000
; EXPECT: 0F00 A5 00 5C 5C 5C 5C FF
; ============================================================================

RT_COLOR = 1                    ; arm the RT_LR lo-res section (lr_fill et al.)
.include "basicrt_tms.s"        ; textual include — lr_* are module-local
.include "apple1.inc"

MB = $0F00

.segment "ENTRY"
main:
        APPLE1_PREAMBLE

        ; Power-on registers are zeroed -> display blanked: the canonical
        ; window for fill bursts. Fill 2 pages ($0400-$05FF) with $5C.
        LDA     #$00
        STA     lr_al
        LDA     #$04
        STA     lr_ah
        LDX     #2              ; 2 x 256 bytes
        LDA     #$5C            ; fill byte — must survive the in-loop pad18
        JSR     lr_fill

        ; Read back the fill boundary fenceposts. Unfilled VRAM keeps POM1's
        ; deterministic power-on zebra seed (even addr = $FF, odd = $00 —
        ; TMS9918::reset), which makes the fenceposts sharp:
        ;   $03FF -> 00   (one before, odd -> seeded $00)
        ;   $0400 -> 5C   (first)
        ;   $04FF -> 5C   (page-1 last)
        ;   $0500 -> 5C   (page-2 first)
        ;   $05FF -> 5C   (very last)
        ;   $0600 -> FF   (one after, even -> seed intact: no overrun)
        LDX     #1              ; MB+1
        LDA     #$FF
        LDY     #$03
        JSR     sample          ; $03FF
        LDA     #$00
        LDY     #$04
        JSR     sample          ; $0400
        LDA     #$FF
        LDY     #$04
        JSR     sample          ; $04FF
        LDA     #$00
        LDY     #$05
        JSR     sample          ; $0500
        LDA     #$FF
        LDY     #$05
        JSR     sample          ; $05FF
        LDA     #$00
        LDY     #$06
        JSR     sample          ; $0600

        LDA     #$A5
        STA     MB
spin:   JMP     spin

; sample: read VRAM byte at Y:A (hi:lo) into MB+X, X += 1.
sample:
        STA     lr_al
        STY     lr_ah
        JSR     lr_setr         ; ends with a pad18 cushion
        LDA     VDP_DATA
        STA     MB,X
        JSR     tms9918_pad18
        INX
        RTS
