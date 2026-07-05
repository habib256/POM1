; ============================================================================
; beep.inc -- 1-bit "beeper" sound via the ACI speaker flip-flop.
; ----------------------------------------------------------------------------
; The Apple 1 has no tone generator: sound is made by TOGGLING the ACI output
; flip-flop and letting the deck speaker click. In POM1 a READ of any address
; in $C000-$C0FF EXCEPT $C081 (the tape-IN poll) flips the speaker level
; (Memory.cpp: the "ACI_toggle" bus handle -> CassetteDevice::toggleOutput).
; Reading it at a steady interval P produces a square wave of period 2*P, i.e.
; frequency = CPU_CLOCK / (2 * cycles-per-toggle). This is the same mechanism
; the GEN2 "A-1-CrazyCycle" chiptune uses.
;
; $C030 is the canonical toggle address (matches the Apple-II speaker soft
; switch and the CrazyCycle demo). Any $C0xx != $C081 works identically.
;
; REQUIREMENTS
;   - ACI must be enabled (Hardware menu / --enable is implicit on ACI presets).
;   - NO stream-mode tape (mp3/ogg/wav) inserted: while an audio stream plays,
;     CassetteDevice::fillAudioBuffer() does NOT mix the live pulse queue, so
;     toggles are silent. A program tape (or empty deck) is fine.
;
; SFX DATA FORMAT (consumed by beep_sfx.asm)
;   An "SFX" is a flat list of 2-byte STEPS, terminated by a step whose length
;   byte is 0:
;       .byte period, length     ; step 0
;       .byte period, length     ; step 1
;       ...
;       .byte $00,    $00        ; terminator (length = 0 ends the SFX)
;   period : inner-delay count -> PITCH. Larger = lower note. 0 = REST (silence
;            for the step's duration, no toggle) -- use for gaps/rhythm.
;   length : number of speaker half-cycles at this pitch -> DURATION of the step.
;   A pitch SWEEP (laser/coin/explosion) is just successive steps with a
;   rising/falling `period`. See beep_sfx_bank.inc for worked examples.
; ============================================================================

.ifndef BEEP_INC_
BEEP_INC_ = 1

BEEP_TOGGLE = $C030        ; read here toggles the ACI/speaker flip-flop

.endif ; BEEP_INC_
