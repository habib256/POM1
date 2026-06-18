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
; the leftmost pixel (matches dev/lib/gen2/bbfont_cp437.inc).
;
; Interface — the C wrapper sets these (defined here, in the zero page) then
; JSRs _gen2_blit_glyph once per glyph:
;     _gen2_g_glyph : zp pointer to the 8 glyph bytes
;     _gen2_g_col   : starting byte column  (x / 7),   0..39
;     _gen2_g_mask  : starting bit mask     (1 << x%7), one of $01,$02,...,$40
;     _gen2_g_y     : top scanline (0..176) — 16 scanlines y..y+15 are written
; The caller guarantees the whole 16x16 cell is on-screen (the C wrapper clips),
; so no bounds checking is done here.

        .export   _gen2_hgr_init
        .export   _gen2_lores_init
        .export   _gen2_hgr_clear
        .export   _gen2_blit_glyph
        .export   _gen2_blit_glyph_color
        .export   _gen2_blit_glyph8
        .export   _gen2_puts_run8
        .export   _gen2_fill_rect_asm
        .export   _gen2_plot_asm, _gen2_unplot_asm
        .export   _gen2_pixrect_asm
        .export   _gen2_colorize_asm
        .export   _gen2_puts_run
        .export   _gen2_utoa
        .export   _gen2_blit_run
        .export   _gen2_blit7_run
        .exportzp _gen2_b_col, _gen2_b_mask, _gen2_b_w, _gen2_b_h
        .exportzp _gen2_b_stride, _gen2_b_y, _gen2_b_mode, _gen2_b_src
        .exportzp _gen2_t_col, _gen2_t_bit, _gen2_t_n, _gen2_t_color
        .exportzp _gen2_t_s, _gen2_t_font
        .exportzp _gen2_u_lo, _gen2_u_hi, _gen2_u_ptr
        .exportzp _gen2_g_glyph, _gen2_g_col, _gen2_g_mask, _gen2_g_y
        .exportzp _gen2_f_y0, _gen2_f_rows, _gen2_f_col0, _gen2_f_cols, _gen2_f_val
        .exportzp _gen2_p_x, _gen2_p_y
        .exportzp _gen2_r_x, _gen2_r_xr, _gen2_r_y0, _gen2_r_rows, _gen2_r_mode
        .exportzp _gen2_z_col0, _gen2_z_ncols, _gen2_z_y0, _gen2_z_rows
        .exportzp _gen2_z_ce, _gen2_z_co, _gen2_z_hi
        .import   _gen2_rowlo, _gen2_rowhi, _gen2_col7, _gen2_mask7
        .import   _gen2_hgr_base          ; HIRES draw-page hi base ($20/$40)
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
; gen2_colorize parameter block — recolour an already-drawn (white) glyph region
; to an NTSC artifact colour: each byte becomes (byte & carrier[col&1]) | hibit.
_gen2_z_col0:  .res 1        ; first byte column of the text box
_gen2_z_ncols: .res 1        ; number of byte columns
_gen2_z_y0:    .res 1        ; top scanline
_gen2_z_rows:  .res 1        ; number of scanlines
_gen2_z_ce:    .res 1        ; carrier mask for EVEN byte columns
_gen2_z_co:    .res 1        ; carrier mask for ODD  byte columns
_gen2_z_hi:    .res 1        ; high bit to OR in ($00 green/violet, $80 orange/blue)
; asm-internal scratch (derived per call, persist across the row loop):
gr_colL:  .res 1             ; left  byte column
gr_colR:  .res 1             ; right byte column (== colL if rect fits one byte)
gr_keepL: .res 1             ; left  byte: AND mask
gr_setL:  .res 1             ; left  byte: OR  mask
gr_setF:  .res 1             ; full bytes: stored directly ($7F fill / $00 erase)
gr_keepR: .res 1             ; right byte: AND mask
gr_setR:  .res 1             ; right byte: OR  mask
gr_or:    .res 1             ; scratch: byte to OR into the framebuffer (cell|hi)
; gen2_blit_glyph_color (row-at-a-time) scratch:
;   bit / car0..3 are computed ONCE per glyph; m0..m2 are rebuilt per row.
gbc_bit:  .res 1             ; shift count derived from g_mask (0..6)
gbc_car0: .res 1             ; carrier byte for cell at g_col + 0
gbc_car1: .res 1             ; carrier byte for cell at g_col + 1
gbc_car2: .res 1             ; carrier byte for cell at g_col + 2
gbc_car3: .res 1             ; carrier byte for cell at g_col + 3 (used only when bit = 6)
gbc_m0:   .res 1             ; row's shifted doubled mask, byte 0 (bits 0..7)
gbc_m1:   .res 1             ;   byte 1 (bits 8..15)
gbc_m2:   .res 1             ;   byte 2 (bits 16..23 — only bits 0..5 ever set)
; gen2_puts (asm string renderer) parameter block:
_gen2_t_col:   .res 1        ; current byte column of the pen
_gen2_t_bit:   .res 1        ; current bit within the column (0..6)
_gen2_t_n:     .res 1        ; glyph cells left to draw (C precomputes the fit)
_gen2_t_color: .res 1        ; 0 = white blitter, else colour blitter
_gen2_t_s:     .res 2        ; string pointer
_gen2_t_font:  .res 2        ; glyph table base (kBBFontAscii)
; gen2_utoa (16-bit unsigned -> decimal ASCII) parameter block:
_gen2_u_lo:    .res 1        ; value low byte  (consumed by the subtraction)
_gen2_u_hi:    .res 1        ; value high byte
_gen2_u_ptr:   .res 2        ; output buffer pointer
; gen2_hgr_blit (1bpp sprite, MSB-first) parameter block:
_gen2_b_col:   .res 1        ; start byte column of the pen
_gen2_b_mask:  .res 1        ; start bit mask (1 << (x % 7))
_gen2_b_w:     .res 1        ; pixels to draw per row (right-clipped by C)
_gen2_b_h:     .res 1        ; rows (bottom-clipped by C)
_gen2_b_stride:.res 1        ; source bytes per row = (w+7)/8
_gen2_b_y:     .res 1        ; current scanline (advanced per row)
_gen2_b_mode:  .res 1        ; 0 SET (OR) / 1 CLEAR (AND ~) / 2 XOR (EOR)
_gen2_b_src:   .res 2        ; source row pointer (advanced per row)
b_srcidx:      .res 1        ; source byte index within the current row

