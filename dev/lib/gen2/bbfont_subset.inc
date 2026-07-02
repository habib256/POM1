; =============================================================================
; bbfont_subset.inc — HUD/title font for HGR_Sokoban (GEN2)
; =============================================================================
; Source glyphs: Michael Pohoreski, apple2_hgr_font_tutorial (asm/fontbb.s).
; Encoding: AND #$7F; 8 rows top→bottom; bit 0 = left; no per-byte rev7.
;
; Exception: 'Q' (index 31) uses the left-7 raster from
; fonts/font_codepage_437_8x8.png (same GEN2 encoding).
;
; Full CP437 0..255: bbfont_cp437.inc (HGR_BBFontShow).
;
; FROZEN manual subset — NOT emitted by tools/build_shared_font.py (note the
; hand-tuned 'Q' exception above). Do not edit glyphs here; fix the master
; bbfont_cp437.inc instead, or re-derive this table by hand from it if the
; subset must change.
; =============================================================================

BBFONT_SOKOBAN_BYTES_PER_GLYPH = 8
BBFONT_SOKOBAN_GLYPH_COUNT     = 38

HGR_Sokoban_bbfont:
        .byte $1E, $33, $33, $33, $33, $33, $1E, $00  ; 0 '0'
        .byte $0C, $0E, $0F, $0C, $0C, $0C, $3F, $00  ; 1 '1'
        .byte $1E, $3F, $33, $38, $0E, $3F, $3F, $00  ; 2 '2'
        .byte $1E, $3F, $30, $3E, $30, $3F, $1E, $00  ; 3 '3'
        .byte $38, $3C, $36, $33, $3F, $30, $30, $00  ; 4 '4'
        .byte $3F, $3F, $03, $1F, $30, $3F, $1E, $00  ; 5 '5'
        .byte $1E, $3F, $03, $1F, $33, $3F, $1E, $00  ; 6 '6'
        .byte $3F, $3F, $30, $18, $0C, $0C, $0C, $00  ; 7 '7'
        .byte $1E, $3F, $33, $1E, $33, $3F, $1E, $00  ; 8 '8'
        .byte $1E, $3F, $33, $3E, $30, $3F, $1E, $00  ; 9 '9'
        .byte $33, $3F, $33, $33, $33, $33, $33, $00  ; 10 'M'
        .byte $33, $33, $33, $33, $33, $1E, $0C, $00  ; 11 'V'
        .byte $00, $1C, $1C, $00, $00, $1C, $1C, $00  ; 12 ':'
        .byte $1E, $33, $03, $1E, $30, $33, $1E, $00  ; 13 'S'
        .byte $1E, $3F, $33, $33, $33, $3F, $1E, $00  ; 14 'O'
        .byte $33, $3B, $1F, $0F, $1F, $3B, $33, $00  ; 15 'K'
        .byte $1F, $3F, $33, $1F, $33, $3F, $1F, $00  ; 16 'B'
        .byte $1E, $3F, $33, $3F, $3F, $33, $33, $00  ; 17 'A'
        .byte $33, $33, $37, $3F, $3B, $33, $33, $00  ; 18 'N'
        .byte $1F, $3F, $33, $3F, $1F, $03, $03, $00  ; 19 'P'
        .byte $1F, $3F, $33, $3F, $1F, $3B, $33, $00  ; 20 'R'
        .byte $3F, $3F, $03, $1F, $03, $3F, $3F, $00  ; 21 'E'
        .byte $03, $03, $03, $03, $03, $3F, $3F, $00  ; 22 'L'
        .byte $00, $00, $00, $00, $00, $00, $00, $00  ; 23 space
        .byte $1E, $3F, $03, $3B, $33, $3F, $1E, $00  ; 24 'G'
        .byte $33, $33, $33, $3F, $3F, $33, $33, $00  ; 25 'H'
        .byte $3F, $3F, $0C, $0C, $0C, $0C, $0C, $00  ; 26 'T'
        .byte $1F, $3F, $33, $33, $33, $3F, $1F, $00  ; 27 'D'
        .byte $33, $33, $33, $1E, $0C, $0C, $0C, $00  ; 28 'Y'
        .byte $3F, $3F, $0C, $0C, $0C, $3F, $3F, $00  ; 29 'I'
        .byte $33, $33, $33, $33, $33, $3F, $1E, $00  ; 30 'U'
        .byte $1E, $33, $33, $33, $3B, $1E, $38, $00  ; 31 'Q' CP437 PNG
        .byte $33, $33, $33, $33, $33, $3F, $33, $00  ; 32 'W'
        .byte $3F, $3F, $18, $0C, $06, $3F, $3F, $00  ; 33 'Z'
        .byte $1E, $3F, $33, $03, $33, $3F, $1E, $00  ; 34 'C'
        .byte $33, $33, $1E, $0C, $1E, $33, $33, $00  ; 35 'X'
        .byte $38, $1C, $0E, $0E, $0E, $1C, $38, $00  ; 36 '('
        .byte $07, $0E, $1C, $1C, $1C, $0E, $07, $00  ; 37 ')'

.assert (* - HGR_Sokoban_bbfont) = (BBFONT_SOKOBAN_GLYPH_COUNT * BBFONT_SOKOBAN_BYTES_PER_GLYPH), error
