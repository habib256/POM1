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

; ONERR handler address (low/high). 0 = no handler armed. The compiler's program
; prologue clears these; ONERR GOTO n stores L<n>. Range-checking runtime routines
; that detect an Applesoft "illegal quantity" jmp through it when armed. Always
; exported (2 ZP bytes); the program .importzp's them only when it uses ONERR, and
; ld65 leaves them unreferenced otherwise.
rt_onerr_lo: .res 1
rt_onerr_hi: .res 1
.exportzp rt_onerr_lo, rt_onerr_hi

ptr_lo: .res 1          ; framebuffer pointer (plot)
ptr_hi: .res 1
tptr:   .res 2          ; table pointer (col/mask lookup with 16-bit X)

lrcolor: .res 1         ; lo-res colour, duplicated into both nibbles
lrtmp:   .res 1         ; lo-res plot scratch (byte being modified)
lrvy:    .res 1         ; VLIN running Y
lrvy2:   .res 1         ; VLIN endpoint Y

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
.exportzp pen           ; compiler prologue seeds pen when HPLOT may run before HGR

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
        lda #3                  ; default pen non-zero so HPLOT before HCOLOR draws
        sta pen
        rts
.endif

.ifdef RT_HCOLOR
.export rt_hcolor
; rt_hcolor: A = colour (GEN2 HGR plot is additive; kept for parity / future).
rt_hcolor:
        sta pen
        rts
.endif

; ---- lo-res graphics (standard Apple II layout) -----------------------------
; 40 wide x 48 high; each cell a 4-bit colour nibble (even row = low nibble, odd
; row = high nibble; two rows share one byte). Routines mirror sketchs/gen2/
; applesoft_gen2/gen2gfx.inc (GFX_GR / GFX_COLOR / lores_plot / HLIN / VLIN /
; TEXT / HOME) but ALWAYS target lo-res PAGE 2 ($0800-$0BFF), never page 1.
;
; WHY PAGE 2: the GEN2 video scanner can only display lo-res from page 1 ($0400) or
; page 2 ($0800), and the runtime always uses page 2. To keep the framebuffer clear
; of the program image, a GEN2 lo-res build loads at $0C00 (basicc_native_gen2_lores.cfg)
; instead of $0300 -- above both lo-res pages -- so GR/PLOT painting $0800-$0BFF never
; overwrites the running code, and a lo-res program up to ~5 KB ($0C00-$1FFF) runs
; intact. HGR (framebuffer at $2000) keeps the full $0300-$1FFF window. The TMS9918
; keeps pixels in VRAM, so it stays at $0300. (The page-2 choice + $0C00 base are set
; by the build/deploy; the runtime below is load-address agnostic.)
.if .defined(RT_GR) .or .defined(RT_COLOR) .or .defined(RT_LORESPLOT) .or .defined(RT_HLIN) .or .defined(RT_VLIN) .or .defined(RT_TEXT) .or .defined(RT_HOME)
LR_NEEDED = 1
.include "gen2.inc"             ; GEN2_TEXTOFF..GEN2_LORES soft switches
.endif

.ifdef RT_GR
.export rt_gr
; rt_gr: A = page (ignored; lo-res always targets page 2 $0800). Switch to lo-res,
; reset colour to 0, clear the $0800-$0BFF page.
rt_gr:
        lda GEN2_TEXTOFF
        lda GEN2_LORES
        lda GEN2_PAGE2
        lda GEN2_MIXOFF
        lda #0
        sta lrcolor
        ; fall into lores_clear
lores_clear:                    ; zero $0800-$0BFF (1 KB lo-res page 2)
        lda #$00
        sta ptr_lo
        lda #$08
        sta ptr_hi
        ldx #$04                ; 4 pages
        ldy #0
        lda #0
@l:     sta (ptr_lo),y
        iny
        bne @l
        inc ptr_hi
        dex
        bne @l
        rts
.endif  ; RT_GR

.ifdef RT_COLOR
.export rt_color
; rt_color: A = COLOR= value (0..15; Applesoft masks &15). Duplicate the low nibble
; into both nibbles so a single store paints either row parity.
rt_color:
        and #$0F
        sta lrtmp
        asl a
        asl a
        asl a
        asl a
        ora lrtmp
        sta lrcolor
        rts
.endif  ; RT_COLOR

; rt_loresplot: plot (rt_x0, rt_y0) low bytes in lrcolor. Guards x<40, y<48 by
; skipping (matches the ROM: out-of-range lo-res plots are silently ignored, so
; ONERR is never tripped here -- the GEN2 Applesoft ROM behaves identically).
.if .defined(RT_LORESPLOT) .or .defined(RT_HLIN) .or .defined(RT_VLIN)
.export rt_loresplot
rt_loresplot:
        lda rt_x0+1             ; x must be 0..39 (hi byte 0)
        bne @skip
        lda rt_x0
        cmp #40
        bcs @skip
        lda rt_y0+1             ; y must be 0..47 (hi byte 0)
        bne @skip
        lda rt_y0
        cmp #48
        bcs @skip
        lsr a                   ; row = y/2 (0..23) -> $0800 page row base
        tax
        lda lores_lo,x
        sta ptr_lo
        lda lores_hi,x
        sta ptr_hi
        ldy rt_x0               ; column 0..39
        lda (ptr_lo),y
        sta lrtmp
        lda rt_y0
        and #$01
        bne @odd
        lda lrtmp               ; even y -> low nibble
        and #$F0
        sta lrtmp
        lda lrcolor
        and #$0F
        ora lrtmp
        jmp @wr
