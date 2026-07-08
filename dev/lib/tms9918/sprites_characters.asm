; ============================================================================
; sprites_characters.asm  --  33 sprites (16x16, TMS9918 sprite mode)
; derived: dev/lib/gen2/sprites/sprites_characters_hgr.asm -- after editing this master rerun:
;   python3 tools/build_hgr_sprites.py --only characters
; ----------------------------------------------------------------------------
; SCROLL-O-SPRITES "Characters" by Quale, May 2013, CC-BY-3.0.
; Lifted from pic/undefined - Imgur.png by tools/extract_scroll_sprites.py.
;
; Layout: each 16x16 sprite occupies 32 bytes -- the first 16 bytes are
; the left half (column 0..7), the next 16 the right half (column 8..15).
; Native TMS9918 16x16 sprite layout: stream the 32 bytes into a
; sprite-pattern slot starting at the ACTIVE MODE'S sprite-pattern table
; + slot*32 (asm Mode-1 init_vdp_g1: R6=$07 -> $3800; C lib SCREEN1/SCREEN2
; tables: R6=$03 -> $1800 — on those layouts $3800 is the NAME table!).
; ============================================================================
.export char_normal_m_pat, char_archer_m_pat, char_knight_m_pat, char_priest_m_pat, char_thief_m_pat
.export char_wizard_m_pat, char_monk_m_pat, char_ruler_m_pat, char_child_m_pat, char_dog_pat
.export char_lunatic_m_pat, char_wrestler_m_pat, char_phantom_m_pat, char_normal_f_pat, char_archer_f_pat
.export char_knight_f_pat, char_priest_f_pat, char_thief_f_pat, char_wizard_f_pat, char_monk_f_pat
.export char_ruler_f_pat, char_child_f_pat, char_cat_pat, char_lunatic_f_pat, char_wrestler_f_pat
.export char_phantom_f_pat, char_elder_m_pat, char_pirate_m_pat, char_guard_pat, char_cultist_pat
.export char_necromancer_m_pat, char_assassin_pat, char_mermaid_f_pat

.segment "CODE"

; slot 01/33 of "Characters" row -- normal_m  (was: villager)
char_normal_m_pat:
        .byte $00, $07, $0F, $0F, $0B, $0F, $0C, $0F
        .byte $07, $08, $1F, $37, $37, $06, $04, $00
        .byte $00, $E0, $F0, $F0, $D0, $F0, $30, $F0
        .byte $E0, $10, $F8, $EC, $EC, $60, $20, $00
; slot 02/33 of "Characters" row -- archer_m  (was: villager2)
char_archer_m_pat:
        .byte $00, $07, $0F, $0F, $0B, $0F, $0C, $8F
        .byte $87, $48, $5F, $37, $37, $06, $14, $00
        .byte $00, $E0, $F0, $F0, $D4, $F2, $32, $F2
        .byte $E6, $1E, $F8, $E2, $E2, $62, $24, $00
; slot 03/33 of "Characters" row -- knight_m  (was: worker)
char_knight_m_pat:
        .byte $00, $07, $0F, $0F, $0B, $0F, $0C, $0F
        .byte $03, $78, $6B, $6B, $6B, $36, $04, $00
        .byte $00, $E0, $F2, $F2, $D2, $F2, $32, $F2
        .byte $E7, $18, $FE, $E2, $E0, $60, $20, $00
; slot 04/33 of "Characters" row -- priest_m  (was: traveler)
char_priest_m_pat:
        .byte $00, $07, $0F, $0F, $0B, $0F, $0C, $0F
        .byte $07, $08, $1F, $37, $36, $07, $0F, $00
        .byte $02, $E7, $F2, $F2, $D2, $F2, $32, $F2
        .byte $E6, $1E, $FE, $60, $22, $62, $F2, $00
; slot 05/33 of "Characters" row -- thief_m  (was: knight)
char_thief_m_pat:
        .byte $0F, $07, $0F, $08, $15, $17, $13, $08
        .byte $07, $08, $1F, $37, $37, $06, $04, $00
        .byte $C0, $E0, $F0, $10, $A8, $E8, $C8, $10
        .byte $E0, $11, $FB, $EA, $EC, $60, $20, $00
