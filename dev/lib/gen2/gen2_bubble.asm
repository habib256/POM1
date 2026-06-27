; ============================================================================
; gen2_bubble.asm -- full-width speech-bubble frame for the GEN2 HGR LOGO build.
; ----------------------------------------------------------------------------
; Replaces dev/lib/tms9918/bubble.asm (same draw_bubble symbol). Two HGR-aware
; upgrades over the TMS version:
;
;   1. FULL 280-px WIDTH. The bubble spans x = 4..275 using plot_set_x16, the
;      9-bit-X plotter, instead of line_xy's 8-bit endpoints (which top out at
;      255 and can't even form a >255-px-wide run because dx overflows a byte).
;      So the frame now uses the whole HGR scanline, not just the low 256 px.
;   2. 2-PIXEL VERTICAL SIDES. A 1-px vertical line on HGR lands on one byte-
;      column parity every scanline -> NTSC-artifacts to a solid colour. The
;      left side (x=4,5) and right side (x=274,275) are each drawn 2 px wide so
;      both parities are lit -> clean solid white. Horizontal runs already read
;      white, so top/bottom stay 1 px.
;
; The tail is centred at x=140 (the true centre of the 280-px screen) so it
; points at the doubled narrator emote, which DEMO2 parks at SETXY 140.
; ============================================================================

.ifdef CODETANK_BUILD

.export   draw_bubble
.import   plot_set_x16
.import   plot_mode
.import   line_xy
.import   hgr_lo, hgr_hi        ; scanline base LUTs (band clear)
.importzp pix_x, pix_y, pix_xh
.importzp ln_x0, ln_y0, ln_x1, ln_y1

.segment "ZEROPAGE"
bx_lo:  .res 1          ; current/target column, low byte (16-bit)
bx_hi:  .res 1          ; current column, high byte (0 or 1)
bex_lo: .res 1          ; horizontal-run end column, low byte
bex_hi: .res 1          ; horizontal-run end column, high byte
bby:    .res 1          ; current / start scanline
bey:    .res 1          ; vertical-run end scanline

.segment "CODE"

; bub_hrun: OR-plot a horizontal run at y=bby from (bx_lo:bx_hi) to
;   (bex_lo:bex_hi) inclusive. 16-bit column counter so it crosses x=255.
bub_hrun:
@l:     lda bx_lo
        sta pix_x
        lda bx_hi
        sta pix_xh
        lda bby
        sta pix_y
        jsr plot_set_x16
        lda bx_lo
        cmp bex_lo
        bne @inc
        lda bx_hi
        cmp bex_hi
        beq @done
@inc:   inc bx_lo
        bne @l
        inc bx_hi
        jmp @l
@done:  rts

; bub_vrun: OR-plot a vertical run at the fixed column (bx_lo:bx_hi) from
;   y=bby to y=bey inclusive.
bub_vrun:
@l:     lda bx_lo
        sta pix_x
        lda bx_hi
        sta pix_xh
        lda bby
        sta pix_y
        jsr plot_set_x16
        lda bby
        cmp bey
        beq @done
        inc bby
        jmp @l
@done:  rts

; clear_band: zero HGR scanlines 80..112 (the speech-bubble band) so a fresh
;   SAY replaces the previous frame's bubble + text WITHOUT a full-screen CS.
;   The narrator emote sits above y=80 and is animated by its own XOR
;   erase/redraw (SETSHAPE), so it is never wiped here -- that is the whole
;   point of the reversible XOR sprite. Uses bx_lo/bx_hi as a scratch pointer.
clear_band:
        ldx #80
@row:   lda hgr_lo,x
        sta bx_lo
        lda hgr_hi,x
        sta bx_hi
        ldy #39
        lda #0
@col:   sta (bx_lo),y
        dey
        bpl @col
        inx
        cpx #113
        bne @row
        rts

draw_bubble:
        jsr clear_band          ; wipe only the bubble band, not the whole image
        lda #0
        sta plot_mode
        ; --- top edge: y=80, x 4..275 ---
        lda #4
        sta bx_lo
        lda #0
        sta bx_hi
        lda #<275
        sta bex_lo
        lda #>275
        sta bex_hi
        lda #80
        sta bby
        jsr bub_hrun
        ; --- bottom edge: y=112, x 4..275 ---
        lda #4
        sta bx_lo
        lda #0
        sta bx_hi
        lda #<275
        sta bex_lo
        lda #>275
        sta bex_hi
        lda #112
        sta bby
        jsr bub_hrun
        ; --- left side 2 px wide: x=4 then x=5, y 80..112 ---
        lda #4
        sta bx_lo
        lda #0
        sta bx_hi
        lda #80
        sta bby
        lda #112
        sta bey
        jsr bub_vrun
        lda #5
        sta bx_lo
        lda #0
        sta bx_hi
        lda #80
        sta bby
        lda #112
        sta bey
        jsr bub_vrun
        ; --- right side 2 px wide: x=275 then x=274, y 80..112 ---
        lda #<275
        sta bx_lo
        lda #>275
        sta bx_hi
        lda #80
        sta bby
        lda #112
        sta bey
        jsr bub_vrun
        lda #<274
        sta bx_lo
        lda #>274
        sta bx_hi
        lda #80
        sta bby
        lda #112
        sta bey
        jsr bub_vrun
        ; --- tail centred at x=140 (8-bit, via line_xy):
        ;     (136,80) -> (140,72) -> (144,80) ---
        lda #136
        sta ln_x0
        lda #80
        sta ln_y0
        lda #140
        sta ln_x1
        lda #72
        sta ln_y1
        jsr line_xy
        lda #140
        sta ln_x0
        lda #72
        sta ln_y0
        lda #144
        sta ln_x1
        lda #80
        sta ln_y1
        jmp line_xy

.endif  ; CODETANK_BUILD
