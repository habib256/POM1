; ============================================================================
; t15_hgr_text8_blit2.s — micro-test: gen2/hgr_text8.asm + gen2/hgr_blit2.asm
; ============================================================================
; GUARDS the two byte-aligned lib modules the TMS->GEN2 game ports share:
;
; hgr_text8 (glyph '!' = 8 rows of $C3 in a local 2-glyph font):
;   - TMS bit order (ht_rev=1): stored byte must be rev7($C3) = $43 on
;     glyph rows 0 ($2004) and 1 ($2404) at byte column 4;
;   - cursor advance: second '!' lands at column 5 ($2005);
;   - VDP-style wrap (window cols 4..5, ht_wrap=6): the third '!' snaps
;     to column 4 of the NEXT text row (scanline 8 -> $2084);
;   - HGR bit order (ht_rev=0): $C3 stored as $43 ($2085) — the colour
;     attributes strip glyph bit 7 even in WHITE (the palette bit is
;     ht_cbit's job now, never the font's);
;   - hgr_putc8 PRESERVES A, X and Y (ported print loops keep counters
;     in all three): A='!' ($21), X=$11, Y=$22 read back intact.
;
; hgr_blit2 / hgr_blit4:
;   - STORE mode: 2x2 source $11 $22 / $33 $44 at col 10, scanline 32
;     lands at $220A/$220B (y=32) and $260A/$260B (y=33);
;   - FLASH mode (EOR #$7F then OR) re-blitted over row 0 only: both
;     bytes saturate to $7F, row 1 untouched ($33 $44);
;   - hgr_blit4 STORE: 4x1 source $01..$04 at col 20, scanline 40
;     lands at $2294..$2297;
;   - hgr_blit2 PALFLIP (mode 3, EOR #$80 then OR) of the same 2x1 row
;     onto a clean area at col 24, scanline 48: $91 $A2 at $2318/$2319.
;
; POM1-LIB-MICRO-TEST
; LIBS:
; CFG: micro.cfg
; PRESET: 11
; LOAD: 0300
; RUN: 0300
; STEPS: 30000
; EXPECT: 0F00 A5
; EXPECT: 0F01 43 43 43 43 43
; EXPECT: 0F06 21 11 22
; EXPECT: 0F09 7F 7F 33 44
; EXPECT: 0F0D 01 02 03 04
; EXPECT: 0F11 91 A2
; ============================================================================

MB = $0F00

.segment "CODE"

start:
        ; --- hgr_text8 setup: local font, TMS order, window cols 4..5 --
        LDA #<font
        STA ht_font_lo
        LDA #>font
        STA ht_font_hi
        LDA #1
        STA ht_rev
        LDA #4
        STA ht_left
        LDA #6
        STA ht_wrap
        LDA #$7F                ; colour attrs: WHITE pass-through
        STA ht_cm_ev
        STA ht_cm_od
        LDA #0
        STA ht_cbit
        STA ht_page             ; page 1
        STA bl_page
        LDA #4
        STA ht_col
        LDA #0
        STA ht_sl

        LDA #'!'                ; col 4, scanlines 0..7
        JSR hgr_putc8
        LDA #'!'                ; col 5
        JSR hgr_putc8
        LDA #'!'                ; wraps -> col 4, scanlines 8..15
        JSR hgr_putc8
        LDA #0
        STA ht_rev              ; HGR bit order: raw bytes
        LDA #'!'                ; col 5, scanline 8
        JSR hgr_putc8
        ; register preservation (draws at col 4, scanline 16 — unprobed)
        LDX #$11
        LDY #$22
        LDA #'!'
        JSR hgr_putc8
        STA MB+6
        STX MB+7
        STY MB+8

        ; --- hgr_blit2 STORE: 2x2 at col 10, scanline 32 ----------------
        LDA #<src2
        STA bl_src
        LDA #>src2
        STA bl_src+1
        LDA #10
        STA bl_col
        LDA #32
        STA bl_sl
        LDA #2
        STA bl_h
        LDA #2                  ; STORE
        STA bl_mode
        JSR hgr_blit2
        ; --- hgr_blit2 FLASH over row 0 only ----------------------------
        LDA #1
        STA bl_h
        LDA #1                  ; FLASH
        STA bl_mode
        JSR hgr_blit2
        ; --- hgr_blit4 STORE: 4x1 at col 20, scanline 40 ----------------
        LDA #<src4
        STA bl_src
        LDA #>src4
        STA bl_src+1
        LDA #20
        STA bl_col
        LDA #40
        STA bl_sl
        LDA #1
        STA bl_h
        LDA #2                  ; STORE
        STA bl_mode
        JSR hgr_blit4
        ; --- hgr_blit2 PALFLIP: 2x1 at col 24, scanline 48 --------------
        LDA #<src2
        STA bl_src
        LDA #>src2
        STA bl_src+1
        LDA #24
        STA bl_col
        LDA #48
        STA bl_sl
        LDA #1
        STA bl_h
        LDA #3                  ; PALFLIP
        STA bl_mode
        JSR hgr_blit2

        ; --- Mailbox: framebuffer probes --------------------------------
        LDA $2004               ; '!' TMS order, row 0
        STA MB+1
        LDA $2404               ; row 1 of the same glyph
        STA MB+2
        LDA $2005               ; second '!' (cursor advance)
        STA MB+3
        LDA $2084               ; wrapped '!' (scanline 8)
        STA MB+4
        LDA $2085               ; HGR-order raw byte
        STA MB+5
        LDA $220A               ; blit2 row 0 after FLASH
        STA MB+9
        LDA $220B
        STA MB+10
        LDA $260A               ; blit2 row 1 (STORE only)
        STA MB+11
        LDA $260B
        STA MB+12
        LDA $2294               ; blit4 row
        STA MB+13
        LDA $2295
        STA MB+14
        LDA $2296
        STA MB+15
        LDA $2297
        STA MB+16
        LDA $2318               ; blit2 PALFLIP row
        STA MB+17
        LDA $2319
        STA MB+18

        LDA #$A5                ; magic written LAST
        STA MB
spin:   JMP spin

; --- Local 2-glyph font: ' ' (blank) + '!' (8 rows of $C3) -------------
font:
.repeat 8
        .byte $00
.endrepeat
.repeat 8
        .byte $C3
.endrepeat

; --- Blit sources -------------------------------------------------------
src2:   .byte $11, $22
        .byte $33, $44
src4:   .byte $01, $02, $03, $04

; --- Libs under test (textual includes) ---------------------------------
.include "hgr_scanline.inc"
.include "hgr_text8.asm"
.include "hgr_blit2.asm"
