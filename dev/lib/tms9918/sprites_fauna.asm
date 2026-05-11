; ============================================================================
; sprites_fauna.asm  --  13 sprites (16x16, TMS9918 sprite mode)
; Labels _fauna_* : export cc65/C (voir demos/sprite_animals). Régénérer
; dev/lib/hgr/sprites/sprites_fauna_hgr.asm après édition :
;   python3 tools/build_hgr_sprites.py --only fauna
; ----------------------------------------------------------------------------
; SCROLL-O-SPRITES "Fauna" by Quale, May 2013, CC-BY-3.0.
; Lifted from pic/undefined - Imgur.png by tools/extract_scroll_sprites.py.
;
; Layout: each 16x16 sprite occupies 32 bytes -- the first 16 bytes are
; the left half (column 0..7), the next 16 the right half (column 8..15).
; Native TMS9918 16x16 sprite layout: stream the 32 bytes into a
; sprite-pattern slot starting at base $3800 + slot*32.
; ============================================================================
; Symboles avec préfixe _ pour linkage cc65 / C.
.export _fauna_dog_pat, _fauna_rabbit_pat, _fauna_spider_pat, _fauna_octopus_pat, _fauna_cat_pat
.export _fauna_snake_pat, _fauna_bat_pat, _fauna_kitten_pat, _fauna_bear_pat, _fauna_snail_pat
.export _fauna_lion_pat, _fauna_tiger_pat, _fauna_horse_pat

.segment "CODE"

; slot 01/13 of "Fauna" row -- dog
_fauna_dog_pat:
        .byte $00, $00, $00, $00, $18, $18, $0F, $56
        .byte $3F, $00, $07, $0F, $17, $07, $07, $0B
        .byte $00, $00, $00, $00, $60, $60, $80, $C0
        .byte $E2, $E4, $F4, $F4, $F4, $F4, $F4, $D8
; slot 02/13 of "Fauna" row -- rabbit
_fauna_rabbit_pat:
        .byte $00, $00, $00, $10, $24, $66, $66, $76
        .byte $7F, $7D, $77, $52, $40, $00, $00, $00
        .byte $00, $00, $00, $08, $24, $66, $66, $6E
        .byte $FE, $BE, $EE, $4A, $02, $00, $00, $00
; slot 03/13 of "Fauna" row -- spider
_fauna_spider_pat:
        .byte $00, $1E, $1F, $0F, $13, $04, $01, $2F
        .byte $1F, $1F, $1F, $2F, $07, $09, $02, $00
        .byte $00, $40, $90, $E0, $F4, $F8, $FC, $F8
        .byte $F4, $80, $20, $C8, $F0, $A8, $F8, $00
; slot 04/13 of "Fauna" row -- octopus
_fauna_octopus_pat:
        .byte $00, $13, $17, $27, $27, $13, $69, $5F
        .byte $0D, $3B, $49, $52, $12, $10, $08, $08
        .byte $00, $C8, $E8, $E4, $E4, $C8, $96, $FA
        .byte $B0, $DC, $92, $4A, $48, $08, $10, $10
; slot 05/13 of "Fauna" row -- cat
_fauna_cat_pat:
        .byte $00, $0E, $1F, $17, $10, $09, $00, $03
        .byte $08, $07, $07, $3D, $7B, $58, $48, $28
        .byte $00, $00, $00, $00, $40, $80, $20, $C0
        .byte $10, $E0, $E0, $BC, $DE, $1A, $12, $14
; slot 06/13 of "Fauna" row -- snake
_fauna_snake_pat:
        .byte $00, $00, $00, $00, $03, $07, $07, $0E
        .byte $6D, $6D, $2A, $22, $1C, $1F, $23, $54
        .byte $00, $00, $00, $00, $F8, $FC, $FC, $46
        .byte $BA, $AA, $6A, $E6, $7E, $7C, $80, $FC
; slot 07/13 of "Fauna" row -- bat
_fauna_bat_pat:
        .byte $00, $00, $0F, $38, $7F, $7F, $1F, $00
        .byte $01, $07, $0B, $0F, $0A, $08, $04, $03
        .byte $00, $80, $80, $00, $FC, $FE, $FE, $7E
        .byte $FE, $FC, $D0, $F0, $50, $10, $20, $C0
; slot 08/13 of "Fauna" row -- kitten
_fauna_kitten_pat:
        .byte $00, $06, $05, $07, $03, $23, $63, $60
        .byte $6E, $7F, $3F, $3F, $39, $31, $21, $21
        .byte $00, $06, $0A, $FE, $FC, $6C, $FC, $F0
        .byte $94, $0C, $FC, $FC, $8E, $02, $00, $00
; slot 09/13 of "Fauna" row -- bear
_fauna_bear_pat:
        .byte $00, $06, $05, $07, $22, $43, $4F, $5D
        .byte $5E, $3E, $3F, $3F, $37, $36, $26, $26
        .byte $00, $06, $FA, $FE, $F4, $FC, $0C, $98
        .byte $64, $F4, $0C, $DC, $1C, $1C, $3C, $2A
; slot 10/13 of "Fauna" row -- snail
_fauna_snail_pat:
        .byte $00, $1B, $17, $0F, $1F, $1D, $1F, $2E
        .byte $77, $79, $7C, $7E, $7F, $7F, $77, $37
        .byte $00, $F6, $FA, $FC, $FE, $EE, $FE, $1C
        .byte $3A, $E6, $0E, $1E, $FE, $BC, $3C, $2A
; slot 11/13 of "Fauna" row -- lion
_fauna_lion_pat:
        .byte $00, $01, $07, $0E, $1F, $1F, $1B, $19
        .byte $1C, $1E, $1F, $1F, $0F, $17, $3B, $48
        .byte $00, $A0, $F0, $F8, $F8, $98, $80, $D0
        .byte $91, $62, $02, $FE, $C0, $BC, $AA, $2A
; slot 12/13 of "Fauna" row -- tiger
_fauna_tiger_pat:
        .byte $00, $5D, $37, $0E, $1F, $1F, $3B, $39
        .byte $3C, $3E, $3F, $3F, $3F, $0F, $37, $68
        .byte $00, $A4, $FC, $FC, $D4, $80, $88, $E8
        .byte $89, $72, $02, $FE, $C0, $BC, $AA, $2A
; slot 13/13 of "Fauna" row -- horse
_fauna_horse_pat:
        .byte $02, $07, $0E, $0F, $1F, $1D, $1C, $1F
        .byte $1F, $1F, $3F, $2F, $22, $22, $12, $02
        .byte $40, $80, $C0, $F0, $F0, $C0, $30, $00
        .byte $F0, $E8, $EC, $EC, $66, $20, $20, $20
