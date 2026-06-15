; gen2_blit.s — fast 8x8 glyph blitter for Uncle Bernie's GEN2 HGR (cc65).
;
; Hand-written 6502 replacement for the inner pixel loop of gen2_hgr_puts. The
; pure-C version computed px/7 and px%7 (software division — cc65 has no DIV)
; for EVERY plotted pixel: ~4 divisions per lit pixel, hundreds of cycles each,
; which made drawing a single line of text cost millions of cycles.
;
; This routine does NO runtime division. The C wrapper passes the glyph's
; starting byte column (x/7) and bit mask (1<<(x%7)) — one division per GLYPH,
; in C — and the inner loop then advances the column/mask INCREMENTALLY (a shift
; + a wrap test) as it walks the 16 doubled pixels of each row. Scanline base
; addresses come from the gen2_rowlo/gen2_rowhi tables (built once in C). The
; result is ~50-100x faster than the per-pixel-division C path.
;
; Geometry: each of the glyph's 8 source rows is drawn on TWO scanlines and each
; set source bit lights TWO horizontal pixels (pixel-doubling -> solid white,
; no NTSC colour fringe), giving 16x16 white cells. bit 0 of each glyph byte is
; the leftmost pixel (matches dev/lib/hgr/bbfont_cp437.inc).
;
; Interface — the C wrapper sets these (defined here, in the zero page) then
; JSRs _gen2_blit_glyph once per glyph:
;     _gen2_g_glyph : zp pointer to the 8 glyph bytes
;     _gen2_g_col   : starting byte column  (x / 7),   0..39
;     _gen2_g_mask  : starting bit mask     (1 << x%7), one of $01,$02,...,$40
;     _gen2_g_y     : top scanline (0..176) — 16 scanlines y..y+15 are written
; The caller guarantees the whole 16x16 cell is on-screen (the C wrapper clips),
; so no bounds checking is done here.

        .export   _gen2_blit_glyph
        .export   _gen2_fill_rect_asm
        .export   _gen2_plot_asm, _gen2_unplot_asm
        .export   _gen2_pixrect_asm
        .exportzp _gen2_g_glyph, _gen2_g_col, _gen2_g_mask, _gen2_g_y
        .exportzp _gen2_f_y0, _gen2_f_rows, _gen2_f_col0, _gen2_f_cols, _gen2_f_val
        .exportzp _gen2_p_x, _gen2_p_y
        .exportzp _gen2_r_x, _gen2_r_xr, _gen2_r_y0, _gen2_r_rows, _gen2_r_mode
        .import   _gen2_rowlo, _gen2_rowhi, _gen2_col7, _gen2_mask7
        .importzp ptr1, ptr2, tmp1, tmp2, tmp3, tmp4

; --- interface variables (zero page) ----------------------------------------
        .segment "ZEROPAGE"
_gen2_g_glyph: .res 2        ; pointer to 8 glyph bytes
_gen2_g_col:   .res 1        ; starting byte column
_gen2_g_mask:  .res 1        ; starting bit mask
_gen2_g_y:     .res 1        ; top scanline
; gen2_hgr_fill_rect parameter block:
_gen2_f_y0:    .res 1        ; top scanline of the rectangle
_gen2_f_rows:  .res 1        ; number of scanlines (>= 1)
_gen2_f_col0:  .res 1        ; first byte column
_gen2_f_cols:  .res 1        ; number of byte columns (>= 1)
_gen2_f_val:   .res 1        ; fill byte (0 = erase)
; gen2_hgr_plot / gen2_hgr_unplot parameter block:
_gen2_p_x:     .res 2        ; pixel x (0..279)
_gen2_p_y:     .res 1        ; pixel y (0..191)
; gen2_hgr_fill_pixrect / clear_pixrect parameter block. The C wrapper only
; passes the geometry + mode; the asm derives the byte columns and per-region
; (keep,set) masks (via the col7/mask7 LUTs) and applies (byte & keep) | set to
; each byte — one uniform op handles both fill (set bits) and erase (clear bits).
_gen2_r_x:     .res 2        ; left pixel x (0..279)
_gen2_r_xr:    .res 2        ; right pixel x (0..279, clipped)
_gen2_r_y0:    .res 1        ; top scanline
_gen2_r_rows:  .res 1        ; number of scanlines (>= 1)
_gen2_r_mode:  .res 1        ; 1 = fill (white), 0 = clear (black)
; asm-internal scratch (derived per call, persist across the row loop):
gr_colL:  .res 1             ; left  byte column
gr_colR:  .res 1             ; right byte column (== colL if rect fits one byte)
gr_keepL: .res 1             ; left  byte: AND mask
gr_setL:  .res 1             ; left  byte: OR  mask
gr_setF:  .res 1             ; full bytes: stored directly ($7F fill / $00 erase)
gr_keepR: .res 1             ; right byte: AND mask
gr_setR:  .res 1             ; right byte: OR  mask

