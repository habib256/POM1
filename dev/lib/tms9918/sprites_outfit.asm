; ============================================================================
; sprites_outfit.asm  --  27 sprites (16x16, TMS9918 sprite mode)
; derived: dev/lib/gen2/sprites/sprites_outfit_hgr.asm -- after editing this master rerun:
;   python3 tools/build_hgr_sprites.py --only outfit
; ----------------------------------------------------------------------------
; SCROLL-O-SPRITES "Outfit" by Quale, May 2013, CC-BY-3.0.
; Lifted from pic/undefined - Imgur.png by tools/extract_scroll_sprites.py.
;
; Layout: each 16x16 sprite occupies 32 bytes -- the first 16 bytes are
; the left half (column 0..7), the next 16 the right half (column 8..15).
; Native TMS9918 16x16 sprite layout: stream the 32 bytes into a
; sprite-pattern slot starting at the ACTIVE MODE'S sprite-pattern table
; + slot*32 (asm Mode-1 init_vdp_g1: R6=$07 -> $3800; C lib SCREEN1/SCREEN2
; tables: R6=$03 -> $1800 — on those layouts $3800 is the NAME table!).
; ============================================================================
.export outfit_dagger_pat, outfit_sword_pat, outfit_axe_pat, outfit_greataxe_pat, outfit_spear_pat
.export outfit_mace_pat, outfit_hammer_pat, outfit_shield_small_pat, outfit_shield_pat, outfit_xweapons_pat
.export outfit_bow_pat, outfit_helmet_pat, outfit_hat_pat, outfit_boot_pat, outfit_gauntlet_pat
.export outfit_belt_pat, outfit_pants_pat, outfit_breeches_pat, outfit_belt_buckle_pat, outfit_cloak_pat
.export outfit_tunic_pat, outfit_vest_pat, outfit_robe_pat, outfit_amulet_pat, outfit_horned_helm_pat
.export outfit_crown_pat, outfit_glasses_pat

.segment "CODE"

; slot 01/27 of "Outfit" row -- dagger
outfit_dagger_pat:
        .byte $00, $00, $00, $00, $03, $03, $00, $03
        .byte $07, $0E, $1D, $1A, $1C, $00, $00, $00
        .byte $00, $00, $00, $18, $18, $A0, $C0, $60
        .byte $B0, $B0, $00, $00, $00, $00, $00, $00
; slot 02/27 of "Outfit" row -- sword
outfit_sword_pat:
        .byte $00, $00, $00, $00, $00, $01, $03, $07
        .byte $37, $36, $1B, $0C, $17, $63, $60, $00
        .byte $00, $1E, $3E, $7A, $F2, $E4, $C8, $90
        .byte $20, $40, $80, $00, $00, $00, $00, $00
; slot 03/27 of "Outfit" row -- axe
outfit_axe_pat:
        .byte $00, $00, $00, $00, $00, $00, $00, $02
        .byte $06, $0C, $18, $30, $60, $60, $00, $00
        .byte $00, $00, $10, $28, $44, $86, $8E, $9C
        .byte $B8, $F0, $E0, $C0, $00, $00, $00, $00
; slot 04/27 of "Outfit" row -- greataxe
outfit_greataxe_pat:
        .byte $00, $01, $01, $00, $00, $00, $00, $01
        .byte $03, $07, $0E, $1C, $38, $70, $60, $00
        .byte $00, $FE, $FA, $F2, $62, $22, $92, $CA
        .byte $86, $00, $00, $00, $00, $00, $00, $00
; slot 05/27 of "Outfit" row -- spear
outfit_spear_pat:
        .byte $00, $00, $00, $00, $00, $00, $00, $01
        .byte $03, $03, $08, $1C, $38, $70, $60, $00
        .byte $00, $06, $0E, $1C, $38, $30, $80, $C0
        .byte $80, $00, $00, $00, $00, $00, $00, $00
; slot 06/27 of "Outfit" row -- mace
outfit_mace_pat:
        .byte $00, $00, $00, $01, $01, $03, $03, $07
        .byte $07, $0F, $0F, $1E, $38, $30, $00, $00
        .byte $00, $00, $F8, $FC, $FC, $E4, $C4, $8C
        .byte $38, $E0, $80, $00, $00, $00, $00, $00