@odd:   lda lrtmp               ; odd y -> high nibble
        and #$0F
        sta lrtmp
        lda lrcolor
        and #$F0
        ora lrtmp
@wr:    ldy rt_x0
        sta (ptr_lo),y
@skip:  rts
.endif  ; LORES_PLOT/HLIN/VLIN

.ifdef RT_HLIN
.export rt_hlin
; rt_hlin: HLIN x1,x2 AT y -- x1=rt_x0, x2=rt_x1, y=rt_y0 (low bytes).
rt_hlin:
@loop:  jsr rt_loresplot
        lda rt_x0
        cmp rt_x1
        beq @done
        bcc @inc
        dec rt_x0
        jmp @loop
@inc:   inc rt_x0
        jmp @loop
@done:  rts
.endif  ; RT_HLIN

.ifdef RT_VLIN
.export rt_vlin
; rt_vlin: VLIN y1,y2 AT x -- the compiler emits y1=rt_x0, y2=rt_x1, x=rt_y0.
; Stage the fixed X once into rt_x0 (what rt_loresplot reads as X) and walk the
; running Y in rt_y0 (what it reads as Y) from y1..y2. lrtmp2 holds the loop state
; so rt_loresplot's use of rt_x0/rt_y0 is fine each pass.
rt_vlin:
        lda rt_x0               ; vy = y1 (running Y)
        sta lrvy
        lda rt_x1               ; vy2 = y2 (endpoint, low byte)
        sta lrvy2
        lda rt_y0               ; vx = fixed X (low byte) -> rt_x0 for the plot
        sta rt_x0
        lda #0                  ; X hi byte = 0 (guarded 0..39 by rt_loresplot)
        sta rt_x0+1
@loop:  lda lrvy                ; rt_y0 = running Y
        sta rt_y0
        lda #0
        sta rt_y0+1
        jsr rt_loresplot
        lda lrvy
        cmp lrvy2
        beq @done
        bcc @inc
        dec lrvy
        jmp @loop
@inc:   inc lrvy
        jmp @loop
@done:  rts
.endif  ; RT_VLIN

.ifdef RT_TEXT
.export rt_text
; rt_text: back to TEXT mode. Display page 2 ($0800), consistent with the page-2
; lo-res framebuffer (so TEXT after GR doesn't re-expose the $0400 code page).
rt_text:
        lda GEN2_TEXTON
        lda GEN2_PAGE2
        rts
.endif  ; RT_TEXT

.ifdef RT_HOME
.export rt_home
; rt_home: clear the $0800 text page (fill spaces $A0). Page 2, matching rt_text.
rt_home:
        lda #$00
        sta ptr_lo
        lda #$08
        sta ptr_hi
        ldx #$04
        ldy #0
        lda #$A0
@l:     sta (ptr_lo),y
        iny
        bne @l
        inc ptr_hi
        dex
        bne @l
        rts
.endif  ; RT_HOME

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
        lda (tptr),y            ; A = pixel mask
        ldx pen                 ; HCOLOR 0 = erase (AND ~mask), else set (OR mask)
        beq @clr
        ora (ptr_lo),y
        sta (ptr_lo),y
        rts
@clr:   eor #$FF
        and (ptr_lo),y
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

; ---- lo-res / text row-base table ($0800 page, Apple II interleave) ----------
; lores_lo/hi[row] = base address of byte-row `row` (0..23) in the $0800 page.
; A lo-res cell row (0..47) maps to byte-row row/2; the two pixel rows live in the
; low/high nibble of each byte. Same low bytes as gen2gfx.inc lores_lo; high bytes
; are page-2 ($08-$0B) since the native lo-res runtime always uses page 2.
.ifdef LR_NEEDED
.segment "RODATA"
lores_lo:
        .byte $00,$80,$00,$80,$00,$80,$00,$80
        .byte $28,$A8,$28,$A8,$28,$A8,$28,$A8
        .byte $50,$D0,$50,$D0,$50,$D0,$50,$D0
lores_hi:
        .byte $08,$08,$09,$09,$0A,$0A,$0B,$0B
        .byte $08,$08,$09,$09,$0A,$0A,$0B,$0B
        .byte $08,$08,$09,$09,$0A,$0A,$0B,$0B
.endif

; ---- GEN2 graphics leaf routines (project libs) -- only what's referenced ----
.ifdef RT_HGR
.include "gen2_init.asm"
.include "hgr_clear.asm"
.endif
.if .defined(RT_PLOT) .or .defined(RT_LINE)
.include "hgr_scanline.inc"
.endif
