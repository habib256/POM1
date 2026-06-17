; ============================================================================
; subbyte_fill.asm -- 4-pixel sub-byte block fill via 7-phase mask LUTs
; ============================================================================
;   subbyte_fill_4 -- OR a 4-pixel-wide block over 4 scanlines into the
;                     HGR framebuffer. Used for thin walls, narrow tiles,
;                     dotted lines — anything that doesn't align on the
;                     7 px byte boundary.
;
;     Inputs:   X            = grid column gx (0..68)
;               sb_ptr_lo:hi = scanline address of the FIRST row of the
;                              4-row block (the +$400 stride between
;                              consecutive rows in an HGR group is added
;                              automatically).
;
;     Output:   read-modify-write OR over 4 scanlines, 1 or 2 bytes wide
;               depending on the gx%7 phase.
;
;     Clobbers: A, X, Y. The caller's `tmp`/`tmp2` slots are used as
;               scratch for the two masks across the 4-row inner loop.
;
;     Constraint: the 4 scanlines must lie in the SAME HGR group (Apple
;       II non-linear layout). Within a group, consecutive scanlines are
;       at +$400. Crossing a group boundary needs full hgr_lo/hi lookup
;       per row — see dev/lib/hgr/hgr_tables.inc for the table.
;
; ZP usage:
;   sb_ptr_lo, sb_ptr_hi  -- caller-declared pointer (alias to your own
;                            scanline pointer slots before the include).
;   tmp, tmp2             -- imported via lib/apple1/zp.inc.
;
; To use without zp.inc, declare tmp/tmp2 in your project ZEROPAGE.
;
; Caller responsibility: include subbyte4.inc BEFORE this module (the
; mask LUTs sb4_byte_off/sb4_mask1/sb4_mask2 must be in scope).
; ============================================================================

.ifndef sb_ptr_lo
.segment "ZEROPAGE"
sb_ptr_lo:      .res 1
sb_ptr_hi:      .res 1
.endif

.segment "CODE"

subbyte_fill_4:
        ; Decode the 3 LUTs for this gx into tmp / tmp2 / Y.
        LDA     sb4_mask1,X
        STA     tmp                     ; tmp = mask1
        LDA     sb4_mask2,X
        STA     tmp2                    ; tmp2 = mask2 (0 → single byte)
        LDA     sb4_byte_off,X
        TAY                             ; Y = byte offset within scanline

        LDX     #4                      ; 4 scanlines per block
@row:
        ; First byte: byte[Y] |= tmp
        LDA     (sb_ptr_lo),Y
        ORA     tmp
        STA     (sb_ptr_lo),Y

        ; Second byte: only if mask2 ≠ 0
        LDA     tmp2
        BEQ     @no_s
        INY
        LDA     (sb_ptr_lo),Y
        ORA     tmp2
        STA     (sb_ptr_lo),Y
        DEY
@no_s:
        ; Advance scanline pointer by +$0400 (Apple-II HGR within-group stride)
        LDA     sb_ptr_hi
        CLC
        ADC     #$04
        STA     sb_ptr_hi

        DEX
        BNE     @row
        RTS
