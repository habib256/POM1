; ============================================================================
; sprites_unliving.asm  --  8 sprites (16x16, TMS9918 sprite mode)
; ----------------------------------------------------------------------------
; SCROLL-O-SPRITES "The Unliving" by Quale, May 2013, CC-BY-3.0.
; Lifted from pic/undefined - Imgur.png by tools/extract_scroll_sprites.py.
;
; Layout: each 16x16 sprite occupies 32 bytes -- the first 16 bytes are
; the left half (column 0..7), the next 16 the right half (column 8..15).
; Native TMS9918 16x16 sprite layout: stream the 32 bytes into a
; sprite-pattern slot starting at base $3800 + slot*32.
; ============================================================================
.export undead_undead_pat, undead_skull_small_pat, undead_skeleton_pat, undead_crossbones_big_pat, undead_death_pat
.export undead_ghost_pat, undead_wraith_pat, undead_shroud_pat

.segment "CODE"

; slot 01/8 of "The Unliving" row -- skull (renamed UNDEAD: this is the
; canonical "weakest undead" sprite for roguelike consumers; the literal
; Quale label was "skull" but the rogue project treats it as the generic
; first-tier undead).
undead_undead_pat:
        .byte $00, $00, $17, $0F, $09, $09, $0F, $2E
        .byte $0C, $04, $38, $57, $47, $07, $06, $02
        .byte $00, $02, $E0, $F0, $F0, $D0, $F0, $B0
        .byte $30, $22, $18, $D4, $E4, $E0, $60, $50
; slot 02/8 of "The Unliving" row -- skull_small
undead_skull_small_pat:
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $1B, $27, $04, $08, $08
        .byte $00, $00, $00, $00, $18, $18, $10, $00
        .byte $78, $70, $F0, $F8, $FC, $CC, $84, $80
; slot 03/8 of "The Unliving" row -- crossbones (renamed SKELETON:
; roguelike consumers use these crossed bones as the second-tier
; undead "skeleton" — visually a warrior reduced to bones).
undead_skeleton_pat:
        .byte $00, $00, $07, $0F, $0F, $09, $09, $0E
        .byte $03, $03, $18, $27, $31, $33, $04, $04
        .byte $00, $00, $E0, $F0, $F0, $90, $90, $70
        .byte $C0, $40, $18, $E4, $8C, $CC, $20, $20
; slot 04/8 of "The Unliving" row -- crossbones_big
undead_crossbones_big_pat:
        .byte $00, $03, $07, $37, $77, $40, $69, $2E
        .byte $03, $3B, $78, $07, $31, $33, $04, $04
        .byte $00, $C0, $E0, $EC, $EE, $02, $96, $74
        .byte $C0, $5C, $1E, $E0, $8C, $CC, $20, $20
; slot 05/8 of "The Unliving" row -- mummy (renamed DEATH: roguelike
; consumers use this wrapped/bandaged figure as the strongest tier
; "death" undead).
undead_death_pat:
        .byte $00, $63, $77, $4F, $48, $51, $51, $56
        .byte $53, $53, $78, $3F, $5F, $47, $47, $4C
        .byte $00, $F0, $E0, $F0, $10, $88, $88, $68
        .byte $C8, $48, $10, $F8, $FC, $E4, $F0, $D8
; slot 06/8 of "The Unliving" row -- ghost
undead_ghost_pat:
        .byte $00, $07, $0F, $1D, $1B, $5F, $7D, $7C
        .byte $3C, $1E, $1F, $1F, $0F, $03, $00, $00
        .byte $00, $E0, $F0, $F8, $B8, $DA, $7E, $3E
        .byte $3C, $B8, $F8, $F8, $FA, $FC, $00, $00
; slot 07/8 of "The Unliving" row -- wraith
undead_wraith_pat:
        .byte $00, $20, $70, $27, $0F, $1C, $18, $1C
        .byte $1C, $1A, $18, $0F, $07, $00, $10, $00
        .byte $00, $00, $00, $F0, $F8, $1C, $0C, $CC
        .byte $CC, $1C, $F8, $F0, $84, $00, $00, $00
; slot 08/8 of "The Unliving" row -- shroud
undead_shroud_pat:
        .byte $00, $03, $07, $0F, $08, $10, $14, $10
        .byte $10, $00, $30, $57, $4F, $0F, $07, $0C
        .byte $00, $F0, $E0, $F0, $10, $08, $28, $08
        .byte $08, $00, $18, $D4, $E4, $E0, $F0, $D8
