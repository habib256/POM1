; ============================================================================
; sprites_creatures.asm  --  8 sprites (16x16, TMS9918 sprite mode)
; derived: dev/lib/gen2/sprites/sprites_creatures_hgr.asm -- after editing this master rerun:
;   python3 tools/build_hgr_sprites.py --only creatures
; ----------------------------------------------------------------------------
; SCROLL-O-SPRITES "Creatures" by Quale, May 2013, CC-BY-3.0.
; Lifted from pic/undefined - Imgur.png by tools/extract_scroll_sprites.py.
;
; Layout: each 16x16 sprite occupies 32 bytes -- the first 16 bytes are
; the left half (column 0..7), the next 16 the right half (column 8..15).
; Native TMS9918 16x16 sprite layout: stream the 32 bytes into a
; sprite-pattern slot starting at the ACTIVE MODE'S sprite-pattern table
; + slot*32 (asm Mode-1 init_vdp_g1: R6=$07 -> $3800; C lib SCREEN1/SCREEN2
; tables: R6=$03 -> $1800 — on those layouts $3800 is the NAME table!).
; ============================================================================
.export creat_jelly_pat, creat_kraken_pat, creat_gazer_pat, creat_imp_pat, creat_ifrit_pat
.export creat_mimic_pat, creat_automaton_pat, creat_golem_pat

.segment "CODE"

; slot 01/8 of "Creatures" row -- jelly  (was: wolf)
creat_jelly_pat:
        .byte $00, $00, $00, $00, $0F, $1F, $3F, $3F
        .byte $2F, $20, $26, $2C, $20, $20, $3E, $78
        .byte $00, $10, $06, $06, $F0, $88, $C4, $C4
        .byte $84, $24, $04, $04, $94, $04, $04, $D2
; slot 02/8 of "Creatures" row -- kraken  (was: dragon)
creat_kraken_pat:
        .byte $00, $20, $10, $10, $20, $61, $63, $67
        .byte $67, $73, $40, $3D, $7E, $4F, $57, $36
        .byte $00, $10, $20, $62, $04, $E4, $F2, $C2
        .byte $BC, $7E, $F2, $EA, $EC, $40, $32, $DC
; slot 03/8 of "Creatures" row -- gazer  (was: cyclops)
creat_gazer_pat:
        .byte $00, $07, $1F, $3F, $38, $73, $74, $74
        .byte $74, $73, $78, $3F, $3C, $18, $62, $6D
        .byte $00, $E0, $F8, $FC, $7C, $3E, $BE, $BE
        .byte $BE, $3E, $7E, $FC, $C4, $02, $CC, $DE
; slot 04/8 of "Creatures" row -- imp  (was: blob)
creat_imp_pat:
        .byte $00, $00, $00, $00, $03, $07, $0F, $0C
        .byte $0A, $08, $14, $3B, $3C, $6F, $0C, $08
        .byte $00, $00, $00, $00, $F0, $E8, $F0, $30
        .byte $50, $10, $28, $DC, $3C, $F6, $30, $10
; slot 05/8 of "Creatures" row -- ifrit  (was: wisp)
creat_ifrit_pat:
        .byte $00, $02, $06, $04, $01, $07, $0E, $15
        .byte $10, $14, $10, $08, $34, $57, $43, $00
        .byte $00, $10, $40, $C0, $E0, $E0, $B0, $70
        .byte $18, $58, $18, $10, $2C, $CA, $E2, $10
; slot 06/8 of "Creatures" row -- mimic  (was: rat)
creat_mimic_pat:
        .byte $00, $00, $07, $04, $00, $08, $00, $73
        .byte $7F, $7F, $7F, $7F, $7F, $00, $08, $18
        .byte $00, $00, $5C, $06, $06, $46, $06, $B4
        .byte $B4, $AC, $BC, $BC, $BC, $00, $10, $18
; slot 07/8 of "Creatures" row -- automaton  (was: floater)
creat_automaton_pat:
        .byte $00, $00, $00, $07, $0F, $0B, $0F, $08
        .byte $30, $4F, $4F, $6F, $6F, $00, $04, $0C
        .byte $00, $80, $00, $E0, $F6, $D6, $F2, $12
        .byte $0C, $F0, $F0, $F0, $F0, $00, $10, $38
; slot 08/8 of "Creatures" row -- golem  (was: goblin)
creat_golem_pat:
        .byte $00, $07, $07, $07, $01, $77, $76, $77
        .byte $77, $67, $48, $6F, $6F, $0F, $0C, $1C
        .byte $00, $E0, $E0, $E0, $80, $EE, $6E, $EE
        .byte $EE, $E6, $12, $F6, $F6, $F0, $30, $38
