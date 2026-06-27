; ============================================================================
; gen2_text_bitmap.asm -- HGR 8x8 glyph blitter for the GEN2 LOGO "full" build.
; ----------------------------------------------------------------------------
; Drop-in replacement for dev/lib/tms9918/text_bitmap.asm: same public symbol
; (text_blit_glyph) and the same (A, pix_x, pix_y, pen_color) contract, but it
; paints into the GEN2 HGR framebuffer via plot_set + the Beautiful Boot 8x8
; font (bbfont) instead of the TMS9918 pattern/colour tables. Used by LOGO's
; on-bitmap text (HELP / LABEL / SAY / LIST / the buffer editor).
;
; Gated by CODETANK_BUILD, exactly like the TMS module, so the same "full"
; feature flag enables it. Link THIS instead of text_bitmap.asm in a GEN2 build.
;
; API (identical to the TMS version):
;   text_blit_glyph   A = ASCII char (bit 7 ignored). pix_x/pix_y = pixel
;                     top-left of the glyph. Draws an 8x8 glyph in OR mode at
;                     the current pen colour (via plot_set). Clobbers A,X,Y,
;                     mptr_lo/hi, tmp, tmp2, pix_x, pix_y, and the plotter ZP.
;
; bbfont encoding: 8 bytes/glyph, row 0 = top, bit 0 = leftmost pixel -- so a
; LSR walks columns left-to-right. (Same master font the GEN2 demos use.)
; ============================================================================

.ifdef CODETANK_BUILD

.export text_blit_glyph

.import   plot_set
.import   plot_mode
.importzp pix_x, pix_y
.importzp tmp, tmp2, mptr_lo, mptr_hi

.segment "ZEROPAGE"
tb_x0:   .res 1          ; glyph top-left X (preserved across plot_set)
tb_y0:   .res 1          ; glyph top-left Y
tb_row:  .res 1          ; current row 0..7
tb_col:  .res 1          ; current column 0..7
tb_bits: .res 1          ; remaining bits of the current row (LSB = next col)

.segment "CODE"

text_blit_glyph:
        AND #$7F
        ; mptr = HGR_BBFont + A*8  (16-bit)
        STA tmp
        LDA #0
        STA tmp2
        ASL tmp
        ROL tmp2
        ASL tmp
        ROL tmp2
        ASL tmp
        ROL tmp2
        CLC
        LDA tmp
        ADC #<HGR_BBFont
        STA mptr_lo
        LDA tmp2
        ADC #>HGR_BBFont
        STA mptr_hi
        ; latch top-left + force OR draw (text never erases)
        LDA pix_x
        STA tb_x0
        LDA pix_y
        STA tb_y0
        LDA #0
        STA plot_mode
        STA tb_row
@row:
        ; load this row's glyph byte
        LDY tb_row
        LDA (mptr_lo),Y
        STA tb_bits
        ; pix_y = y0 + row
        LDA tb_y0
        CLC
        ADC tb_row
        STA pix_y
        LDA #0
        STA tb_col
@col:
        LSR tb_bits             ; next column's bit -> C (bit0 = leftmost)
        BCC @next
        ; pix_x = x0 + col, then plot (plot_set clobbers A/X/Y -- all loop
        ; state lives in ZP so that is fine)
        LDA tb_x0
        CLC
        ADC tb_col
        STA pix_x
        JSR plot_set
@next:
        INC tb_col
        LDA tb_col
        CMP #8
        BNE @col
        INC tb_row
        LDA tb_row
        CMP #8
        BNE @row
        RTS

; the Beautiful Boot 8x8 font (HGR_BBFont, 256 glyphs x 8 B). bit 0 = left.
        .include "bbfont_cp437.inc"

.endif  ; CODETANK_BUILD
