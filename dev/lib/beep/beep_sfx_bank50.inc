; ============================================================================
; beep_sfx_bank50.inc -- a varied 50-cue bank of 1-bit beeper SFX.
; ----------------------------------------------------------------------------
; Ready-to-play tables for beep_sfx.asm (format: 2-byte [period,length] steps,
; length 0 ends; period = pitch, 0 = rest -- see beep.inc). `.include` this and
; play any cue by its label:  LDX #<sfx_coin / LDY #>sfx_coin / JSR sfx_play
; (blocking) or sfx_start + per-frame sfx_tick (non-blocking). The Beeper SFX
; editor loads the same 50 from its built-in bank browser (audition + tweak +
; re-export). Higher period = deeper note; a descending period = a falling
; sweep, an alternating period = a warble.
; ============================================================================

; --- pickups / rewards ------------------------------------------------------
sfx_coin:
        .byte $60, $18
        .byte $40, $28
        .byte $00, $00
sfx_coin2:
        .byte $50, $14
        .byte $38, $24
        .byte $00, $00
sfx_pickup:
        .byte $70, $10
        .byte $50, $10
        .byte $38, $18
        .byte $00, $00
sfx_powerup:
        .byte $80, $0C
        .byte $60, $0C
        .byte $48, $0C
        .byte $38, $0C
        .byte $28, $18
        .byte $00, $00
sfx_oneup:
        .byte $60, $10
        .byte $48, $10
        .byte $38, $10
        .byte $30, $20
        .byte $00, $00

; --- shots / lasers ---------------------------------------------------------
sfx_laser:
        .byte $18, $06
        .byte $22, $06
        .byte $2C, $06
        .byte $36, $06
        .byte $40, $06
        .byte $4A, $06
        .byte $00, $00
sfx_laser2:
        .byte $14, $05
        .byte $20, $05
        .byte $2C, $05
        .byte $38, $05
        .byte $44, $05
        .byte $00, $00
sfx_shoot:
        .byte $20, $08
        .byte $30, $08
        .byte $44, $08
        .byte $00, $00
sfx_zap:
        .byte $16, $08
        .byte $30, $08
        .byte $60, $06
        .byte $00, $00
sfx_blaster:
        .byte $1C, $06
        .byte $28, $06
        .byte $34, $06
        .byte $40, $0A
        .byte $00, $00

; --- explosions / impacts ---------------------------------------------------
sfx_explosion:
        .byte $A0, $18
        .byte $C0, $18
        .byte $E0, $20
        .byte $F0, $28
        .byte $00, $00
sfx_boom:
        .byte $B0, $20
        .byte $D0, $20
        .byte $F0, $30
        .byte $00, $00
sfx_hit:
        .byte $70, $18
        .byte $00, $04
        .byte $90, $20
        .byte $00, $00
sfx_hurt:
        .byte $50, $08
        .byte $70, $08
        .byte $90, $10
        .byte $00, $00
sfx_damage:
        .byte $40, $06
        .byte $80, $06
        .byte $40, $06
        .byte $80, $0C
        .byte $00, $00

; --- movement ---------------------------------------------------------------
sfx_jump:
        .byte $70, $08
        .byte $58, $08
        .byte $44, $08
        .byte $34, $08
        .byte $00, $00
sfx_hop:
        .byte $60, $06
        .byte $48, $06
        .byte $38, $0A
        .byte $00, $00
sfx_bounce:
        .byte $50, $08
        .byte $38, $08
        .byte $50, $08
        .byte $00, $00
sfx_land:
        .byte $60, $08
        .byte $90, $08
        .byte $C0, $10
        .byte $00, $00
sfx_thud:
        .byte $A0, $10
        .byte $D0, $18
        .byte $00, $00
sfx_step:
        .byte $80, $06
        .byte $A0, $08
        .byte $00, $00

; --- UI / menu --------------------------------------------------------------
sfx_click:
        .byte $40, $04
        .byte $00, $00
sfx_select:
        .byte $50, $08
        .byte $38, $0C
        .byte $00, $00
sfx_confirm:
        .byte $48, $0A
        .byte $30, $14
        .byte $00, $00
