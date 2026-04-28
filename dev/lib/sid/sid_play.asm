; ============================================================================
; sid_play.asm -- voice-1 note triggering for SID jingles + SFX
; ============================================================================
; Three small routines targeting voice 1 (the most common single-voice
; usage). Voices 2 and 3 follow the same pattern — duplicate and adjust
; the register addresses if you need polyphony.
;
;   sid_v1_note A      -- A = note index (0..95, see sid_notes.inc).
;                         Looks up the 16-bit frequency from
;                         sid_notes_lo/hi and writes it to the V1
;                         frequency registers. Does NOT trigger the
;                         gate — caller calls sid_v1_gate next.
;                         Clobbers A, X. Y preserved.
;
;   sid_v1_gate A      -- A = waveform mask (e.g. SID_TRI, SID_PULSE,
;                         SID_SAW, SID_NOISE) — bit 0 (gate) is ORed in.
;                         Writes A | SID_GATE to SID_V1_CR. This is the
;                         "trigger" — ADSR starts attack phase.
;                         Clobbers A. X, Y preserved.
;
;   sid_v1_off         -- gate off (writes 0 to SID_V1_CR). Triggers the
;                         release phase of the ADSR envelope.
;                         Clobbers A. X, Y preserved.
;
; Typical usage (single voice, plays a note for ~250 ms):
;       LDA #NOTE_C4
;       JSR sid_v1_note
;       LDA #SID_TRI
;       JSR sid_v1_gate
;       LDA #250
;       JSR delay_ms_a
;       JSR sid_v1_off
;
; Caller responsibility:
;   - sid.inc + sid_notes.inc must be in scope (this module .includes
;     sid.inc but NOT sid_notes.inc — caller chooses where the table
;     data lives in their address space).
; ============================================================================

.include "sid.inc"

.segment "CODE"

; ----------------------------------------------------------------------------
; sid_v1_note -- A = note index, set V1 frequency. No gate trigger.
; ----------------------------------------------------------------------------
sid_v1_note:
        TAX
        LDA     sid_notes_lo,X
        STA     SID_V1_FREQLO
        LDA     sid_notes_hi,X
        STA     SID_V1_FREQHI
        RTS

; ----------------------------------------------------------------------------
; sid_v1_gate -- A = waveform mask, write A | SID_GATE to SID_V1_CR.
;                Triggers ADSR attack. Implicit retrigger if the gate
;                was already on (SID re-enters attack on every gate edge).
; ----------------------------------------------------------------------------
sid_v1_gate:
        ORA     #SID_GATE
        STA     SID_V1_CR
        RTS

; ----------------------------------------------------------------------------
; sid_v1_off -- write 0 to SID_V1_CR. ADSR enters release phase.
; ----------------------------------------------------------------------------
sid_v1_off:
        LDA     #$00
        STA     SID_V1_CR
        RTS
