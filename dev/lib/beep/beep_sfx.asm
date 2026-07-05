; ============================================================================
; beep_sfx.asm -- data-driven 1-bit SFX player for the ACI speaker ($C030).
; ----------------------------------------------------------------------------
; Plays the compact SFX tables described in beep.inc (2-byte [period,length]
; steps, length=0 terminates). This is the RUNTIME the SID/beeper editor will
; export to: an editor "sound effect" is exactly one of these tables plus a
; JSR to one of the entry points below.
;
;   sfx_start  X=lo, Y=hi of an SFX table  -- arm it (does not play a note yet).
;   sfx_tick   -- play ONE step (a short burst) of the armed SFX, then advance.
;               NON-BLOCKING ACROSS STEPS: call once per video frame and the SFX
;               plays out over as many frames as it has steps, so a game keeps
;               running between steps. No-op when idle -> safe to call every
;               frame unconditionally. Clobbers A,X,Y.
;   sfx_active A != 0 while an SFX is still playing (test after sfx_tick).
;   sfx_play   X=lo, Y=hi -- BLOCKING: play the whole SFX now (title jingles /
;               death stings where blocking the CPU is fine). Clobbers A,X,Y.
;
; A single burst still blocks for its own (short, ~ms) duration -- 1-bit sound
; is CPU-toggled, there is no timer. `sfx_tick` just bounds that to one step.
;
; ZERO PAGE: sfx_ptr/sfx_per/sfx_len (.exportzp'd). The player owns them; a
; caller sharing the apple1/zp.inc pool must not alias these three.
;
; Requires beep.inc in scope (this module .includes it). ACI enabled + no
; audio-stream tape -- see beep.inc REQUIREMENTS.
; ============================================================================

.include "beep.inc"

.exportzp sfx_ptr, sfx_per, sfx_len
.export   sfx_start, sfx_tick, sfx_active, sfx_play

.segment "ZEROPAGE"
sfx_ptr:  .res 2          ; -> current step in the armed SFX; hi=0 => idle
sfx_per:  .res 1          ; current step period  (pitch)
sfx_len:  .res 1          ; current step length  (duration, in half-cycles)

.segment "CODE"

; ----------------------------------------------------------------------------
; sfx_start -- X=lo, Y=hi of an SFX table. Arms it; first sound on next tick.
; ----------------------------------------------------------------------------
sfx_start:
        stx     sfx_ptr
        sty     sfx_ptr+1
        rts

; ----------------------------------------------------------------------------
; sfx_active -- A = hi byte of the step pointer: 0 when the SFX has ended.
; ----------------------------------------------------------------------------
sfx_active:
        lda     sfx_ptr+1
        rts

; ----------------------------------------------------------------------------
; sfx_tick -- play the current step, then advance. No-op when idle.
; ----------------------------------------------------------------------------
sfx_tick:
        lda     sfx_ptr+1
        bne     @go
        rts                     ; idle: nothing armed
@go:
        ldy     #0
        lda     (sfx_ptr),y     ; period (pitch)
        sta     sfx_per
        iny
        lda     (sfx_ptr),y     ; length (duration)
        bne     @play
        lda     #0              ; length 0 = terminator -> back to idle
        sta     sfx_ptr+1
        rts
@play:
        sta     sfx_len
        jsr     sfx_burst       ; sound this step (blocks for the burst only)
        lda     sfx_ptr         ; advance to the next 2-byte step
        clc
        adc     #2
        sta     sfx_ptr
        bcc     @done
        inc     sfx_ptr+1
@done:
        rts

; ----------------------------------------------------------------------------
; sfx_burst -- sound sfx_len speaker half-cycles at pitch sfx_per.
;   sfx_per > 0 : toggle the speaker, then delay sfx_per (square wave).
;   sfx_per = 0 : REST -- no toggle, just the (maximal) delay -> a silent gap.
; ----------------------------------------------------------------------------
sfx_burst:
        ldx     sfx_len
@hc:
        lda     sfx_per
        beq     @rest           ; period 0 => rest: skip the toggle
        lda     BEEP_TOGGLE     ; click the ACI speaker (side-effecting read)
@rest:
        ldy     sfx_per         ; inner pitch delay (Y=0 -> 256 iters = long rest)
@dly:
        dey
        bne     @dly
        dex
        bne     @hc
        rts

; ----------------------------------------------------------------------------
; sfx_play -- X=lo, Y=hi. Blocking: run every step of the SFX back to back.
; ----------------------------------------------------------------------------
sfx_play:
        jsr     sfx_start
@loop:
        jsr     sfx_tick
        lda     sfx_ptr+1
        bne     @loop
        rts
