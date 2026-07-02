; ============================================================================
; t05_sprite_attr_terminator.s — micro-test: sprite_triangle sprite_attr_write
; ============================================================================
; GUARDS: the SAT-terminator dodge — tri_y = 217 computes hardware
;   Y = 217 - 8 - 1 = $D0, which is the SAT chain TERMINATOR: a ship drifting
;   off the bottom edge would silently blank every higher-numbered sprite.
;   sprite_attr_write must nudge that one value to $D1 (off-screen, inert).
;   A re-introduced raw `SBC #9 / STA VDP_DATA` fails at MB+1. Also pins the
;   colour defensive mask (best-practices §3: colour & $0F — a stray bit 7
;   would set EARLY CLOCK and shift the sprite 32 px left) and the
;   $3B00 + slot*4 SAT addressing for both a dodged and a normal write.
;
; POM1-LIB-MICRO-TEST
; LIBS: tms9918/sprite_triangle.asm m6502/math.asm tms9918/tms9918_pad.asm
; CFG: micro.cfg
; PRESET: 1
; LOAD: 0300
; RUN: 0300
; STEPS: 5000
; EXPECT: 0F00 A5 D1 5C 0C 0F 5B 0C 00 04
; ============================================================================

.include "apple1.inc"
.include "tms9918.inc"

.import sprite_attr_write
.import tri_x, tri_y, tri_slot, tri_color
.import tms9918_pad18

; sprite_triangle.asm + math.asm consume caller-provided ZP/BSS slots
; (the shared-ZP contract documented in both lib headers).
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

        ; --- case 1: tri_y = 217 -> raw Y would be $D0, must be dodged to $D1
        LDA     #217
        STA     tri_y
        LDA     #100            ; X byte = 100 - 8 = $5C
        STA     tri_x
        LDA     #3
        STA     tri_slot        ; name = slot*4 = $0C (16x16: 4 names/slot)
        LDA     #$1F            ; colour with junk bits -> masked to $0F
        STA     tri_color
        LDA     #3              ; SAT entry 3 -> $3B0C
        JSR     sprite_attr_write

        ; --- case 2: ordinary Y (no dodge) at slot 0 ------------------------
        LDA     #100            ; Y byte = 100 - 9 = $5B
        STA     tri_y
        LDA     #20             ; X byte = 20 - 8 = $0C
        STA     tri_x
        LDA     #0
        STA     tri_slot        ; name = 0
        LDA     #$04
        STA     tri_color       ; colour = $04 (already clean)
        LDA     #0              ; SAT entry 0 -> $3B00
        JSR     sprite_attr_write

        ; --- read back SAT[3] then SAT[0] (display off: power-on regs) ------
        LDA     #$0C            ; $3B0C, read mode (bit 6 clear)
        STA     VDP_CTRL
        JSR     tms9918_pad18
        LDA     #$3B
        STA     VDP_CTRL
        JSR     tms9918_pad18
        LDX     #1              ; MB+1..4: D1 5C 0C 0F
        LDY     #4
        JSR     read_y_bytes

        LDA     #$00            ; $3B00, read mode
        STA     VDP_CTRL
        JSR     tms9918_pad18
        LDA     #$3B
        STA     VDP_CTRL
        JSR     tms9918_pad18
        LDX     #5              ; MB+5..8: 5B 0C 00 04
        LDY     #4
        JSR     read_y_bytes

        LDA     #$A5
        STA     MB
spin:   JMP     spin

read_y_bytes:
        LDA     VDP_DATA
        STA     MB,X
        JSR     tms9918_pad18
        INX
        DEY
        BNE     read_y_bytes
        RTS
