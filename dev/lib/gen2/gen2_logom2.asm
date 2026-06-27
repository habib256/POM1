; ============================================================================
; gen2_logom2.asm -- GEN2 HGR backend for the Apple-1 LOGO interpreter.
;
; Drop-in replacement for dev/lib/tms9918/tms9918m2.asm: it exposes the SAME
; public seam (symbols + ZP slots) so LOGO's hardware-independent core links
; unchanged, but every pixel lands in the Uncle Bernie GEN2 HGR framebuffer
; ($2000-$3FFF, 280x192 NTSC artifact colour) instead of TMS9918 Mode-2 VRAM.
;
; Public symbols (identical to the TMS backend):
;   init_vdp_g2     -- put the GEN2 card in GRAPHICS+HIRES+PAGE1, seed pen=white
;   clear_bitmap    -- zero the 8 KB HGR framebuffer
;   disable_sprites -- no-op (HGR has no hardware sprites; turtle = software blit)
;   vdp_set_write   -- no-op (HGR is RAM-mapped, no VRAM address latch)
;   vdp_set_read    -- no-op
;   calc_pix_addr   -- (pix_x,pix_y) -> pix_addr_lo:hi = HGR scanline byte base
;   plot_set        -- plot at (pix_x,pix_y), OR (+pen colour) or XOR per plot_mode
;   line_xy         -- Bresenham (ln_x0,y0)->(ln_x1,y1), 16-bit signed err
;
; Coordinate space: LOGO drives 8-bit pix_x (0..255). HGR is 280 wide, so this
; backend uses columns 0..255 and leaves a 24 px right margin unused -- keeps
; the whole interpreter byte-for-byte 8-bit, matching the old TMS 256-wide
; screen. (A future revision can widen to 16-bit x for the full 280.)
;
; HGR colour: the card has only ~6 artifact colours (vs TMS's 15), set by the
; byte's palette high bit (green/violet family vs blue/orange) and the pixel
; column parity. pen_color (0..15, LOGO/TMS index) maps through pen_hi_tbl to
; the palette high bit; the column parity is whatever the line happens to hit.
; This is an inherent HGR limitation -- thin colour lines alias, exactly as on
; real Apple II / GEN2 hardware.
;
; Owns ZP slots: pix_x, pix_y, pix_addr_lo, pix_addr_hi, pix_mask, pix_byte,
;                ln_x0, ln_y0, ln_x1, ln_y1, ln_dx, ln_dy, ln_sx, ln_sy,
;                ln_err, ln_err_hi, pen_color.
;
; Imports (caller must define): tmp, tmp2 (ZP scratch), plot_mode (BSS byte).
; ============================================================================

.include "gen2.inc"

; --- exported ZP slots (same names the TMS backend exported) ----------------
.exportzp pix_x, pix_y, pix_addr_lo, pix_addr_hi, pix_mask, pix_byte
.exportzp pix_xh          ; high byte of a 9-bit X (0 or 1) for plot_set_x16
.exportzp ln_x0, ln_y0, ln_x1, ln_y1, ln_dx, ln_dy, ln_sx, ln_sy
.exportzp ln_x0h, ln_x1h   ; 9-bit-X line endpoints (set by callers of line_xy16)
.exportzp ln_err, ln_err_hi
.exportzp pen_color

; --- imports ----------------------------------------------------------------
.importzp tmp, tmp2
.import   plot_mode

; --- exported routines (same seam) ------------------------------------------
.export init_vdp_g2, clear_bitmap, disable_sprites
.export vdp_set_write, vdp_set_read, calc_pix_addr, plot_set, plot_set_x16
.export line_xy, line_xy16
.export hgr_lo, hgr_hi      ; scanline base LUTs (gen2_bubble clears its band)
; The LOGO interpreter unconditionally .imports these TMS silicon-strict
; timing helpers (their call sites are scattered outside the gated turtle
; region). On HGR there is no VDP write-window to pad for, so resolve them to
; bare RTS stubs here -- the TMS build links the real ones from
; dev/lib/tms9918/tms9918_pad.asm and never links this file.
.export tms9918_pad12, vdp_display_off

; ----------------------------------------------------------------------------
.segment "ZEROPAGE"
pix_x:        .res 1
pix_xh:       .res 1     ; 9-bit X high byte (0 = cols 0..255, 1 = cols 256..279)
pix_y:        .res 1
pix_addr_lo:  .res 1
pix_addr_hi:  .res 1
pix_mask:     .res 1
pix_byte:     .res 1
ln_x0:        .res 1
ln_x0h:       .res 1     ; line endpoint 0, X high byte (9-bit X)
ln_y0:        .res 1
ln_x1:        .res 1
ln_x1h:       .res 1     ; line endpoint 1, X high byte
ln_y1:        .res 1
ln_dx:        .res 1
ln_dxh:       .res 1     ; |dx| high byte (16-bit, for >255-px-wide lines)
ln_dy:        .res 1
ln_sx:        .res 1
ln_sy:        .res 1
ln_err:       .res 1
ln_err_hi:    .res 1
pen_color:    .res 1     ; 0..15 LOGO/TMS palette index (SETPC). $0F = white.

; ============================================================================
.segment "CODE"
; ============================================================================

; init_vdp_g2: bring the GEN2 card up in GRAPHICS + HIRES + PAGE1 + full screen
;   and seed the pen to white. Mirrors the TMS init_vdp_g2 contract (seed
;   pen_color so projects that never call SETPC keep the white-on-black look).
init_vdp_g2:
        lda GEN2_TEXTOFF        ; graphics mode (read = toggle)
        lda GEN2_HIRES
        lda GEN2_PAGE1
        lda GEN2_MIXOFF
        lda #$0F                ; default pen = white (15)
        sta pen_color
        rts

; clear_bitmap: zero the 8 KB HGR page-1 framebuffer ($2000-$3FFF).
clear_bitmap:
        lda #$00
        tay
        sta pix_addr_lo
        ldx #$20
        stx pix_addr_hi
@clr:   sta (pix_addr_lo),y
        iny
        bne @clr
        inc pix_addr_hi
        ldx pix_addr_hi
        cpx #$40
        bne @clr
        rts

; disable_sprites / vdp_set_write / vdp_set_read: no-ops on HGR (kept so the
;   interpreter's explicit calls resolve and cost only a JSR/RTS).
; tms9918_pad12 / vdp_display_off: TMS silicon-strict timing helpers the
;   interpreter imports unconditionally -- no-ops on HGR.
disable_sprites:
vdp_set_write:
vdp_set_read:
tms9918_pad12:
vdp_display_off:
        rts

; calc_pix_addr: (pix_x,pix_y) -> pix_addr_lo:hi = base byte of scanline pix_y
;   in HGR page 1 (column 0). The byte column for pix_x is hgr_col[pix_x];
;   callers that need it index (pix_addr_lo),Y with Y = hgr_col[pix_x].
calc_pix_addr:
        ldx pix_y
        lda hgr_lo,x
        sta pix_addr_lo
        lda hgr_hi,x
        sta pix_addr_hi
        rts

; plot_set: plot (pix_x,pix_y). plot_mode 0 = OR (draw, applies pen colour),
;   1 = XOR (turtle/erase, leaves the trail colour byte's palette bit alone).
; plot_set: 8-bit X entry (pix_x = 0..255). Forces pix_xh = 0 then falls into
;   the 9-bit core, so every existing 8-bit caller (line_xy, emote, text) is
;   unchanged.
plot_set:
        lda #0
        sta pix_xh
; plot_set_x16: 9-bit X entry. Caller sets pix_x = low byte of the screen
;   column and pix_xh = high byte (0 for cols 0..255, 1 for cols 256..279, where
;   pix_x then holds col-256 = 0..23). Lets HGR-aware code (the speech bubble)
;   use the FULL 280-px width, not just the low 256.
plot_set_x16:
        lda pix_y
        cmp #192
        bcc @ok
        rts
@ok:    ldx pix_y
        lda hgr_lo,x
        sta pix_addr_lo
        lda hgr_hi,x
        sta pix_addr_hi
        ldx pix_x
        lda pix_xh
        bne @xhi
        lda hgr_mask,x        ; cols 0..255
        sta pix_mask
        ldy hgr_col,x
        jmp @merge
@xhi:   ; high path: valid only for pix_xh == 1 AND pix_x (=col-256) < 24, i.e.
        ;   screen column 256..279. Anything else (xh>=2 from overflow, xh=$FF
        ;   from a negative X, or x>=280) is off-screen -> skip the plot. This
        ;   lets turtle vertices that fall just past the edge clip cleanly.
        cmp #1
        bne @off
        cpx #24
        bcs @off
        lda hgr_mask_hi,x     ; cols 256..279 (X = col-256 = 0..23)
        sta pix_mask
        ldy hgr_col_hi,x
@merge: lda (pix_addr_lo),y
        ldx plot_mode
        bne @xor
        ; --- OR draw: light pixel, force byte palette bit to pen family ------
        ora pix_mask
        and #$7F
        ldx pen_color
        ora pen_hi_tbl,x
        sta (pix_addr_lo),y
        rts
@xor:   ; --- XOR erase: toggle pixel only, preserve palette/colour ----------
        eor pix_mask
        sta (pix_addr_lo),y
@off:   rts                   ; (also the off-screen-X early-out target)

; pen_color (0..15) -> HGR palette high bit. $00 = green/violet family,
;   $80 = blue/orange family. White (15) stays $00.
pen_hi_tbl:
        ;      0    1    2    3    4    5    6    7
        .byte $00, $00, $00, $00, $80, $80, $80, $80
        ;      8    9   10   11   12   13   14   15
        .byte $80, $80, $00, $00, $00, $00, $00, $00

; --- HGR column / mask for screen columns 256..279 -------------------------
; The shared hgr_col / hgr_mask tables (hgr_plot_tables.inc) stop at 256
; entries, so plot_set_x16's high path uses these 24-entry extensions instead
; of overrunning them. byte column = x/7 (cols 36..39 = the rightmost 4 of the
; 40-byte scanline, unused by 0..255 content); bit = $01 << (x % 7).
;   index i = col - 256  (0..23),  x = 256 + i
hgr_col_hi:
        .byte 36, 36, 36
        .byte 37, 37, 37, 37, 37, 37, 37
        .byte 38, 38, 38, 38, 38, 38, 38
        .byte 39, 39, 39, 39, 39, 39, 39
hgr_mask_hi:
        .byte $10, $20, $40
        .byte $01, $02, $04, $08, $10, $20, $40
        .byte $01, $02, $04, $08, $10, $20, $40
        .byte $01, $02, $04, $08, $10, $20, $40

; ----------------------------------------------------------------------------
; line_xy: Bresenham, 16-bit signed err. Byte-for-byte the same algorithm as
;   the TMS backend -- only plot_set differs underneath. Inputs ln_x0/y0/x1/y1.
; ----------------------------------------------------------------------------
; line_xy: 8-bit-X entry. Clears the X high bytes so legacy callers that only
;   set ln_x0/ln_x1 (the editor, the bubble tail) keep their 0..255 behaviour,
;   then falls into the 9-bit core.
line_xy:
        LDA #0
        STA ln_x0h
        STA ln_x1h
; line_xy16: 9-bit-X entry. Caller sets ln_x0/ln_x0h and ln_x1/ln_x1h so the
;   turtle can draw across the full 0..279 HGR width. dx is 16-bit (a >255-px
;   line would overflow an 8-bit dx); dy stays 8-bit (Y is 0..191).
line_xy16:
        ; --- dx (16-bit) + sx ---
        SEC
        LDA ln_x1
        SBC ln_x0
        STA ln_dx
        LDA ln_x1h
        SBC ln_x0h
        STA ln_dxh
        BCS @xpos               ; x1 >= x0
        ; negate 16-bit dx, sx = -1
        SEC
        LDA #0
        SBC ln_dx
        STA ln_dx
        LDA #0
        SBC ln_dxh
        STA ln_dxh
        LDA #$FF
        STA ln_sx
        JMP @dy
@xpos:  LDA #$01
        STA ln_sx
@dy:    ; --- dy (8-bit) + sy ---
        SEC
        LDA ln_y1
        SBC ln_y0
        BCS @syp
        EOR #$FF
        CLC
        ADC #1
        STA ln_dy
        LDA #$FF
        STA ln_sy
        JMP @init
@syp:   STA ln_dy
        LDA #$01
        STA ln_sy
@init:  ; --- err = dx - dy (16-bit signed) ---
        SEC
        LDA ln_dx
        SBC ln_dy
        STA ln_err
        LDA ln_dxh
        SBC #0
        STA ln_err_hi
        LDA ln_x0
        STA pix_x
        LDA ln_x0h
        STA pix_xh
        LDA ln_y0
        STA pix_y
@step:  JSR plot_set_x16
        ; end test: x0 == x1 (both bytes) and y0 == y1
        LDA ln_x0
        CMP ln_x1
        BNE @do
        LDA ln_x0h
        CMP ln_x1h
        BNE @do
        LDA ln_y0
        CMP ln_y1
        BEQ @end
@do:    LDA ln_err
        STA tmp
        LDA ln_err_hi
        STA tmp2
        ASL tmp
        ROL tmp2
        ; test 1: step x if 2*err >= -dy  (dy 8-bit, zero-extended)
        CLC
        LDA tmp
        ADC ln_dy
        LDA tmp2
        ADC #0
        BMI @no_x
        ; err -= dy
        SEC
        LDA ln_err
        SBC ln_dy
        STA ln_err
        LDA ln_err_hi
        SBC #0
        STA ln_err_hi
        ; x0 += sx (16-bit)
        LDA ln_sx
        BPL @xinc
        LDA ln_x0
        BNE @decok
        DEC ln_x0h
@decok: DEC ln_x0
        JMP @after_x
@xinc:  INC ln_x0
        BNE @after_x
        INC ln_x0h
@after_x:
        LDA ln_x0
        STA pix_x
        LDA ln_x0h
        STA pix_xh
@no_x:  ; test 2: step y if 2*err < dx  (dx 16-bit)
        SEC
        LDA tmp
        SBC ln_dx
        LDA tmp2
        SBC ln_dxh
        BPL @no_y
        ; err += dx (16-bit)
        CLC
        LDA ln_err
        ADC ln_dx
        STA ln_err
        LDA ln_err_hi
        ADC ln_dxh
        STA ln_err_hi
        LDA ln_sy
        BPL @syp2
        DEC ln_y0
        JMP @after_y
@syp2:  INC ln_y0
@after_y:
        LDA ln_y0
        STA pix_y
@no_y:  JMP @step
@end:   RTS

; --- HGR lookup tables ------------------------------------------------------
        .include "hgr_scanline.inc"     ; hgr_lo[192] / hgr_hi[192]
        .include "hgr_plot_tables.inc"  ; hgr_col[280] / hgr_mask[280]
