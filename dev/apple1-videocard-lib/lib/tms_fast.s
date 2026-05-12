; ----------------------------------------------------------------------------
; apple1-videocard-lib — POM1 CodeTank cc65 port
; Derived from nippur72/apple1-videocard-lib (Antonino "Nino" Porcino).
;   https://github.com/nippur72/apple1-videocard-lib
; Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
; ----------------------------------------------------------------------------
;
; tms_fast.s — VRAM fast paths (ca65)
;
; Tight burst loops, no per-byte TMS_IO_DELAY: matches Nino's upstream KickC
; cadence. The POM1 silicon-strict model accepts back-to-back $CC00 writes
; outside VBLANK contention; if you need strict per-byte pacing on real
; hardware, fall back to the C variants in tms9918.c (tms_copy_to_vram).
;
; cc65 calling convention reminder
;   - leftmost args pushed on cc65 software stack (sp/sp+1),
;   - last arg in A (byte) or A/X (word low/high).
;   - retrieve via popa / popax in reverse order.

.export _tms_fill_vram
.export _tms_copy_to_vram_fast
.export _tms_shadow_flush

.import popa, popax
.importzp ptr1

.import _tms_sprite_shadow

VDP_DATA = $CC00
VDP_REG  = $CC01

WRITE_TO_VRAM_BITS = $40
HI_ADDR_MASK       = $3F

; -----------------------------------------------------------------
; Scratch locals (BSS so we don't trample cc65 ZP)
; -----------------------------------------------------------------
.bss
fill_val:       .res 1
size_lo:        .res 1
size_hi:        .res 1

.code

; -----------------------------------------------------------------
; void __cdecl__ tms_fill_vram(unsigned addr, unsigned char val, unsigned count);
;
;   addr   — VRAM destination (14-bit).
;   val    — byte written `count` times.
;   count  — 0..65535. count=0 is a no-op.
; -----------------------------------------------------------------
_tms_fill_vram:
        ; AX = count (last arg, word)
        sta     size_lo
        stx     size_hi

        jsr     popa             ; A = val
        sta     fill_val

        jsr     popax            ; AX = addr (lo=A, hi=X)
        sta     VDP_REG          ; low addr
        txa
        and     #HI_ADDR_MASK
        ora     #WRITE_TO_VRAM_BITS
        sta     VDP_REG          ; high addr | $40 (latch write)

        ldy     fill_val

        ; full-page loop (256 bytes at a time)
        lda     size_hi
        beq     @tail_only
@full_pages:
        ldx     #0
@page_inner:
        sty     VDP_DATA
        inx
        bne     @page_inner
        dec     size_hi
        bne     @full_pages

@tail_only:
        ldx     size_lo
        beq     @done
@tail_loop:
        sty     VDP_DATA
        dex
        bne     @tail_loop

@done:
        rts


; -----------------------------------------------------------------
; void __cdecl__ tms_copy_to_vram_fast(const void *src, unsigned size, unsigned dest);
;
;   src   — host RAM (or ROM) pointer.
;   size  — 0..65535 bytes.
;   dest  — VRAM destination (14-bit).
;
; Equivalent to tms_copy_to_vram() in tms9918.c, ~3-4x faster (no IO delay,
; no inner increment overhead from cc65).
; -----------------------------------------------------------------
_tms_copy_to_vram_fast:
        ; AX = dest (word, last arg)
        sta     VDP_REG
        txa
        and     #HI_ADDR_MASK
        ora     #WRITE_TO_VRAM_BITS
        sta     VDP_REG

        jsr     popax            ; AX = size
        sta     size_lo
        stx     size_hi

        jsr     popax            ; AX = src
        sta     ptr1
        stx     ptr1+1

        ldy     #0

        lda     size_hi
        beq     @tail_only

@full_pages:
        lda     (ptr1),y
        sta     VDP_DATA
        iny
        bne     @full_pages
        inc     ptr1+1
        dec     size_hi
        bne     @full_pages

@tail_only:
        ldx     size_lo
        beq     @done
        ldy     #0
@tail_loop:
        lda     (ptr1),y
        sta     VDP_DATA
        iny
        dex
        bne     @tail_loop

@done:
        rts


; -----------------------------------------------------------------
; void __cdecl__ tms_shadow_flush(void);
;
; Copy 128 bytes from tms_sprite_shadow[] to VRAM SAT ($3B00).
; Call inside VBLANK (after tms_wait_end_of_frame) for tear-free updates.
; -----------------------------------------------------------------
_tms_shadow_flush:
        lda     #$00
        sta     VDP_REG
        lda     #($3B | WRITE_TO_VRAM_BITS)
        sta     VDP_REG

        ldx     #0
@loop:
        lda     _tms_sprite_shadow,x
        sta     VDP_DATA
        inx
        cpx     #128
        bne     @loop
        rts
