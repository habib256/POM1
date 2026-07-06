; gen2_hgr_x2.s — hand-asm gen2_hgr_inflate_x2 (mono x1 -> x2 single-colour).
; -----------------------------------------------------------------------------
; The C form of this function (gen2_hgr_x2.c, kept as the HOST reference for the
; hgr_inflate_x2_smoke cross-check) is MISCOMPILED by cc65 2.18 -Oirs: the
; optimiser drops the whole pixel-setting body, so every x2 sprite inflates to
; all-zero on the 6502. Rather than fight the optimiser with #pragma optimize
; (correct but slow naive codegen), the target build uses this hand-asm: fast,
; and immune to the miscompile. Pinned on-target by dev/lib/test/micro/
; t13_c_inflate_x2.c (ctest lib_micro_tests) and cross-checked against the host
; C / the editor's magnifyColor2x by hgr_inflate_x2_smoke.
;
; void gen2_hgr_inflate_x2(const unsigned char *mono, unsigned char wbytes,
;                          unsigned char h, unsigned char color,
;                          unsigned char *out);
;
; cc65 __near__ ABI (verified by disassembling a probe): the last arg `out`
; arrives in A/X; after `jsr pushax` the C stack holds, from (sp):
;   0-1 out   2 color   3 h   4 wbytes   5-6 mono
;
; Algorithm (byte-identical to the C / magnifyColor2x): each lit mono pixel at
; source column sx lights the two doubled dots at columns 2*sx (if litLeft) and
; 2*sx+1 (if litRight) on BOTH doubled rows, OR'ing in the palette bit. No
; divides: source (byte sb, mask sm) and dest-left (byte db, bit dbit) positions
; are stepped incrementally; the two dest rows share one pointer (bot[b] is
; top[b+dW] since dW+b < 256). Each row-pair is zeroed just before it is filled.

.export     _gen2_hgr_inflate_x2
.import     pushax, incsp7
.importzp   sp
.macpack    longbranch          ; jeq/jne/jcs/jcc for the long cross-body branches

; GEN2_X2_* colour ids (must match gen2.h)
X2_WHITE  = 1
X2_VIOLET = 2
X2_GREEN  = 3
X2_BLUE   = 4
X2_ORANGE = 5

.segment "ZEROPAGE"
gx2_m:    .res 2          ; current mono row pointer
gx2_top:  .res 2          ; current top doubled-row pointer (bot = top + dW)
gx2_col:  .res 1          ; colour id
gx2_pal:  .res 1          ; palette bit ($00 or $80)
gx2_litL: .res 1          ; light the left dot?  (0/1)
gx2_litR: .res 1          ; light the right dot? (0/1)
gx2_wb:   .res 1          ; wbytes
gx2_h:    .res 1          ; rows
gx2_dW:   .res 1          ; wbytes*2 (doubled byte width)
gx2_tdw:  .res 1          ; wbytes*4 (= 2*dW, bytes per row-pair)
gx2_wpx:  .res 1          ; wbytes*7 (source columns)
gx2_sy:   .res 1          ; source row counter
gx2_sx:   .res 1          ; source column counter
gx2_sb:   .res 1          ; source byte index within row
gx2_sm:   .res 1          ; source bit mask (1<<sbit)
gx2_db:   .res 1          ; dest left byte index
gx2_dbit: .res 1          ; dest left bit index (0..6)
gx2_rb:   .res 1          ; dest right byte index (scratch)
gx2_orv:  .res 1          ; OR value for the current dot (mask|pal)

.segment "RODATA"
gx2_pow2: .byte 1,2,4,8,16,32,64,128

.segment "CODE"
_gen2_hgr_inflate_x2:
        jsr     pushax                  ; out -> (sp),0-1

        ; --- read args off the C stack ---
        ldy     #0
        lda     (sp),y
        sta     gx2_top                 ; top = out (low)
        ldy     #1
        lda     (sp),y
        sta     gx2_top+1
        ldy     #2
        lda     (sp),y
        sta     gx2_col
        ldy     #3
        lda     (sp),y
        sta     gx2_h
        ldy     #4
        lda     (sp),y
        sta     gx2_wb
        ldy     #5
        lda     (sp),y
        sta     gx2_m                   ; mono (low)
        ldy     #6
        lda     (sp),y
        sta     gx2_m+1

        ; --- derived widths ---
        lda     gx2_wb
        asl     a
        sta     gx2_dW                  ; dW  = wb*2
        asl     a
        sta     gx2_tdw                 ; tdw = wb*4 (= 2*dW)
        lda     gx2_wb                  ; wpx = wb*7 = wb*8 - wb
        asl     a
        asl     a
        asl     a
        sec
        sbc     gx2_wb
        sta     gx2_wpx

        ; --- decode colour -> litL / litR / pal ---
        lda     #0
        sta     gx2_litL
        sta     gx2_litR
        sta     gx2_pal
        lda     gx2_col
        cmp     #X2_WHITE
        bne     @c_nw
        lda     #1
        sta     gx2_litL
        sta     gx2_litR
        jmp     @c_done
