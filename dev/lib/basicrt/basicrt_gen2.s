; basicrt_gen2.s -- native BASIC runtime (rt_* ABI) for the GEN2 HGR card.
;
; Implements the ABI BasicNativeCompiler's generated code calls:
;   rt_hgr / rt_hcolor / rt_plot / rt_line / rt_mul / rt_div / rt_cmp16
; plus zero-page ABI scratch (rt_px/rt_py/rt_x0..rt_y1/rt_a/rt_b). The compiled
; program + this runtime link into a standalone binary that needs only the GEN2
; card -- no Applesoft interpreter.
;
; Plot/line use a FULL 16-bit X (0..279), the real GEN2 hi-res width (the project
; plot_pixel lib is 8-bit X / 256-wide), via local 280-entry column/mask tables.
; Scanline base tables (hgr_lo/hgr_hi, Y-indexed) are reused from the lib.
;
; Build: ca65 -I dev/lib/gen2 -I dev/lib/apple1 -I dev/lib/basicrt.

.setcpu "6502"

; ---- zero page -------------------------------------------------------------
.segment "ZEROPAGE"
rt_px:  .res 2          ; current/plot X (0..279)
rt_py:  .res 2          ; current/plot Y (0..191, low byte)
rt_x0:  .res 2
rt_y0:  .res 2
rt_x1:  .res 2
rt_y1:  .res 2
rt_a:   .res 2
rt_b:   .res 2
.exportzp rt_px, rt_py, rt_x0, rt_y0, rt_x1, rt_y1, rt_a, rt_b

ptr_lo: .res 1          ; framebuffer pointer (plot)
ptr_hi: .res 1
tptr:   .res 2          ; table pointer (col/mask lookup with 16-bit X)

m_prod: .res 2          ; shared math scratch (basicrt_math.inc)
m_rem:  .res 2
m_sign: .res 1

ln_dx:  .res 2          ; 16-bit Bresenham state
ln_dy:  .res 2
ln_sx:  .res 2
ln_sy:  .res 2
ln_err: .res 2
ln_e2:  .res 2
ln_tmp: .res 2
pen:    .res 1

; ---- code ------------------------------------------------------------------
; Each routine is gated on a feature flag (-D RT_xxx) so the build assembles ONLY
; the runtime the compiled program imports -- unused routines and the 560-byte
; pixel tables never reach the binary. The compiler emits minimal imports and the
; driver derives the matching -D flags.
.segment "CODE"

.ifdef RT_HGR
.export rt_hgr
; rt_hgr: A = page (ignored; plot targets page 1 $2000). Init + clear.
rt_hgr:
        jsr gen2_hgr_init
        jsr clear_hgr
        rts
.endif

.ifdef RT_HCOLOR
.export rt_hcolor
; rt_hcolor: A = colour (GEN2 HGR plot is additive; kept for parity / future).
rt_hcolor:
        sta pen
        rts
.endif

.if .defined(RT_PLOT) .or .defined(RT_LINE)
.export rt_plot
; rt_plot: set pixel at (rt_px 0..279, rt_py 0..191). 16-bit X via col/mask tables.
rt_plot:
        lda rt_py+1             ; clip Y to 0..191 (high byte must be 0, Y<192)
        bne @done
        lda rt_py
        cmp #192
        bcs @done
        lda rt_px+1             ; clip X to 0..279
        beq @xok                ; hi==0 -> 0..255, valid
        cmp #1
        bne @done               ; hi>1 -> X>=512, clip
        lda rt_px               ; hi==1 -> valid iff low < 24 (X < 280)
        cmp #<280
        bcs @done
@xok:
        ldx rt_py
        lda hgr_lo,x
        sta ptr_lo
        lda hgr_hi,x
        sta ptr_hi
        clc                     ; tptr = col280 + X ; ptr += col
        lda #<col280
        adc rt_px
        sta tptr
        lda #>col280
        adc rt_px+1
        sta tptr+1
        ldy #0
        lda (tptr),y
        clc
        adc ptr_lo
        sta ptr_lo
        bcc @nc
        inc ptr_hi
