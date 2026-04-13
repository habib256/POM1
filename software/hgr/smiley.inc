; =============================================================================
; smiley.inc — 28x28 HGR (POM1)
; =============================================================================
; Source: pixel-set-with-black-white-emojis-...2144.avif
; Grid 6x4, row0 cols0-5; 14x14 NN resize, x2 + HGR gap-bridge (NTSC).
; 28 lines x 4 bytes = 112 bytes/glyph.
; Order: 0=grin 1=laugh 2=wink 3=neutral 4=sad 5=surprised (L→R on sheet)
; =============================================================================

HGR_SMILEY_BYTES_PER_GLYPH = 112
HGR_SMILEY_GLYPH_COUNT     = 6

hgr_smiley28_font:
        ; --- 0 grin (from AVIF r0c0) ---
        .byte $00, $00, $00, $00  ; y0
        .byte $00, $00, $00, $00  ; y1
        .byte $00, $00, $00, $00  ; y2
        .byte $00, $00, $00, $00  ; y3
        .byte $00, $00, $00, $00  ; y4
        .byte $00, $00, $00, $00  ; y5
        .byte $00, $60, $7F, $1F  ; y6
        .byte $00, $60, $7F, $1F  ; y7
        .byte $00, $78, $7F, $1F  ; y8
        .byte $00, $78, $7F, $1F  ; y9
        .byte $00, $7E, $7F, $1F  ; y10
        .byte $00, $7E, $7F, $1F  ; y11
        .byte $40, $7F, $7F, $1F  ; y12
        .byte $40, $7F, $7F, $1F  ; y13
        .byte $40, $7F, $73, $07  ; y14
        .byte $40, $7F, $73, $07  ; y15
        .byte $40, $7F, $73, $07  ; y16
        .byte $40, $7F, $73, $07  ; y17
        .byte $40, $7F, $70, $07  ; y18
        .byte $40, $7F, $70, $07  ; y19
        .byte $40, $7F, $7F, $1F  ; y20
        .byte $40, $7F, $7F, $1F  ; y21
        .byte $40, $7F, $7F, $1F  ; y22
        .byte $40, $7F, $7F, $1F  ; y23
        .byte $40, $7F, $0F, $18  ; y24
        .byte $40, $7F, $0F, $18  ; y25
        .byte $40, $7F, $73, $07  ; y26
        .byte $40, $7F, $73, $07  ; y27
        ; --- 1 laugh (from AVIF r0c1) ---
        .byte $00, $00, $00, $00  ; y0
        .byte $00, $00, $00, $00  ; y1
        .byte $00, $00, $00, $00  ; y2
        .byte $00, $00, $00, $00  ; y3
        .byte $00, $00, $00, $00  ; y4
        .byte $00, $00, $00, $00  ; y5
        .byte $0C, $00, $00, $18  ; y6
        .byte $0C, $00, $00, $18  ; y7
        .byte $3C, $00, $00, $1E  ; y8
        .byte $3C, $00, $00, $1E  ; y9
        .byte $7C, $01, $40, $1F  ; y10
        .byte $7C, $01, $40, $1F  ; y11
        .byte $7C, $07, $70, $1F  ; y12
        .byte $7C, $07, $70, $1F  ; y13
        .byte $7C, $07, $70, $1F  ; y14
        .byte $7C, $07, $70, $1F  ; y15
        .byte $7C, $07, $70, $01  ; y16
        .byte $7C, $07, $70, $01  ; y17
        .byte $70, $07, $70, $1F  ; y18
        .byte $70, $07, $70, $1F  ; y19
        .byte $7C, $07, $70, $1F  ; y20
        .byte $7C, $07, $70, $1F  ; y21
        .byte $7C, $07, $70, $1F  ; y22
        .byte $7C, $07, $70, $1F  ; y23
        .byte $7C, $07, $70, $1F  ; y24
        .byte $7C, $07, $70, $1F  ; y25
        .byte $7C, $07, $70, $1F  ; y26
        .byte $7C, $07, $70, $1F  ; y27
        ; --- 2 wink (from AVIF r0c2) ---
        .byte $00, $00, $00, $00  ; y0
        .byte $00, $00, $00, $00  ; y1
        .byte $00, $00, $00, $00  ; y2
        .byte $00, $00, $00, $00  ; y3
        .byte $00, $00, $00, $00  ; y4
        .byte $00, $00, $00, $00  ; y5
        .byte $7C, $7F, $00, $00  ; y6
        .byte $7C, $7F, $00, $00  ; y7
        .byte $7C, $7F, $0F, $00  ; y8
        .byte $7C, $7F, $0F, $00  ; y9
        .byte $7C, $7F, $3F, $00  ; y10
        .byte $7C, $7F, $3F, $00  ; y11
        .byte $7C, $7F, $7F, $01  ; y12
        .byte $7C, $7F, $7F, $01  ; y13
        .byte $7C, $7F, $7F, $01  ; y14
        .byte $7C, $7F, $7F, $01  ; y15
        .byte $70, $07, $70, $01  ; y16
        .byte $70, $07, $70, $01  ; y17
        .byte $70, $67, $7F, $01  ; y18
        .byte $70, $67, $7F, $01  ; y19
        .byte $7C, $7F, $7F, $01  ; y20
        .byte $7C, $7F, $7F, $01  ; y21
        .byte $7C, $7F, $7F, $01  ; y22
        .byte $7C, $7F, $7F, $01  ; y23
        .byte $70, $67, $7F, $01  ; y24
        .byte $70, $67, $7F, $01  ; y25
        .byte $0C, $78, $7F, $01  ; y26
        .byte $0C, $78, $7F, $01  ; y27
        ; --- 3 neutral (from AVIF r0c3) ---
        .byte $00, $00, $00, $00  ; y0
        .byte $00, $00, $00, $00  ; y1
        .byte $00, $00, $00, $00  ; y2
        .byte $00, $00, $00, $00  ; y3
        .byte $00, $00, $00, $00  ; y4
        .byte $00, $00, $00, $00  ; y5
        .byte $00, $60, $7F, $1F  ; y6
        .byte $00, $60, $7F, $1F  ; y7
        .byte $00, $78, $7F, $1F  ; y8
        .byte $00, $78, $7F, $1F  ; y9
        .byte $00, $7E, $7F, $1F  ; y10
        .byte $00, $7E, $7F, $1F  ; y11
        .byte $40, $7F, $7F, $1F  ; y12
        .byte $40, $7F, $7F, $1F  ; y13
        .byte $40, $7F, $7C, $19  ; y14
        .byte $40, $7F, $7C, $19  ; y15
        .byte $40, $7F, $7C, $19  ; y16
        .byte $40, $7F, $7C, $19  ; y17
        .byte $40, $7F, $7C, $19  ; y18
        .byte $40, $7F, $7C, $19  ; y19
        .byte $40, $7F, $7F, $1F  ; y20
        .byte $40, $7F, $7F, $1F  ; y21
        .byte $40, $7F, $7F, $1F  ; y22
        .byte $40, $7F, $7F, $1F  ; y23
        .byte $40, $7F, $00, $18  ; y24
        .byte $40, $7F, $00, $18  ; y25
        .byte $40, $7F, $7F, $1F  ; y26
        .byte $40, $7F, $7F, $1F  ; y27
        ; --- 4 sad (from AVIF r0c4) ---
        .byte $00, $00, $00, $00  ; y0
        .byte $00, $00, $00, $00  ; y1
        .byte $00, $00, $00, $00  ; y2
        .byte $00, $00, $00, $00  ; y3
        .byte $00, $00, $00, $00  ; y4
        .byte $00, $00, $00, $00  ; y5
        .byte $00, $00, $00, $18  ; y6
        .byte $00, $00, $00, $18  ; y7
        .byte $3C, $00, $00, $1E  ; y8
        .byte $3C, $00, $00, $1E  ; y9
        .byte $7C, $01, $40, $1F  ; y10
        .byte $7C, $01, $40, $1F  ; y11
        .byte $7C, $07, $70, $1F  ; y12
        .byte $7C, $07, $70, $1F  ; y13
        .byte $7C, $07, $70, $1F  ; y14
        .byte $7C, $07, $70, $1F  ; y15
        .byte $7C, $07, $70, $1F  ; y16
        .byte $7C, $07, $70, $1F  ; y17
        .byte $7C, $07, $70, $1F  ; y18
        .byte $7C, $07, $70, $1F  ; y19
        .byte $7C, $07, $70, $1F  ; y20
        .byte $7C, $07, $70, $1F  ; y21
        .byte $7C, $07, $70, $1F  ; y22
        .byte $7C, $07, $70, $1F  ; y23
        .byte $7C, $07, $70, $1F  ; y24
        .byte $7C, $07, $70, $1F  ; y25
        .byte $7C, $07, $70, $1F  ; y26
        .byte $7C, $07, $70, $1F  ; y27
        ; --- 5 surprised (from AVIF r0c5) ---
        .byte $00, $00, $00, $00  ; y0
        .byte $00, $00, $00, $00  ; y1
        .byte $00, $00, $00, $00  ; y2
        .byte $00, $00, $00, $00  ; y3
        .byte $00, $00, $00, $00  ; y4
        .byte $00, $00, $00, $00  ; y5
        .byte $7C, $7F, $00, $00  ; y6
        .byte $7C, $7F, $00, $00  ; y7
        .byte $7C, $7F, $0F, $00  ; y8
        .byte $7C, $7F, $0F, $00  ; y9
        .byte $7C, $7F, $3F, $00  ; y10
        .byte $7C, $7F, $3F, $00  ; y11
        .byte $7C, $7F, $7F, $01  ; y12
        .byte $7C, $7F, $7F, $01  ; y13
        .byte $70, $67, $7F, $01  ; y14
        .byte $70, $67, $7F, $01  ; y15
        .byte $70, $67, $7F, $01  ; y16
        .byte $70, $67, $7F, $01  ; y17
        .byte $70, $67, $7F, $01  ; y18
        .byte $70, $67, $7F, $01  ; y19
        .byte $7C, $7F, $7F, $01  ; y20
        .byte $7C, $7F, $7F, $01  ; y21
        .byte $7C, $7F, $7F, $01  ; y22
        .byte $7C, $7F, $7F, $01  ; y23
        .byte $70, $67, $7F, $01  ; y24
        .byte $70, $67, $7F, $01  ; y25
        .byte $0C, $78, $7F, $01  ; y26
        .byte $0C, $78, $7F, $01  ; y27

.assert (* - hgr_smiley28_font) = (HGR_SMILEY_GLYPH_COUNT * HGR_SMILEY_BYTES_PER_GLYPH), error