; zero-page scratch aliases (cc65 leaves tmp1..tmp4 free for leaf asm routines):
curcol  = tmp1               ; current byte column   (also Y during a store)
curmask = tmp2               ; current bit mask
curbits = tmp3               ; remaining source bits of the current row
rowcnt  = tmp4               ; row counter 0..7
; ptr1 = base of scanline yy ; ptr2 = base of scanline yy+1

        .segment "CODE"

; --- plot one pixel at (curcol, curmask) on BOTH scanlines, then advance ------
plot_one:
        ldy curcol
        lda curmask
        ora (ptr1),y         ; scanline yy
        sta (ptr1),y
        lda curmask
        ora (ptr2),y         ; scanline yy+1
        sta (ptr2),y
        ; fall through to advance

; --- advance the pen one pixel: mask <<= 1, wrapping into the next column -----
advance:
        asl curmask          ; 1,2,4,...,64 -> 2,4,...,128
        lda curmask
        cmp #$80             ; was the column's last (bit 6) pixel just passed?
        bne @done
        lda #$01             ; wrap: back to bit 0 of the next byte column
        sta curmask
        inc curcol
@done:
        rts

; --- _gen2_blit_glyph : draw the 8x8 glyph, pixel-doubled to 16x16 -----------
_gen2_blit_glyph:
        lda #0
        sta rowcnt
@rowloop:
        ; ptr1 = gen2_row(y + 2*row), ptr2 = gen2_row(y + 2*row + 1)
        lda rowcnt
        asl a                ; row * 2
        clc
        adc _gen2_g_y        ; yy = y + 2*row
        tay
        lda _gen2_rowlo,y
        sta ptr1
        lda _gen2_rowhi,y
        sta ptr1+1
        iny                  ; yy + 1
        lda _gen2_rowlo,y
        sta ptr2
        lda _gen2_rowhi,y
        sta ptr2+1
        ; load this row's 8 source bits
        ldy rowcnt
        lda (_gen2_g_glyph),y
        sta curbits
        ; reset pen to the glyph's left edge
        lda _gen2_g_col
        sta curcol
        lda _gen2_g_mask
        sta curmask
        ; walk 8 source bits, each lighting (or skipping) 2 doubled pixels
        ldx #8
@bitloop:
        lsr curbits          ; bit 0 (leftmost) -> carry
        bcc @skip
        jsr plot_one         ; lit: 2 doubled pixels
        jsr plot_one
        jmp @next
@skip:
        jsr advance          ; clear: skip 2 pixels
        jsr advance
@next:
        dex
        bne @bitloop
        ; next glyph row
        inc rowcnt
        lda rowcnt
        cmp #8
        bne @rowloop
        rts

; --- _gen2_fill_rect_asm : fill a byte-aligned rectangle of HIRES page 1 ------
; Stores _gen2_f_val into byte columns [col0, col0+cols) on each of `rows`
; scanlines starting at f_y0 — the fast erase behind text/sprites. Scanline
; bases come from the gen2_rowlo/gen2_rowhi tables. The C wrapper guarantees
; rows >= 1, cols >= 1, and that the rectangle is on-screen.
_gen2_fill_rect_asm:
        lda _gen2_f_y0
        sta tmp1             ; current scanline
        lda _gen2_f_rows
        sta tmp2             ; rows remaining
@frow:
        ldy tmp1
        lda _gen2_rowlo,y
        sta ptr1
        lda _gen2_rowhi,y
        sta ptr1+1
        ldx _gen2_f_cols     ; column counter
        ldy _gen2_f_col0     ; current column
        lda _gen2_f_val      ; (preserved across STA/INY/DEX)
@fcol:
        sta (ptr1),y
        iny
        dex
        bne @fcol
        inc tmp1             ; next scanline
        dec tmp2
        bne @frow
        rts

; --- pixel address setup: shared by plot/unplot ------------------------------
; From _gen2_p_x / _gen2_p_y, leaves: ptr1 = scanline base, Y = byte column,
; A = bit mask (1 << x%7). Uses the gen2_col7 / gen2_mask7 LUTs (no division)
; and the gen2_rowlo / gen2_rowhi scanline table. x is split into two ranges
; because a 6502 index is 8-bit: x<256 indexes the LUT directly, x>=256 (only
; 256..279 on a 280-wide screen) indexes it at +256 with the low byte (0..23).
gen2_pixsetup:
        lda _gen2_p_x+1      ; x high byte
        bne @hi
        ldx _gen2_p_x        ; x low (0..255)
        lda _gen2_col7,x
        sta tmp1
        lda _gen2_mask7,x
        jmp @row
@hi:
        ldx _gen2_p_x        ; x low (0..23 -> x 256..279)
        lda _gen2_col7+256,x
        sta tmp1
        lda _gen2_mask7+256,x
@row:
        sta tmp2             ; mask
        ldy _gen2_p_y
        lda _gen2_rowlo,y
        sta ptr1
        lda _gen2_rowhi,y
        sta ptr1+1
        ldy tmp1             ; Y = byte column
        lda tmp2             ; A = mask
        rts

