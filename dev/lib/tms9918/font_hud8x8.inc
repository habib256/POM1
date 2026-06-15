; ============================================================================
; font_hud8x8.inc -- shared 8x8 HUD mini-font for TMS9918 games
; ============================================================================
; The HUD overlay font used by the TMS9918 game ROMs (Snake, Sokoban) to draw
; on-screen score/title text in TMS9918 pattern table slots. Each glyph is one
; 8x8 cell, MSB = leftmost pixel, 1 byte per scanline (8 bytes/glyph). Bottom
; row of every glyph is $00 (1-px inter-line gap).
;
; This file emits ONLY the 37 glyphs that are byte-identical across Snake and
; Sokoban: TMS char codes 56..92 — i.e.
;
;     M V : 0 1 2 3 4 5 6 7 8 9 S O K B A N P R E L <space> G H T D Y I U Q
;     W Z C X F
;
; The two slim parenthesis glyphs (char 93 '(' and char 94 ')') are NOT
; included here because the two ROMs draw them slightly differently. Each
; project keeps its own '(' / ')' as a LOCAL OVERRIDE placed *immediately
; after* this include, so the on-ROM byte layout (char 56..94 contiguous,
; 8 bytes each) is preserved exactly.
;
; Usage (both projects):
;     hud_patterns:
;             .include "font_hud8x8.inc"      ; emits chars 56..92 (tms_hud_font)
;             ; char 93 '(' — project-specific
;             .byte ...
;             ; char 94 ')' — project-specific
;             .byte ...
;
; `tms_hud_font` is a plain label before the data — it adds no bytes, so
; `hud_patterns` still points at char 56 and `hud_patterns+256` still indexes
; the second VRAM copy half exactly as before.
;
; cc65 include path: dev/lib/tms9918/  (-I ../../lib/tms9918)
;
; NOTE: no .ifndef guard — this is an inline data fragment meant to be
; included exactly once, at the font's location in the ROM image.
; ============================================================================

tms_hud_font:
        ; char 56 'M'
        .byte $44, $6C, $54, $44, $44, $44, $44, $00
        ; char 57 'V'
        .byte $44, $44, $44, $44, $28, $28, $10, $00
        ; char 58 ':'
        .byte $00, $00, $10, $00, $00, $10, $00, $00
        ; char 59 '0'
        .byte $38, $44, $44, $44, $44, $44, $38, $00
        ; char 60 '1'
        .byte $10, $30, $10, $10, $10, $10, $38, $00
        ; char 61 '2'
        .byte $38, $44, $04, $08, $10, $20, $7C, $00
        ; char 62 '3'
        .byte $38, $44, $04, $38, $04, $44, $38, $00
        ; char 63 '4'
        .byte $44, $44, $44, $7C, $04, $04, $04, $00
        ; char 64 '5'
        .byte $7C, $40, $40, $78, $04, $44, $38, $00
        ; char 65 '6'
        .byte $18, $20, $40, $78, $44, $44, $38, $00
        ; char 66 '7'
        .byte $7C, $04, $08, $10, $20, $20, $20, $00
        ; char 67 '8'
        .byte $38, $44, $44, $38, $44, $44, $38, $00
        ; char 68 '9'
        .byte $38, $44, $44, $3C, $04, $08, $30, $00
        ; char 69 'S'
        .byte $38, $44, $40, $38, $04, $44, $38, $00
        ; char 70 'O'
        .byte $38, $44, $44, $44, $44, $44, $38, $00
        ; char 71 'K'
        .byte $44, $48, $50, $60, $50, $48, $44, $00
        ; char 72 'B'
        .byte $78, $44, $44, $78, $44, $44, $78, $00
        ; char 73 'A'
        .byte $38, $44, $44, $7C, $44, $44, $44, $00
        ; char 74 'N'
        .byte $44, $64, $54, $4C, $44, $44, $44, $00
        ; char 75 'P'
        .byte $78, $44, $44, $78, $40, $40, $40, $00
        ; char 76 'R'
        .byte $78, $44, $44, $78, $50, $48, $44, $00
        ; char 77 'E'
        .byte $7C, $40, $40, $78, $40, $40, $7C, $00
        ; char 78 'L'
        .byte $40, $40, $40, $40, $40, $40, $7C, $00
        ; char 79 ' '
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        ; char 80 'G'
        .byte $38, $44, $40, $4E, $44, $44, $38, $00
        ; char 81 'H'
        .byte $44, $44, $44, $7C, $44, $44, $44, $00
        ; char 82 'T'
        .byte $7C, $10, $10, $10, $10, $10, $10, $00
        ; char 83 'D'
        .byte $78, $44, $44, $44, $44, $44, $78, $00
        ; char 84 'Y'
        .byte $44, $44, $28, $10, $10, $10, $10, $00
        ; char 85 'I'
        .byte $38, $10, $10, $10, $10, $10, $38, $00
        ; char 86 'U'
        .byte $44, $44, $44, $44, $44, $44, $38, $00
        ; char 87 'Q'
        .byte $38, $44, $44, $44, $54, $48, $34, $00
        ; char 88 'W'
        .byte $44, $44, $44, $54, $54, $6C, $44, $00
        ; char 89 'Z'
        .byte $7C, $04, $08, $10, $20, $40, $7C, $00
        ; char 90 'C'
        .byte $38, $44, $40, $40, $40, $44, $38, $00
        ; char 91 'X'
        .byte $44, $44, $28, $10, $28, $44, $44, $00
        ; char 92 'F'
        .byte $7C, $40, $40, $78, $40, $40, $40, $00