@nc:    clc                     ; tptr = mask280 + X ; OR mask
        lda #<mask280
        adc rt_px
        sta tptr
        lda #>mask280
        adc rt_px+1
        sta tptr+1
        lda (tptr),y
        ora (ptr_lo),y
        sta (ptr_lo),y
@done:  rts
.endif  ; RT_PLOT/RT_LINE

.ifdef RT_LINE
.export rt_line
; rt_line: 16-bit Bresenham (rt_x0,rt_y0)->(rt_x1,rt_y1), running point in rt_px/py.
rt_line:
        lda rt_x0
        sta rt_px
        lda rt_x0+1
        sta rt_px+1
        lda rt_y0
        sta rt_py
        lda rt_y0+1
        sta rt_py+1
        ; dx = |x1-x0|, sx
        sec
        lda rt_x1
        sbc rt_x0
        sta ln_dx
        lda rt_x1+1
        sbc rt_x0+1
        sta ln_dx+1
        bpl @dxp
        sec
        lda #0
        sbc ln_dx
        sta ln_dx
        lda #0
        sbc ln_dx+1
        sta ln_dx+1
        lda #$FF
        sta ln_sx
        sta ln_sx+1
        jmp @dy
@dxp:   lda #1
        sta ln_sx
        lda #0
        sta ln_sx+1
@dy:    sec
        lda rt_y1
        sbc rt_y0
        sta ln_dy
        lda rt_y1+1
        sbc rt_y0+1
        sta ln_dy+1
        bpl @dyp
        sec
        lda #0
        sbc ln_dy
        sta ln_dy
        lda #0
        sbc ln_dy+1
        sta ln_dy+1
        lda #$FF
        sta ln_sy
        sta ln_sy+1
        jmp @ie
@dyp:   lda #1
        sta ln_sy
        lda #0
        sta ln_sy+1
@ie:    sec                     ; err = dx - dy
        lda ln_dx
        sbc ln_dy
        sta ln_err
        lda ln_dx+1
        sbc ln_dy+1
        sta ln_err+1
@loop:
        jsr rt_plot
        lda rt_px               ; reached end point?
        cmp rt_x1
        bne @cont
        lda rt_px+1
        cmp rt_x1+1
        bne @cont
        lda rt_py
        cmp rt_y1
        bne @cont
        lda rt_py+1
        cmp rt_y1+1
        bne @cont
        rts
@cont:
        lda ln_err              ; e2 = err * 2
        asl
        sta ln_e2
        lda ln_err+1
        rol
        sta ln_e2+1
        ; if e2 > -dy  (e2 + dy > 0): err -= dy; x += sx
        clc
        lda ln_e2
        adc ln_dy
        sta ln_tmp
        lda ln_e2+1
        adc ln_dy+1
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
        sbc ln_dy+1
        sta ln_err+1
        clc
        lda rt_px
        adc ln_sx
        sta rt_px
        lda rt_px+1
        adc ln_sx+1
        sta rt_px+1
@skipx:
        ; if e2 < dx  (dx - e2 > 0): err += dx; y += sy
        sec
        lda ln_dx
        sbc ln_e2
        sta ln_tmp
        lda ln_dx+1
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
        adc ln_dx+1
        sta ln_err+1
        clc
        lda rt_py
        adc ln_sy
        sta rt_py
        lda rt_py+1
        adc ln_sy+1
        sta rt_py+1
@skipy:
        jmp @loop
.endif  ; RT_LINE

; ---- shared 16-bit integer math (rt_mul / rt_div / rt_cmp16) gated inside -----
.include "basicrt_math.inc"

; ---- 280-wide column / mask tables (only when a pixel routine is built) ------
; col280[X] = X/7 (byte within the scanline); mask280[X] = 1 << (X mod 7).
.if .defined(RT_PLOT) .or .defined(RT_LINE)
.segment "RODATA"
col280:
.repeat 280, I
        .byte I / 7
.endrepeat
mask280:
.repeat 280, I
        .byte 1 << (I .mod 7)
.endrepeat
.endif

; ---- GEN2 graphics leaf routines (project libs) -- only what's referenced ----
.ifdef RT_HGR
.include "gen2_init.asm"
.include "hgr_clear.asm"
.endif
.if .defined(RT_PLOT) .or .defined(RT_LINE)
.include "hgr_scanline.inc"
.endif