; --- _gen2_plot_asm : set (OR in) one white pixel ----------------------------
_gen2_plot_asm:
        jsr gen2_pixsetup    ; Y=col, A=mask, ptr1=scanline base
        ora (ptr1),y
        sta (ptr1),y
        rts

; --- _gen2_unplot_asm : clear (AND off) one pixel ----------------------------
_gen2_unplot_asm:
        jsr gen2_pixsetup
        eor #$ff             ; ~mask
        and (ptr1),y
        sta (ptr1),y
        rts

; --- _gen2_pixrect_asm : fill/erase a PIXEL rectangle ------------------------
; Derives the byte columns + edge masks for the rectangle [x..xr] from the
; col7/mask7 LUTs (no division), builds per-region (keep,set) values for the
; requested mode, then for each scanline applies (byte & keep) | set to the left
; partial byte, stores setF into the full bytes between, and (byte & keep) | set
; to the right partial byte. colL == colR means the rect fits in one byte (only
; the left step runs). One uniform op does fill (mode=1, set pixels) or erase
; (mode=0, clear pixels). Replaces a per-pixel gen2_hgr_plot loop: a 6x6 block is
; one call instead of 36, and the inner run is a tight STA loop.
_gen2_pixrect_asm:
        ; --- left edge: gr_colL = col7[x], leftMask (bits x%7..6) -> tmp1 ----
        lda _gen2_r_x+1
        bne @xhi
        ldx _gen2_r_x
        lda _gen2_col7,x
        sta gr_colL
        lda _gen2_mask7,x
        jmp @xmask
@xhi:
        ldx _gen2_r_x
        lda _gen2_col7+256,x
        sta gr_colL
        lda _gen2_mask7+256,x
@xmask:
        sec                  ; A = 1<<bitL ; leftMask = (~(A-1)) & $7F
        sbc #1
        eor #$ff
        and #$7f
        sta tmp1             ; tmp1 = leftMask
        ; --- right edge: gr_colR = col7[xr], rightMask (bits 0..xr%7) -> tmp2 -
        lda _gen2_r_xr+1
        bne @rhi
        ldx _gen2_r_xr
        lda _gen2_col7,x
        sta gr_colR
        lda _gen2_mask7,x
        jmp @rmask
@rhi:
        ldx _gen2_r_xr
        lda _gen2_col7+256,x
        sta gr_colR
        lda _gen2_mask7+256,x
@rmask:
        asl a                ; A = 1<<bitR ; rightMask = ((A<<1)-1) & $7F
        sec
        sbc #1
        and #$7f
        sta tmp2             ; tmp2 = rightMask
        ; --- build keep/set from leftMask/rightMask + mode ------------------
        lda gr_colL
        cmp gr_colR
        bne @multi
        ; single byte: combined mask = leftMask & rightMask
        lda tmp1
        and tmp2
        sta tmp1             ; tmp1 = combined mask
        lda _gen2_r_mode
        bne @one_set
        lda tmp1             ; clear: keepL = ~mask, setL = 0
        eor #$ff
        sta gr_keepL
        lda #0
        sta gr_setL
        jmp @fill
@one_set:
        lda #$ff             ; fill: keepL = $FF, setL = mask
        sta gr_keepL
        lda tmp1
        sta gr_setL
        jmp @fill
@multi:
        lda _gen2_r_mode
        bne @multi_set
        ; clear: keep = ~mask, set/full = 0
        lda tmp1
        eor #$ff
        sta gr_keepL
        lda tmp2
        eor #$ff
        sta gr_keepR
        lda #0
        sta gr_setL
        sta gr_setF
        sta gr_setR
        jmp @fill
@multi_set:
        lda #$ff             ; fill: keep edges, OR edge masks, full = $7F
        sta gr_keepL
        sta gr_keepR
        lda tmp1
        sta gr_setL
        lda tmp2
        sta gr_setR
        lda #$7f
        sta gr_setF
@fill:
        lda _gen2_r_y0
        sta tmp1             ; current scanline
        lda _gen2_r_rows
        sta tmp2             ; rows remaining
@prow:
        ldy tmp1
        lda _gen2_rowlo,y
        sta ptr1
        lda _gen2_rowhi,y
        sta ptr1+1
        ; left (or single) byte: (byte & keepL) | setL
        ldy gr_colL
        lda (ptr1),y
        and gr_keepL
        ora gr_setL
        sta (ptr1),y
        ; single-byte rectangle? then this row is done
        lda gr_colL
        cmp gr_colR
        beq @prowend
        ; full bytes colL+1 .. colR-1 (A holds setF across the run)
        ldy gr_colL
        iny
        lda gr_setF
@pfull:
        cpy gr_colR
        bcs @pright
        sta (ptr1),y
        iny
        bne @pfull           ; Y is a column 1..39, never 0 -> always loops
@pright:
        ldy gr_colR
        lda (ptr1),y
        and gr_keepR
        ora gr_setR
        sta (ptr1),y
@prowend:
        inc tmp1             ; next scanline
        dec tmp2
        bne @prow
        rts
