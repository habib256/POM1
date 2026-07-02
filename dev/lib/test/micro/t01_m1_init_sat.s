; ============================================================================
; t01_m1_init_sat.s — micro-test: tms9918m1 init_vdp_g1 + disable_sprites
; ============================================================================
; GUARDS: the May-2026 ghost-sprite bug (LOGO demo2 + Life CodeTank): a single
;   $D0 at SAT[0].Y was not enough — power-on noise in SAT entries past slot 0
;   showed as floating sprites. init_vdp_g1 must park the WHOLE SAT
;   ($D0 then 127 x $D1) and wipe name + pattern tables. If disable_sprites
;   ever regresses to the old single-terminator form, the $1B04/$1B7F reads
;   below stop returning $D1 and this test fails. Zero-DROP also pins the
;   silicon-strict pacing of init (pad18 bridges) and of the read-back loop.
;
; POM1-LIB-MICRO-TEST
; LIBS: tms9918/tms9918m1.asm tms9918/tms9918_pad.asm
; CFG: micro.cfg
; PRESET: 1
; LOAD: 0300
; RUN: 0300
; STEPS: 120000
; EXPECT: 0F00 A5 D0 D1 D1 D1 D1 D1 D1 00 00 00 00 00 00
; ============================================================================

.include "apple1.inc"
.include "tms9918.inc"

.import init_vdp_g1, vdp_set_read
.importzp vdp_lo, vdp_hi
.import tms9918_pad18

; tms9918m1.asm .importzp's a caller-owned `tmp` scratch byte.
.segment "ZEROPAGE"
tmp:    .res 1
.exportzp tmp

MB = $0F00

.segment "CODE"
main:
        APPLE1_PREAMBLE

        JSR     init_vdp_g1     ; regs + 16 KB wipe + SAT park (display ends ON)

        ; --- SAT head $1B00..: expect $D0 then $D1 x5 ----------------------
        LDA     #$00
        STA     vdp_lo
        LDA     #$1B
        STA     vdp_hi
        JSR     vdp_set_read
        LDX     #1              ; MB+1..MB+6
        LDY     #6
        JSR     read_y_bytes

        ; --- SAT tail $1B7F: 128th parked byte, expect $D1 -----------------
        LDA     #$7F
        STA     vdp_lo
        LDA     #$1B
        STA     vdp_hi
        JSR     vdp_set_read
        LDY     #1              ; MB+7
        JSR     read_y_bytes

        ; --- name table $1800..: expect 00 x4 (wiped) ----------------------
        LDA     #$00
        STA     vdp_lo
        LDA     #$18
        STA     vdp_hi
        JSR     vdp_set_read
        LDY     #4              ; MB+8..MB+11
        JSR     read_y_bytes

        ; --- pattern table $0000..: expect 00 x2 (wiped) -------------------
        LDA     #$00
        STA     vdp_lo
        STA     vdp_hi
        JSR     vdp_set_read
        LDY     #2              ; MB+12..MB+13
        JSR     read_y_bytes

        LDA     #$A5            ; magic LAST — a crash never fakes a pass
        STA     MB
spin:   JMP     spin

; read_y_bytes: Y bytes from the primed VDP read stream into MB+X.
;   Display is ON after init_vdp_g1, so pace each read with pad18
;   (silicon-strict gates data-port READS too — 'r' drops).
read_y_bytes:
        LDA     VDP_DATA
        STA     MB,X
        JSR     tms9918_pad18
        INX
        DEY
        BNE     read_y_bytes
        RTS
