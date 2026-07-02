; ============================================================================
; sprites_magick.asm  --  15 sprites (16x16, TMS9918 sprite mode)
; derived: dev/lib/gen2/sprites/sprites_magick_hgr.asm -- after editing this master rerun:
;   python3 tools/build_hgr_sprites.py --only magick
; ----------------------------------------------------------------------------
; SCROLL-O-SPRITES "Magick" by Quale, May 2013, CC-BY-3.0.
; Lifted from pic/undefined - Imgur.png by tools/extract_scroll_sprites.py.
;
; Layout: each 16x16 sprite occupies 32 bytes -- the first 16 bytes are
; the left half (column 0..7), the next 16 the right half (column 8..15).
; Native TMS9918 16x16 sprite layout: stream the 32 bytes into a
; sprite-pattern slot starting at the ACTIVE MODE'S sprite-pattern table
; + slot*32 (asm Mode-1 init_vdp_g1: R6=$07 -> $3800; C lib SCREEN1/SCREEN2
; tables: R6=$03 -> $1800 — on those layouts $3800 is the NAME table!).
; ============================================================================
.export magic_wand_pat, magic_wand_orb_pat, magic_wand_skull_pat, magic_orb_pat, magic_ankh_pat
.export magic_effigy_pat, magic_scroll_pat, magic_scroll_open_pat, magic_runestone_pat, magic_book_pat
.export magic_mirror_pat, magic_vortex_pat, magic_portal_pat, magic_hand_pat, magic_sun_pat

.segment "CODE"

; slot 01/15 of "Magick" row -- wand
magic_wand_pat:
        .byte $00, $00, $01, $00, $00, $04, $01, $01
        .byte $03, $07, $0E, $1C, $38, $70, $60, $00
        .byte $00, $08, $00, $78, $FC, $C4, $B4, $AC
        .byte $B8, $02, $00, $20, $00, $00, $00, $00
; slot 02/15 of "Magick" row -- wand_orb
magic_wand_orb_pat:
        .byte $00, $00, $01, $00, $00, $04, $00, $02
        .byte $02, $07, $0E, $1C, $38, $70, $60, $00
        .byte $00, $10, $00, $78, $EA, $C8, $88, $F0
        .byte $04, $80, $20, $00, $00, $00, $00, $00
; slot 03/15 of "Magick" row -- wand_skull
magic_wand_skull_pat:
        .byte $00, $00, $00, $01, $01, $01, $00, $01
        .byte $03, $07, $0E, $1C, $38, $70, $60, $00
        .byte $00, $00, $F8, $FC, $98, $98, $F4, $3C
        .byte $A8, $00, $00, $00, $00, $00, $00, $00
; slot 04/15 of "Magick" row -- orb
magic_orb_pat:
        .byte $00, $00, $07, $08, $17, $2F, $2F, $2F
        .byte $27, $20, $22, $10, $08, $07, $00, $00
        .byte $00, $00, $E0, $10, $88, $C4, $C4, $C4
        .byte $A4, $74, $24, $88, $10, $E0, $00, $00
; slot 05/15 of "Magick" row -- ankh
magic_ankh_pat:
        .byte $00, $03, $07, $04, $06, $07, $03, $3F
        .byte $3F, $03, $3B, $03, $03, $00, $03, $00
        .byte $00, $C0, $E0, $20, $60, $E0, $C0, $FC
        .byte $FC, $C0, $DC, $C0, $C0, $00, $C0, $00
; slot 06/15 of "Magick" row -- effigy
magic_effigy_pat:
        .byte $00, $00, $00, $0F, $17, $3F, $3F, $0C
        .byte $0C, $0C, $33, $33, $1F, $1D, $00, $00
        .byte $00, $00, $00, $60, $F0, $F8, $E8, $70
        .byte $38, $38, $F0, $C0, $00, $00, $00, $00
; slot 07/15 of "Magick" row -- scroll
magic_scroll_pat:
        .byte $00, $07, $09, $0B, $08, $0F, $08, $0F
        .byte $0C, $0F, $08, $0F, $6C, $4F, $3F, $00
        .byte $00, $FC, $FE, $FE, $00, $F8, $08, $F8
        .byte $18, $F8, $08, $F8, $18, $F8, $F0, $00
; slot 08/15 of "Magick" row -- scroll_open
magic_scroll_open_pat:
        .byte $00, $2F, $77, $17, $20, $00, $10, $1F
        .byte $10, $1F, $00, $2F, $77, $17, $20, $00
        .byte $00, $F4, $FA, $FA, $00, $00, $10, $F0
        .byte $10, $F0, $00, $F4, $FA, $FA, $00, $00
; slot 09/15 of "Magick" row -- runestone
magic_runestone_pat:
        .byte $00, $00, $00, $03, $0F, $1C, $18, $19
        .byte $1F, $1F, $1F, $1C, $17, $17, $0F, $00
        .byte $00, $30, $F0, $F0, $30, $30, $70, $F4
        .byte $F4, $C4, $04, $04, $F4, $F4, $FC, $00
; slot 10/15 of "Magick" row -- book
magic_book_pat:
        .byte $00, $7F, $40, $5F, $5F, $5F, $5F, $5F
        .byte $7F, $00, $7F, $00, $26, $3F, $7F, $00
        .byte $00, $FE, $02, $FE, $FE, $FE, $FE, $FE
        .byte $FE, $00, $FE, $00, $64, $FC, $FE, $00
; slot 11/15 of "Magick" row -- mirror
magic_mirror_pat:
        .byte $00, $07, $18, $20, $46, $40, $41, $20
        .byte $58, $47, $60, $78, $7F, $3F, $4F, $01
        .byte $00, $E0, $18, $04, $42, $12, $82, $04
        .byte $1A, $E2, $06, $1E, $FE, $FC, $F2, $80
; slot 12/15 of "Magick" row -- vortex
magic_vortex_pat:
        .byte $00, $40, $20, $03, $04, $08, $11, $52
        .byte $12, $11, $10, $08, $07, $20, $41, $00
        .byte $00, $82, $04, $E0, $10, $08, $E0, $10
        .byte $4A, $88, $08, $10, $E0, $04, $02, $00
; slot 13/15 of "Magick" row -- portal
magic_portal_pat:
        .byte $00, $00, $10, $7F, $10, $14, $12, $10
        .byte $11, $12, $14, $10, $3F, $10, $10, $00
        .byte $00, $08, $08, $FC, $08, $28, $48, $88
        .byte $08, $48, $28, $08, $FE, $08, $00, $00
; slot 14/15 of "Magick" row -- hand
magic_hand_pat:
        .byte $00, $40, $21, $05, $05, $05, $05, $65
        .byte $07, $26, $16, $0E, $07, $23, $40, $00
        .byte $00, $02, $44, $40, $40, $50, $50, $56
        .byte $F0, $30, $B0, $30, $F0, $E4, $02, $00
; slot 15/15 of "Magick" row -- sun
magic_sun_pat:
        .byte $00, $03, $0C, $10, $21, $21, $41, $40
        .byte $40, $46, $26, $20, $10, $0C, $03, $00
        .byte $00, $C0, $70, $88, $04, $04, $06, $8A
        .byte $72, $02, $04, $04, $08, $30, $C0, $00
