; ============================================================================
; sprites_characters.asm  --  33 sprites (16x16, TMS9918 sprite mode)
; ----------------------------------------------------------------------------
; SCROLL-O-SPRITES "Characters" by Quale, May 2013, CC-BY-3.0.
; Lifted from pic/undefined - Imgur.png by tools/extract_scroll_sprites.py.
;
; Layout: each 16x16 sprite occupies 32 bytes -- the first 16 bytes are
; the left half (column 0..7), the next 16 the right half (column 8..15).
; Native TMS9918 16x16 sprite layout: stream the 32 bytes into a
; sprite-pattern slot starting at base $3800 + slot*32.
; ============================================================================
.export char_villager_pat, char_villager2_pat, char_worker_pat, char_traveler_pat, char_knight_pat
.export char_warrior_pat, char_archer_pat, char_king_pat, char_youth_pat, char_imp_pat
.export char_child_pat, char_stout_pat, char_priest_pat, char_adventurer_pat, char_wizard_pat
.export char_knight2_pat, char_cleric_pat, char_paladin_pat, char_grim_pat, char_hooded_pat
.export char_woman_pat, char_kitten_pat, char_cat_big_pat, char_monk_pat, char_dwarf_pat
.export char_skull_pat, char_princess_pat, char_skeleton_pat, char_mage_pat, char_robed_pat
.export char_priest2_pat, char_squire_pat, char_death_pat

.segment "CODE"

; slot 01/33 of "Characters" row -- villager
char_villager_pat:
        .byte $00, $00, $07, $0F, $0F, $0B, $0F, $0C
        .byte $0F, $07, $08, $1F, $37, $37, $06, $04
        .byte $00, $00, $E0, $F0, $F0, $D0, $F0, $30
        .byte $F0, $E0, $10, $F8, $EC, $EC, $60, $20
; slot 02/33 of "Characters" row -- villager2
char_villager2_pat:
        .byte $00, $00, $07, $0F, $0F, $0B, $0F, $0C
        .byte $8F, $87, $48, $5F, $37, $37, $06, $14
        .byte $00, $00, $E0, $F0, $F0, $D4, $F2, $32
        .byte $F2, $E6, $1E, $F8, $E2, $E2, $62, $24
; slot 03/33 of "Characters" row -- worker
char_worker_pat:
        .byte $00, $00, $07, $0F, $0F, $0B, $0F, $0C
        .byte $0F, $03, $78, $6B, $6B, $6B, $36, $04
        .byte $00, $00, $E0, $F2, $F2, $D2, $F2, $32
        .byte $F2, $E7, $18, $FE, $E2, $E0, $60, $20
; slot 04/33 of "Characters" row -- traveler
char_traveler_pat:
        .byte $00, $00, $07, $0F, $0F, $0B, $0F, $0C
        .byte $0F, $07, $08, $1F, $37, $36, $07, $0F
        .byte $00, $02, $E7, $F2, $F2, $D2, $F2, $32
        .byte $F2, $E6, $1E, $FE, $60, $22, $62, $F2
; slot 05/33 of "Characters" row -- knight
char_knight_pat:
        .byte $00, $0F, $07, $0F, $08, $15, $17, $13
        .byte $08, $07, $08, $1F, $37, $37, $06, $04
        .byte $00, $C0, $E0, $F0, $10, $A8, $E8, $C8
        .byte $10, $E0, $11, $FB, $EA, $EC, $60, $20
; slot 06/33 of "Characters" row -- warrior
char_warrior_pat:
        .byte $00, $00, $07, $0F, $0F, $0B, $0F, $0C
        .byte $0F, $07, $08, $1E, $37, $36, $06, $0C
        .byte $0C, $0C, $E1, $F0, $F2, $D5, $F2, $32
        .byte $F2, $E6, $1E, $7E, $E0, $60, $60, $30
; slot 07/33 of "Characters" row -- archer
char_archer_pat:
        .byte $00, $00, $07, $0F, $0F, $0B, $0F, $0C
        .byte $0F, $07, $08, $1E, $37, $30, $07, $0F
        .byte $00, $00, $E0, $F0, $F2, $D2, $F2, $32
        .byte $F2, $E6, $1E, $7E, $E0, $12, $E2, $F2
; slot 08/33 of "Characters" row -- king
char_king_pat:
        .byte $00, $1D, $0F, $00, $0F, $1B, $1F, $1E
        .byte $2F, $07, $3B, $1D, $36, $36, $06, $0C
        .byte $00, $B8, $F0, $00, $F0, $D8, $F8, $78
        .byte $F4, $E0, $DC, $B8, $6C, $6C, $60, $30
; slot 09/33 of "Characters" row -- youth
char_youth_pat:
        .byte $00, $00, $00, $00, $00, $03, $07, $05
        .byte $07, $06, $03, $04, $0F, $0B, $03, $02
        .byte $00, $00, $00, $00, $00, $C0, $E0, $A0
        .byte $E0, $60, $D0, $30, $E0, $C0, $C0, $40
