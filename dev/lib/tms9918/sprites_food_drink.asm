; ============================================================================
; sprites_food_drink.asm  --  22 sprites (16x16, TMS9918 sprite mode)
; ----------------------------------------------------------------------------
; SCROLL-O-SPRITES "Food & Drink" by Quale, May 2013, CC-BY-3.0.
; Lifted from pic/undefined - Imgur.png by tools/extract_scroll_sprites.py.
;
; Layout: each 16x16 sprite occupies 32 bytes -- the first 16 bytes are
; the left half (column 0..7), the next 16 the right half (column 8..15).
; Native TMS9918 16x16 sprite layout: stream the 32 bytes into a
; sprite-pattern slot starting at base $3800 + slot*32.
; ============================================================================
.export food_meat_pat, food_bowl_pat, food_shrimp_pat, food_fish_pat, food_melon_pat
.export food_pie_pat, food_bread_pat, food_cake_pat, food_apple_pat, food_pumpkin_pat
.export food_cheese_pat, food_herb_pat, food_flower_pat, food_mushroom_pat, food_pepper_pat
.export food_cupcake_pat, food_mug_pat, food_jug_pat, food_teapot_pat, food_bottle_pat
.export food_sack_pat, food_roll_pat

.segment "CODE"

; slot 01/22 of "Food & Drink" row -- meat
food_meat_pat:
        .byte $00, $00, $00, $00, $00, $00, $03, $0F
        .byte $3B, $5E, $7E, $6C, $78, $31, $1E, $00
        .byte $00, $18, $1E, $1E, $30, $60, $C0, $60
        .byte $00, $40, $40, $80, $80, $00, $00, $00
; slot 02/22 of "Food & Drink" row -- bowl
food_bowl_pat:
        .byte $00, $3C, $43, $40, $40, $40, $47, $44
        .byte $43, $40, $60, $7F, $7F, $3F, $1F, $00
        .byte $00, $00, $00, $C0, $30, $08, $04, $82
        .byte $82, $02, $06, $FE, $FE, $FC, $F8, $00
; slot 03/22 of "Food & Drink" row -- shrimp
food_shrimp_pat:
        .byte $00, $00, $00, $00, $00, $03, $07, $00
        .byte $1C, $3E, $47, $43, $13, $22, $4C, $00
        .byte $00, $02, $74, $F8, $1C, $8C, $CC, $E8
        .byte $60, $60, $40, $00, $00, $00, $00, $00
; slot 04/22 of "Food & Drink" row -- fish
food_fish_pat:
        .byte $00, $00, $00, $00, $06, $0D, $0B, $17
        .byte $3F, $3F, $7D, $7F, $47, $67, $3C, $00
        .byte $00, $00, $00, $18, $6C, $F6, $F0, $E0
        .byte $E0, $E0, $C0, $C0, $B0, $60, $00, $00
; slot 05/22 of "Food & Drink" row -- melon
food_melon_pat:
        .byte $00, $00, $03, $0F, $18, $16, $36, $30
        .byte $38, $3F, $1F, $1F, $0F, $03, $00, $00
        .byte $00, $00, $C0, $F0, $F8, $78, $7C, $74
        .byte $E8, $FC, $B8, $C8, $F4, $44, $08, $00
; slot 06/22 of "Food & Drink" row -- pie
food_pie_pat:
        .byte $00, $03, $03, $07, $0F, $1E, $2F, $7D
        .byte $5F, $4F, $26, $13, $08, $04, $03, $00
        .byte $00, $C0, $60, $E0, $F8, $FC, $DE, $FE
        .byte $E2, $C2, $9C, $20, $40, $80, $00, $00
; slot 07/22 of "Food & Drink" row -- bread
food_bread_pat:
        .byte $00, $00, $03, $07, $0F, $03, $1D, $1E
        .byte $1E, $1F, $1F, $1F, $0F, $07, $00, $00
        .byte $00, $00, $C0, $E0, $F0, $F0, $38, $F8
        .byte $F8, $78, $78, $F8, $F0, $E0, $00, $00
; slot 08/22 of "Food & Drink" row -- cake
food_cake_pat:
        .byte $00, $00, $00, $07, $0E, $1F, $3F, $36
        .byte $3E, $3E, $27, $27, $3F, $00, $00, $00
        .byte $00, $00, $00, $00, $C0, $F0, $FC, $3C
        .byte $3C, $38, $F8, $FC, $CC, $00, $00, $00
; slot 09/22 of "Food & Drink" row -- apple
food_apple_pat:
        .byte $00, $00, $06, $07, $03, $0D, $1E, $18
        .byte $1F, $1F, $1F, $0F, $0F, $07, $00, $00
        .byte $00, $00, $00, $00, $00, $F0, $78, $18
        .byte $F8, $F8, $F8, $F0, $F0, $E0, $00, $00