; slot 07/27 of "Outfit" row -- hammer
outfit_hammer_pat:
        .byte $00, $00, $01, $02, $04, $04, $02, $01
        .byte $00, $06, $0E, $1C, $38, $70, $60, $00
        .byte $00, $C0, $2C, $1C, $08, $1C, $3E, $7E
        .byte $FC, $78, $30, $00, $00, $00, $00, $00
; slot 08/27 of "Outfit" row -- shield_small
outfit_shield_small_pat:
        .byte $00, $00, $0F, $1F, $3F, $3F, $3E, $3D
        .byte $3D, $3C, $38, $30, $10, $0F, $00, $00
        .byte $00, $00, $F0, $F8, $E4, $C4, $04, $84
        .byte $84, $04, $04, $04, $08, $F0, $00, $00
; slot 09/27 of "Outfit" row -- shield
outfit_shield_pat:
        .byte $00, $30, $3C, $3F, $3F, $3F, $3F, $3D
        .byte $3D, $3E, $3F, $3F, $3F, $1F, $07, $00
        .byte $00, $0C, $3C, $E4, $04, $04, $04, $84
        .byte $84, $04, $04, $04, $04, $18, $E0, $00
; slot 10/27 of "Outfit" row -- xweapons
outfit_xweapons_pat:
        .byte $00, $60, $7F, $03, $21, $10, $08, $04
        .byte $01, $02, $04, $08, $00, $00, $00, $00
        .byte $00, $06, $82, $C8, $D0, $A0, $58, $BC
        .byte $1C, $0C, $84, $44, $24, $16, $06, $00
; slot 11/27 of "Outfit" row -- bow
outfit_bow_pat:
        .byte $00, $00, $00, $00, $00, $01, $02, $04
        .byte $19, $36, $6E, $1D, $0B, $06, $04, $00
        .byte $00, $36, $12, $48, $96, $22, $48, $90
        .byte $20, $40, $80, $00, $00, $00, $00, $00
; slot 12/27 of "Outfit" row -- helmet
outfit_helmet_pat:
        .byte $00, $00, $00, $07, $0F, $1F, $1F, $1F
        .byte $01, $11, $10, $18, $0C, $00, $00, $00
        .byte $00, $00, $00, $E0, $F0, $F8, $F8, $F8
        .byte $80, $88, $08, $18, $30, $00, $00, $00
; slot 13/27 of "Outfit" row -- hat
outfit_hat_pat:
        .byte $00, $00, $00, $0F, $1F, $1F, $1F, $1F
        .byte $10, $10, $1E, $1E, $3C, $00, $00, $00
        .byte $00, $00, $00, $F0, $F8, $F8, $F8, $F8
        .byte $08, $08, $78, $78, $3C, $00, $00, $00
; slot 14/27 of "Outfit" row -- boot
outfit_boot_pat:
        .byte $00, $00, $00, $00, $00, $00, $01, $03
        .byte $07, $0F, $07, $78, $07, $00, $00, $00
        .byte $00, $00, $00, $18, $74, $F4, $F0, $F0
        .byte $F0, $F0, $80, $78, $86, $00, $00, $00
; slot 15/27 of "Outfit" row -- gauntlet
outfit_gauntlet_pat:
        .byte $00, $00, $00, $00, $14, $54, $54, $7D
        .byte $7F, $42, $3C, $7E, $7E, $42, $3C, $00
        .byte $00, $28, $2A, $2A, $BE, $FE, $42, $3C
        .byte $7E, $7E, $42, $3C, $00, $00, $00, $00
; slot 16/27 of "Outfit" row -- belt
outfit_belt_pat:
        .byte $00, $00, $00, $00, $00, $2C, $76, $6E
        .byte $76, $6E, $76, $42, $42, $3C, $00, $00
        .byte $00, $00, $2C, $76, $6E, $76, $6E, $76
        .byte $42, $42, $3C, $00, $00, $00, $00, $00