sfx_cancel:
        .byte $38, $0A
        .byte $58, $14
        .byte $00, $00
sfx_deny:
        .byte $90, $0C
        .byte $00, $06
        .byte $90, $0C
        .byte $00, $00
sfx_error:
        .byte $A0, $10
        .byte $B0, $10
        .byte $A0, $10
        .byte $00, $00
sfx_blip:
        .byte $30, $06
        .byte $00, $00
sfx_beep:
        .byte $40, $10
        .byte $00, $00

; --- bells / tones ----------------------------------------------------------
sfx_ding:
        .byte $28, $0C
        .byte $20, $14
        .byte $00, $00
sfx_bell:
        .byte $30, $08
        .byte $24, $08
        .byte $30, $08
        .byte $24, $18
        .byte $00, $00
sfx_chime:
        .byte $2C, $0A
        .byte $22, $0A
        .byte $1C, $14
        .byte $00, $00

; --- alarms / warnings ------------------------------------------------------
sfx_alarm:
        .byte $30, $10
        .byte $48, $10
        .byte $30, $10
        .byte $48, $10
        .byte $00, $00
sfx_siren:
        .byte $40, $18
        .byte $60, $18
        .byte $40, $18
        .byte $60, $18
        .byte $00, $00
sfx_warning:
        .byte $50, $0C
        .byte $70, $0C
        .byte $50, $0C
        .byte $70, $0C
        .byte $00, $00
sfx_buzz:
        .byte $60, $04
        .byte $68, $04
        .byte $60, $04
        .byte $68, $04
        .byte $60, $04
        .byte $68, $04
        .byte $00, $00

; --- sci-fi -----------------------------------------------------------------
sfx_teleport:
        .byte $80, $06
        .byte $60, $06
        .byte $48, $06
        .byte $38, $06
        .byte $2C, $06
        .byte $20, $06
        .byte $18, $0A
        .byte $00, $00
sfx_warp:
        .byte $20, $06
        .byte $40, $06
        .byte $60, $06
        .byte $40, $06
        .byte $20, $0A
        .byte $00, $00
sfx_charge:
        .byte $A0, $0C
        .byte $80, $0C
        .byte $60, $0C
        .byte $48, $0C
        .byte $34, $18
        .byte $00, $00
sfx_thrust:
        .byte $90, $08
        .byte $94, $08
        .byte $90, $08
        .byte $94, $08
        .byte $00, $00
sfx_engine:
        .byte $B0, $0C
        .byte $B8, $0C
        .byte $B0, $0C
        .byte $B8, $0C
        .byte $00, $00

; --- magic / sparkle --------------------------------------------------------
sfx_heal:
        .byte $50, $10
        .byte $40, $10
        .byte $34, $18
        .byte $00, $00
sfx_magic:
        .byte $30, $06
        .byte $24, $06
        .byte $30, $06
        .byte $24, $06
        .byte $1C, $0C
        .byte $00, $00
sfx_sparkle:
        .byte $20, $05
        .byte $18, $05
        .byte $20, $05
        .byte $18, $0A
        .byte $00, $00

; --- water / nature ---------------------------------------------------------
sfx_bubble:
        .byte $60, $06
        .byte $48, $06
        .byte $38, $06
        .byte $00, $00
sfx_splash:
        .byte $40, $06
        .byte $70, $06
        .byte $50, $06
        .byte $80, $0A
        .byte $00, $00
sfx_chirp:
        .byte $28, $05
        .byte $1C, $08
        .byte $00, $00
sfx_ricochet:
        .byte $18, $05
        .byte $28, $05
        .byte $18, $05
        .byte $28, $08
        .byte $00, $00

; --- fail / end -------------------------------------------------------------
sfx_death:
        .byte $30, $0A
        .byte $40, $0A
        .byte $50, $0A
        .byte $70, $0A
        .byte $A0, $0A
        .byte $E0, $18
        .byte $00, $00
sfx_gameover:
        .byte $40, $18
        .byte $00, $06
        .byte $50, $18
        .byte $00, $06
        .byte $70, $18
        .byte $00, $06
        .byte $A0, $28
        .byte $00, $00
