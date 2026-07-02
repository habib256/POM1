; gen2_sprmask.s -- masked pre-shifted sprite kernels for GEN2 HGR (cc65).
;
; The SPRMASK family: the save-under complement to the XOR pre-shift engine
; (gen2_blit.s _gen2_xs_run). XOR is self-erasing but TRASHES a non-uniform
; background while the sprite overlaps it (decor shows through the sprite as
; toggled garbage). These kernels instead do a MASKED blit
;
;     dst[j] = (dst[j] & mask[j]) | data[j]
;
; over a 7-phase pre-shifted bank (gen2_mspr_t in gen2.h: `data` = sprite
; pixels, bit 7 clear; `mask` = ~coverage with bit 7 ALWAYS 1, so the
; background byte's HIRES palette-group bit survives every draw), paired with
; a save-under/restore of the covered byte rectangle. Draw over anything,
; restore, and the background is byte-identical -- what the sprite ENGINE
; (gen2_sprengine.c) is built on.
;
; All four entries share one setup (ms_setup): column x/7 and phase x%7 come
; from the gen2_col7[]/gen2_phase7[] LUTs (no runtime divide -- same trick as
; _gen2_xs_run, including the +256 split for x >= 256), the gen2_mspr_t is
; dereferenced, the phase offset phase*(h*stride) is applied to BOTH the data
; and mask banks by loop-add (no software multiply), and the sprite is
; right-clipped (col+stride > 40) and bottom-clipped (y+h > 192) exactly like
; _gen2_xs_run. Scanline bases come from _gen2_rowlo/_gen2_rowhi, which the C
; runtime keeps pointed at the CURRENT DRAW PAGE (gen2_set_draw_page rewrites
; the hi table), so these kernels are page-agnostic.
;
; The under-buffer is a plain linear stride*h byte block: row r of the sprite
; rectangle occupies under[r*stride .. r*stride+w) (w = clipped width; the
; clipped tail bytes of a row are simply never touched). Save and restore walk
; it with the SAME setup, so a restore at the same (x, y, spr) is exact.
;
; Interface -- the C wrapper (gen2_sprengine.c) stores these, then JSRs:
;     _gen2_ms_x     : pixel x (0..279, 16-bit)
;     _gen2_ms_y     : pixel y (0..191)
;     _gen2_ms_spr   : pointer to the gen2_mspr_t {data, mask, stride, h}
;     _gen2_ms_under : pointer to the caller's stride*h save-under buffer
; The caller guarantees x <= 279 / y <= 191 and stride, h >= 1 (the kernels
; still edge-clip on the right/bottom).

        .export   _gen2_ms_run
        .export   _gen2_ms_save_run
        .export   _gen2_ms_restore_run
        .export   _gen2_msu_run
        .exportzp _gen2_ms_x, _gen2_ms_y, _gen2_ms_spr, _gen2_ms_under
        .import   _gen2_rowlo, _gen2_rowhi, _gen2_col7, _gen2_phase7
        .importzp ptr1, ptr2, ptr3, ptr4, tmp1, tmp2, tmp3, tmp4

; --- interface variables (zero page) ----------------------------------------
        .segment "ZEROPAGE"
_gen2_ms_x:     .res 2       ; pixel x (0..279)
_gen2_ms_y:     .res 1       ; pixel y (0..191)
_gen2_ms_spr:   .res 2       ; -> gen2_mspr_t {data lo/hi, mask lo/hi, stride, h}
_gen2_ms_under: .res 2       ; -> linear save-under buffer (stride*h bytes)
; asm-internal state (derived by ms_setup, persists across the row loop):
ms_col:    .res 1            ; start byte column (x / 7)
ms_yy:     .res 1            ; current scanline (advanced per row)
ms_w:      .res 1            ; bytes drawn per row (right-clipped)
ms_h:      .res 1            ; rows remaining (bottom-clipped)
ms_stride: .res 1            ; source bytes per row (uniform across phases)
ms_blk:    .res 2            ; phase block size = h * stride (16-bit)

; zero-page pointer roles (cc65 leaves ptr1..ptr4/tmp1..tmp4 free for leaf asm):
;   ptr1 = framebuffer row base + col (rebuilt per row from the row tables)
;   ptr2 = data row pointer  (phase-adjusted, advanced by stride per row)
;   ptr3 = mask row pointer  (phase-adjusted, advanced by stride per row)
;   ptr4 = under row pointer (advanced by stride per row)
; tmp1 = phase, tmp2 = stride, tmp3 = full (unclipped) h -- setup scratch only.

        .segment "CODE"

; --- ms_setup : shared per-call setup for all four kernels --------------------
; From the _gen2_ms_* parameter block, derive col/phase, deref the gen2_mspr_t,
; apply the phase offset to data+mask, clip, and point ptr4 at the under buffer.
ms_setup:
        ; col = gen2_col7[x] ; phase = gen2_phase7[x] (LUTs, no divide).
        ; x >= 256 (only 256..279 on a 280-px screen) indexes the tables at
        ; +256 with the low byte -- a 6502 index register is 8-bit.
        lda _gen2_ms_x+1
        bne @xhi
        ldx _gen2_ms_x
        lda _gen2_col7,x
        ldy _gen2_phase7,x
        jmp @xset
@xhi:
        ldx _gen2_ms_x
        lda _gen2_col7+256,x
        ldy _gen2_phase7+256,x
@xset:
        sta ms_col
        sty tmp1                  ; tmp1 = phase (0..6)
        ; deref spr -> read data/mask/stride/h (ptr1 is free until the row loop)
        lda _gen2_ms_spr
        sta ptr1
        lda _gen2_ms_spr+1
        sta ptr1+1
        ldy #0
        lda (ptr1),y              ; data low
        sta ptr2
        iny
        lda (ptr1),y              ; data high
        sta ptr2+1
        iny
        lda (ptr1),y              ; mask low
        sta ptr3
        iny
        lda (ptr1),y              ; mask high
        sta ptr3+1
        iny
        lda (ptr1),y              ; stride
        sta ms_stride
        sta ms_w                  ; default w = stride
        sta tmp2                  ; tmp2 = stride
        iny
        lda (ptr1),y              ; h
        sta ms_h                  ; default h
        sta tmp3                  ; tmp3 = full h (for the phase offset)
        lda _gen2_ms_y
        sta ms_yy
        ; right clip: col + stride > 40 ? w = 40 - col
        lda ms_col
        clc
        adc tmp2
        cmp #41
        bcc @wok
        lda #40
        sec
        sbc ms_col
        sta ms_w
@wok:
        ; bottom clip: y + h > 192 ? h = 192 - y
        lda _gen2_ms_y
        clc
        adc tmp3
        bcs @clipb                ; y+h >= 256 -> clip
        cmp #193
        bcc @hok
@clipb:
        lda #192
        sec
        sbc _gen2_ms_y
        sta ms_h
@hok:
        ; phase block = h * stride via loop-add (h iterations, no multiply)
        lda #0
        sta ms_blk
        sta ms_blk+1
        ldx tmp3                  ; full h
@hloop:
        clc
        lda ms_blk
        adc tmp2                  ; + stride
        sta ms_blk
        bcc @h2
        inc ms_blk+1
@h2:
        dex
        bne @hloop
        ; data += phase*blk ; mask += phase*blk (phase iterations of a 16-bit add)
        ldx tmp1                  ; phase
        beq @poff
@ploop:
        clc
        lda ptr2
        adc ms_blk
        sta ptr2
        lda ptr2+1
        adc ms_blk+1
        sta ptr2+1
        clc
        lda ptr3
        adc ms_blk
        sta ptr3
        lda ptr3+1
        adc ms_blk+1
        sta ptr3+1
        dex
        bne @ploop
@poff:
        ; ptr4 = under buffer
        lda _gen2_ms_under
        sta ptr4
        lda _gen2_ms_under+1
        sta ptr4+1
        rts

; --- ms_rowbase : ptr1 = rowbase(ms_yy) + ms_col ------------------------------
; ~24 cycles. The row tables already carry the current DRAW page in their high
; bytes (gen2_set_draw_page), so no page test is needed here.
ms_rowbase:
        ldy ms_yy
        lda _gen2_rowlo,y
        clc
        adc ms_col
        sta ptr1
        lda _gen2_rowhi,y
        adc #0
        sta ptr1+1
        rts

; --- ms_next_row : advance data/mask/under by stride, yy += 1, rows -= 1 ------
; Returns with Z clear while rows remain (callers loop on BNE). ~45 cycles.
ms_next_row:
        clc
        lda ptr2                ; data += stride
        adc ms_stride
        sta ptr2
        bcc @m
        inc ptr2+1
@m:
        clc
        lda ptr3                ; mask += stride
        adc ms_stride
        sta ptr3
        bcc @u
        inc ptr3+1
@u:
        clc
        lda ptr4                ; under += stride (linear buffer, row-major)
        adc ms_stride
        sta ptr4
        bcc @y
        inc ptr4+1
@y:
        inc ms_yy
        dec ms_h                ; Z = done (flags survive the RTS)
        rts

; --- _gen2_ms_run : masked pre-shifted blit (no save-under) -------------------
; Per byte: dst = (dst & mask) | data. Inner loop 29 cycles/byte:
;     lda (ptr1),y 5 / and (ptr3),y 5 / ora (ptr2),y 5 / sta (ptr1),y 6
;     iny 2 / cpy ms_w 3 / bne 3
; mask bit 7 = 1 by ABI, so the background's palette-group bit is preserved.
_gen2_ms_run:
        jsr ms_setup
@row:
        jsr ms_rowbase
        ldy #0
@col:
        lda (ptr1),y            ; background byte              5
        and (ptr3),y            ; & mask (1-bits KEEP)         5
        ora (ptr2),y            ; | pre-shifted sprite data    5
        sta (ptr1),y            ;                              6
        iny                     ;                              2
        cpy ms_w                ;                              3
        bne @col                ;                              3  = 29 cyc/byte
        jsr ms_next_row
        bne @row
        rts

; --- _gen2_ms_save_run : copy the covered rect framebuffer -> under buffer ----
; Straight-line copy, 19 cycles/byte (lda (ptr1),y 5 / sta (ptr4),y 6 / iny 2 /
; cpy 3 / bne 3). Row r lands at under[r*stride]; clipped tail bytes untouched.
_gen2_ms_save_run:
        jsr ms_setup
@row:
        jsr ms_rowbase
        ldy #0
@col:
        lda (ptr1),y            ; framebuffer                  5
        sta (ptr4),y            ; -> under buffer              6
        iny                     ;                              2
        cpy ms_w                ;                              3
        bne @col                ;                              3  = 19 cyc/byte
        jsr ms_next_row
        bne @row
        rts

; --- _gen2_ms_restore_run : copy the under buffer -> framebuffer --------------
; Exact inverse of _gen2_ms_save_run (19 cycles/byte). Call with the SAME
; (x, y, spr, under) the save/draw used and the background is byte-identical.
_gen2_ms_restore_run:
        jsr ms_setup
@row:
        jsr ms_rowbase
        ldy #0
@col:
        lda (ptr4),y            ; under buffer                 5
        sta (ptr1),y            ; -> framebuffer               6
        iny                     ;                              2
        cpy ms_w                ;                              3
        bne @col                ;                              3  = 19 cyc/byte
        jsr ms_next_row
        bne @row
        rts

; --- _gen2_msu_run : save-under + masked draw, ONE pass -----------------------
; The engine's hot path: per row the background byte is copied to the under
; buffer and immediately replaced by (dst & mask) | data -- one row-base
; computation, one Y walk, instead of a save pass followed by a draw pass.
; Inner loop 35 cycles/byte:
;     lda (ptr1),y 5 / sta (ptr4),y 6 / and (ptr3),y 5 / ora (ptr2),y 5 /
;     sta (ptr1),y 6 / iny 2 / cpy 3 / bne 3
; (vs 19 + 29 = 48 for separate save + draw passes, plus a second setup).
_gen2_msu_run:
        jsr ms_setup
@row:
        jsr ms_rowbase
        ldy #0
@col:
        lda (ptr1),y            ; background byte              5
        sta (ptr4),y            ; save it under                6
        and (ptr3),y            ; & mask (1-bits KEEP)         5
        ora (ptr2),y            ; | pre-shifted sprite data    5
        sta (ptr1),y            ;                              6
        iny                     ;                              2
        cpy ms_w                ;                              3
        bne @col                ;                              3  = 35 cyc/byte
        jsr ms_next_row
        bne @row
        rts
