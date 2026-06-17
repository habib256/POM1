; ============================================================================
; sprites_boss.asm  --  4 x 16x16 sprite slots tiled into a 32x32 boss
; ----------------------------------------------------------------------------
; Auto-generated from Hexany's Monster Menagerie creature_024.png (CC0).
; Source: https://hexany-ives.itch.io/hexanys-monster-menagerie
;
; Each 16x16 block is one TMS9918 sprite slot (32 bytes, TL/BL/TR/BR
; in 8x8 chunks). place_all_sprites in TMS_Rogue.asm paints them as
; a 2x2 tile around the boss anchor (col*16 .. col*16+31 px).
;
; DO NOT EDIT BY HAND. Re-run tools/extract_hexany_boss_sprite.py
; to regenerate from a different source PNG.
; ============================================================================

.export boss_sprite_pats

.segment "CODE"

boss_sprite_pats:
; Q_TL — top-left  16x16 of the 32x32 boss
        .byte $00, $01, $03, $03, $03, $03, $03, $03
        .byte $01, $00, $01, $07, $1F, $39, $30, $20
        .byte $00, $80, $00, $00, $00, $80, $CB, $FD
        .byte $FF, $FF, $34, $EF, $F8, $F1, $D3, $B9
; Q_TR — top-right 16x16 of the 32x32 boss
        .byte $00, $03, $01, $01, $01, $01, $D3, $BF
        .byte $FF, $FE, $28, $F1, $1F, $8F, $CB, $9D
        .byte $00, $00, $80, $80, $80, $80, $80, $80
        .byte $00, $00, $80, $A0, $B8, $9C, $8C, $C4
; Q_BL — bottom-left  16x16 of the 32x32 boss
        .byte $01, $33, $39, $31, $1C, $00, $01, $01
        .byte $00, $02, $02, $04, $00, $00, $00, $00
        .byte $9C, $8E, $E3, $F2, $B8, $6C, $75, $58
        .byte $94, $52, $4A, $4A, $92, $91, $00, $00
; Q_BR — bottom-right 16x16 of the 32x32 boss
        .byte $39, $71, $C7, $4C, $18, $33, $A4, $0A
        .byte $2A, $C9, $A5, $94, $54, $41, $00, $00
        .byte $C0, $8C, $1C, $0C, $38, $00, $A0, $90
        .byte $50, $40, $40, $40, $80, $00, $00, $00