; slot 10/33 of "Characters" row -- imp
char_imp_pat:
        .byte $00, $00, $00, $08, $0C, $0F, $07, $05
        .byte $07, $27, $2B, $1C, $1F, $1F, $19, $11
        .byte $00, $00, $00, $00, $0E, $FC, $F8, $E8
        .byte $F8, $18, $B0, $00, $E0, $E0, $20, $20
; slot 11/33 of "Characters" row -- child
char_child_pat:
        .byte $00, $00, $07, $0F, $0F, $0B, $0E, $0C
        .byte $3D, $37, $18, $0F, $07, $0F, $1C, $00
        .byte $00, $00, $E0, $F0, $D0, $F0, $00, $C0
        .byte $0C, $EC, $18, $F0, $E0, $E0, $60, $40
; slot 12/33 of "Characters" row -- stout
char_stout_pat:
        .byte $00, $00, $07, $0F, $0F, $0D, $0F, $0E
        .byte $17, $38, $7F, $6F, $11, $1F, $1F, $0C
        .byte $00, $00, $E0, $F0, $F0, $B0, $F0, $70
        .byte $E8, $1C, $FE, $F6, $88, $F8, $F8, $30
; slot 13/33 of "Characters" row -- priest
char_priest_pat:
        .byte $00, $07, $08, $10, $10, $14, $10, $10
        .byte $10, $18, $17, $20, $48, $48, $79, $0B
        .byte $00, $E0, $10, $08, $08, $28, $08, $08
        .byte $08, $18, $E8, $04, $12, $12, $9E, $D0
; slot 14/33 of "Characters" row -- adventurer
char_adventurer_pat:
        .byte $00, $00, $07, $0F, $1F, $1B, $1F, $5E
        .byte $37, $03, $0C, $1F, $37, $07, $06, $02
        .byte $00, $00, $E0, $F0, $F8, $D8, $F8, $7A
        .byte $EC, $C0, $30, $F8, $EC, $E0, $60, $40
; slot 15/33 of "Characters" row -- wizard
char_wizard_pat:
        .byte $00, $00, $07, $0F, $1F, $1B, $1F, $5E
        .byte $B7, $83, $44, $4F, $3F, $37, $06, $12
        .byte $00, $00, $E0, $F0, $F0, $D4, $F2, $72
        .byte $E2, $C6, $3C, $F2, $E2, $E2, $62, $44
; slot 16/33 of "Characters" row -- knight2
char_knight2_pat:
        .byte $00, $00, $07, $0F, $1F, $1B, $1F, $5E
        .byte $37, $03, $78, $6B, $6B, $6B, $36, $02
        .byte $00, $00, $E0, $F2, $FA, $DA, $FA, $7A
        .byte $EA, $C7, $38, $FE, $E2, $E0, $60, $40
; slot 17/33 of "Characters" row -- cleric
char_cleric_pat:
        .byte $00, $00, $07, $0F, $1F, $1B, $1F, $5E
        .byte $37, $03, $0C, $1F, $37, $06, $07, $0F
        .byte $00, $02, $E7, $F2, $FA, $DA, $FA, $7A
        .byte $EA, $C6, $3E, $F8, $62, $22, $62, $F2
; slot 18/33 of "Characters" row -- paladin
char_paladin_pat:
        .byte $00, $0F, $07, $0F, $08, $15, $17, $13
        .byte $08, $07, $08, $1F, $37, $07, $06, $02
        .byte $00, $C0, $E0, $F0, $10, $A8, $E8, $C8
        .byte $10, $E0, $11, $FB, $EA, $EC, $60, $40
; slot 19/33 of "Characters" row -- grim
char_grim_pat:
        .byte $00, $00, $07, $0F, $1F, $1B, $1F, $5E
        .byte $37, $03, $0C, $1E, $37, $06, $06, $0C
        .byte $0C, $0C, $E1, $F0, $FA, $D5, $FA, $7A
        .byte $EA, $C6, $3E, $78, $E0, $60, $60, $30
; slot 20/33 of "Characters" row -- hooded
char_hooded_pat:
        .byte $00, $00, $07, $0F, $1F, $1B, $1F, $5E
        .byte $37, $03, $0C, $1E, $37, $00, $07, $0F
        .byte $00, $00, $E0, $F0, $FA, $DA, $FA, $7A
        .byte $EA, $C6, $3E, $78, $E2, $12, $E2, $F2
; slot 21/33 of "Characters" row -- woman
char_woman_pat:
        .byte $00, $15, $0F, $00, $1F, $1B, $3F, $3E
        .byte $37, $33, $4C, $1C, $37, $07, $07, $0F
        .byte $00, $A8, $F0, $00, $F8, $D8, $FC, $7C
        .byte $EC, $CC, $32, $38, $EC, $E0, $E0, $F0
