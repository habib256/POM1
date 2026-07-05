; ============================================================================
; beep_sfx_bank.inc -- a small starter bank of 1-bit SFX for beep_sfx.asm.
; ----------------------------------------------------------------------------
; `.include` this where you want the data to live (CODE/RODATA), then play a
; cue with its label:  LDX #<sfx_coin / LDY #>sfx_coin / JSR sfx_play  (blocking)
; or arm it with sfx_start and drive sfx_tick per frame (non-blocking).
;
; Format (see beep.inc): 2-byte [period, length] steps; period=pitch (lower =
; deeper), length=duration in speaker half-cycles, period 0 = REST. Every table
; ends with a 0 length byte. These are hand-tuned illustrations of the three
; canonical SFX shapes -- the editor emits tables in exactly this form.
; ============================================================================

; --- coin / pickup : a quick two-note UP blip (bright, ascending) -----------
sfx_coin:
        .byte $60, $18          ; lower note, short
        .byte $40, $28          ; higher note, held
        .byte $00, $00          ; end

; --- laser / shoot : a fast DOWNWARD pitch sweep (falling zap) ---------------
sfx_laser:
        .byte $18, $06
        .byte $22, $06
        .byte $2C, $06
        .byte $36, $06
        .byte $40, $06
        .byte $4A, $06          ; period rising -> pitch falling
        .byte $00, $00

; --- hit / explosion : low buzz + a rest + a lower thud (impact) -------------
sfx_hit:
        .byte $70, $20          ; low buzz
        .byte $00, $04          ; brief silence (rest: period 0)
        .byte $90, $30          ; deeper thud, longer
        .byte $00, $00
