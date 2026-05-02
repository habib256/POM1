; ============================================================================
; sprites_exploration.asm  --  21 sprites (16x16, TMS9918 sprite mode)
; ----------------------------------------------------------------------------
; SCROLL-O-SPRITES "Exploration" by Quale, May 2013, CC-BY-3.0.
; Lifted from pic/undefined - Imgur.png by tools/extract_scroll_sprites.py.
;
; Layout: each 16x16 sprite occupies 32 bytes -- the first 16 bytes are
; the left half (column 0..7), the next 16 the right half (column 8..15).
; Native TMS9918 16x16 sprite layout: stream the 32 bytes into a
; sprite-pattern slot starting at base $3800 + slot*32.
; ============================================================================
.export expl_torch_pat, expl_lantern_pat, expl_bomb_pat, expl_pickaxe_pat, expl_rope_pat
.export expl_flask_pat, expl_coin_pat, expl_chest_closed_pat, expl_chest_open_pat, expl_mortar_pat
.export expl_key_small_pat, expl_key_pat, expl_lock_pat, expl_book_pat, expl_compass_pat
.export expl_scroll_pat, expl_clock_pat, expl_poison_pat, expl_bone_pat, expl_shell_pat
.export expl_mask_pat

.segment "CODE"

; slot 01/21 of "Exploration" row -- torch
expl_torch_pat:
        .byte $00, $00, $00, $04, $00, $01, $13, $07
        .byte $04, $02, $07, $0C, $18, $30, $60, $00
        .byte $00, $40, $68, $F0, $F0, $F8, $98, $38
        .byte $78, $F0, $E0, $00, $00, $00, $00, $00
; slot 02/21 of "Exploration" row -- lantern
expl_lantern_pat:
        .byte $00, $03, $02, $05, $0C, $2F, $07, $00
        .byte $67, $07, $07, $2B, $0C, $07, $00, $00
        .byte $00, $C0, $40, $A0, $30, $F4, $E0, $00
        .byte $E6, $E0, $E0, $D4, $30, $E0, $00, $00
; slot 03/21 of "Exploration" row -- bomb
expl_bomb_pat:
        .byte $00, $00, $00, $00, $00, $00, $00, $09
        .byte $1D, $3C, $7F, $7F, $7F, $7E, $7C, $00
        .byte $00, $18, $14, $12, $3E, $70, $E0, $C0
        .byte $80, $00, $00, $80, $00, $00, $00, $00
; slot 04/21 of "Exploration" row -- pickaxe
expl_pickaxe_pat:
        .byte $00, $0F, $01, $00, $00, $00, $00, $01
        .byte $03, $07, $0E, $1C, $38, $70, $60, $00
        .byte $00, $80, $EC, $7C, $38, $1C, $CC, $C6
        .byte $86, $02, $02, $02, $00, $00, $00, $00
; slot 05/21 of "Exploration" row -- rope
expl_rope_pat:
        .byte $00, $00, $01, $41, $21, $20, $20, $20
        .byte $22, $24, $24, $25, $19, $01, $00, $00
        .byte $00, $F0, $08, $08, $08, $90, $F0, $C0
        .byte $F0, $00, $90, $08, $08, $08, $F0, $00
; slot 06/21 of "Exploration" row -- flask
expl_flask_pat:
        .byte $00, $00, $00, $01, $00, $0F, $1E, $3F
        .byte $3F, $2E, $20, $20, $10, $0F, $00, $00
        .byte $00, $30, $48, $88, $48, $88, $44, $20
        .byte $20, $20, $20, $20, $40, $80, $00, $00
; slot 07/21 of "Exploration" row -- coin
expl_coin_pat:
        .byte $00, $00, $00, $00, $3F, $7F, $73, $08
        .byte $7B, $73, $7F, $7F, $7F, $00, $00, $00
        .byte $00, $00, $00, $00, $D8, $BC, $BC, $00
        .byte $BC, $BC, $BC, $BC, $BC, $00, $00, $00
; slot 08/21 of "Exploration" row -- chest_closed
expl_chest_closed_pat:
        .byte $00, $00, $07, $04, $04, $04, $04, $73
        .byte $7F, $7F, $7F, $7F, $7F, $00, $00, $00
        .byte $00, $00, $5C, $06, $06, $06, $06, $BC
        .byte $BC, $BC, $BC, $BC, $BC, $00, $00, $00
