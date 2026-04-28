; ============================================================================
; sid.inc -- equates for the P-LAB A1-SID card (MOS 6581 / 8580)
; ============================================================================
; The SID lives at $C800-$CFFF on the Apple-1 bus. 29 visible registers,
; addr & 0x1F (the chip mirrors every $20 bytes inside the window — but
; the canonical addresses below are what every program in this tree uses).
;
; Three voices, 7 registers per voice, plus filter/volume/paddles globals.
;
; Pure equates, no segments. Safe to include from any segment. Outer
; guard makes the include idempotent — sid_init.asm and sid_play.asm
; each include sid.inc, so a project that pulls those in must not
; double-define every symbol.
; ============================================================================

.ifndef _SID_INC_LOADED_
_SID_INC_LOADED_ = 1

; --- Voice 1 ($C800 + 0..6) ------------------------------------------------
SID_V1_FREQLO   = $C800     ; 16-bit frequency, low byte
SID_V1_FREQHI   = $C801     ; 16-bit frequency, high byte
SID_V1_PWMLO    = $C802     ; pulse-width LSB
SID_V1_PWMHI    = $C803     ; pulse-width MSB (low 4 bits used)
SID_V1_CR       = $C804     ; control: gate + waveform mask
SID_V1_AD       = $C805     ; attack (high nibble) / decay (low nibble)
SID_V1_SR       = $C806     ; sustain (high nibble) / release (low nibble)

; --- Voice 2 ($C807 + 0..6) ------------------------------------------------
SID_V2_FREQLO   = $C807
SID_V2_FREQHI   = $C808
SID_V2_PWMLO    = $C809
SID_V2_PWMHI    = $C80A
SID_V2_CR       = $C80B
SID_V2_AD       = $C80C
SID_V2_SR       = $C80D

; --- Voice 3 ($C80E + 0..6) ------------------------------------------------
SID_V3_FREQLO   = $C80E
SID_V3_FREQHI   = $C80F
SID_V3_PWMLO    = $C810
SID_V3_PWMHI    = $C811
SID_V3_CR       = $C812
SID_V3_AD       = $C813
SID_V3_SR       = $C814

; --- Globals ($C815-$C81A) -------------------------------------------------
SID_FCLO        = $C815     ; filter cutoff frequency, low (3 bits used)
SID_FCHI        = $C816     ; filter cutoff frequency, high
SID_RES_FILT    = $C817     ; resonance + filter routing per voice
SID_VOLUME      = $C818     ; mode (high nibble) + master volume (low nibble)
SID_PADDLE_X    = $C819     ; A1-SID paddle X (read-only on POM1)
SID_PADDLE_Y    = $C81A     ; A1-SID paddle Y (read-only on POM1)

; --- Waveform / control bits in CR (per voice) -----------------------------
SID_GATE        = $01       ; bit 0: 1 = ADSR runs, 0 = release
SID_SYNC        = $02       ; bit 1: oscillator sync to next voice
SID_RING        = $04       ; bit 2: ring modulation
SID_TEST        = $08       ; bit 3: noise reset / silence
SID_TRI         = $10       ; bit 4: triangle waveform
SID_SAW         = $20       ; bit 5: sawtooth
SID_PULSE       = $40       ; bit 6: pulse (PWM controlled by PWMLO/HI)
SID_NOISE       = $80       ; bit 7: noise

; --- Volume mode bits in $C818 (high nibble) -------------------------------
SID_FILT_LP     = $10       ; low-pass enable
SID_FILT_BP     = $20       ; band-pass enable
SID_FILT_HP     = $40       ; high-pass enable
SID_VOICE3_OFF  = $80       ; voice 3 muted (allows V3 as LFO)

.endif  ; _SID_INC_LOADED_
