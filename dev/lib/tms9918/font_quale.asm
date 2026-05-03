; ============================================================================
; font_quale.asm  --  8x8 glyph table from Quale's SCROLL-O-SPRITES sheet
; ----------------------------------------------------------------------------
; SCROLL-O-SPRITES font by Quale, May 2013, CC-BY-3.0.
; Lifted from pic/undefined - Imgur.png by tools/extract_scroll_font.py.
;
; Layout: each glyph = 8 bytes (one byte per row, MSB = column 0).
; Compatible with TMS9918 Mode-2 charmap blits (see lib/tms9918/text_bitmap.asm) and Apple-1 charmap.rom.
;
; Tradeoffs (see header of extract_scroll_font.py):
;   - Uppercase accented chars: accent dropped (body only).
;   - Lowercase first 26: body + descenders captured.
;   - Lowercase accented row 1: accent dropped.
;   - Lowercase accented row 2: full 8x8 glyph fits.
;   - Uppercase accented row 2 (band 3 of original sheet) is
;     OMITTED -- 10-row glyphs cannot fit 8x8.
; ============================================================================
.export font_quale_digits_0, font_quale_digits_1, font_quale_digits_2, font_quale_digits_3
.export font_quale_digits_4, font_quale_digits_5, font_quale_digits_6, font_quale_digits_7
.export font_quale_digits_8, font_quale_digits_9, font_quale_upper_A, font_quale_upper_B
.export font_quale_upper_C, font_quale_upper_D, font_quale_upper_E, font_quale_upper_F
.export font_quale_upper_G, font_quale_upper_H, font_quale_upper_I, font_quale_upper_J
.export font_quale_upper_K, font_quale_upper_L, font_quale_upper_M, font_quale_upper_N
.export font_quale_upper_O, font_quale_upper_P, font_quale_upper_Q, font_quale_upper_R
.export font_quale_upper_S, font_quale_upper_T, font_quale_upper_U, font_quale_upper_V
.export font_quale_upper_W, font_quale_upper_X, font_quale_upper_Y, font_quale_upper_Z
.export font_quale_upper_A_grave, font_quale_upper_A_acute, font_quale_upper_A_circ, font_quale_upper_A_tilde
.export font_quale_upper_AE_or_C, font_quale_upper_E_grave, font_quale_lower_a, font_quale_lower_b
.export font_quale_lower_c, font_quale_lower_d, font_quale_lower_e, font_quale_lower_f
.export font_quale_lower_g, font_quale_lower_h, font_quale_lower_i, font_quale_lower_j
.export font_quale_lower_k, font_quale_lower_l, font_quale_lower_m, font_quale_lower_n
.export font_quale_lower_o, font_quale_lower_p, font_quale_lower_q, font_quale_lower_r
.export font_quale_lower_s, font_quale_lower_t, font_quale_lower_u, font_quale_lower_v
.export font_quale_lower_w, font_quale_lower_x, font_quale_lower_y, font_quale_lower_z
.export font_quale_lower_a_grave, font_quale_lower_a_acute, font_quale_lower_a_circ, font_quale_lower_a_tilde
.export font_quale_lower_ae_or_c, font_quale_lower_e_grave, font_quale_lower_acc2_e_acute, font_quale_lower_acc2_e_circ
.export font_quale_lower_acc2_e_diaer, font_quale_lower_acc2_i_grave, font_quale_lower_acc2_i_acute, font_quale_lower_acc2_i_circ
.export font_quale_lower_acc2_i_diaer, font_quale_lower_acc2_o_grave, font_quale_lower_acc2_o_acute, font_quale_lower_acc2_o_circ
.export font_quale_lower_acc2_o_tilde, font_quale_punct_hash, font_quale_punct_percent, font_quale_punct_at
.export font_quale_punct_dollar, font_quale_punct_period, font_quale_punct_comma, font_quale_punct_excl
.export font_quale_punct_quest, font_quale_punct_colon, font_quale_punct_semi, font_quale_punct_apos
.export font_quale_punct_quote, font_quale_punct_lparen, font_quale_punct_rparen, font_quale_punct_lbrack
.export font_quale_punct_rbrack, font_quale_punct_times, font_quale_punct_slash, font_quale_punct_bslash
.export font_quale_punct_plus, font_quale_punct_minus, font_quale_punct_lt, font_quale_punct_eq
.export font_quale_punct_gt, font_quale_punct_tilde, font_quale_decor_deco01, font_quale_decor_deco02
.export font_quale_decor_deco03, font_quale_decor_deco04, font_quale_decor_deco05, font_quale_decor_deco06
.export font_quale_decor_deco07, font_quale_decor_deco08, font_quale_decor_deco09, font_quale_decor_deco10
.export font_quale_decor_deco11, font_quale_decor_deco12, font_quale_decor_deco13, font_quale_decor_deco14
.export font_quale_decor_deco15, font_quale_decor_deco16, font_quale_decor_deco17, font_quale_decor_deco18
.export font_quale_decor_deco19
.export font_quale_digits_data, font_quale_upper_data, font_quale_lower_data, font_quale_lower_acc2_data, font_quale_punct_data, font_quale_decor_data
font_quale_count = 129
.export font_quale_count

