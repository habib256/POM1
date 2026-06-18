; ============================================================================
; text_bitmap.asm  --  8x8 monochrome glyph blitter for TMS9918 Mode-2
; ----------------------------------------------------------------------------
; Renders an Apple-1 charmap glyph into the bitmap pattern table at the
; cell aligned to (pix_x, pix_y), and tints the matching colour cell with
; pen_color. Lifted from TMS_Logo_16k.asm so any Mode-2 project that wants
; on-bitmap text overlays (scores, "GAME OVER", tutorial captions, narrator
; bubbles) can .import the routine instead of re-implementing it.
;
; The whole module is gated by CODETANK_BUILD because the 1 024-byte
; charmap.rom .incbin doesn't fit any 8 KB or 16 KB DRAM CODE slot --
; only the cartridge ROM has the headroom. Callers building for a DRAM
; target should not pass `-D CODETANK_BUILD`; the .o file then assembles
; empty and adds no bytes.
;
; ----------------------------------------------------------------------------
; API:
;   text_blit_glyph        A = ASCII char (bit 7 ignored). Reads pix_x/pix_y as
;                     the cell-aligned pixel coordinate of the glyph's
;                     top-left corner; the routine masks low 3 bits to
;                     guarantee cell alignment, so callers can pass
;                     unaligned values. Tints the cell with pen_color.
;                     Clobbers A, X, Y, mptr_lo/hi, tmp, tmp2,
;                     pix_addr_lo/hi.
;
; Caller-provided ZP (must be .exportzp'd by the caller):
;   tmp, tmp2         scratch (1 byte each)
;   mptr_lo, mptr_hi  16-bit cursor into charmap_table (1 byte each)
;
; Lib-provided ZP (already exported by tms9918m2.asm):
;   pix_x, pix_y                 input position (cell-aligned on entry/exit)
;   pix_addr_lo/hi               clobbered, holds VRAM cursor
;   pen_color                    foreground colour for the colour cell
; ============================================================================

        .import tms9918_pad12  ; silicon-strict pad12-v3 (helper from tms9918_pad.asm)
.ifdef CODETANK_BUILD

.include "tms9918.inc"          ; VDP_DATA / VDP_CTRL

.export   text_blit_glyph

.import   calc_pix_addr, vdp_set_write
.importzp pix_x, pix_y, pix_addr_lo, pix_addr_hi, pen_color
.importzp tmp, tmp2, mptr_lo, mptr_hi

.segment "CODE"

; ----------------------------------------------------------------------------
; text_blit_glyph: paint one 8x8 glyph + its colour cell. See module header for
;   the full register / ZP contract.
; ----------------------------------------------------------------------------
text_blit_glyph:
        AND #$7F                  ; clamp to printable range
        STA tmp                   ; tmp:tmp2 = A * 8 (16-bit shift)
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
        ADC #<charmap_table
        STA mptr_lo
        LDA tmp2
        ADC #>charmap_table
        STA mptr_hi
        ; force cell-aligned write addr (low 3 bits of pix_y = 0)
        LDA pix_x
        AND #$F8
        STA pix_x
        LDA pix_y
        AND #$F8
        STA pix_y
        JSR calc_pix_addr
        JSR vdp_set_write
        LDY #0
@row:   LDA (mptr_lo),Y
        ; Reverse byte: charmap.rom is encoded LSB-toward-left (the
        ; Apple-1 character display reads bits 1..5 as columns 0..4).
        ; TMS9918 bitmap mode reads bit 7 = leftmost pixel, so we need
        ; bit-reverse before writing or every glyph appears mirrored.
        ; Pattern: LSR src (bit 0 -> C), ROL dst (C -> bit 0, shift dst
        ; left). After 8 iters: dst[i] = src[7-i].
        STA tmp
        LDA #0
        LDX #8
@bitrev:
        LSR tmp
        ROL
        DEX
        BNE @bitrev
        STA VDP_DATA
        INY
        CPY #8
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @row
        ; --- paint the cell's 8 colour-table bytes with pen_color so callers
        ;     also tint LABEL/SAY text. The colour table mirrors the
        ;     pattern table layout at base $2000, so we can re-prime the
        ;     VDP cursor with the same pix_addr but the $2000 base bit set.
        LDA pix_addr_lo
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA pix_addr_hi
        ORA #$60                  ; $20 (colour base) | $40 (write enable)
        STA VDP_CTRL
        JSR     tms9918_pad12   ; MANUAL: cmd-byte → first @col STA. Natural
                                ; bridge LDA+ASL×4+ORA+LDX = 15c gives gap=19c
                                ; (LOGO LABEL/SAY drop site, May 2026).
        LDA pen_color
        ASL
        ASL
        ASL
        ASL                       ; pen_color in high nibble
        ORA #$01                  ; transparent background ($x1)
        LDX #8
@col:   STA VDP_DATA
        JSR tms9918_pad12       ; silicon-strict 12c (loop-back inner @col)
        DEX
        BNE @col
        RTS

; ============================================================================
; charmap_table: 1024-byte 8x8 monochrome ASCII font, lifted verbatim
;   from roms/charmap.rom. Format: 1 byte per row, LSB = leftmost pixel
;   (Apple-1 native order; the upload bit-reverses on the fly because the
;   TMS9918 reads bit 7 = leftmost), 8 rows per glyph, 128 glyphs (codes
;   0..127). Local label (not
;   exported) -- callers blit through the API, not through direct font
;   reads.
; ============================================================================
charmap_table:
        .incbin "../../../roms/charmap.rom"

.endif  ; CODETANK_BUILD
