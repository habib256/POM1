; basicrt_gen2.s -- native BASIC runtime (rt_* ABI) for the GEN2 HGR card.
;
; Implements the fixed ABI that BasicNativeCompiler's generated code calls:
;   rt_hgr / rt_hcolor / rt_plot / rt_line / rt_mul / rt_div / rt_cmp16
; plus the zero-page scratch (rt_px/rt_py/rt_x0..rt_y1/rt_a/rt_b). Wraps the
; project's GEN2 graphics asm (gen2_hgr_init, clear_hgr, plot_pixel) -- those are
; leaf graphics routines, not the Applesoft interpreter. The compiled program +
; this runtime link into a standalone binary that needs only the GEN2 card.
;
; Build: ca65 -I dev/lib/gen2 -I dev/lib/apple1  (see basicc_native.cfg).
; NOTE: plot_pixel takes an 8-bit X (cur_x), so this phase draws X in 0..255
; (the integer demos stay in range). A 16-bit-X plot is a later refinement.

.setcpu "6502"

; ---- zero page: ABI scratch (exported) + lib + math/line scratch -----------
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

; GEN2 plot lib ZP (referenced by plot_pixel / clear_hgr)
cur_x:  .res 1
cur_y:  .res 1
ptr_lo: .res 1
ptr_hi: .res 1

; math + line scratch
m_prod: .res 2
m_rem:  .res 2
m_sign: .res 1
ln_dx:  .res 1
ln_dy:  .res 1
ln_sx:  .res 1
ln_sy:  .res 1
ln_err: .res 2
ln_e2:  .res 2
ln_tmp: .res 2
pen:    .res 1

; ---- code ------------------------------------------------------------------
.segment "CODE"

.export rt_hgr, rt_hcolor, rt_plot, rt_line, rt_mul, rt_div, rt_cmp16

; rt_hgr: A = page (ignored; plot_pixel always targets page 1 $2000). Init + clear.
rt_hgr:
        jsr gen2_hgr_init
        jsr clear_hgr
        rts

; rt_hcolor: A = colour (GEN2 HGR plot is additive; kept for parity / future).
rt_hcolor:
        sta pen
        rts

; rt_plot: plot (rt_px, rt_py) -- low bytes (X 0..255, Y 0..191).
rt_plot:
        lda rt_px
        sta cur_x
        lda rt_py
        sta cur_y
        jmp plot_pixel

; rt_line: Bresenham (rt_x0,rt_y0)->(rt_x1,rt_y1) via plot_pixel (8-bit coords).
rt_line:
        lda rt_x0
        sta cur_x
        lda rt_y0
        sta cur_y
        ; dx = |x1-x0|, sx
        sec
        lda rt_x1
        sbc rt_x0
        bcs @dxp
        eor #$FF
        clc
        adc #1
        ldy #$FF
        sty ln_sx
        jmp @dxs
@dxp:   ldy #$01
        sty ln_sx
@dxs:   sta ln_dx
        ; dy = |y1-y0|, sy
        sec
        lda rt_y1
        sbc rt_y0
        bcs @dyp
        eor #$FF
        clc
        adc #1
        ldy #$FF
        sty ln_sy
        jmp @dys
@dyp:   ldy #$01
        sty ln_sy
@dys:   sta ln_dy
        ; err = dx - dy (sign-extended to 16 bits)
        sec
        lda ln_dx
        sbc ln_dy
        sta ln_err
        lda #0
        sbc #0
        sta ln_err+1
@loop:
        jsr plot_pixel
        lda cur_x
        cmp rt_x1
        bne @cont
        lda cur_y
        cmp rt_y1
        bne @cont
        rts
@cont:
        ; e2 = err * 2
        lda ln_err
        asl
        sta ln_e2
        lda ln_err+1
        rol
        sta ln_e2+1
        ; if e2 > -dy  (i.e. e2 + dy > 0): err -= dy; cur_x += sx
        clc
        lda ln_e2
        adc ln_dy
        sta ln_tmp
        lda ln_e2+1
        adc #0
        sta ln_tmp+1
        lda ln_tmp+1
        bmi @skipx
        lda ln_tmp
        ora ln_tmp+1
        beq @skipx
        sec
        lda ln_err
        sbc ln_dy
        sta ln_err
        lda ln_err+1
        sbc #0
        sta ln_err+1
        lda cur_x
        clc
        adc ln_sx
        sta cur_x
@skipx:
        ; if e2 < dx  (i.e. dx - e2 > 0): err += dx; cur_y += sy
        sec
        lda ln_dx
        sbc ln_e2
        sta ln_tmp
        lda #0
        sbc ln_e2+1
        sta ln_tmp+1
        lda ln_tmp+1
        bmi @skipy
        lda ln_tmp
        ora ln_tmp+1
        beq @skipy
        clc
        lda ln_err
        adc ln_dx
        sta ln_err
        lda ln_err+1
        adc #0
        sta ln_err+1
        lda cur_y
        clc
        adc ln_sy
        sta cur_y
@skipy:
        jmp @loop

; ---- shared 16-bit integer math (rt_mul / rt_div / rt_cmp16) ----------------
.include "basicrt_math.inc"

; ---- GEN2 graphics leaf routines (project libs) ----------------------------
.include "gen2_init.asm"
.include "hgr_tables.inc"
