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
        .byte $FF, $FE, $FC, $FC, $FC, $FC, $FC, $FC
        .byte $FE, $FF, $FE, $F8, $E0, $C6, $CF, $DF
        .byte $FF, $7F, $FF, $FF, $FF, $7F, $34, $02
        .byte $00, $00, $CB, $10, $07, $0E, $2C, $46
; Q_TR — top-right 16x16 of the 32x32 boss
        .byte $FF, $FC, $FE, $FE, $FE, $FE, $2C, $40
        .byte $00, $01, $D7, $0E, $E0, $70, $34, $62
        .byte $FF, $FF, $7F, $7F, $7F, $7F, $7F, $7F
        .byte $FF, $FF, $7F, $5F, $47, $63, $73, $3B
; Q_BL — bottom-left  16x16 of the 32x32 boss
        .byte $FE, $CC, $C6, $CE, $E3, $FF, $FE, $FE
        .byte $FF, $FD, $FD, $FB, $FF, $FF, $FF, $FF
        .byte $63, $71, $1C, $0D, $47, $93, $8A, $A7
        .byte $6B, $AD, $B5, $B5, $6D, $6E, $FF, $FF
; Q_BR — bottom-right 16x16 of the 32x32 boss
        .byte $C6, $8E, $38, $B3, $E7, $CC, $5B, $F5
        .byte $D5, $36, $5A, $6B, $AB, $BE, $FF, $FF
        .byte $3F, $73, $E3, $F3, $C7, $FF, $5F, $6F
        .byte $AF, $BF, $BF, $BF, $7F, $FF, $FF, $FF