; slot 06/33 of "Characters" row -- wizard_m  (was: warrior)
char_wizard_m_pat:
        .byte $00, $07, $0F, $0F, $0B, $0F, $0C, $0F
        .byte $07, $08, $1E, $37, $36, $06, $0C, $00
        .byte $0C, $E1, $F0, $F2, $D5, $F2, $32, $F2
        .byte $E6, $1E, $7E, $E0, $60, $60, $30, $00
; slot 07/33 of "Characters" row -- monk_m  (was: archer)
char_monk_m_pat:
        .byte $00, $07, $0F, $0F, $0B, $0F, $0C, $0F
        .byte $07, $08, $1E, $37, $30, $07, $0F, $00
        .byte $00, $E0, $F0, $F2, $D2, $F2, $32, $F2
        .byte $E6, $1E, $7E, $E0, $12, $E2, $F2, $00
; slot 08/33 of "Characters" row -- ruler_m  (was: king)
char_ruler_m_pat:
        .byte $1D, $0F, $00, $0F, $1B, $1F, $1E, $2F
        .byte $07, $3B, $1D, $36, $36, $06, $0C, $00
        .byte $B8, $F0, $00, $F0, $D8, $F8, $78, $F4
        .byte $E0, $DC, $B8, $6C, $6C, $60, $30, $00
; slot 09/33 of "Characters" row -- child_m  (was: youth)
char_child_m_pat:
        .byte $00, $00, $00, $00, $03, $07, $05, $07
        .byte $06, $03, $04, $0F, $0B, $03, $02, $00
        .byte $00, $00, $00, $00, $C0, $E0, $A0, $E0
        .byte $60, $D0, $30, $E0, $C0, $C0, $40, $00
; slot 10/33 of "Characters" row -- dog  (was: imp)
char_dog_pat:
        .byte $00, $00, $08, $0C, $0F, $07, $05, $07
        .byte $27, $2B, $1C, $1F, $1F, $19, $11, $00
        .byte $00, $00, $00, $0E, $FC, $F8, $E8, $F8
        .byte $18, $B0, $00, $E0, $E0, $20, $20, $00
; slot 11/33 of "Characters" row -- lunatic_m  (was: child)
char_lunatic_m_pat:
        .byte $00, $07, $0F, $0F, $0B, $0E, $0C, $3D
        .byte $37, $18, $0F, $07, $0F, $1C, $00, $00
        .byte $00, $E0, $F0, $D0, $F0, $00, $C0, $0C
        .byte $EC, $18, $F0, $E0, $E0, $60, $40, $00
; slot 12/33 of "Characters" row -- wrestler_m  (was: stout)
char_wrestler_m_pat:
        .byte $00, $07, $0F, $0F, $0D, $0F, $0E, $17
        .byte $38, $7F, $6F, $11, $1F, $1F, $0C, $00
        .byte $00, $E0, $F0, $F0, $B0, $F0, $70, $E8
        .byte $1C, $FE, $F6, $88, $F8, $F8, $30, $00
; slot 13/33 of "Characters" row -- phantom_m  (was: priest)
char_phantom_m_pat:
        .byte $07, $08, $10, $10, $14, $10, $10, $10
        .byte $18, $17, $20, $48, $48, $79, $0B, $00
        .byte $E0, $10, $08, $08, $28, $08, $08, $08
        .byte $18, $E8, $04, $12, $12, $9E, $D0, $00
; slot 14/33 of "Characters" row -- normal_f  (was: adventurer)
char_normal_f_pat:
        .byte $00, $07, $0F, $1F, $1B, $1F, $5E, $37
        .byte $03, $0C, $1F, $37, $07, $06, $02, $00
        .byte $00, $E0, $F0, $F8, $D8, $F8, $7A, $EC
        .byte $C0, $30, $F8, $EC, $E0, $60, $40, $00
