; ============================================================================
; sprites_symbols_hgr.inc  --  23 sprites_symbols renormalised for GEN2 HGR
; ----------------------------------------------------------------------------
; Auto-generated from dev/lib/tms9918/sprites_symbols.asm by
; dev/projects/hgr_symbols/_gen_sprites_symbols_hgr.py.
;
; Layout: each sprite = 16 rows x 3 bytes = 48 B.
; HGR encoding: bit 0 = leftmost pixel within byte; bit 7 stays
; clear (NTSC group selector). 16-px sprite occupies cols 0..6
; in byte 0, cols 7..13 in byte 1, cols 14..15 in bits 0..1 of
; byte 2. Render by storing the 3 bytes at (hgr_row+X+0..2)
; for each of the 16 rows.
;
; Source: SCROLL-O-SPRITES "Symbols" by Quale, May 2013, CC-BY-3.0.
; ============================================================================

HGR_SYMBOL_BYTES_PER_GLYPH = 48
HGR_SYMBOL_GLYPH_COUNT     = 23

hgr_symbols_data:
        ; --- 00 sym_at_pat ---
        .byte $00, $00, $00  ; y00
        .byte $00, $00, $00  ; y01
        .byte $60, $0F, $00  ; y02
        .byte $18, $30, $00  ; y03
        .byte $0C, $60, $00  ; y04
        .byte $46, $4B, $01  ; y05
        .byte $66, $4C, $01  ; y06
        .byte $66, $4C, $01  ; y07
        .byte $66, $4C, $01  ; y08
        .byte $66, $4C, $01  ; y09
        .byte $46, $7B, $00  ; y10
        .byte $0C, $00, $00  ; y11
        .byte $18, $30, $00  ; y12
        .byte $60, $0F, $00  ; y13
        .byte $00, $00, $00  ; y14
        .byte $00, $00, $00  ; y15
        ; --- 01 sym_arrow_up_pat ---
        .byte $00, $00, $00  ; y00
        .byte $00, $00, $00  ; y01
        .byte $00, $03, $00  ; y02
        .byte $40, $07, $00  ; y03
        .byte $60, $0F, $00  ; y04
        .byte $70, $1F, $00  ; y05
        .byte $78, $3F, $00  ; y06
        .byte $7C, $7F, $00  ; y07
        .byte $40, $07, $00  ; y08
        .byte $40, $07, $00  ; y09
        .byte $40, $07, $00  ; y10
        .byte $40, $07, $00  ; y11
        .byte $40, $07, $00  ; y12
        .byte $40, $07, $00  ; y13
        .byte $00, $00, $00  ; y14
        .byte $00, $00, $00  ; y15
        ; --- 02 sym_arrow_ne_pat ---
        .byte $00, $00, $00  ; y00
        .byte $00, $00, $00  ; y01
        .byte $00, $00, $00  ; y02
        .byte $70, $3F, $00  ; y03
        .byte $60, $3F, $00  ; y04
        .byte $40, $3F, $00  ; y05
        .byte $00, $3F, $00  ; y06
        .byte $40, $3F, $00  ; y07
        .byte $60, $3F, $00  ; y08
        .byte $70, $3B, $00  ; y09
        .byte $78, $31, $00  ; y10
        .byte $7C, $20, $00  ; y11
        .byte $38, $00, $00  ; y12
        .byte $10, $00, $00  ; y13
        .byte $00, $00, $00  ; y14
        .byte $00, $00, $00  ; y15
        ; --- 03 sym_plus_pat ---
        .byte $00, $00, $00  ; y00
        .byte $00, $00, $00  ; y01
        .byte $00, $00, $00  ; y02
        .byte $40, $07, $00  ; y03
        .byte $40, $07, $00  ; y04
        .byte $40, $07, $00  ; y05
        .byte $78, $3F, $00  ; y06
        .byte $78, $3F, $00  ; y07
        .byte $78, $3F, $00  ; y08
        .byte $78, $3F, $00  ; y09
        .byte $40, $07, $00  ; y10
        .byte $40, $07, $00  ; y11
        .byte $40, $07, $00  ; y12
        .byte $00, $00, $00  ; y13
        .byte $00, $00, $00  ; y14
        .byte $00, $00, $00  ; y15
        ; --- 04 sym_x_pat ---
        .byte $00, $00, $00  ; y00
        .byte $00, $00, $00  ; y01
        .byte $00, $00, $00  ; y02
        .byte $18, $30, $00  ; y03
        .byte $38, $38, $00  ; y04
        .byte $70, $1C, $00  ; y05
        .byte $60, $0F, $00  ; y06
        .byte $40, $07, $00  ; y07
        .byte $40, $07, $00  ; y08
        .byte $60, $0F, $00  ; y09
        .byte $70, $1C, $00  ; y10
        .byte $38, $38, $00  ; y11
        .byte $18, $30, $00  ; y12
        .byte $00, $00, $00  ; y13
        .byte $00, $00, $00  ; y14
        .byte $00, $00, $00  ; y15
        ; --- 05 sym_heart_pat ---
        .byte $00, $00, $00  ; y00
        .byte $00, $00, $00  ; y01
        .byte $00, $00, $00  ; y02
        .byte $78, $3C, $00  ; y03
        .byte $7C, $7F, $00  ; y04
        .byte $7C, $7F, $00  ; y05
        .byte $7C, $7F, $00  ; y06
        .byte $7C, $7F, $00  ; y07
        .byte $78, $3F, $00  ; y08
        .byte $70, $1F, $00  ; y09
        .byte $60, $0F, $00  ; y10
        .byte $40, $07, $00  ; y11
        .byte $00, $03, $00  ; y12
        .byte $00, $00, $00  ; y13
        .byte $00, $00, $00  ; y14
        .byte $00, $00, $00  ; y15
        ; --- 06 sym_star_pat ---
        .byte $00, $00, $00  ; y00
        .byte $00, $03, $00  ; y01
        .byte $00, $03, $00  ; y02
        .byte $40, $07, $00  ; y03
        .byte $40, $07, $00  ; y04
        .byte $7E, $7F, $01  ; y05
        .byte $7C, $7F, $00  ; y06
        .byte $78, $3F, $00  ; y07
        .byte $70, $1F, $00  ; y08
        .byte $78, $3F, $00  ; y09
        .byte $78, $3F, $00  ; y10
        .byte $7C, $7C, $00  ; y11
        .byte $1C, $70, $00  ; y12
        .byte $06, $40, $01  ; y13
        .byte $00, $00, $00  ; y14
        .byte $00, $00, $00  ; y15
        ; --- 07 sym_target_pat ---
        .byte $00, $00, $00  ; y00
        .byte $00, $03, $00  ; y01
        .byte $0C, $60, $00  ; y02
        .byte $64, $4F, $00  ; y03
        .byte $70, $1F, $00  ; y04
        .byte $78, $3F, $00  ; y05
        .byte $78, $3F, $00  ; y06
        .byte $7A, $3F, $01  ; y07
        .byte $7A, $3F, $01  ; y08
        .byte $78, $3F, $00  ; y09
        .byte $78, $3F, $00  ; y10
        .byte $70, $1F, $00  ; y11
        .byte $64, $4F, $00  ; y12
        .byte $0C, $60, $00  ; y13
        .byte $00, $03, $00  ; y14
        .byte $00, $00, $00  ; y15
        ; --- 08 sym_moon_pat ---
        .byte $00, $00, $00  ; y00
        .byte $00, $00, $00  ; y01
        .byte $70, $03, $00  ; y02
        .byte $78, $0F, $00  ; y03
        .byte $40, $1F, $00  ; y04
        .byte $00, $1F, $00  ; y05
        .byte $00, $3E, $00  ; y06
        .byte $00, $3E, $00  ; y07
        .byte $00, $3E, $00  ; y08
        .byte $00, $3E, $00  ; y09
        .byte $00, $1F, $00  ; y10
        .byte $40, $1F, $00  ; y11
        .byte $78, $0F, $00  ; y12
        .byte $70, $03, $00  ; y13
        .byte $00, $00, $00  ; y14
        .byte $00, $00, $00  ; y15
        ; --- 09 sym_eye_pat ---
        .byte $00, $00, $00  ; y00
        .byte $40, $07, $00  ; y01
        .byte $70, $1F, $00  ; y02
        .byte $78, $3F, $00  ; y03
        .byte $3C, $78, $00  ; y04
        .byte $1C, $70, $00  ; y05
        .byte $0E, $63, $01  ; y06
        .byte $4E, $67, $01  ; y07
        .byte $4E, $67, $01  ; y08
        .byte $0E, $63, $01  ; y09
        .byte $1C, $70, $00  ; y10
        .byte $3C, $78, $00  ; y11
        .byte $78, $3F, $00  ; y12
        .byte $70, $1F, $00  ; y13
        .byte $40, $07, $00  ; y14
        .byte $00, $00, $00  ; y15
        ; --- 10 sym_warning_pat ---
        .byte $00, $00, $00  ; y00
        .byte $00, $03, $00  ; y01
        .byte $40, $07, $00  ; y02
        .byte $40, $07, $00  ; y03
        .byte $60, $0F, $00  ; y04
        .byte $60, $0C, $00  ; y05
        .byte $70, $1C, $00  ; y06
        .byte $70, $1C, $00  ; y07
        .byte $78, $3C, $00  ; y08
        .byte $78, $3C, $00  ; y09
        .byte $7C, $7F, $00  ; y10
        .byte $7C, $7C, $00  ; y11
        .byte $7E, $7C, $01  ; y12
        .byte $7E, $7F, $01  ; y13
        .byte $00, $00, $00  ; y14
        .byte $00, $00, $00  ; y15
        ; --- 11 sym_note_pat ---
        .byte $00, $00, $00  ; y00
        .byte $00, $30, $00  ; y01
        .byte $00, $3C, $00  ; y02
        .byte $00, $3F, $00  ; y03
        .byte $60, $3F, $00  ; y04
        .byte $60, $27, $00  ; y05
        .byte $60, $21, $00  ; y06
        .byte $20, $20, $00  ; y07
        .byte $20, $2C, $00  ; y08
        .byte $20, $3C, $00  ; y09
        .byte $2C, $3C, $00  ; y10
        .byte $3C, $18, $00  ; y11
        .byte $3C, $00, $00  ; y12
        .byte $18, $00, $00  ; y13
        .byte $00, $00, $00  ; y14
        .byte $00, $00, $00  ; y15
        ; --- 12 sym_fire_pat ---
        .byte $00, $00, $00  ; y00
        .byte $00, $01, $00  ; y01
        .byte $08, $13, $00  ; y02
        .byte $4C, $37, $00  ; y03
        .byte $4C, $7F, $00  ; y04
        .byte $64, $7F, $00  ; y05
        .byte $70, $7F, $01  ; y06
        .byte $7C, $7F, $01  ; y07
        .byte $3E, $76, $01  ; y08
        .byte $4E, $58, $01  ; y09
        .byte $16, $60, $01  ; y10
        .byte $0E, $40, $01  ; y11
        .byte $0C, $40, $00  ; y12
        .byte $18, $30, $00  ; y13
        .byte $60, $0F, $00  ; y14
        .byte $00, $00, $00  ; y15
        ; --- 13 sym_sparkle_pat ---
        .byte $00, $00, $00  ; y00
        .byte $00, $00, $00  ; y01
        .byte $08, $0C, $00  ; y02
        .byte $2A, $0C, $00  ; y03
        .byte $1C, $00, $00  ; y04
        .byte $2A, $20, $00  ; y05
        .byte $08, $28, $01  ; y06
        .byte $00, $70, $00  ; y07
        .byte $00, $28, $01  ; y08
        .byte $00, $20, $00  ; y09
        .byte $00, $01, $00  ; y10
        .byte $20, $05, $00  ; y11
        .byte $40, $03, $00  ; y12
        .byte $20, $05, $00  ; y13
        .byte $00, $01, $00  ; y14
        .byte $00, $00, $00  ; y15
        ; --- 14 sym_drop_pat ---
        .byte $00, $00, $00  ; y00
        .byte $00, $03, $00  ; y01
        .byte $40, $04, $00  ; y02
        .byte $40, $04, $00  ; y03
        .byte $60, $09, $00  ; y04
        .byte $60, $09, $00  ; y05
        .byte $70, $13, $00  ; y06
        .byte $70, $13, $00  ; y07
        .byte $78, $23, $00  ; y08
        .byte $78, $23, $00  ; y09
        .byte $68, $21, $00  ; y10
        .byte $08, $20, $00  ; y11
        .byte $08, $20, $00  ; y12
        .byte $10, $10, $00  ; y13
        .byte $60, $0F, $00  ; y14
        .byte $00, $00, $00  ; y15
        ; --- 15 sym_lightning_pat ---
        .byte $00, $00, $00  ; y00
        .byte $00, $00, $00  ; y01
        .byte $40, $3F, $00  ; y02
        .byte $40, $1F, $00  ; y03
        .byte $60, $0F, $00  ; y04
        .byte $60, $07, $00  ; y05
        .byte $70, $03, $00  ; y06
        .byte $70, $3F, $00  ; y07
        .byte $78, $1F, $00  ; y08
        .byte $00, $0F, $00  ; y09
        .byte $00, $07, $00  ; y10
        .byte $40, $03, $00  ; y11
        .byte $40, $01, $00  ; y12
        .byte $60, $00, $00  ; y13
        .byte $20, $00, $00  ; y14
        .byte $00, $00, $00  ; y15
        ; --- 16 sym_spiral_pat ---
        .byte $00, $00, $00  ; y00
        .byte $40, $07, $00  ; y01
        .byte $70, $1F, $00  ; y02
        .byte $78, $3F, $00  ; y03
        .byte $3C, $78, $00  ; y04
        .byte $1C, $70, $00  ; y05
        .byte $0E, $63, $01  ; y06
        .byte $4E, $64, $01  ; y07
        .byte $4E, $66, $01  ; y08
        .byte $4E, $71, $01  ; y09
        .byte $0C, $7F, $00  ; y10
        .byte $1C, $3E, $00  ; y11
        .byte $38, $00, $00  ; y12
        .byte $70, $00, $00  ; y13
        .byte $40, $0B, $00  ; y14
        .byte $00, $00, $00  ; y15
        ; --- 17 sym_potion_pat ---
        .byte $00, $00, $00  ; y00
        .byte $00, $3E, $00  ; y01
        .byte $00, $7F, $00  ; y02
        .byte $40, $7F, $01  ; y03
        .byte $40, $7F, $01  ; y04
        .byte $40, $3E, $01  ; y05
        .byte $40, $00, $01  ; y06
        .byte $1C, $41, $00  ; y07
        .byte $3E, $3E, $00  ; y08
        .byte $2E, $00, $00  ; y09
        .byte $22, $00, $00  ; y10
        .byte $1C, $06, $00  ; y11
        .byte $00, $0F, $00  ; y12
        .byte $00, $09, $00  ; y13
        .byte $00, $06, $00  ; y14
        .byte $00, $00, $00  ; y15
        ; --- 18 sym_zzz_pat ---
        .byte $00, $00, $00  ; y00
        .byte $00, $7F, $01  ; y01
        .byte $00, $7F, $01  ; y02
        .byte $00, $70, $00  ; y03
        .byte $00, $38, $00  ; y04
        .byte $3E, $1C, $00  ; y05
        .byte $10, $0E, $00  ; y06
        .byte $08, $7F, $01  ; y07
        .byte $04, $7F, $01  ; y08
        .byte $3E, $00, $00  ; y09
        .byte $00, $00, $00  ; y10
        .byte $40, $07, $00  ; y11
        .byte $00, $02, $00  ; y12
        .byte $00, $01, $00  ; y13
        .byte $40, $07, $00  ; y14
        .byte $00, $00, $00  ; y15
        ; --- 19 sym_skull_pat ---
        .byte $00, $00, $00  ; y00
        .byte $60, $0F, $00  ; y01
        .byte $70, $1F, $00  ; y02
        .byte $78, $3F, $00  ; y03
        .byte $78, $3F, $00  ; y04
        .byte $18, $33, $00  ; y05
        .byte $08, $23, $00  ; y06
        .byte $08, $23, $00  ; y07
        .byte $70, $1C, $00  ; y08
        .byte $40, $07, $00  ; y09
        .byte $50, $15, $00  ; y10
        .byte $10, $10, $00  ; y11
        .byte $30, $1B, $00  ; y12
        .byte $60, $0F, $00  ; y13
        .byte $40, $07, $00  ; y14
        .byte $00, $00, $00  ; y15
        ; --- 20 sym_swords_pat ---
        .byte $00, $00, $00  ; y00
        .byte $0E, $60, $01  ; y01
        .byte $1E, $70, $01  ; y02
        .byte $3E, $78, $01  ; y03
        .byte $7C, $7C, $00  ; y04
        .byte $38, $3E, $00  ; y05
        .byte $10, $1F, $00  ; y06
        .byte $40, $0F, $00  ; y07
        .byte $60, $07, $00  ; y08
        .byte $70, $13, $00  ; y09
        .byte $7C, $79, $00  ; y10
        .byte $7C, $7C, $00  ; y11
        .byte $38, $38, $00  ; y12
        .byte $34, $58, $00  ; y13
        .byte $02, $00, $01  ; y14
        .byte $00, $00, $00  ; y15
        ; --- 21 sym_shield_pat ---
        .byte $00, $00, $00  ; y00
        .byte $78, $3F, $00  ; y01
        .byte $04, $40, $00  ; y02
        .byte $74, $41, $00  ; y03
        .byte $74, $41, $00  ; y04
        .byte $74, $41, $00  ; y05
        .byte $74, $41, $00  ; y06
        .byte $74, $41, $00  ; y07
        .byte $74, $41, $00  ; y08
        .byte $74, $41, $00  ; y09
        .byte $68, $21, $00  ; y10
        .byte $50, $11, $00  ; y11
        .byte $20, $09, $00  ; y12
        .byte $40, $04, $00  ; y13
        .byte $00, $03, $00  ; y14
        .byte $00, $00, $00  ; y15
        ; --- 22 sym_hourglass_pat ---
        .byte $00, $00, $00  ; y00
        .byte $78, $3F, $00  ; y01
        .byte $78, $3F, $00  ; y02
        .byte $10, $10, $00  ; y03
        .byte $10, $10, $00  ; y04
        .byte $10, $10, $00  ; y05
        .byte $20, $08, $00  ; y06
        .byte $40, $04, $00  ; y07
        .byte $40, $04, $00  ; y08
        .byte $20, $08, $00  ; y09
        .byte $10, $13, $00  ; y10
        .byte $50, $17, $00  ; y11
        .byte $70, $1F, $00  ; y12
        .byte $78, $3F, $00  ; y13
        .byte $78, $3F, $00  ; y14
        .byte $00, $00, $00  ; y15