; slot 22/33 of "Characters" row -- kitten
char_kitten_pat:
        .byte $00, $00, $00, $00, $0C, $13, $07, $05
        .byte $07, $06, $03, $04, $0B, $03, $03, $07
        .byte $00, $00, $00, $00, $30, $C8, $E0, $A0
        .byte $E0, $60, $D0, $20, $C0, $C0, $C0, $E0
; slot 23/33 of "Characters" row -- cat_big
char_cat_big_pat:
        .byte $00, $00, $00, $04, $06, $07, $27, $15
        .byte $17, $17, $1B, $1C, $1F, $1F, $19, $11
        .byte $00, $00, $00, $08, $18, $F8, $F8, $E8
        .byte $B8, $58, $F0, $00, $E0, $E0, $20, $20
; slot 24/33 of "Characters" row -- monk
char_monk_pat:
        .byte $00, $00, $07, $0F, $1F, $1B, $1F, $5E
        .byte $36, $03, $0C, $1F, $37, $07, $06, $02
        .byte $00, $00, $E0, $F0, $D8, $F8, $3A, $3C
        .byte $60, $CC, $38, $F0, $E0, $E0, $60, $40
; slot 25/33 of "Characters" row -- dwarf
char_dwarf_pat:
        .byte $00, $00, $07, $0F, $1F, $5D, $3F, $0E
        .byte $17, $38, $7F, $6F, $11, $1F, $1F, $0C
        .byte $00, $00, $E0, $F0, $F8, $BA, $FC, $70
        .byte $E8, $1C, $FE, $F6, $88, $F8, $F8, $30
; slot 26/33 of "Characters" row -- skull
char_skull_pat:
        .byte $00, $07, $08, $10, $20, $24, $E0, $A0
        .byte $48, $3C, $13, $20, $48, $78, $09, $05
        .byte $00, $E0, $10, $08, $04, $24, $07, $05
        .byte $12, $3C, $C8, $04, $12, $1E, $90, $A0
; slot 27/33 of "Characters" row -- princess
char_princess_pat:
        .byte $00, $00, $00, $07, $0F, $0F, $0F, $09
        .byte $0F, $16, $37, $7B, $6B, $45, $46, $4C
        .byte $00, $00, $00, $E0, $F0, $F0, $F0, $90
        .byte $F0, $68, $EC, $DC, $DC, $AC, $A0, $30
; slot 28/33 of "Characters" row -- skeleton
char_skeleton_pat:
        .byte $00, $00, $07, $09, $0E, $0B, $0F, $0C
        .byte $0F, $07, $18, $3F, $37, $37, $06, $04
        .byte $00, $00, $E0, $F0, $60, $90, $90, $70
        .byte $F0, $E0, $18, $FC, $EC, $EC, $60, $20
; slot 29/33 of "Characters" row -- mage
char_mage_pat:
        .byte $00, $01, $07, $0F, $0F, $09, $08, $0E
        .byte $02, $7A, $68, $6B, $6B, $69, $6A, $34
        .byte $02, $82, $E7, $F0, $F2, $92, $12, $72
        .byte $72, $66, $16, $D8, $E2, $82, $62, $22
; slot 30/33 of "Characters" row -- robed
char_robed_pat:
        .byte $00, $03, $07, $0F, $08, $10, $13, $16
        .byte $0B, $04, $1C, $26, $3A, $2A, $02, $0C
        .byte $00, $F0, $E8, $F0, $10, $08, $C8, $68
        .byte $D0, $20, $38, $64, $5C, $54, $40, $30
; slot 31/33 of "Characters" row -- priest2
char_priest2_pat:
        .byte $60, $61, $23, $27, $4F, $48, $50, $53
        .byte $56, $67, $7B, $7B, $05, $46, $47, $4F
        .byte $00, $F8, $FC, $E4, $F2, $10, $08, $C8
        .byte $68, $E0, $D0, $D8, $AC, $AC, $60, $F0
; slot 32/33 of "Characters" row -- squire
char_squire_pat:
        .byte $00, $03, $07, $0F, $08, $10, $13, $16
        .byte $0B, $04, $0B, $1F, $37, $37, $06, $04
        .byte $00, $F0, $E0, $F0, $10, $08, $C8, $68
        .byte $D0, $20, $D1, $FB, $EA, $EC, $60, $20
; slot 33/33 of "Characters" row -- death
char_death_pat:
        .byte $00, $00, $07, $0F, $1F, $1B, $3F, $3E
        .byte $37, $33, $4C, $1F, $37, $07, $07, $03
        .byte $00, $00, $E0, $F0, $F8, $D8, $F8, $78
        .byte $E8, $D0, $30, $E4, $C6, $27, $FC, $F0
