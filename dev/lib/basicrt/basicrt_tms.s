; basicrt_tms.s -- native BASIC runtime (rt_* ABI) for the TMS9918 card.
;
; Same ABI as basicrt_gen2.s, but graphics go to the TMS9918 VDP. Wraps the
; project's TMS9918 Mode-II asm (init_vdp_g2 / disable_sprites / clear_bitmap /
; plot_set / line_xy from tms9918m2.asm; tms9918_pad12 from tms9918_pad.asm) --
; leaf graphics routines, not the interpreter. Link those objects alongside.
;
; Build: ca65 -I dev/lib/tms9918 -I dev/lib/apple1 ; link with tms9918m2.o +
; tms9918_pad.o (see tools/basicc_native.sh).

.setcpu "6502"

; ---- zero page -------------------------------------------------------------
.segment "ZEROPAGE"
rt_px:  .res 2
rt_py:  .res 2
rt_x0:  .res 2
rt_y0:  .res 2
rt_x1:  .res 2
rt_y1:  .res 2
rt_a:   .res 2
rt_b:   .res 2
.exportzp rt_px, rt_py, rt_x0, rt_y0, rt_x1, rt_y1, rt_a, rt_b

m_prod: .res 2
m_rem:  .res 2
m_sign: .res 1
tmp:    .res 1          ; tms9918m2 scratch (it .importzp's these)
tmp2:   .res 1
.exportzp tmp, tmp2

; All graphics (and the VDP lib it imports) are gated on the program actually
; using a graphics statement, so a non-graphics TMS program drops them entirely.
; The driver also omits tms9918m2.o / tms9918_pad.o from the link when no graphics.
RT_GFX = .defined(RT_HGR) .or .defined(RT_PLOT) .or .defined(RT_LINE) .or .defined(RT_HCOLOR)

.if RT_GFX
; TMS9918 lib ABI (defined in tms9918m2.asm)
.import init_vdp_g2: absolute, disable_sprites: absolute, clear_bitmap: absolute
.import plot_set: absolute, line_xy: absolute
.importzp pix_x, pix_y, pen_color, ln_x0, ln_y0, ln_x1, ln_y1

.segment "BSS"
plot_mode: .res 1       ; 0 = OR (tms9918m2 .import's it); zeroed by the program prologue
.export plot_mode

.segment "CODE"

.ifdef RT_HGR
.export rt_hgr
; rt_hgr: A = page (ignored). Init Mode II, arm sprite chain, clear bitmap.
rt_hgr:
        jsr init_vdp_g2
        jsr disable_sprites
        jsr clear_bitmap
        rts
.endif

.ifdef RT_HCOLOR
.export rt_hcolor
; rt_hcolor: A = pen colour (0..15).
rt_hcolor:
        sta pen_color
        rts
.endif

.ifdef RT_PLOT
.export rt_plot
; rt_plot: plot (rt_px, rt_py) low bytes (X 0..255, Y 0..191).
rt_plot:
        lda rt_px
        sta pix_x
        lda rt_py
        sta pix_y
        lda #0
        sta plot_mode
        jmp plot_set
.endif

.ifdef RT_LINE
.export rt_line
; rt_line: line (rt_x0,rt_y0)->(rt_x1,rt_y1) via the TMS Bresenham line_xy.
rt_line:
        lda rt_x0
        sta ln_x0
        lda rt_y0
        sta ln_y0
        lda rt_x1
        sta ln_x1
        lda rt_y1
        sta ln_y1
        lda #0
        sta plot_mode
        jmp line_xy
.endif
.endif  ; RT_GFX

; ---- shared 16-bit integer math (rt_mul / rt_div / rt_cmp16) ----------------
.include "basicrt_math.inc"