.segment "CODE"

; ---- digits: ASCII digits 0-9 ----
font_quale_digits_data:
font_quale_digits_0:
        .byte $00, $3C, $66, $6E, $76, $66, $66, $3C
font_quale_digits_1:
        .byte $00, $18, $38, $18, $18, $18, $18, $7E
font_quale_digits_2:
        .byte $00, $3C, $66, $06, $1C, $30, $60, $7E
font_quale_digits_3:
        .byte $00, $3C, $66, $06, $1C, $06, $66, $3C
font_quale_digits_4:
        .byte $00, $0E, $1E, $36, $66, $7E, $06, $06
font_quale_digits_5:
        .byte $00, $7E, $60, $60, $7C, $06, $06, $7C
font_quale_digits_6:
        .byte $00, $3C, $60, $60, $7C, $66, $66, $3C
font_quale_digits_7:
        .byte $00, $7E, $06, $06, $0C, $0C, $18, $18
font_quale_digits_8:
        .byte $00, $3C, $66, $66, $3C, $66, $66, $3C
font_quale_digits_9:
        .byte $00, $3C, $66, $66, $3E, $06, $06, $3C

; ---- upper: Uppercase A-Z + 6 accented (accent dropped: 8x8 limit) ----
font_quale_upper_data:
font_quale_upper_A:
        .byte $00, $3C, $66, $66, $66, $7E, $66, $66
font_quale_upper_B:
        .byte $00, $7C, $66, $66, $7C, $66, $66, $7C
font_quale_upper_C:
        .byte $00, $3C, $66, $60, $60, $60, $66, $3C
font_quale_upper_D:
        .byte $00, $7C, $66, $66, $66, $66, $66, $7C
font_quale_upper_E:
        .byte $00, $7E, $60, $60, $78, $60, $60, $7E
font_quale_upper_F:
        .byte $00, $7E, $60, $60, $78, $60, $60, $60
font_quale_upper_G:
        .byte $00, $3C, $66, $60, $6E, $66, $66, $3E
font_quale_upper_H:
        .byte $00, $66, $66, $66, $7E, $66, $66, $66
font_quale_upper_I:
        .byte $00, $3C, $18, $18, $18, $18, $18, $3C
font_quale_upper_J:
        .byte $00, $3C, $18, $18, $18, $18, $58, $30
font_quale_upper_K:
        .byte $00, $66, $66, $6C, $78, $6C, $66, $66
font_quale_upper_L:
        .byte $00, $60, $60, $60, $60, $60, $60, $7E
font_quale_upper_M:
        .byte $00, $76, $7E, $6A, $6A, $6A, $62, $62
font_quale_upper_N:
        .byte $00, $66, $66, $76, $6E, $66, $66, $66