; slot 17/27 of "Outfit" row -- pants
outfit_pants_pat:
        .byte $00, $00, $00, $3E, $3E, $00, $1E, $1E
        .byte $06, $3A, $7E, $7E, $7C, $00, $00, $00
        .byte $00, $00, $00, $7C, $7C, $00, $78, $78
        .byte $60, $5C, $7E, $7E, $3E, $00, $00, $00
; slot 18/27 of "Outfit" row -- breeches
outfit_breeches_pat:
        .byte $00, $00, $3D, $00, $2F, $2F, $1F, $3C
        .byte $3C, $3C, $3C, $3C, $00, $00, $00, $00
        .byte $00, $00, $BC, $00, $F4, $F4, $F8, $3C
        .byte $3C, $3C, $3C, $3C, $00, $00, $00, $00
; slot 19/27 of "Outfit" row -- belt_buckle
outfit_belt_buckle_pat:
        .byte $00, $00, $00, $00, $00, $3F, $40, $7B
        .byte $7A, $7B, $3A, $03, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $FC, $02, $DE
        .byte $5E, $5E, $5C, $C0, $00, $00, $00, $00
; slot 20/27 of "Outfit" row -- cloak
outfit_cloak_pat:
        .byte $00, $00, $00, $0C, $1B, $38, $7C, $7E
        .byte $7E, $0E, $07, $08, $1E, $1C, $00, $00
        .byte $00, $00, $00, $30, $D8, $1C, $3E, $7E
        .byte $7E, $70, $E0, $10, $78, $38, $00, $00
; slot 21/27 of "Outfit" row -- tunic
outfit_tunic_pat:
        .byte $00, $00, $00, $3C, $3D, $3C, $1C, $0E
        .byte $11, $1F, $1F, $00, $0F, $00, $00, $00
        .byte $00, $00, $00, $3C, $BC, $3C, $38, $70
        .byte $88, $F8, $F8, $00, $F0, $00, $00, $00
; slot 22/27 of "Outfit" row -- vest
outfit_vest_pat:
        .byte $00, $00, $03, $04, $3C, $3A, $3D, $3E
        .byte $3F, $1E, $01, $1F, $0F, $00, $00, $00
        .byte $00, $00, $C0, $20, $3C, $5C, $BC, $7C
        .byte $FC, $78, $80, $F8, $F0, $00, $00, $00
; slot 23/27 of "Outfit" row -- robe
outfit_robe_pat:
        .byte $00, $03, $07, $0F, $08, $10, $10, $10
        .byte $08, $04, $1D, $3C, $3C, $38, $30, $00
        .byte $00, $F0, $E0, $F0, $10, $08, $08, $08
        .byte $10, $20, $B8, $3C, $3C, $1C, $0C, $00
; slot 24/27 of "Outfit" row -- amulet
outfit_amulet_pat:
        .byte $00, $00, $00, $0E, $1E, $1E, $1D, $03
        .byte $1E, $1C, $1C, $0C, $06, $03, $00, $00
        .byte $00, $00, $00, $E0, $F0, $F8, $8C, $04
        .byte $04, $04, $04, $08, $10, $E0, $00, $00
; slot 25/27 of "Outfit" row -- horned_helm
outfit_horned_helm_pat:
        .byte $00, $00, $18, $27, $20, $20, $30, $1B
        .byte $07, $0F, $0F, $0F, $0B, $04, $03, $00
        .byte $00, $00, $18, $E4, $04, $04, $0C, $D8
        .byte $20, $90, $90, $90, $10, $20, $C0, $00
; slot 26/27 of "Outfit" row -- crown
outfit_crown_pat:
        .byte $00, $00, $00, $00, $00, $00, $21, $33
        .byte $2A, $1F, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $84, $CC
        .byte $54, $F8, $00, $00, $00, $00, $00, $00
; slot 27/27 of "Outfit" row -- glasses
outfit_glasses_pat:
        .byte $00, $00, $00, $00, $00, $00, $3F, $78
        .byte $48, $08, $07, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $FE, $E2
        .byte $E2, $A2, $1C, $00, $00, $00, $00, $00
