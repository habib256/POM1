// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Built-in 50-cue beeper SFX bank for the editor's bank browser. This is the
// C++-side mirror of dev/lib/beep/beep_sfx_bank50.inc (the game-loadable asm
// copy) — same cues, same [period,length] values, in the same table text so it
// parses through sfxbeep::parseSfxAsm. Kept as embedded text (not a file read)
// so the portable editor is self-contained on every host incl. WASM. If you edit
// one, edit the other; they are a static catalogue so drift is unlikely.

#ifndef SFXBEEP_SFX_BANK_H
#define SFXBEEP_SFX_BANK_H

namespace sfxbeep {

inline const char* kSfxBank50 =
    // pickups / rewards
    "sfx_coin:\n .byte $60,$18\n .byte $40,$28\n .byte $00,$00\n"
    "sfx_coin2:\n .byte $50,$14\n .byte $38,$24\n .byte $00,$00\n"
    "sfx_pickup:\n .byte $70,$10\n .byte $50,$10\n .byte $38,$18\n .byte $00,$00\n"
    "sfx_powerup:\n .byte $80,$0C\n .byte $60,$0C\n .byte $48,$0C\n .byte $38,$0C\n .byte $28,$18\n .byte $00,$00\n"
    "sfx_oneup:\n .byte $60,$10\n .byte $48,$10\n .byte $38,$10\n .byte $30,$20\n .byte $00,$00\n"
    // shots / lasers
    "sfx_laser:\n .byte $18,$06\n .byte $22,$06\n .byte $2C,$06\n .byte $36,$06\n .byte $40,$06\n .byte $4A,$06\n .byte $00,$00\n"
    "sfx_laser2:\n .byte $14,$05\n .byte $20,$05\n .byte $2C,$05\n .byte $38,$05\n .byte $44,$05\n .byte $00,$00\n"
    "sfx_shoot:\n .byte $20,$08\n .byte $30,$08\n .byte $44,$08\n .byte $00,$00\n"
    "sfx_zap:\n .byte $16,$08\n .byte $30,$08\n .byte $60,$06\n .byte $00,$00\n"
    "sfx_blaster:\n .byte $1C,$06\n .byte $28,$06\n .byte $34,$06\n .byte $40,$0A\n .byte $00,$00\n"
    // explosions / impacts
    "sfx_explosion:\n .byte $A0,$18\n .byte $C0,$18\n .byte $E0,$20\n .byte $F0,$28\n .byte $00,$00\n"
    "sfx_boom:\n .byte $B0,$20\n .byte $D0,$20\n .byte $F0,$30\n .byte $00,$00\n"
    "sfx_hit:\n .byte $70,$18\n .byte $00,$04\n .byte $90,$20\n .byte $00,$00\n"
    "sfx_hurt:\n .byte $50,$08\n .byte $70,$08\n .byte $90,$10\n .byte $00,$00\n"
    "sfx_damage:\n .byte $40,$06\n .byte $80,$06\n .byte $40,$06\n .byte $80,$0C\n .byte $00,$00\n"
    // movement
    "sfx_jump:\n .byte $70,$08\n .byte $58,$08\n .byte $44,$08\n .byte $34,$08\n .byte $00,$00\n"
    "sfx_hop:\n .byte $60,$06\n .byte $48,$06\n .byte $38,$0A\n .byte $00,$00\n"
    "sfx_bounce:\n .byte $50,$08\n .byte $38,$08\n .byte $50,$08\n .byte $00,$00\n"
    "sfx_land:\n .byte $60,$08\n .byte $90,$08\n .byte $C0,$10\n .byte $00,$00\n"
    "sfx_thud:\n .byte $A0,$10\n .byte $D0,$18\n .byte $00,$00\n"
    "sfx_step:\n .byte $80,$06\n .byte $A0,$08\n .byte $00,$00\n"
    // UI / menu
    "sfx_click:\n .byte $40,$04\n .byte $00,$00\n"
    "sfx_select:\n .byte $50,$08\n .byte $38,$0C\n .byte $00,$00\n"
    "sfx_confirm:\n .byte $48,$0A\n .byte $30,$14\n .byte $00,$00\n"
    "sfx_cancel:\n .byte $38,$0A\n .byte $58,$14\n .byte $00,$00\n"
    "sfx_deny:\n .byte $90,$0C\n .byte $00,$06\n .byte $90,$0C\n .byte $00,$00\n"
    "sfx_error:\n .byte $A0,$10\n .byte $B0,$10\n .byte $A0,$10\n .byte $00,$00\n"
    "sfx_blip:\n .byte $30,$06\n .byte $00,$00\n"
    "sfx_beep:\n .byte $40,$10\n .byte $00,$00\n"
    // bells / tones
    "sfx_ding:\n .byte $28,$0C\n .byte $20,$14\n .byte $00,$00\n"
    "sfx_bell:\n .byte $30,$08\n .byte $24,$08\n .byte $30,$08\n .byte $24,$18\n .byte $00,$00\n"
    "sfx_chime:\n .byte $2C,$0A\n .byte $22,$0A\n .byte $1C,$14\n .byte $00,$00\n"
    // alarms / warnings
    "sfx_alarm:\n .byte $30,$10\n .byte $48,$10\n .byte $30,$10\n .byte $48,$10\n .byte $00,$00\n"
    "sfx_siren:\n .byte $40,$18\n .byte $60,$18\n .byte $40,$18\n .byte $60,$18\n .byte $00,$00\n"
    "sfx_warning:\n .byte $50,$0C\n .byte $70,$0C\n .byte $50,$0C\n .byte $70,$0C\n .byte $00,$00\n"
    "sfx_buzz:\n .byte $60,$04\n .byte $68,$04\n .byte $60,$04\n .byte $68,$04\n .byte $60,$04\n .byte $68,$04\n .byte $00,$00\n"
    // sci-fi
    "sfx_teleport:\n .byte $80,$06\n .byte $60,$06\n .byte $48,$06\n .byte $38,$06\n .byte $2C,$06\n .byte $20,$06\n .byte $18,$0A\n .byte $00,$00\n"
    "sfx_warp:\n .byte $20,$06\n .byte $40,$06\n .byte $60,$06\n .byte $40,$06\n .byte $20,$0A\n .byte $00,$00\n"
    "sfx_charge:\n .byte $A0,$0C\n .byte $80,$0C\n .byte $60,$0C\n .byte $48,$0C\n .byte $34,$18\n .byte $00,$00\n"
    "sfx_thrust:\n .byte $90,$08\n .byte $94,$08\n .byte $90,$08\n .byte $94,$08\n .byte $00,$00\n"
    "sfx_engine:\n .byte $B0,$0C\n .byte $B8,$0C\n .byte $B0,$0C\n .byte $B8,$0C\n .byte $00,$00\n"
    // magic / sparkle
    "sfx_heal:\n .byte $50,$10\n .byte $40,$10\n .byte $34,$18\n .byte $00,$00\n"
    "sfx_magic:\n .byte $30,$06\n .byte $24,$06\n .byte $30,$06\n .byte $24,$06\n .byte $1C,$0C\n .byte $00,$00\n"
    "sfx_sparkle:\n .byte $20,$05\n .byte $18,$05\n .byte $20,$05\n .byte $18,$0A\n .byte $00,$00\n"
    // water / nature
    "sfx_bubble:\n .byte $60,$06\n .byte $48,$06\n .byte $38,$06\n .byte $00,$00\n"
    "sfx_splash:\n .byte $40,$06\n .byte $70,$06\n .byte $50,$06\n .byte $80,$0A\n .byte $00,$00\n"
    "sfx_chirp:\n .byte $28,$05\n .byte $1C,$08\n .byte $00,$00\n"
    "sfx_ricochet:\n .byte $18,$05\n .byte $28,$05\n .byte $18,$05\n .byte $28,$08\n .byte $00,$00\n"
    // fail / end
    "sfx_death:\n .byte $30,$0A\n .byte $40,$0A\n .byte $50,$0A\n .byte $70,$0A\n .byte $A0,$0A\n .byte $E0,$18\n .byte $00,$00\n"
    "sfx_gameover:\n .byte $40,$18\n .byte $00,$06\n .byte $50,$18\n .byte $00,$06\n .byte $70,$18\n .byte $00,$06\n .byte $A0,$28\n .byte $00,$00\n";

}  // namespace sfxbeep

#endif  // SFXBEEP_SFX_BANK_H