@c_nw:  cmp     #X2_VIOLET
        bne     @c_ng
        lda     #1
        sta     gx2_litL
        jmp     @c_done
@c_ng:  cmp     #X2_GREEN
        bne     @c_nb
        lda     #1
        sta     gx2_litR
        jmp     @c_done
@c_nb:  cmp     #X2_BLUE
        bne     @c_no
        lda     #1
        sta     gx2_litL
        lda     #$80
        sta     gx2_pal
        jmp     @c_done
@c_no:  cmp     #X2_ORANGE
        bne     @c_done
        lda     #1
        sta     gx2_litR
        lda     #$80
        sta     gx2_pal
@c_done:

        ; --- rows ---
        lda     #0
        sta     gx2_sy
@row:
        lda     gx2_sy
        cmp     gx2_h
        jcs     @finish                 ; sy >= h -> done

        ; zero this row-pair: (top),0 .. (top),tdw-1
        ldy     #0
        lda     #0
@zero:  sta     (gx2_top),y
        iny
        cpy     gx2_tdw
        bne     @zero

        ; init source & dest bit positions
        lda     #0
        sta     gx2_sb
        sta     gx2_db
        sta     gx2_dbit
        sta     gx2_sx
        lda     #1
        sta     gx2_sm                  ; source bit 0

@col:
        lda     gx2_sx
        cmp     gx2_wpx
        jcs     @rowadv                 ; sx >= wpx -> next row

        ; lit = mono[sb] & sm ?
        ldy     gx2_sb
        lda     (gx2_m),y
        and     gx2_sm
        jeq     @advance                ; not lit

        ; --- LEFT dot: byte db, bit dbit ---
        lda     gx2_litL
        jeq     @noleft
        ldx     gx2_dbit
        lda     gx2_pow2,x
        ora     gx2_pal
        sta     gx2_orv
        ldy     gx2_db
        lda     (gx2_top),y
        ora     gx2_orv
        sta     (gx2_top),y             ; top[db]
        tya
        clc
        adc     gx2_dW
        tay                             ; y = db + dW
        lda     (gx2_top),y
        ora     gx2_orv
        sta     (gx2_top),y             ; bot[db]
@noleft:

        ; --- RIGHT dot: column dc+1 ---
        lda     gx2_litR
        jeq     @noright
        lda     gx2_dbit
        cmp     #6
        bne     @r_same
        ; dbit == 6: right dot is bit 0 of the next byte
        ldx     #0
        lda     gx2_db
        clc
        adc     #1
        sta     gx2_rb
        jmp     @r_mask
@r_same:
        ; right dot is bit dbit+1 of the same byte
        ldx     gx2_dbit
        inx
        lda     gx2_db
        sta     gx2_rb
@r_mask:
        lda     gx2_pow2,x
        ora     gx2_pal
        sta     gx2_orv
        ldy     gx2_rb
        lda     (gx2_top),y
        ora     gx2_orv
        sta     (gx2_top),y             ; top[rb]
        tya
        clc
        adc     gx2_dW
        tay
        lda     (gx2_top),y
        ora     gx2_orv
        sta     (gx2_top),y             ; bot[rb]
@noright:

@advance:
        ; source bit: 1,2,4,...,64 then wrap to next byte (7 bits/byte)
        asl     gx2_sm
        lda     gx2_sm
        cmp     #$80
        bne     @no_sbwrap
        lda     #1
        sta     gx2_sm
        inc     gx2_sb
@no_sbwrap:
        ; dest left bit: += 2, wrap at 7 into next byte
        lda     gx2_dbit
        clc
        adc     #2
        cmp     #7
        bcc     @no_dbwrap
        sbc     #7                      ; carry set here -> subtract 7
        inc     gx2_db
@no_dbwrap:
        sta     gx2_dbit
        inc     gx2_sx
        jmp     @col

@rowadv:
        ; m += wbytes
        lda     gx2_m
        clc
        adc     gx2_wb
        sta     gx2_m
        bcc     @m_nc
        inc     gx2_m+1
@m_nc:
        ; top += tdw (2*dW)
        lda     gx2_top
        clc
        adc     gx2_tdw
        sta     gx2_top
        bcc     @t_nc
        inc     gx2_top+1
@t_nc:
        inc     gx2_sy
        jmp     @row

@finish:
        jmp     incsp7                  ; drop 7 arg bytes + rts
