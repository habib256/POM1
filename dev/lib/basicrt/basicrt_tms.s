; basicrt_tms.s -- native BASIC runtime (rt_* ABI) for the TMS9918 card.
;
; Same ABI as basicrt_gen2.s, but graphics go to the TMS9918 VDP. Hi-res (HGR /
; HPLOT / HCOLOR) wraps the project's TMS9918 Mode-II asm (init_vdp_g2 /
; disable_sprites / clear_bitmap / plot_set / line_xy from tms9918m2.asm).
; Lo-res (GR / COLOR= / PLOT / HLIN / VLIN / TEXT / HOME) drives Multicolor mode
; with self-contained routines below -- needs only tms9918_pad12 from
; tms9918_pad.asm, not tms9918m2.o.
;
; Build: ca65 -I dev/lib/tms9918 -I dev/lib/apple1 ; link with tms9918_pad.o
; (always, for drawing programs) + tms9918m2.o (hi-res only). See
; tools/basicc_native.sh.

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

; ONERR handler address (low/high). 0 = no handler armed. Always exported (2 ZP
; bytes); the program .importzp's them only when it uses ONERR. Card-agnostic.
rt_onerr_lo: .res 1
rt_onerr_hi: .res 1
.exportzp rt_onerr_lo, rt_onerr_hi

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

; ============================================================================
; Lo-res graphics -- TMS9918 Multicolor mode (64x48 of 4x4 blocks)
; ============================================================================
; Same rt_* ABI as the GEN2 lo-res runtime (rt_gr / rt_color / rt_loresplot /
; rt_hlin / rt_vlin / rt_text / rt_home), so the native compiler emits identical
; call sites for either card. The pixels live in the TMS9918's private VRAM
; (pattern table $0000, banded name table $0800) instead of a memory-mapped
; Apple-II framebuffer, so every cell is a read-modify-write through the two VDP
; ports, with a silicon-strict 12c gap (JSR tms9918_pad12) between back-to-back
; VDP stores. Self-contained: needs only tms9918_pad12 from tms9918_pad.asm (the
; hi-res VDP lib tms9918m2.o is NOT required for a lo-res-only program).
;
; Coordinate model mirrors sketchs/tms9918/applesoft_tms9918/tmsgfx.inc: Multicolor
; is 64 wide x 48 high, each block one of 16 palette colours. An Applesoft listing
; written for the 40-wide Apple lo-res screen (PLOT/HLIN/VLIN 0..39) fits inside
; the 64-wide field unchanged; out-of-range plots (x>=64 / y>=48) are silently
; skipped, matching the ROM (no ONERR trip), exactly like the GEN2 runtime.
RT_LR = .defined(RT_GR) .or .defined(RT_COLOR) .or .defined(RT_LORESPLOT) .or .defined(RT_HLIN) .or .defined(RT_VLIN) .or .defined(RT_TEXT) .or .defined(RT_HOME)

.if RT_LR
.include "tms9918.inc"           ; VDP_DATA $CC00 / VDP_CTRL $CC01
.import tms9918_pad12: absolute  ; silicon-strict 12c gap (tms9918_pad.asm)

.segment "ZEROPAGE"
lr_src: .res 2                   ; register-table pointer for lr_load_regs

.segment "BSS"
lr_col:  .res 1                  ; lo-res colour 0..15 (single nibble)
lr_al:   .res 1                  ; VRAM pattern address lo
lr_ah:   .res 1                  ; VRAM pattern address hi
lr_byte: .res 1                  ; VRAM byte being read-modify-written
lr_run:  .res 1                  ; HLIN/VLIN running coordinate
lr_end:  .res 1                  ; HLIN/VLIN endpoint coordinate

.segment "CODE"

; ---- VDP address latch + register loader (shared by GR/TEXT) ----------------
; lr_setw / lr_setr: arm the VRAM auto-increment write / read address from
; lr_al:lr_ah. Each ends with a pad12 cushion, so the caller's first VDP_DATA
; access lands >=24c after the command byte (matches tms9918m2 vdp_set_*).
lr_setw:
        lda lr_al
        sta VDP_CTRL
        jsr tms9918_pad12
        lda lr_ah
        ora #$40                 ; bit 6 = write
        sta VDP_CTRL
        jsr tms9918_pad12
        rts
lr_setr:
        lda lr_al
        sta VDP_CTRL
        jsr tms9918_pad12
        lda lr_ah                ; bit 6 = 0 -> read
        sta VDP_CTRL
        jsr tms9918_pad12
        rts

; lr_load_regs: write 8 VDP registers from (lr_src),y with R1's display bit forced
; OFF, so the table-fill bursts that follow run blanked (strict-mode access window
; is widest with the display off). Caller re-arms R1 (display ON) afterwards.
lr_load_regs:
        ldy #0