; slot 10/22 of "Food & Drink" row -- pumpkin
food_pumpkin_pat:
        .byte $00, $00, $01, $01, $0C, $3B, $77, $77
        .byte $77, $77, $77, $37, $07, $03, $00, $00
        .byte $00, $C0, $80, $80, $30, $DC, $EE, $EE
        .byte $EE, $EE, $EE, $EC, $E0, $C0, $00, $00
; slot 11/22 of "Food & Drink" row -- cheese
food_cheese_pat:
        .byte $00, $02, $1B, $39, $3C, $3F, $2F, $3F
        .byte $1F, $0E, $03, $01, $03, $02, $00, $00
        .byte $00, $38, $70, $68, $0C, $FC, $BC, $EC
        .byte $F8, $F0, $C0, $80, $40, $A0, $80, $00
; slot 12/22 of "Food & Drink" row -- herb
food_herb_pat:
        .byte $00, $00, $00, $00, $03, $07, $0F, $0E
        .byte $1F, $1F, $3F, $3F, $7C, $00, $00, $00
        .byte $00, $00, $02, $EC, $D8, $B8, $78, $F0
        .byte $F0, $E0, $C0, $00, $00, $00, $00, $00
; slot 13/22 of "Food & Drink" row -- flower
food_flower_pat:
        .byte $00, $01, $03, $1B, $1C, $0C, $03, $01
        .byte $00, $01, $31, $3A, $1A, $04, $04, $00
        .byte $00, $C0, $E0, $EC, $1C, $18, $E0, $C0
        .byte $00, $00, $00, $00, $00, $00, $00, $00
; slot 14/22 of "Food & Drink" row -- mushroom
food_mushroom_pat:
        .byte $00, $00, $00, $07, $1F, $3F, $38, $31
        .byte $1B, $03, $03, $07, $07, $03, $00, $00
        .byte $00, $00, $00, $F0, $F8, $F8, $18, $98
        .byte $B0, $80, $80, $80, $80, $00, $00, $00
; slot 15/22 of "Food & Drink" row -- pepper
food_pepper_pat:
        .byte $00, $00, $00, $01, $03, $07, $0F, $1F
        .byte $17, $13, $00, $70, $78, $3B, $18, $00
        .byte $00, $30, $38, $BC, $DC, $E0, $F0, $D0
        .byte $90, $10, $20, $40, $80, $00, $00, $00
; slot 16/22 of "Food & Drink" row -- cupcake
food_cupcake_pat:
        .byte $00, $00, $01, $03, $0F, $1D, $36, $3F
        .byte $1D, $2F, $10, $1D, $1D, $0D, $07, $00
        .byte $00, $40, $80, $C0, $F0, $A8, $7C, $EC
        .byte $B8, $F4, $08, $48, $48, $50, $E0, $00
; slot 17/22 of "Food & Drink" row -- mug
food_mug_pat:
        .byte $00, $40, $07, $0F, $1F, $1F, $1F, $0F
        .byte $11, $1C, $1D, $1D, $1D, $6C, $37, $00
        .byte $00, $00, $F0, $F8, $FA, $F8, $FC, $F6
        .byte $8A, $4A, $4A, $4C, $48, $10, $E0, $00
; slot 18/22 of "Food & Drink" row -- jug
food_jug_pat:
        .byte $00, $00, $00, $07, $0C, $0C, $1F, $37
        .byte $38, $3F, $3F, $1F, $2F, $10, $0F, $00
        .byte $00, $00, $00, $E0, $30, $30, $F8, $EC
        .byte $1C, $FC, $FC, $F8, $F4, $08, $F0, $00
; slot 19/22 of "Food & Drink" row -- teapot
food_teapot_pat:
        .byte $00, $03, $06, $07, $3B, $7C, $4F, $4F
        .byte $6F, $3F, $1F, $0F, $03, $04, $03, $00
        .byte $00, $C0, $60, $E0, $DC, $3E, $F2, $F2
        .byte $F6, $FC, $F8, $F0, $C0, $20, $C0, $00
; slot 20/22 of "Food & Drink" row -- bottle
food_bottle_pat:
        .byte $00, $00, $00, $00, $03, $06, $07, $03
        .byte $04, $0E, $0E, $0E, $0E, $04, $03, $00
        .byte $00, $00, $00, $00, $C0, $60, $E0, $C0
        .byte $20, $10, $10, $10, $10, $20, $C0, $00
; slot 21/22 of "Food & Drink" row -- sack
food_sack_pat:
        .byte $00, $00, $03, $06, $07, $03, $04, $0E
        .byte $1C, $3C, $38, $38, $30, $18, $07, $00
        .byte $00, $00, $C0, $60, $E0, $C0, $20, $10
        .byte $08, $04, $04, $04, $04, $18, $E0, $00
; slot 22/22 of "Food & Drink" row -- roll
food_roll_pat:
        .byte $00, $03, $06, $07, $1B, $20, $38, $3E
        .byte $3F, $3F, $3F, $3F, $1F, $06, $01, $00
        .byte $00, $C0, $60, $E0, $D8, $04, $04, $04
        .byte $04, $04, $04, $04, $18, $60, $80, $00