font_quale_upper_O:
        .byte $00, $3C, $66, $66, $66, $66, $66, $3C
font_quale_upper_P:
        .byte $00, $7C, $66, $66, $66, $7C, $60, $60
font_quale_upper_Q:
        ; Hand-fixed: Quale's extraction emitted the same bytes as 'O'
        ; (no descender) so QWERTY/QZSD read as OWERTY/OZSD. Restore an
        ; O-shape body with a clear 2-pixel tail under the bottom-right
        ; (so 'Q' is unambiguously distinct from 'O' even at 8x8).
        .byte $00, $3C, $66, $66, $6A, $66, $3C, $03
font_quale_upper_R:
        .byte $00, $7C, $66, $66, $66, $7C, $66, $66
font_quale_upper_S:
        .byte $00, $3C, $66, $60, $3C, $06, $66, $3C
font_quale_upper_T:
        .byte $00, $7E, $18, $18, $18, $18, $18, $18
font_quale_upper_U:
        .byte $00, $66, $66, $66, $66, $66, $66, $3C
font_quale_upper_V:
        .byte $00, $66, $66, $66, $66, $66, $3C, $18
font_quale_upper_W:
        .byte $00, $62, $62, $6A, $6A, $6A, $7E, $76
font_quale_upper_X:
        .byte $00, $66, $66, $3C, $18, $3C, $66, $66
font_quale_upper_Y:
        .byte $00, $66, $66, $66, $3C, $18, $18, $18
font_quale_upper_Z:
        .byte $00, $7E, $06, $0C, $18, $30, $60, $7E
font_quale_upper_A_grave:
        .byte $00, $3C, $66, $66, $66, $7E, $66, $66
font_quale_upper_A_acute:
        .byte $00, $3C, $66, $66, $66, $7E, $66, $66
font_quale_upper_A_circ:
        .byte $00, $3C, $66, $66, $66, $7E, $66, $66
font_quale_upper_A_tilde:
        .byte $00, $3C, $66, $66, $66, $7E, $66, $66
font_quale_upper_AE_or_C:
        .byte $00, $3C, $66, $60, $60, $60, $66, $3C
font_quale_upper_E_grave:
        .byte $00, $7E, $60, $60, $78, $60, $60, $7E

; ---- lower: Lowercase a-z + 6 accented (body+descenders preserved; accent dropped) ----
font_quale_lower_data:
font_quale_lower_a:
        .byte $00, $3C, $06, $3E, $66, $3E, $00, $00
font_quale_lower_b:
        .byte $60, $7C, $66, $66, $66, $7C, $00, $00
font_quale_lower_c:
        .byte $00, $3C, $66, $60, $66, $3C, $00, $00
font_quale_lower_d:
        .byte $06, $3E, $66, $66, $66, $3E, $00, $00
font_quale_lower_e:
        .byte $00, $3C, $66, $7E, $60, $3C, $00, $00
font_quale_lower_f:
        .byte $36, $30, $78, $30, $30, $30, $00, $00
font_quale_lower_g:
        .byte $00, $3E, $66, $66, $66, $3E, $06, $3C
font_quale_lower_h:
        .byte $60, $7C, $66, $66, $66, $66, $00, $00
font_quale_lower_i:
        .byte $00, $3C, $18, $18, $18, $3C, $00, $00
font_quale_lower_j:
        .byte $00, $3C, $18, $18, $18, $18, $18, $30
font_quale_lower_k:
        .byte $60, $66, $6C, $78, $6C, $66, $00, $00
font_quale_lower_l:
        .byte $30, $30, $30, $30, $30, $1C, $00, $00
font_quale_lower_m:
        .byte $00, $7C, $7E, $6A, $6A, $6A, $00, $00
font_quale_lower_n:
        .byte $00, $7C, $66, $66, $66, $66, $00, $00
font_quale_lower_o:
        .byte $00, $3C, $66, $66, $66, $3C, $00, $00
font_quale_lower_p:
        .byte $00, $7C, $66, $66, $66, $7C, $60, $60
