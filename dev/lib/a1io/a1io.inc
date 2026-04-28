; ============================================================================
; a1io.inc -- equates for the P-LAB A1-IO & RTC card (65C22 + ATMEGA32 + DS3231)
; ============================================================================
; The card occupies VIA window $2000-$200F. Inside an emulated ATMEGA32
; broadcasts 24 virtual registers on a 100-cycle period via PORTA STROBE
; (bit 7) + PORTB DATA. Read-only on the CPU side: write back to PORTB
; cycles the broadcast index for sync.
;
; Bus protocol (per the emulator's A1IO_RTC.cpp):
;   $2000  VIA_IRB / PORTB  -- value of the currently-broadcast register
;   $2001  VIA_IRA / PORTA  -- bit 7 = STROBE, bits 0-4 = register index (0..23)
;
; The strobe goes HIGH when the ATMEGA latches a new register, and
; clears as soon as the CPU does an LDA on PORTA. Two read patterns:
;   - Fast poll: spin on `LDA VIA_IRA / BPL` (consumes strobe each
;     iteration; the next strobe arrives within ~100 cycles).
;   - Targeted: spin until the desired reg ID appears, then read PORTB.
;     a1io_read_reg below implements this.
;
; ⚠ Mutex with GEN2 HGR: both cards live at $2000+. POM1 multiplexing-
; fantasy presets coexist them, real hardware does not — see CLAUDE.md
; "Parmigiani's golden rule".
;
; Idempotent: outer .ifndef guard.
; ============================================================================

.ifndef _A1IO_INC_LOADED_
_A1IO_INC_LOADED_ = 1

VIA_IRB     = $2000     ; PORTB data (broadcast register value)
VIA_IRA     = $2001     ; PORTA data (bit 7 strobe + bits 0-4 reg index)

; --- Register map (broadcast in cyclic order 0..23) ------------------------
A1IO_REG_HOURS    = 0   ; 0..23 (24-hour)
A1IO_REG_MINUTES  = 1   ; 0..59
A1IO_REG_SECONDS  = 2   ; 0..59
A1IO_REG_DAY      = 3   ; 1..31
A1IO_REG_MONTH    = 4   ; 1..12
A1IO_REG_YEAR     = 5   ; year - 2000 (0..255)
A1IO_REG_DS3231_T = 6   ; DS3231 die temperature (°C)
A1IO_REG_PROBE_HI = 7   ; DS18B20 probe, high byte (0 = disabled)
A1IO_REG_PROBE_LO = 8   ; DS18B20 probe, low byte
A1IO_REG_RESV9    = 9   ; reserved
A1IO_REG_ADC0     = 10  ; analog input 0
A1IO_REG_ADC1     = 11
A1IO_REG_ADC2     = 12
A1IO_REG_ADC3     = 13
A1IO_REG_ADC4     = 14
A1IO_REG_ADC5     = 15
A1IO_REG_ADC6     = 16
A1IO_REG_ADC7     = 17  ; analog input 7
A1IO_REG_RESV18   = 18  ; reserved
A1IO_REG_RESV19   = 19  ; reserved
A1IO_REG_DIN0     = 20  ; digital input 0 (0 or 1)
A1IO_REG_DIN1     = 21
A1IO_REG_DIN2     = 22
A1IO_REG_DIN3     = 23  ; digital input 3

A1IO_NUM_REGS     = 24

; --- 16-bit shift-register output (74HC164 cascade via VIA SR) -------------
; Writing to VIA SR ($200A) shifts bits out to the cascaded shift registers.
; Outputs 0-7 are on chip 1, 8-15 on chip 2 (MSB-first per byte).
VIA_SR      = $200A     ; shift register (write-only output)

.endif  ; _A1IO_INC_LOADED_
