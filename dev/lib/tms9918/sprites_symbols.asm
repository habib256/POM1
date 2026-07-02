; ============================================================================
; sprites_symbols.asm  --  23 sprites (16x16, TMS9918 sprite mode)
; ----------------------------------------------------------------------------
; SCROLL-O-SPRITES "Symbols" by Quale, May 2013, CC-BY-3.0.
; Lifted from pic/undefined - Imgur.png by tools/extract_scroll_sprites.py.
;
; Layout: each 16x16 sprite occupies 32 bytes -- the first 16 bytes are
; the left half (column 0..7), the next 16 the right half (column 8..15).
; Native TMS9918 16x16 sprite layout: stream the 32 bytes into a
; sprite-pattern slot starting at the ACTIVE MODE'S sprite-pattern table
; + slot*32 (asm Mode-1 init_vdp_g1: R6=$07 -> $3800; C lib SCREEN1/SCREEN2
; tables: R6=$03 -> $1800 — on those layouts $3800 is the NAME table!).
; ============================================================================
.export sym_at_pat, sym_arrow_up_pat, sym_arrow_ne_pat, sym_plus_pat, sym_x_pat
.export sym_heart_pat, sym_star_pat, sym_target_pat, sym_moon_pat, sym_eye_pat
.export sym_warning_pat, sym_note_pat, sym_fire_pat, sym_sparkle_pat, sym_drop_pat
.export sym_lightning_pat, sym_spiral_pat, sym_potion_pat, sym_zzz_pat, sym_skull_pat
.export sym_swords_pat, sym_shield_pat, sym_hourglass_pat

.segment "CODE"

; slot 01/23 of "Symbols" row -- at
sym_at_pat:
        .byte $00, $00, $07, $18, $30, $63, $66, $66
        .byte $66, $66, $63, $30, $18, $07, $00, $00
        .byte $00, $00, $E0, $18, $0C, $A6, $66, $66
        .byte $66, $66, $BC, $00, $18, $E0, $00, $00
; slot 02/23 of "Symbols" row -- arrow_up
sym_arrow_up_pat:
        .byte $00, $00, $01, $03, $07, $0F, $1F, $3F
        .byte $03, $03, $03, $03, $03, $03, $00, $00
        .byte $00, $00, $80, $C0, $E0, $F0, $F8, $FC
        .byte $C0, $C0, $C0, $C0, $C0, $C0, $00, $00
; slot 03/23 of "Symbols" row -- arrow_ne
sym_arrow_ne_pat:
        .byte $00, $00, $00, $0F, $07, $03, $01, $03
        .byte $07, $0F, $1F, $3E, $1C, $08, $00, $00
        .byte $00, $00, $00, $F8, $F8, $F8, $F8, $F8
        .byte $F8, $B8, $18, $08, $00, $00, $00, $00
; slot 04/23 of "Symbols" row -- plus
sym_plus_pat:
        .byte $00, $00, $00, $03, $03, $03, $1F, $1F
        .byte $1F, $1F, $03, $03, $03, $00, $00, $00
        .byte $00, $00, $00, $C0, $C0, $C0, $F8, $F8
        .byte $F8, $F8, $C0, $C0, $C0, $00, $00, $00
; slot 05/23 of "Symbols" row -- x
sym_x_pat:
        .byte $00, $00, $00, $18, $1C, $0E, $07, $03
        .byte $03, $07, $0E, $1C, $18, $00, $00, $00
        .byte $00, $00, $00, $18, $38, $70, $E0, $C0
        .byte $C0, $E0, $70, $38, $18, $00, $00, $00
; slot 06/23 of "Symbols" row -- heart
sym_heart_pat:
        .byte $00, $00, $00, $1E, $3F, $3F, $3F, $3F
        .byte $1F, $0F, $07, $03, $01, $00, $00, $00
        .byte $00, $00, $00, $78, $FC, $FC, $FC, $FC
        .byte $F8, $F0, $E0, $C0, $80, $00, $00, $00
; slot 07/23 of "Symbols" row -- star
sym_star_pat:
        .byte $00, $01, $01, $03, $03, $7F, $3F, $1F
        .byte $0F, $1F, $1F, $3E, $38, $60, $00, $00
        .byte $00, $80, $80, $C0, $C0, $FE, $FC, $F8
        .byte $F0, $F8, $F8, $7C, $1C, $06, $00, $00
; slot 08/23 of "Symbols" row -- target
sym_target_pat:
        .byte $00, $01, $30, $27, $0F, $1F, $1F, $5F
        .byte $5F, $1F, $1F, $0F, $27, $30, $01, $00
        .byte $00, $80, $0C, $E4, $F0, $F8, $F8, $FA
        .byte $FA, $F8, $F8, $F0, $E4, $0C, $80, $00
; slot 09/23 of "Symbols" row -- moon
sym_moon_pat:
        .byte $00, $00, $0F, $1F, $03, $01, $00, $00
        .byte $00, $00, $01, $03, $1F, $0F, $00, $00
        .byte $00, $00, $80, $E0, $F0, $F0, $F8, $F8
        .byte $F8, $F8, $F0, $F0, $E0, $80, $00, $00
