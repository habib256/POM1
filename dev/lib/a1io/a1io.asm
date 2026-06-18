; ============================================================================
; a1io.asm -- A1-IO/RTC virtual-register reader
; ============================================================================
; Single primitive that the entire register set hangs off:
;
;   a1io_read_reg -- X = register index (0..23), returns A = value.
;                    Spins on the broadcast cycle until the requested
;                    register ID appears on PORTA, then latches the
;                    value from PORTB. Worst-case wait ≈ 24 × 100 ≈
;                    2400 CPU cycles (~2.3 ms at 1.022 MHz).
;                    Clobbers A. Y preserved. X preserved.
;
; Higher-level helpers are one-liners — caller does:
;
;       LDX #A1IO_REG_HOURS
;       JSR a1io_read_reg
;       STA my_h
;
; Convenience wrappers for full date+time, ADC sweeps, etc. are
; project-specific (different output layouts). Build them on top of
; a1io_read_reg in your project source.
;
; ZP usage: 1 byte (a1io_target). Default declared via .ifndef so
; ZP-tight projects can alias to existing scratch:
;       a1io_target = my_byte0
;       .include "a1io.asm"
;
; Caller responsibility: a1io.inc included (this module includes it).
; ============================================================================

.include "a1io.inc"

.ifndef a1io_target
.segment "ZEROPAGE"
a1io_target:    .res 1
.endif

.segment "CODE"

a1io_read_reg:
        STX     a1io_target
@poll:  LDA     VIA_IRA                 ; consumes strobe; A bit 7 = strobe
        BPL     @poll                   ; loop while strobe = 0 (no new reg yet)
        AND     #$1F                    ; low 5 bits = reg index; protocol broadcasts
                                        ; only 0..23, the spare codes are headroom
        CMP     a1io_target
        BNE     @poll                   ; not the reg we want; wait next period
        LDA     VIA_IRB                 ; latch the value
        RTS