; slot 09/21 of "Exploration" row -- chest_open
expl_chest_open_pat:
        .byte $00, $07, $08, $09, $18, $37, $78, $7F
        .byte $3F, $5F, $47, $28, $3D, $1F, $07, $00
        .byte $00, $E0, $10, $90, $18, $EC, $1E, $FE
        .byte $FC, $FA, $E2, $14, $BC, $F8, $E0, $00
; slot 10/21 of "Exploration" row -- mortar
expl_mortar_pat:
        .byte $00, $00, $00, $03, $01, $24, $70, $06
        .byte $16, $40, $70, $79, $39, $1B, $03, $00
        .byte $00, $00, $00, $80, $70, $F0, $06, $06
        .byte $8E, $1E, $9E, $EC, $F0, $F8, $E0, $00
; slot 11/21 of "Exploration" row -- key_small
expl_key_small_pat:
        .byte $00, $00, $00, $00, $1C, $3E, $22, $23
        .byte $23, $22, $3E, $1C, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $FC
        .byte $FC, $1C, $14, $00, $00, $00, $00, $00
; slot 12/21 of "Exploration" row -- key
expl_key_pat:
        .byte $00, $00, $00, $00, $06, $09, $39, $4F
        .byte $4F, $39, $09, $06, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $FC
        .byte $FC, $1C, $14, $00, $00, $00, $00, $00
; slot 13/21 of "Exploration" row -- lock
expl_lock_pat:
        .byte $00, $00, $00, $00, $07, $0C, $08, $07
        .byte $08, $1A, $3A, $39, $3F, $3F, $1F, $00
        .byte $00, $00, $00, $00, $C0, $60, $E0, $C0
        .byte $20, $F0, $F8, $F8, $F8, $F0, $C0, $00
; slot 14/21 of "Exploration" row -- book
expl_book_pat:
        .byte $00, $00, $0F, $1F, $1F, $0F, $10, $0E
        .byte $00, $00, $00, $3E, $7F, $7F, $3E, $00
        .byte $00, $00, $80, $C0, $80, $7C, $FE, $FE
        .byte $7C, $82, $7C, $82, $7C, $00, $00, $00
; slot 15/21 of "Exploration" row -- compass
expl_compass_pat:
        .byte $00, $00, $00, $07, $0F, $1F, $1F, $1D
        .byte $19, $1F, $16, $08, $07, $00, $00, $00
        .byte $00, $00, $00, $E0, $D0, $E8, $68, $C8
        .byte $C8, $88, $48, $10, $E0, $00, $00, $00
; slot 16/21 of "Exploration" row -- scroll
expl_scroll_pat:
        .byte $00, $00, $00, $01, $02, $05, $0B, $17
        .byte $17, $16, $10, $10, $1F, $00, $00, $00
        .byte $00, $00, $00, $F8, $F8, $E8, $C8, $88
        .byte $08, $10, $20, $40, $80, $00, $00, $00
; slot 17/21 of "Exploration" row -- clock
expl_clock_pat:
        .byte $00, $00, $00, $07, $0B, $17, $17, $17
        .byte $17, $13, $10, $08, $07, $00, $00, $00
        .byte $00, $00, $00, $E0, $90, $C8, $C8, $C8
        .byte $C8, $88, $08, $10, $E0, $00, $00, $00
; slot 18/21 of "Exploration" row -- poison
expl_poison_pat:
        .byte $00, $00, $01, $03, $01, $01, $02, $03
        .byte $18, $06, $01, $06, $38, $30, $00, $00
        .byte $00, $00, $F0, $F8, $98, $98, $F0, $80
        .byte $18, $60, $80, $60, $1C, $0C, $00, $00
; slot 19/21 of "Exploration" row -- bone
expl_bone_pat:
        .byte $00, $00, $00, $00, $00, $00, $30, $3F
        .byte $1F, $30, $30, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $0C, $0C, $F8
        .byte $FC, $0C, $00, $00, $00, $00, $00, $00
; slot 20/21 of "Exploration" row -- shell
expl_shell_pat:
        .byte $00, $00, $0F, $1F, $3F, $3E, $3D, $3D
        .byte $3C, $3F, $3F, $1F, $07, $00, $00, $00
        .byte $00, $00, $80, $C0, $E0, $60, $60, $D0
        .byte $38, $F8, $F8, $F8, $F0, $00, $00, $00
; slot 21/21 of "Exploration" row -- mask
expl_mask_pat:
        .byte $00, $00, $00, $1F, $31, $30, $38, $1F
        .byte $22, $38, $3D, $3F, $1F, $00, $00, $00
        .byte $00, $00, $00, $80, $F0, $88, $C8, $F8
        .byte $A8, $00, $50, $F8, $F8, $00, $00, $00