@l:     lda (lr_src),y
        cpy #1
        bne @st
        and #$BF                 ; R1 display OFF
@st:    sta VDP_CTRL
        jsr tms9918_pad12
        tya
        ora #$80                 ; cmd = $80 | reg index
        sta VDP_CTRL
        jsr tms9918_pad12
        iny
        cpy #8
        bne @l
        rts

; lr_fill: fill VRAM. lr_al:lr_ah = start address, X = page count (256B units),
; A = fill byte. tms9918_pad12 is a bare RTS, so A survives the inner loop.
lr_fill:
        stx lr_end               ; reuse lr_end as the page counter (no GR active)
        pha
        jsr lr_setw
        pla
@pg:    ldy #0
@by:    sta VDP_DATA
        jsr tms9918_pad12
        iny
        bne @by
        dec lr_end
        bne @pg
        rts

; lr_display_on: re-arm register 1 with A (the mode's display-ON value).
lr_display_on:
        sta VDP_CTRL
        jsr tms9918_pad12
        lda #$81                 ; cmd = $80 | reg 1
        sta VDP_CTRL
        jsr tms9918_pad12
        rts

.ifdef RT_GR
.export rt_gr
; rt_gr: A = page (ignored; the 16 KB VRAM holds a single bitmap). Switch to
; Multicolor, build the banded name table, clear the pattern (colour) table,
; park the sprites, reset colour to 0, display ON.
rt_gr:
        lda #<mc_regs
        sta lr_src
        lda #>mc_regs
        sta lr_src+1
        jsr lr_load_regs
        jsr lr_nametab           ; banded name table at $0800
        lda #$00                 ; clear 1536-byte pattern table at $0000
        sta lr_al
        sta lr_ah
        ldx #6                   ; 6 * 256 = 1536
        lda #$00
        jsr lr_fill
        jsr lr_parksat           ; hide sprites (SAT $0B00, R5=$16)
        lda #0
        sta lr_col
        lda #$C8                 ; R1 = Multicolor, display ON
        jmp lr_display_on

; lr_nametab: 768 bytes at $0800. Band b (0..5) spans 4 char-rows; each of its 4
; rows holds names b*32+0 .. b*32+31, so the pattern table becomes a flat 64x48
; nibble framebuffer.
lr_nametab:
        lda #$00
        sta lr_al
        lda #$08
        sta lr_ah
        jsr lr_setw
        lda #0
        sta lr_byte              ; band base 0,32,..,160 (scratch, GR not plotting)
        ldx #6                   ; 6 bands
@band:  ldy #4                   ; 4 char-rows per band
@rep:   lda #0
        sta lr_run               ; column 0..31 (scratch)
@col:   lda lr_byte
        clc
        adc lr_run
        sta VDP_DATA
        jsr tms9918_pad12
        inc lr_run
        lda lr_run
        cmp #32
        bne @col
        dey
        bne @rep
        lda lr_byte
        clc
        adc #32
        sta lr_byte
        dex
        bne @band
        rts

; lr_parksat: hide all sprites -- write $D0 to sprite 0's Y at the Multicolor SAT
; base ($0B00). The VDP stops scanning sprites at the first Y=$D0.
lr_parksat:
        lda #$00
        sta lr_al
        lda #$0B
        sta lr_ah
        jsr lr_setw
        jsr tms9918_pad12
        lda #$D0
        sta VDP_DATA
        jsr tms9918_pad12
        rts
.endif  ; RT_GR

.ifdef RT_COLOR
.export rt_color
; rt_color: A = COLOR= value (Applesoft masks &15). One palette nibble per block.
rt_color:
        and #$0F
        sta lr_col
        rts
.endif  ; RT_COLOR

; lr_addr: compute the Multicolor pattern address for block (rt_x0, rt_y0) low
; bytes into lr_al:lr_ah. Caller guarantees x<64, y<48. Clobbers A, tmp, tmp2.
.if .defined(RT_LORESPLOT) .or .defined(RT_HLIN) .or .defined(RT_VLIN)
lr_addr:
        lda rt_x0                ; c = x >> 1
        lsr a
        sta tmp                  ; tmp = c
        lda rt_y0                ; R = y >> 1   (char row 0..23)
        lsr a
        sta tmp2                 ; tmp2 = R
        lsr a                    ; addr hi = R >> 2  (band 0..5)
        lsr a
        sta lr_ah
        lda tmp2                 ; byteidx = (R & 3)*2 + (y & 1)
        and #$03
        asl a
        sta lr_al
        lda rt_y0
        and #$01
        ora lr_al
        sta lr_al
        lda tmp                  ; addr lo = c*8 + byteidx
        asl a
        asl a
        asl a
        clc
        adc lr_al
        sta lr_al
        rts

.export rt_loresplot
; rt_loresplot: set block (rt_x0, rt_y0) low bytes to lr_col. Even x = left =
; high nibble; odd x = right = low nibble. Out-of-range (x>=64 / y>=48) skipped.
rt_loresplot:
        lda rt_x0+1              ; x must be 0..63 (hi byte 0)
        bne @skip
        lda rt_x0
        cmp #64
        bcs @skip
        lda rt_y0+1              ; y must be 0..47 (hi byte 0)
        bne @skip
        lda rt_y0
        cmp #48
        bcs @skip
        jsr lr_addr
        jsr lr_setr
        jsr tms9918_pad12        ; +12c before the data read
        lda VDP_DATA
        sta lr_byte
        lda rt_x0
        and #$01
        bne @right
        lda lr_byte              ; even x -> high nibble
        and #$0F
        sta lr_byte
        lda lr_col
        asl a
        asl a
        asl a
        asl a
        ora lr_byte
        jmp @wr
@right: lda lr_byte              ; odd x -> low nibble
        and #$F0
        sta lr_byte
        lda lr_col
        and #$0F
        ora lr_byte
@wr:    sta lr_byte
        jsr lr_setw
        jsr tms9918_pad12        ; +12c before the data write
        lda lr_byte
        sta VDP_DATA
        jsr tms9918_pad12
@skip:  rts
.endif  ; LORESPLOT/HLIN/VLIN

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
; Stage the fixed X into rt_x0 (what rt_loresplot reads as X) and walk the running
; Y in lr_run -> rt_y0 from y1..y2.
rt_vlin:
        lda rt_x0                ; running Y = y1
        sta lr_run
        lda rt_x1                ; endpoint y2
        sta lr_end
        lda rt_y0                ; fixed X -> rt_x0
        sta rt_x0
        lda #0                   ; X hi = 0 (guarded 0..63 by rt_loresplot)
        sta rt_x0+1
@loop:  lda lr_run               ; rt_y0 = running Y
        sta rt_y0
        lda #0
        sta rt_y0+1
        jsr rt_loresplot
        lda lr_run
        cmp lr_end
        beq @done
        bcc @inc
        dec lr_run
        jmp @loop
@inc:   inc lr_run
        jmp @loop
@done:  rts
.endif  ; RT_VLIN

; ---- TEXT / HOME ------------------------------------------------------------
; Native programs PRINT to the Apple-1 terminal (WOZ ECHO $FFEF), never to the
; TMS text console, so TEXT/HOME only need to take the picture down to a clean
; blank text screen: load Text Mode F1 regs, zero the 2 KB pattern table (every
; glyph blank), zero the name table, display ON. No font is uploaded -- the TMS
; screen stays a uniform backdrop. HOME re-uses the same routine (on this card a
; cleared text screen and a homed cursor are visually identical).
.if .defined(RT_TEXT) .or .defined(RT_HOME)
lr_text_mode:
        lda #<vdpt_regs
        sta lr_src
        lda #>vdpt_regs
        sta lr_src+1
        jsr lr_load_regs
        lda #$00                 ; blank all glyphs: zero pattern table $0000 (2 KB)
        sta lr_al
        sta lr_ah
        ldx #8                   ; 8 * 256 = 2048
        lda #$00
        jsr lr_fill
        lda #$00                 ; clear name table $0800 (1 KB) to glyph 0
        sta lr_al
        lda #$08
        sta lr_ah
        ldx #4
        lda #$00
        jsr lr_fill
        lda #$D0                 ; R1 = text mode, display ON
        jmp lr_display_on
.endif

.ifdef RT_TEXT
.export rt_text
rt_text:
        jmp lr_text_mode
.endif  ; RT_TEXT

.ifdef RT_HOME
.export rt_home
rt_home:
        jmp lr_text_mode
.endif  ; RT_HOME

; ---- lookup tables / register sets ------------------------------------------
; Text Mode F1: R1=$D0 text/on, R2=$02 name $0800, R4=$00 pattern $0000,
;               R7=$F0 white-on-black.
vdpt_regs:
        .byte $00,$D0,$02,$00,$00,$00,$00,$F0

; Multicolor: R1=$C8 MC/on, R2=$02 name $0800, R4=$00 pattern $0000, R5=$16
;             sprite attr $0B00 (clear of $0000 pattern + $0800 name), R7=$01
;             black border.
mc_regs:
        .byte $00,$C8,$02,$00,$00,$16,$00,$01
.endif  ; RT_LR

; ---- shared 16-bit integer math (rt_mul / rt_div / rt_cmp16) ----------------
.include "basicrt_math.inc"