font_quale_lower_q:
        .byte $00, $3E, $66, $66, $66, $3E, $06, $06
font_quale_lower_r:
        .byte $00, $6E, $76, $60, $60, $60, $00, $00
font_quale_lower_s:
        .byte $00, $3E, $60, $7E, $06, $7C, $00, $00
font_quale_lower_t:
        .byte $30, $7E, $30, $30, $30, $1C, $00, $00
font_quale_lower_u:
        .byte $00, $66, $66, $66, $66, $3E, $00, $00
font_quale_lower_v:
        .byte $00, $66, $66, $66, $3C, $18, $00, $00
font_quale_lower_w:
        .byte $00, $62, $6A, $6A, $7E, $34, $00, $00
font_quale_lower_x:
        .byte $00, $66, $3C, $18, $3C, $66, $00, $00
font_quale_lower_y:
        .byte $00, $66, $66, $66, $3E, $06, $3C, $00
font_quale_lower_z:
        .byte $00, $7E, $0C, $18, $30, $7E, $00, $00
font_quale_lower_a_grave:
        .byte $00, $3C, $06, $3E, $66, $3E, $00, $00
font_quale_lower_a_acute:
        .byte $00, $3C, $06, $3E, $66, $3E, $00, $00
font_quale_lower_a_circ:
        .byte $00, $3C, $06, $3E, $66, $3E, $00, $00
font_quale_lower_a_tilde:
        .byte $00, $3C, $06, $3E, $66, $3E, $00, $00
font_quale_lower_ae_or_c:
        .byte $00, $3C, $66, $60, $66, $3C, $08, $18
font_quale_lower_e_grave:
        .byte $00, $3C, $66, $7E, $60, $3C, $00, $00

; ---- lower_acc2: Lowercase accented row 2 (full 8x8 fit: accent + body) ----
font_quale_lower_acc2_data:
font_quale_lower_acc2_e_acute:
        .byte $04, $08, $00, $3C, $66, $7E, $60, $3C
font_quale_lower_acc2_e_circ:
        .byte $18, $24, $00, $3C, $66, $7E, $60, $3C
font_quale_lower_acc2_e_diaer:
        .byte $00, $24, $00, $3C, $66, $7E, $60, $3C
font_quale_lower_acc2_i_grave:
        .byte $20, $10, $00, $3C, $66, $66, $66, $3C
font_quale_lower_acc2_i_acute:
        .byte $04, $08, $00, $3C, $66, $66, $66, $3C
font_quale_lower_acc2_i_circ:
        .byte $18, $24, $00, $3C, $66, $66, $66, $3C
font_quale_lower_acc2_i_diaer:
        .byte $00, $24, $00, $3C, $66, $66, $66, $3C
font_quale_lower_acc2_o_grave:
        .byte $20, $10, $00, $66, $66, $66, $66, $3C
font_quale_lower_acc2_o_acute:
        .byte $04, $08, $00, $66, $66, $66, $66, $3C
font_quale_lower_acc2_o_circ:
        .byte $18, $24, $00, $66, $66, $66, $66, $3C
font_quale_lower_acc2_o_tilde:
        .byte $00, $24, $00, $66, $66, $66, $66, $3C

; ---- punct: Punctuation ----
font_quale_punct_data:
font_quale_punct_hash:
        .byte $00, $00, $34, $34, $7E, $34, $7E, $34
font_quale_punct_percent:
        .byte $00, $00, $30, $56, $6C, $18, $36, $6A
font_quale_punct_at:
        .byte $00, $00, $30, $60, $60, $3E, $6C, $6C
font_quale_punct_dollar:
        .byte $00, $00, $3C, $66, $4E, $56, $56, $5E
font_quale_punct_period:
        .byte $00, $08, $3C, $6A, $68, $3C, $16, $56
font_quale_punct_comma:
        .byte $00, $00, $00, $00, $00, $00, $00, $18
font_quale_punct_excl:
        .byte $00, $00, $00, $00, $00, $00, $00, $18
