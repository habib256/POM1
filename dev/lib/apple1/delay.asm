; ============================================================================
; delay.asm -- approximate millisecond delay for Apple-1 (1.022727 MHz)
; ============================================================================
; One routine, no ZP usage (uses X for outer count, Y for inner).
;
;   delay_ms_a -- delay approximately A milliseconds. A = ms count.
;                 Clobbers A, X, Y.
;
; Calibration at POM1_CPU_CLOCK_HZ = 1 022 727 (real Apple-1 clock):
;   - Inner loop (LDY #203 / DEY / BNE @i): 2 + 5*202 + 4 = 1016 cycles
;   - Outer wrap (DEX / BNE @o): 5 cycles taken, 4 cycles falling through
;   - Per ms: ~1021 cycles → 0.998 ms, accuracy ~0.2%.
;
; Edge cases:
;   - A = 0  → wraps to 256 ms (TAX gives X=0, BNE @o falls through to
;              DEX immediately, X becomes $FF and the outer loop runs 256
;              times). If you don't want this, the caller must guard with
;              CMP/BEQ before JSR.
;   - Apple-1 has no interrupts so the loop is timing-stable.
;   - For delays > 255 ms, JSR multiple times (e.g. 1 s = LDA #250 / JSR
;     delay_ms_a / LDA #250 / JSR delay_ms_a / LDA #250 / JSR delay_ms_a /
;     LDA #250 / JSR delay_ms_a).
;
; Drop in via `.include "delay.asm"`.
; ============================================================================

.segment "CODE"

delay_ms_a:
        TAX                     ; X = ms count
@o:     LDY     #203            ; inner reload calibrated for ~1 ms @ 1.022 MHz
@i:     DEY
        BNE     @i
        DEX
        BNE     @o
        RTS
