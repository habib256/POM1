; ----------------------------------------------------------------------------
; P-LAB TMS9918 (Apple-1) — cc65 C runtime, POM1 CodeTank port
; Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
; Software: derived from Antonino "Nino" Porcino's apple1-videocard-lib
;   (https://github.com/nippur72/apple1-videocard-lib).
; Upstream license: unspecified at time of fork (2026-05). Preserve attribution.
; ----------------------------------------------------------------------------
;
; tms_fast.s — VRAM fast paths (ca65)
;
; Silicon contract (juillet 2026): back-to-back $CC00 writes are ONLY legal
; while the VDP is in a free transmission zone (display blanked, or vertical
; retrace). During active display the chip grants the CPU one VRAM slot every
; ~8 cycles worst-case (Mode I/II) — an unpadded burst loses most of its
; bytes on real silicon and under POM1 silicon-strict. The old header claim
; ("the POM1 silicon-strict model accepts back-to-back $CC00 writes outside
; VBLANK contention") was FALSE and is retired.
;
; tms_fill_vram / tms_copy_to_vram_fast therefore blank the display around
; the burst when it is enabled (reading the mirror in tms_regs_latch[1]),
; and restore it afterwards. Consequences callers must know:
;   - The remainder of the current frame shows the backdrop colour — the
;     classic real-hardware idiom for bulk uploads (best-practices §6.4
;     remedy 1). For tear-free *incremental* updates use the VBlank-chunked
;     C variants (screen1.c scroll) or tms_shadow_flush instead.
;   - A VDP register write ALSO loads the VRAM address counter's high bits
;     on real silicon (openMSX, dvik) — so the VRAM address is programmed
;     AFTER the R1 blank write, and the final R1 restore happens after the
;     last data byte, never in between.
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
.import _tms_regs_latch

VDP_DATA = $CC00
VDP_REG  = $CC01

WRITE_TO_VRAM_BITS = $40
HI_ADDR_MASK       = $3F
REG1_DISPLAY_BIT   = $40

; -----------------------------------------------------------------
; Scratch locals (BSS so we don't trample cc65 ZP)
; -----------------------------------------------------------------
.bss
fill_val:       .res 1
size_lo:        .res 1
size_hi:        .res 1
addr_lo:        .res 1
addr_hi:        .res 1
r1_save:        .res 1

.code

; ctrl_pad — 12-cycle spacer (JSR+RTS). Two calls = 24c, comfortably above
; the worst-case active-display control-port gap (~8c slot + D28 prep).
ctrl_pad:
        rts

; blank_display — write R1 with the display bit cleared, UNCONDITIONALLY,
; and leave r1_save holding the mirror's original value. Clobbers A.
;
; Why unconditional (juillet 2026): tms_regs_latch is BSS — zero until the
; program's first tms_init_regs/tms_write_reg. On a warm start (Wozmon 4000R
; re-run, CodeTank game switch) the REAL chip can still have the display ON
; from the previous program while the mirror reads $00 = "blanked": trusting
; the mirror skipped the blank and ran the unpadded burst hot — most bytes
; dropped on real silicon. Writing the blank always is fail-safe: worst case
; the display was already off and we spent one control pair. The value
; (mirror & $3F) | $80 forces 16K=1 + display OFF while preserving whatever
; M/size/mag bits the mirror knows about (zero mirror → blanked Graphics I,
; harmless for a burst).
blank_display:
        lda     _tms_regs_latch+1
        sta     r1_save
        and     #$3F                     ; keep M/size/mag bits from the mirror
        ora     #$80                     ; force 16K = 1, display OFF
        sta     VDP_REG                  ; 1st byte: R1 value (display off)
        jsr     ctrl_pad
        jsr     ctrl_pad
        lda     #$81
        sta     VDP_REG                  ; 2nd byte: write R1
        jsr     ctrl_pad
        jsr     ctrl_pad                 ; settle before the address pair
        rts

; restore_display — undo blank_display (no-op when the display was already
; blanked on entry). Runs in the free zone, minimal pacing suffices.
restore_display:
        lda     r1_save
        and     #REG1_DISPLAY_BIT
        beq     @done
        lda     r1_save
        sta     VDP_REG
        nop
        nop
        lda     #$81
        sta     VDP_REG
@done:  rts

; -----------------------------------------------------------------
; void __cdecl__ tms_fill_vram(unsigned addr, unsigned char val, unsigned count);
;
;   addr   — VRAM destination (14-bit).
;   val    — byte written `count` times.
;   count  — 0..65535. count=0 is a no-op.
;
; Blanks the display around the burst when it was enabled (see header).
; -----------------------------------------------------------------
_tms_fill_vram:
        ; AX = count (last arg, word)
        sta     size_lo
        stx     size_hi

        jsr     popa             ; A = val
        sta     fill_val

        jsr     popax            ; AX = addr (lo=A, hi=X)
        sta     addr_lo
        stx     addr_hi

        lda     size_lo          ; count = 0 is a documented no-op — bail
        ora     size_hi          ; BEFORE the blank bracket (a no-op call
        bne     @go              ; must not flash the backdrop for a frame)
        rts
@go:    jsr     blank_display    ; burst only legal in a free zone

        ; VRAM write address — programmed AFTER the R1 write (a register
        ; write clobbers the address counter's high bits on real silicon).
        lda     addr_lo
        sta     VDP_REG          ; low addr
        nop
        nop
        lda     addr_hi
        and     #HI_ADDR_MASK
        ora     #WRITE_TO_VRAM_BITS
        sta     VDP_REG          ; high addr | $40 (latch write)
        nop
        nop

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
        jmp     restore_display  ; tail-call (restores display, then RTS)


; -----------------------------------------------------------------
; void __cdecl__ tms_copy_to_vram_fast(const void *src, unsigned size, unsigned dest);
;
;   src   — host RAM (or ROM) pointer.
;   size  — 0..65535 bytes.
;   dest  — VRAM destination (14-bit).
;
; Equivalent to tms_copy_to_vram() in tms9918.c, minus the per-byte pacing —
; made silicon-legal by blanking the display around the burst (see header).
; For tear-free incremental updates prefer VBlank-chunked copies.
; -----------------------------------------------------------------
_tms_copy_to_vram_fast:
        ; AX = dest (word, last arg)
        sta     addr_lo
        stx     addr_hi

        jsr     popax            ; AX = size
        sta     size_lo
        stx     size_hi

        jsr     popax            ; AX = src
        sta     ptr1
        stx     ptr1+1

        lda     size_lo          ; size = 0 is a no-op — bail BEFORE the
        ora     size_hi          ; blank bracket (mirrors tms_fill_vram: a
        bne     @go              ; no-op must not flash the backdrop)
        rts
@go:    jsr     blank_display

        lda     addr_lo
        sta     VDP_REG
        nop
        nop
        lda     addr_hi
        and     #HI_ADDR_MASK
        ora     #WRITE_TO_VRAM_BITS
        sta     VDP_REG
        nop
        nop

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
        jmp     restore_display  ; tail-call


; -----------------------------------------------------------------
; void __cdecl__ tms_shadow_flush(void);
;
; Copy 128 bytes from tms_sprite_shadow[] to VRAM SAT ($3B00).
; MUST be called inside VBLANK (after tms_wait_end_of_frame): the ~15c/byte
; cadence and the 2c control pair below are only legal in the free-zone
; slot table. 128 × ~15c ≈ 1.9k cycles fits the ~4.5k-cycle VBlank budget.
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