; zero-page scratch aliases (cc65 leaves tmp1..tmp4 free for leaf asm routines):
curcol  = tmp1               ; current byte column   (also Y during a store)
curmask = tmp2               ; current bit mask
curbits = tmp3               ; remaining source bits of the current row
rowcnt  = tmp4               ; row counter 0..7
; ptr1 = base of scanline yy ; ptr2 = base of scanline yy+1

        .segment "CODE"

; --- mode init: fully determine the $C250-$C257 latch -------------------------
; Bernie Q8: the card's soft-switch latch is INDETERMINATE at power-on (the PLD
; POR is untrustworthy) and the Apple-1 RESET line never touches it. A program
; MUST therefore set EVERY switch pair itself before drawing — we cannot know
; the card's state. These two routines touch all four pairs (TEXT, MIXED, PAGE,
; RES), so whatever junk the latch held, the mode is clean and known afterwards.
;
; Why asm and not C: a read of a soft switch is the toggle; its VALUE is
; discarded. cc65's optimiser (-Oirs) drops a volatile read whose result is
; cast to void — so the C `(void)GEN2_SS[n]` macros compiled to NOTHING and the
; old C gen2_hgr_init was silently an empty `rts`. HIRES only ever "worked" by
; luck (POM1's documented cold latch happens to be GRAPHICS+HIRES+PAGE1). An
; absolute `LDA $C25x` here can never be elided. Each LDA is one real bus read =
; one switch toggle; A/X/Y/flags are scratch (leaf routine, nothing reads them).

; void gen2_hgr_init(void): GRAPHICS + HIRES + PAGE1 + FULL screen.
_gen2_hgr_init:
        lda $C250            ; TEXT off  -> graphics
        lda $C257            ; RES = HIRES
        lda $C254            ; PAGE 1
        lda $C252            ; MIXED off -> full screen
        rts

; void gen2_lores_init(void): GRAPHICS + LORES + PAGE1 + FULL screen.
_gen2_lores_init:
        lda $C250            ; TEXT off  -> graphics
        lda $C256            ; RES = LORES
        lda $C254            ; PAGE 1
        lda $C252            ; MIXED off -> full screen
        rts

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

; --- _gen2_blit_glyph_color : row-at-a-time tinted glyph blitter ------------
; Replaces the per-pixel walk: expand the 8-bit source row to a 16-bit doubled
; mask in two nibble LUT lookups (dbl4tbl), shift left by `bit` to align with
; the pen offset, then OR each of the 3-4 spanning bytes (one per 7-bit cell)
; into both scanlines — carrier and palette bit applied per BYTE, not per pixel.
; Reads the same gen2_g_glyph/col/mask/y + gen2_z_ce/co/hi parameter blocks as
; the white blitter; produces the NTSC artifact colour as the glyph is laid
; down (no white-then-recolorize pass). Cell carriers + the shift count are
; precomputed once per glyph (they depend on g_col parity and g_mask, both
; fixed across the 8 source rows).
_gen2_blit_glyph_color:
        ; Derive bit shift count from g_mask (= 1 << bit), bit in 0..6.
        ldx #0
        lda _gen2_g_mask
@bcnt:  lsr a
        beq @bcd
        inx
        bne @bcnt              ; bit <= 6, never wraps X
@bcd:   stx gbc_bit
        ; Precompute carriers for cells 0..3. Carrier parity alternates from
        ; g_col: even col -> ce/co/ce/co ; odd col -> co/ce/co/ce.
        lda _gen2_g_col
        and #1
        bne @codd
        lda _gen2_z_ce
        sta gbc_car0
        sta gbc_car2
        lda _gen2_z_co
        sta gbc_car1
        sta gbc_car3
        jmp @cdn
@codd:  lda _gen2_z_co
        sta gbc_car0
        sta gbc_car2
        lda _gen2_z_ce
        sta gbc_car1
        sta gbc_car3
@cdn:   lda #0
        sta rowcnt
@crow:
        ; ptr1 = scanline yy ; ptr2 = scanline yy+1 (paired pixel-doubling).
        lda rowcnt
        asl a                  ; row * 2
        clc
        adc _gen2_g_y          ; yy = y + 2*row
        tay
        lda _gen2_rowlo,y
        sta ptr1
        lda _gen2_rowhi,y
        sta ptr1+1
        iny                    ; yy + 1
        lda _gen2_rowlo,y
        sta ptr2
        lda _gen2_rowhi,y
        sta ptr2+1
        ; Expand 8 source bits to 16 doubled bits via two nibble lookups.
        ldy rowcnt
        lda (_gen2_g_glyph),y
        pha
        and #$0F
        tay
        lda _gen2_dbl4tbl,y
        sta gbc_m0             ; doubled bits 0..7
        pla
        lsr a
        lsr a
        lsr a
        lsr a
        tay
        lda _gen2_dbl4tbl,y
        sta gbc_m1             ; doubled bits 8..15
        lda #0
        sta gbc_m2             ; doubled bits 16..23 (filled in by the shift)
        ; Shift (m2:m1:m0) left by `bit` positions; bit = 0 leaves them as-is.
        ; A 3-byte chain is enough: the original 16 bits occupy positions
        ; bit..bit+15 (<= 21), so m2 bits 6..7 stay zero and m3 isn't needed.
        ldy gbc_bit
        beq @nosh
@sh:    asl gbc_m0
        rol gbc_m1
        rol gbc_m2
        dey
        bne @sh
@nosh:
        ; --- Cell 0 (column g_col): doubled bits 0..6 = m0 & $7F ------------
        lda gbc_m0
        and #$7F
        beq @c0sk
        and gbc_car0
        ora _gen2_z_hi
        sta gr_or
        ldy _gen2_g_col
        ora (ptr1),y
        sta (ptr1),y
        lda gr_or
        ora (ptr2),y
        sta (ptr2),y
@c0sk:
        ; --- Cell 1 (column g_col+1): doubled bits 7..13 -------------------
        ; bits 7..13 = ((m1 << 1) | (m0 >> 7)) & $7F
        lda gbc_m0
        asl a                  ; CF = m0 bit 7 (= doubled bit 7)
        lda gbc_m1
        rol a                  ; A = (m1 << 1) | (m0 bit 7)
        and #$7F
        beq @c1sk
        and gbc_car1
        ora _gen2_z_hi
        sta gr_or
        ldy _gen2_g_col
        iny
        ora (ptr1),y
        sta (ptr1),y
        lda gr_or
        ora (ptr2),y
        sta (ptr2),y
@c1sk:
        ; --- Cell 2 (column g_col+2): doubled bits 14..20 ------------------
        ; bits 14..20 = ((m2 << 2) & $7C) | ((m1 >> 6) & $03)
        ; m2 bit 5 (= doubled bit 21) belongs to cell 3, so $7C drops it.
        lda gbc_m1
        lsr a
        lsr a
        lsr a
        lsr a
        lsr a
        lsr a                  ; A = m1 >> 6 (low 2 bits = m1 bits 6..7)
        sta gr_or              ; partial cell 2 in bits 0..1
        lda gbc_m2
        asl a
        asl a                  ; A = m2 << 2 (m2 bit 5 spilled to bit 7)
        and #$7C               ; keep bits 2..6 only
        ora gr_or
        beq @c2sk
        and gbc_car2
        ora _gen2_z_hi
        sta gr_or
        ldy _gen2_g_col
        iny
        iny
        ora (ptr1),y
        sta (ptr1),y
        lda gr_or
        ora (ptr2),y
        sta (ptr2),y
@c2sk:
        ; --- Cell 3 (column g_col+3): doubled bit 21 only, set iff bit = 6 -
        ; (m2 << 0 ... actually m2 bits 5..7, but bits 6..7 stay 0 by design)
        lda gbc_m2
        lsr a
        lsr a
        lsr a
        lsr a
        lsr a                  ; A = m2 >> 5 (low 3 bits, but bits 1..2 = 0)
        and #$07
        beq @c3sk
        and gbc_car3
        ora _gen2_z_hi
        sta gr_or
        ldy _gen2_g_col
        iny
        iny
        iny
        ora (ptr1),y
        sta (ptr1),y
        lda gr_or
        ora (ptr2),y
        sta (ptr2),y
@c3sk:
        inc rowcnt
        lda rowcnt
        cmp #8
        beq @cdone
        jmp @crow
@cdone: rts

; 4-bit -> 8-bit doubled bit LUT: dbl4tbl[n] has each of n's bits 0..3
; replicated horizontally (bit k of n -> bits 2k and 2k+1 of the entry).
_gen2_dbl4tbl:
        .byte $00, $03, $0C, $0F, $30, $33, $3C, $3F
        .byte $C0, $C3, $CC, $CF, $F0, $F3, $FC, $FF

; --- _gen2_puts_run : draw a whole NUL-terminated string in ONE asm loop -------
; Replaces the per-character C loop of gen2_hgr_puts / gen2_hgr_puts_color. Walks
; gen2_t_s, drawing at most gen2_t_n glyph cells (the C wrapper precomputes how
; many fit from x, so there is NO per-char 16-bit clip here). Per glyph: form the
; font address font+(c-$20)*8, set gen2_g_col/mask, then JSR the white or colour
; blitter — chosen ONCE per char via gen2_t_color (not per pixel: a per-pixel
; dispatch would cost more than keeping the two blitters separate saves). Then
; step the 18px pen (bit+=4, col+=2, +1 col on wrap). gen2_g_y set by the caller.
_gen2_puts_run:
@loop:
        lda _gen2_t_n
        beq @done
        ldy #0
        lda (_gen2_t_s),y       ; c = *s
        bne @go
@done:
        rts
@go:
        cmp #$20                ; clamp non-printable -> space
        bcc @space
        cmp #$80
        bcc @okc
@space:
        lda #$20
@okc:
        sec                     ; gen2_g_glyph = font + (c-$20)*8
        sbc #$20
        sta tmp1
        lda #0
        sta tmp2
        asl tmp1
        rol tmp2
        asl tmp1
        rol tmp2
        asl tmp1
        rol tmp2
        lda tmp1
        clc
        adc _gen2_t_font
        sta _gen2_g_glyph
        lda tmp2
        adc _gen2_t_font+1
        sta _gen2_g_glyph+1
        lda _gen2_t_col
        sta _gen2_g_col
        ldx _gen2_t_bit         ; gen2_g_mask = 1 << bit
        lda #1
@mk:
        dex
        bmi @mkd
        asl a
        jmp @mk
@mkd:
        sta _gen2_g_mask
        lda _gen2_t_color       ; draw white or colour
        beq @white
        jsr _gen2_blit_glyph_color
        jmp @adv
@white:
        jsr _gen2_blit_glyph
@adv:
        lda _gen2_t_bit         ; pen: bit += 4 (+ carry into col on wrap)
        clc
        adc #4
        cmp #7
        bcc @nowrap
        sbc #7                  ; carry set by cmp (>=7): A = bit+4-7
        sta _gen2_t_bit
        lda _gen2_t_col
        clc
        adc #3                  ; col += 2, +1 for the bit wrap
        sta _gen2_t_col
        jmp @adv2
@nowrap:
        sta _gen2_t_bit
        lda _gen2_t_col
        clc
        adc #2
        sta _gen2_t_col
@adv2:
        inc _gen2_t_s           ; ++s
        bne @sok
        inc _gen2_t_s+1
@sok:
        dec _gen2_t_n
        jmp @loop

; --- _gen2_blit_glyph8 : draw the 8x8 glyph at NATIVE size (no pixel doubling) -
; The Beautiful Boot font is 7px wide (bit 0 = leftmost, bit 7 always 0), so each
; glyph is a 7px cell; gen2_puts_run8 advances the pen 8px (one blank column =
; inter-char gap). One source bit -> one screen pixel on ONE scanline (vs.
; gen2_blit_glyph's 2x2 doubling). Same gen2_g_glyph/col/mask/y param block.
_gen2_blit_glyph8:
        lda #0
        sta rowcnt
@rowloop:
        ; ptr1 = gen2_row(g_y + row)   (single scanline — no paired ptr2)
        lda rowcnt
        clc
        adc _gen2_g_y
        tay
        lda _gen2_rowlo,y
        sta ptr1
        lda _gen2_rowhi,y
        sta ptr1+1
        ; load this row's 8 source bits
        ldy rowcnt
        lda (_gen2_g_glyph),y
        sta curbits
        ; reset pen to the glyph's left edge
        lda _gen2_g_col
        sta curcol
        lda _gen2_g_mask
        sta curmask
        ldx #8                ; 8 bits (bit 7 is blank -> a no-op plot)
@bitloop:
        lsr curbits           ; bit 0 (leftmost) -> carry
        bcc @skip
        ldy curcol            ; lit: one pixel on this scanline
        lda curmask
        ora (ptr1),y
        sta (ptr1),y
@skip:
        asl curmask           ; advance the pen one pixel
        lda curmask
        cmp #$80
        bne @nextbit
        lda #$01
        sta curmask
        inc curcol
@nextbit:
        dex
        bne @bitloop
        inc rowcnt
        lda rowcnt
        cmp #8
        bne @rowloop
        rts

; --- _gen2_puts_run8 : native 8x8 string render (8px pitch) ------------------
; Same param block as _gen2_puts_run (gen2_t_col/bit/n/s/font); draws each glyph
; with gen2_blit_glyph8 and steps the pen 8px (= 1 byte column + 1 bit).
_gen2_puts_run8:
@loop:
        lda _gen2_t_n
        beq @done
        ldy #0
        lda (_gen2_t_s),y       ; c = *s
        bne @go
@done:
        rts
@go:
        cmp #$20                ; clamp non-printable -> space
        bcc @space
        cmp #$80
        bcc @okc
@space:
        lda #$20
@okc:
        sec                     ; gen2_g_glyph = font + (c-$20)*8
        sbc #$20
        sta tmp1
        lda #0
        sta tmp2
        asl tmp1
        rol tmp2
        asl tmp1
        rol tmp2
        asl tmp1
        rol tmp2
        lda tmp1
        clc
        adc _gen2_t_font
        sta _gen2_g_glyph
        lda tmp2
        adc _gen2_t_font+1
        sta _gen2_g_glyph+1
        lda _gen2_t_col
        sta _gen2_g_col
        ldx _gen2_t_bit         ; gen2_g_mask = 1 << bit
        lda #1
@mk:
        dex
        bmi @mkd
        asl a
        jmp @mk
@mkd:
        sta _gen2_g_mask
        jsr _gen2_blit_glyph8
        ; pen += 8px: bit += 1 (+ carry into col on the 7px wrap)
        lda _gen2_t_bit
        clc
        adc #1
        cmp #7
        bcc @nowrap
        sbc #7                  ; carry set by cmp (>=7): A = bit+1-7
        sta _gen2_t_bit
        lda _gen2_t_col
        clc
        adc #2                  ; col += 1, +1 for the bit wrap
        sta _gen2_t_col
        jmp @adv2
@nowrap:
        sta _gen2_t_bit
        lda _gen2_t_col
        clc
        adc #1
        sta _gen2_t_col
@adv2:
        inc _gen2_t_s           ; ++s
        bne @sok
        inc _gen2_t_s+1
@sok:
        dec _gen2_t_n
        jmp @loop

; --- _gen2_utoa : 16-bit unsigned (gen2_u_lo/hi) -> decimal ASCII at gen2_u_ptr
; NUL-terminated, no leading zeros ("0" for zero). Division-free: subtract each
; power of ten as many times as it fits. Replaces cc65's 16-bit software /10+%10.
_gen2_utoa:
        ldx #0                  ; power index 0..4
        lda #0
        sta tmp3                ; tmp3 != 0 once a digit has been emitted
        ldy #0                  ; output index
@pw:
        lda #0
        sta tmp4                ; digit for this power
@sub:
        lda _gen2_u_lo          ; value -= power (16-bit), while it fits
        sec
        sbc utoa_pw_lo,x
        sta tmp1
        lda _gen2_u_hi
        sbc utoa_pw_hi,x
        bcc @pwdone             ; borrow -> value < power -> next power
        sta _gen2_u_hi
        lda tmp1
        sta _gen2_u_lo
        inc tmp4
        jmp @sub
@pwdone:
        lda tmp4                ; emit if nonzero, already-emitting, or units
        bne @emit
        lda tmp3
        bne @emit
        cpx #4
        bne @skip
@emit:
        lda tmp4
        ora #$30
        sta (_gen2_u_ptr),y
        iny
        lda #1
        sta tmp3
@skip:
        inx
        cpx #5
        bne @pw
        lda #0
        sta (_gen2_u_ptr),y     ; NUL terminator
        rts

utoa_pw_lo: .byte $10, $E8, $64, $0A, $01   ; 10000, 1000, 100, 10, 1  (low byte)
utoa_pw_hi: .byte $27, $03, $00, $00, $00   ; 10000, 1000, 100, 10, 1  (high byte)

; --- _gen2_blit_run : blit a 1bpp MSB-first sprite, mode SET / CLEAR / XOR -----
; HIRES packs 7 pixels/byte (bit 7 = palette), so we cannot shift-and-OR bytes:
; we walk pixel by pixel, reusing the proven pen (`advance`, curcol=tmp1/curmask=
; tmp2, bits 0..6 only — bit 7 is never written, palette preserved). Per source
; row: load the MSB-first bits into a shift register, draw gen2_b_w pixels (the C
; wrapper right-clips so the pen never leaves col 0..39), then step source by
; gen2_b_stride and y by 1. XOR mode makes a moving sprite erasable by re-blitting
; (draw, then draw again at the old spot) — no save/restore, no flicker.
_gen2_blit_run:
@row:
        ldy _gen2_b_y           ; ptr1 = scanline base
        lda _gen2_rowlo,y
        sta ptr1
        lda _gen2_rowhi,y
        sta ptr1+1
        lda _gen2_b_col         ; reset pen to the sprite's left edge
        sta tmp1                ; curcol
        lda _gen2_b_mask
        sta tmp2                ; curmask
        ldy #0                  ; load first source byte of this row
        lda (_gen2_b_src),y
        sta tmp3                ; srcbits (MSB-first shift register)
        lda #8
        sta tmp4                ; bits left in srcbits
        sty b_srcidx            ; srcidx = 0
        ldx _gen2_b_w           ; pixels this row
@px:
        asl tmp3                ; C = next pixel (bit 7 first)
        bcc @skip
        jsr blit_apply          ; pixel set: SET/CLEAR/XOR + advance pen
        jmp @after
@skip:
        jsr advance             ; pixel clear: just advance the pen
@after:
        dec tmp4                ; consumed a source bit
        bne @next
        inc b_srcidx            ; byte exhausted -> next source byte
        ldy b_srcidx
        lda (_gen2_b_src),y
        sta tmp3
        lda #8
        sta tmp4
@next:
        dex
        bne @px
        clc                     ; next source row + next scanline
        lda _gen2_b_src
        adc _gen2_b_stride
        sta _gen2_b_src
        bcc @nyc
        inc _gen2_b_src+1
@nyc:
        inc _gen2_b_y
        dec _gen2_b_h
        bne @row
        rts

; apply one pixel at (curcol=tmp1, curmask=tmp2) on scanline ptr1, then advance.
blit_apply:
        ldy tmp1                ; curcol
        lda _gen2_b_mode
        beq @bset
        cmp #2
        beq @bxor
        lda tmp2                ; CLEAR: dest &= ~mask (keeps the palette bit)
        eor #$FF
        and (ptr1),y
        sta (ptr1),y
        jmp advance
@bset:
        lda tmp2                ; SET: dest |= mask
        ora (ptr1),y
        sta (ptr1),y
        jmp advance
@bxor:
        lda tmp2                ; XOR: dest ^= mask
        eor (ptr1),y
        sta (ptr1),y
        jmp advance

; --- _gen2_blit7_run : BYTE-ALIGNED blit of a 7px/byte sprite (fast path) ------
; The source is pre-packed in the framebuffer's own 7px/byte layout (bit 0 =
; leftmost, bit 7 = 0), so each source byte XOR/OR/ANDs STRAIGHT into a byte
; column — no per-pixel mask walk, no cross-byte shift. The pen is therefore
; byte-aligned: the C wrapper passes b_col = x/7. For a solid sprite this is ~7x
; fewer memory ops than _gen2_blit_run (one op per 7px byte vs one per pixel).
; Reuses the gen2_b_* block: b_col start byte column, b_w bytes drawn per row,
; b_stride source bytes per row, b_h rows, b_y top scanline, b_src source,
; b_mode 0 SET / 1 CLEAR / 2 XOR. Y indexes src[j] AND dest[col+j] together
; (ptr1 already points at rowbase+col), so one counter drives the whole row.
_gen2_blit7_run:
@row:
        ldy _gen2_b_y           ; ptr1 = rowbase(y) + col
        lda _gen2_rowlo,y
        clc
        adc _gen2_b_col
        sta ptr1
        lda _gen2_rowhi,y
        adc #0
        sta ptr1+1
        ldy #0                  ; Y = byte index within the row (src and dest)
@col:
        lda (_gen2_b_src),y
        ldx _gen2_b_mode
        beq @oset
        cpx #2
        beq @oxor
        eor #$FF                ; CLEAR: dest &= ~src
        and (ptr1),y
        sta (ptr1),y
        jmp @cnext
@oset:
        ora (ptr1),y            ; SET: dest |= src
        sta (ptr1),y
        jmp @cnext
@oxor:
        eor (ptr1),y            ; XOR: dest ^= src
        sta (ptr1),y
@cnext:
        iny
        cpy _gen2_b_w
        bne @col
        clc                     ; src += stride ; y += 1 ; rows--
        lda _gen2_b_src
        adc _gen2_b_stride
        sta _gen2_b_src
        bcc @nyc
        inc _gen2_b_src+1
@nyc:
        inc _gen2_b_y
        dec _gen2_b_h
        bne @row
        rts

; --- _gen2_hgr_clear(fill in A) : fill the whole HIRES page-1 framebuffer ------
; Clears/fills $2000-$3FFF (32 pages x 256 bytes) with the byte in A. The byte
; stays in A for every STA, so nothing reloads it; ptr1 walks the pages by its
; high byte ($20..$3F). This is the explicit asm form of the old cc65 C loop —
; the tightest 8 KB fill the 6502 can do (STA (ptr),Y / INY / BNE).
_gen2_hgr_clear:
        ldx _gen2_hgr_base   ; draw-page high byte ($20 page1 / $40 page2)
        stx ptr1+1
        ldy #$00
        sty ptr1             ; ptr1 = base<<8, Y = 0
        ldx #$20             ; 32 pages = 8 KB (page-base independent)
@page:
        sta (ptr1),y         ; store the fill byte (A preserved throughout)
        iny
        bne @page            ; 256 bytes of this page (Y wraps back to 0)
        inc ptr1+1           ; next page
        dex
        bne @page            ; 32 pages -> done (8 KB framebuffer)
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
; Note: every keep mask is ANDed with $7F so bit 7 (the byte's HIRES palette
; bit) is ALWAYS cleared on both edges — without this, a previous draw's
; orange/blue palette would survive a clear and ghost into the next colour cycle
; (the row-at-a-time colour blitter only sets the palette bit when hi != 0).
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
        lda tmp1             ; clear: keepL = ~mask & $7F (also drops palette bit)
        eor #$ff
        and #$7f
        sta gr_keepL
        lda #0
        sta gr_setL
        jmp @fill
@one_set:
        lda #$7f             ; fill: keepL = $7F (clears palette), setL = mask
        sta gr_keepL
        lda tmp1
        sta gr_setL
        jmp @fill
@multi:
        lda _gen2_r_mode
        bne @multi_set
        ; clear: keep = ~mask & $7F (drop palette too), set/full = 0
        lda tmp1
        eor #$ff
        and #$7f
        sta gr_keepL
        lda tmp2
        eor #$ff
        and #$7f
        sta gr_keepR
        lda #0
        sta gr_setL
        sta gr_setF
        sta gr_setR
        jmp @fill
@multi_set:
        lda #$7f             ; fill: clear palette + OR edge masks, full = $7F
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

; --- _gen2_colorize_asm : tint an already-drawn (white) glyph region ----------
; Apple-1 GEN2 HIRES has no per-pixel colour — colour is an NTSC artifact of the
; bit pattern + the byte's high bit. So colour text is drawn WHITE first, then
; this pass masks each byte down to the chosen colour's carrier and sets the high
; bit: b = (b & carrier[col&1]) | hibit. carrier alternates by ABSOLUTE byte
; column parity (the artifact phase flips every 7px byte). See gen2_hgr_puts_color.
_gen2_colorize_asm:
        lda _gen2_z_y0
        sta tmp1             ; current scanline
        lda _gen2_z_rows
        sta tmp2             ; rows remaining
@zrow:
        ldy tmp1
        lda _gen2_rowlo,y
        sta ptr1
        lda _gen2_rowhi,y
        sta ptr1+1
        ldx _gen2_z_ncols    ; column counter
        ldy _gen2_z_col0     ; current (absolute) byte column
@zcol:
        tya                  ; carrier = (col & 1) ? co : ce
        and #1
        bne @zodd
        lda _gen2_z_ce
        jmp @zhave
@zodd:
        lda _gen2_z_co
@zhave:
        and (ptr1),y         ; keep only the carrier bits of the white glyph
        ora _gen2_z_hi       ; set the palette high bit (orange/blue) or nothing
        sta (ptr1),y
        iny
        dex
        bne @zcol
        inc tmp1             ; next scanline
        dec tmp2
        bne @zrow
        rts