; slot 15/33 of "Characters" row -- archer_f  (was: wizard)
char_archer_f_pat:
        .byte $00, $07, $0F, $1F, $1B, $1F, $5E, $B7
        .byte $83, $44, $4F, $3F, $37, $06, $12, $00
        .byte $00, $E0, $F0, $F0, $D4, $F2, $72, $E2
        .byte $C6, $3C, $F2, $E2, $E2, $62, $44, $00
; slot 16/33 of "Characters" row -- knight_f  (was: knight2)
char_knight_f_pat:
        .byte $00, $07, $0F, $1F, $1B, $1F, $5E, $37
        .byte $03, $78, $6B, $6B, $6B, $36, $02, $00
        .byte $00, $E0, $F2, $FA, $DA, $FA, $7A, $EA
        .byte $C7, $38, $FE, $E2, $E0, $60, $40, $00
; slot 17/33 of "Characters" row -- priest_f  (was: cleric)
char_priest_f_pat:
        .byte $00, $07, $0F, $1F, $1B, $1F, $5E, $37
        .byte $03, $0C, $1F, $37, $06, $07, $0F, $00
        .byte $02, $E7, $F2, $FA, $DA, $FA, $7A, $EA
        .byte $C6, $3E, $F8, $62, $22, $62, $F2, $00
; slot 18/33 of "Characters" row -- thief_f  (was: paladin)
char_thief_f_pat:
        .byte $0F, $07, $0F, $08, $15, $17, $13, $08
        .byte $07, $08, $1F, $37, $07, $06, $02, $00
        .byte $C0, $E0, $F0, $10, $A8, $E8, $C8, $10
        .byte $E0, $11, $FB, $EA, $EC, $60, $40, $00
; slot 19/33 of "Characters" row -- wizard_f  (was: grim)
char_wizard_f_pat:
        .byte $00, $07, $0F, $1F, $1B, $1F, $5E, $37
        .byte $03, $0C, $1E, $37, $06, $06, $0C, $00
        .byte $0C, $E1, $F0, $FA, $D5, $FA, $7A, $EA
        .byte $C6, $3E, $78, $E0, $60, $60, $30, $00
; slot 20/33 of "Characters" row -- monk_f  (was: hooded)
char_monk_f_pat:
        .byte $00, $07, $0F, $1F, $1B, $1F, $5E, $37
        .byte $03, $0C, $1E, $37, $00, $07, $0F, $00
        .byte $00, $E0, $F0, $FA, $DA, $FA, $7A, $EA
        .byte $C6, $3E, $78, $E2, $12, $E2, $F2, $00
; slot 21/33 of "Characters" row -- ruler_f  (was: woman)
char_ruler_f_pat:
        .byte $15, $0F, $00, $1F, $1B, $3F, $3E, $37
        .byte $33, $4C, $1C, $37, $07, $07, $0F, $00
        .byte $A8, $F0, $00, $F8, $D8, $FC, $7C, $EC
        .byte $CC, $32, $38, $EC, $E0, $E0, $F0, $00
; slot 22/33 of "Characters" row -- child_f  (was: kitten)
char_child_f_pat:
        .byte $00, $00, $00, $0C, $13, $07, $05, $07
        .byte $06, $03, $04, $0B, $03, $03, $07, $00
        .byte $00, $00, $00, $30, $C8, $E0, $A0, $E0
        .byte $60, $D0, $20, $C0, $C0, $C0, $E0, $00
; slot 23/33 of "Characters" row -- cat  (was: cat_big)
char_cat_pat:
        .byte $00, $00, $04, $06, $07, $27, $15, $17
        .byte $17, $1B, $1C, $1F, $1F, $19, $11, $00
        .byte $00, $00, $08, $18, $F8, $F8, $E8, $B8
        .byte $58, $F0, $00, $E0, $E0, $20, $20, $00
; slot 24/33 of "Characters" row -- lunatic_f  (was: monk)
char_lunatic_f_pat:
        .byte $00, $07, $0F, $1F, $1B, $1F, $5E, $36
        .byte $03, $0C, $1F, $37, $07, $06, $02, $00
        .byte $00, $E0, $F0, $D8, $F8, $3A, $3C, $60
        .byte $CC, $38, $F0, $E0, $E0, $60, $40, $00