font_quale_punct_quest:
        .byte $00, $00, $18, $18, $18, $18, $00, $18
font_quale_punct_colon:
        .byte $00, $00, $3C, $66, $06, $1C, $00, $18
font_quale_punct_semi:
        .byte $00, $00, $00, $18, $18, $00, $18, $18
font_quale_punct_apos:
        .byte $00, $00, $00, $18, $18, $00, $18, $18
font_quale_punct_quote:
        .byte $00, $18, $18, $08, $00, $00, $00, $00
font_quale_punct_lparen:
        .byte $00, $66, $66, $22, $00, $00, $00, $00
font_quale_punct_rparen:
        .byte $00, $00, $0C, $18, $18, $18, $18, $18
font_quale_punct_lbrack:
        .byte $00, $00, $30, $18, $18, $18, $18, $18
font_quale_punct_rbrack:
        .byte $00, $00, $1C, $18, $18, $18, $18, $18
font_quale_punct_times:
        .byte $00, $00, $38, $18, $18, $18, $18, $18
font_quale_punct_slash:
        .byte $00, $24, $18, $18, $24, $00, $00, $00
font_quale_punct_bslash:
        .byte $00, $00, $03, $06, $0C, $18, $30, $60
font_quale_punct_plus:
        .byte $00, $00, $C0, $60, $30, $18, $0C, $06
font_quale_punct_minus:
        .byte $00, $00, $00, $18, $18, $7E, $18, $18
font_quale_punct_lt:
        .byte $00, $00, $00, $00, $00, $7E, $00, $00
font_quale_punct_eq:
        .byte $00, $00, $0C, $18, $30, $60, $30, $18
font_quale_punct_gt:
        .byte $00, $00, $00, $00, $7E, $00, $7E, $00
font_quale_punct_tilde:
        .byte $00, $00, $30, $18, $0C, $06, $0C, $18

; ---- decor: Decorative symbols (best-effort labels; rename as needed) ----
font_quale_decor_data:
font_quale_decor_deco01:
        .byte $00, $00, $00, $00, $00, $00, $0C, $12
font_quale_decor_deco02:
        .byte $00, $00, $00, $00, $00, $00, $0E, $06
font_quale_decor_deco03:
        .byte $00, $00, $00, $00, $00, $00, $10, $30
font_quale_decor_deco04:
        .byte $00, $00, $00, $00, $00, $00, $18, $3C
font_quale_decor_deco05:
        .byte $00, $00, $00, $00, $00, $00, $08, $0C
font_quale_decor_deco06:
        .byte $00, $00, $00, $00, $00, $00, $18, $18
font_quale_decor_deco07:
        .byte $00, $00, $00, $00, $00, $00, $3C, $76
font_quale_decor_deco08:
        .byte $00, $00, $00, $00, $00, $00, $7E, $42
font_quale_decor_deco09:
        .byte $00, $00, $00, $00, $00, $00, $7E, $42
font_quale_decor_deco10:
        .byte $00, $00, $00, $00, $00, $00, $7E, $42
font_quale_decor_deco11:
        .byte $00, $00, $00, $00, $00, $3C, $42, $81
font_quale_decor_deco12:
        .byte $00, $00, $00, $00, $00, $3C, $4E, $8F
font_quale_decor_deco13:
        .byte $00, $00, $00, $00, $00, $3C, $4E, $8F
font_quale_decor_deco14:
        .byte $00, $00, $00, $00, $00, $3C, $4E, $8F
font_quale_decor_deco15:
        .byte $00, $00, $00, $00, $00, $3C, $7E, $FF
font_quale_decor_deco16:
        .byte $00, $FF, $FF, $FF, $FF, $FF, $FF, $FF
font_quale_decor_deco17:
        .byte $00, $EE, $DD, $BB, $77, $EE, $DD, $BB
font_quale_decor_deco18:
        .byte $00, $AA, $55, $AA, $55, $AA, $55, $AA
font_quale_decor_deco19:
        .byte $00, $11, $22, $44, $88, $11, $22, $44