; slot 10/23 of "Symbols" row -- eye
sym_eye_pat:
        .byte $00, $03, $0F, $1F, $3C, $38, $71, $73
        .byte $73, $71, $38, $3C, $1F, $0F, $03, $00
        .byte $00, $C0, $F0, $F8, $3C, $1C, $8E, $CE
        .byte $CE, $8E, $1C, $3C, $F8, $F0, $C0, $00
; slot 11/23 of "Symbols" row -- warning
sym_warning_pat:
        .byte $00, $01, $03, $03, $07, $06, $0E, $0E
        .byte $1E, $1E, $3F, $3E, $7E, $7F, $00, $00
        .byte $00, $80, $C0, $C0, $E0, $60, $70, $70
        .byte $78, $78, $FC, $7C, $7E, $FE, $00, $00
; slot 12/23 of "Symbols" row -- note
sym_note_pat:
        .byte $00, $00, $00, $01, $07, $07, $07, $04
        .byte $04, $04, $34, $3C, $3C, $18, $00, $00
        .byte $00, $18, $78, $F8, $F8, $C8, $08, $08
        .byte $68, $78, $78, $30, $00, $00, $00, $00
; slot 13/23 of "Symbols" row -- fire
sym_fire_pat:
        .byte $00, $01, $11, $33, $33, $27, $0F, $3F
        .byte $7C, $72, $68, $70, $30, $18, $07, $00
        .byte $00, $00, $90, $D8, $FC, $FC, $FE, $FE
        .byte $DE, $36, $0E, $06, $04, $18, $E0, $00
; slot 14/23 of "Symbols" row -- sparkle
sym_sparkle_pat:
        .byte $00, $00, $10, $54, $38, $54, $10, $00
        .byte $00, $00, $01, $05, $03, $05, $01, $00
        .byte $00, $00, $60, $60, $00, $08, $2A, $1C
        .byte $2A, $08, $00, $40, $80, $40, $00, $00
; slot 15/23 of "Symbols" row -- drop
sym_drop_pat:
        .byte $00, $01, $02, $02, $07, $07, $0F, $0F
        .byte $1F, $1F, $17, $10, $10, $08, $07, $00
        .byte $00, $80, $40, $40, $20, $20, $90, $90
        .byte $88, $88, $08, $08, $08, $10, $E0, $00
; slot 16/23 of "Symbols" row -- lightning
sym_lightning_pat:
        .byte $00, $00, $03, $03, $07, $07, $0F, $0F
        .byte $1F, $01, $01, $03, $03, $06, $04, $00
        .byte $00, $00, $F8, $F0, $E0, $C0, $80, $F8
        .byte $F0, $E0, $C0, $80, $00, $00, $00, $00
; slot 17/23 of "Symbols" row -- spiral
sym_spiral_pat:
        .byte $00, $03, $0F, $1F, $3C, $38, $71, $72
        .byte $72, $73, $31, $38, $1C, $0E, $03, $00
        .byte $00, $C0, $F0, $F8, $3C, $1C, $8E, $4E
        .byte $CE, $1E, $FC, $F8, $00, $00, $A0, $00
; slot 18/23 of "Symbols" row -- potion
sym_potion_pat:
        .byte $00, $00, $01, $03, $03, $02, $02, $39
        .byte $7C, $74, $44, $38, $01, $01, $00, $00
        .byte $00, $F8, $FC, $FE, $FE, $FA, $02, $04
        .byte $F8, $00, $00, $C0, $E0, $20, $C0, $00
; slot 19/23 of "Symbols" row -- zzz
sym_zzz_pat:
        .byte $00, $01, $01, $00, $00, $7C, $08, $11
        .byte $21, $7C, $00, $03, $00, $01, $03, $00
        .byte $00, $FE, $FE, $1C, $38, $70, $E0, $FE
        .byte $FE, $00, $00, $C0, $80, $00, $C0, $00
; slot 20/23 of "Symbols" row -- skull
sym_skull_pat:
        .byte $00, $07, $0F, $1F, $1F, $19, $11, $11
        .byte $0E, $03, $0B, $08, $0D, $07, $03, $00
        .byte $00, $E0, $F0, $F8, $F8, $98, $88, $88
        .byte $70, $C0, $50, $10, $B0, $E0, $C0, $00
; slot 21/23 of "Symbols" row -- swords
sym_swords_pat:
        .byte $00, $70, $78, $7C, $3E, $1C, $09, $03
        .byte $07, $0F, $3F, $3E, $1C, $2C, $40, $00
        .byte $00, $0E, $1E, $3E, $7C, $F8, $F0, $E0
        .byte $C0, $90, $3C, $7C, $38, $34, $02, $00
; slot 22/23 of "Symbols" row -- shield
sym_shield_pat:
        .byte $00, $1F, $20, $2F, $2F, $2F, $2F, $2F
        .byte $2F, $2F, $17, $0B, $05, $02, $01, $00
        .byte $00, $F8, $04, $04, $04, $04, $04, $04
        .byte $04, $04, $08, $10, $20, $40, $80, $00
; slot 23/23 of "Symbols" row -- hourglass
sym_hourglass_pat:
        .byte $00, $1F, $1F, $08, $08, $08, $04, $02
        .byte $02, $04, $09, $0B, $0F, $1F, $1F, $00
        .byte $00, $F8, $F8, $10, $10, $10, $20, $40
        .byte $40, $20, $90, $D0, $F0, $F8, $F8, $00