; slot 25/33 of "Characters" row -- wrestler_f  (was: dwarf)
char_wrestler_f_pat:
        .byte $00, $07, $0F, $1F, $5D, $3F, $0E, $17
        .byte $38, $7F, $6F, $11, $1F, $1F, $0C, $00
        .byte $00, $E0, $F0, $F8, $BA, $FC, $70, $E8
        .byte $1C, $FE, $F6, $88, $F8, $F8, $30, $00
; slot 26/33 of "Characters" row -- phantom_f  (was: skull)
char_phantom_f_pat:
        .byte $07, $08, $10, $20, $24, $E0, $A0, $48
        .byte $3C, $13, $20, $48, $78, $09, $05, $00
        .byte $E0, $10, $08, $04, $24, $07, $05, $12
        .byte $3C, $C8, $04, $12, $1E, $90, $A0, $00
; slot 27/33 of "Characters" row -- elder_m  (was: princess)
char_elder_m_pat:
        .byte $00, $00, $07, $0F, $0F, $0F, $09, $0F
        .byte $16, $37, $7B, $6B, $45, $46, $4C, $00
        .byte $00, $00, $E0, $F0, $F0, $F0, $90, $F0
        .byte $68, $EC, $DC, $DC, $AC, $A0, $30, $00
; slot 28/33 of "Characters" row -- pirate_m  (was: skeleton)
char_pirate_m_pat:
        .byte $00, $07, $09, $0E, $0B, $0F, $0C, $0F
        .byte $07, $18, $3F, $37, $37, $06, $04, $00
        .byte $00, $E0, $F0, $60, $90, $90, $70, $F0
        .byte $E0, $18, $FC, $EC, $EC, $60, $20, $00
; slot 29/33 of "Characters" row -- guard  (was: paladin2)
; in the Quale source — the sprite is actually an armoured knight-paladin,
; not a robed mage; the existing char_thief_f_pat covers the first variant).
char_guard_pat:
        .byte $01, $07, $0F, $0F, $09, $08, $0E, $02
        .byte $7A, $68, $6B, $6B, $69, $6A, $34, $00
        .byte $82, $E7, $F0, $F2, $92, $12, $72, $72
        .byte $66, $16, $D8, $E2, $82, $62, $22, $00
; slot 30/33 of "Characters" row -- cultist  (was: robed)
char_cultist_pat:
        .byte $03, $07, $0F, $08, $10, $13, $16, $0B
        .byte $04, $1C, $26, $3A, $2A, $02, $0C, $00
        .byte $F0, $E8, $F0, $10, $08, $C8, $68, $D0
        .byte $20, $38, $64, $5C, $54, $40, $30, $00
; slot 31/33 of "Characters" row -- necromancer_m  (was: priest2)
char_necromancer_m_pat:
        .byte $61, $23, $27, $4F, $48, $50, $53, $56
        .byte $67, $7B, $7B, $05, $46, $47, $4F, $00
        .byte $F8, $FC, $E4, $F2, $10, $08, $C8, $68
        .byte $E0, $D0, $D8, $AC, $AC, $60, $F0, $00
; slot 32/33 of "Characters" row -- assassin  (was: squire)
char_assassin_pat:
        .byte $03, $07, $0F, $08, $10, $13, $16, $0B
        .byte $04, $0B, $1F, $37, $37, $06, $04, $00
        .byte $F0, $E0, $F0, $10, $08, $C8, $68, $D0
        .byte $20, $D1, $FB, $EA, $EC, $60, $20, $00
; slot 33/33 of "Characters" row -- mermaid_f  (was: death)
char_mermaid_f_pat:
        .byte $00, $07, $0F, $1F, $1B, $3F, $3E, $37
        .byte $33, $4C, $1F, $37, $07, $07, $03, $00
        .byte $00, $E0, $F0, $F8, $D8, $F8, $78, $E8
        .byte $D0, $30, $E4, $C6, $27, $FC, $F0, $00
