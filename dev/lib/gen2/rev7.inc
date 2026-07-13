; ============================================================================
; rev7.inc -- rev7_tab: TMS bit order -> HGR bit order (256-byte LUT)
; ----------------------------------------------------------------------------
; rev7_tab[b] reverses the bit order of b and clears bit 7: input bit 7
; (the TMS9918 LEFTMOST pixel) lands on output bit 0 (the HGR leftmost
; pixel), input bit 0 (the TMS rightmost pixel) is DROPPED -- a 7-px HGR
; byte has no slot for it.
;
; Because of that dropped pixel, feeding raw 8-px source bytes through
; this table is only correct for 8-px-GRID content (text glyphs, byte
; masks) where losing the rightmost column is acceptable. Sprite art
; must instead go through the bit-stream repack of hgr_sprite16.asm,
; which always presents 7 fresh stream bits + a zero bit 0 to this
; table (lossless).
;
; Shared by hgr_sprite16.asm and hgr_text8.asm; include-guarded so a
; project may pull both.
; ============================================================================

.ifndef _HGR_REV7_LOADED_
_HGR_REV7_LOADED_ = 1

rev7_tab:
.repeat 256, i
        .byte ((i >> 7) & 1) | (((i >> 6) & 1) << 1) | (((i >> 5) & 1) << 2) | (((i >> 4) & 1) << 3) | (((i >> 3) & 1) << 4) | (((i >> 2) & 1) << 5) | (((i >> 1) & 1) << 6)
.endrepeat

.endif  ; _HGR_REV7_LOADED_
