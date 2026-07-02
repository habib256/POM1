; ============================================================================
; gen2_init.asm -- deterministic GEN2 soft-switch init (Bernie Q8)
; ============================================================================
; The latch at $C250-$C257 is INDETERMINATE at power-on and survives Apple-1
; RESET. Call gen2_hgr_init (or gen2_lores_init) before any GEN2 drawing, and
; again after DevBench Run if a prior sketch left TEXT / MIXED / PAGE2 / LORES.
;
; Each switch is read-only: LDA/BIT toggles; never STA. These routines touch
; all four pairs (TEXT/graphics, MIXED, PAGE, RES) so the mode is known.
;
;   JSR gen2_hgr_init        GRAPHICS + HIRES + PAGE1 + full screen
;   JSR gen2_hgr_init_clear  same, but BLANK-FIRST: clears the page-1
;                            framebuffer ($2000-$3FFF) while the display is
;                            parked on TEXT, THEN flips to HIRES. The card has
;                            no display-enable bit and its framebuffer SRAM is
;                            indeterminate at power-on — plain gen2_hgr_init
;                            shows one frame of SRAM garbage until the program
;                            clears the page itself. ORDER MATTERS: clear
;                            first, flip after. (Existing callers that clear
;                            immediately after init keep plain gen2_hgr_init.)
;   JSR gen2_lores_init      GRAPHICS + LORES + PAGE1 + full screen
;   JSR gen2_text_restore    polite exit: back to TEXT + PAGE1 (the monitor-
;                            visible state) — toggles only, then RTS
;
; Blessed program exit — restore a monitor-visible mode, THEN leave:
;
;       jsr gen2_text_restore
;       jmp WOZMON              ; $FF1A prompt entry (dev/lib/apple1/apple1.inc)
;
; gen2_text_restore deliberately does NOT jump to Wozmon itself: this file is
; a pure GEN2 layer (imports nothing from dev/lib/apple1, and is textually
; .include'd by both bare asm sketches and gen2c's gen2_blit.s), so the JMP
; target stays with the caller, which already owns the apple1.inc equates.
;
; C consumers link gen2_blit.s, which .include's this file as _gen2_hgr_init /
; _gen2_lores_init / _gen2_hgr_init_clear / _gen2_text_restore. Asm sketches:
; .include "gen2_init.asm" and JSR gen2_hgr_init.
; ============================================================================

.ifndef _GEN2_INIT_LOADED_
_GEN2_INIT_LOADED_ = 1

.include "gen2.inc"

.segment "CODE"

gen2_hgr_init:
_gen2_hgr_init:
        lda GEN2_TEXTOFF
        lda GEN2_HIRES
        lda GEN2_PAGE1
        lda GEN2_MIXOFF
        rts

gen2_lores_init:
_gen2_lores_init:
        lda GEN2_TEXTOFF
        lda GEN2_LORES
        lda GEN2_PAGE1
        lda GEN2_MIXOFF
        rts

; --- blank-first HIRES init ---------------------------------------------------
; The GEN2 has no display-enable bit (unlike the TMS9918's R1 BLANK), so the
; "init to a clean screen" discipline is: park the display on TEXT, scrub the
; page-1 framebuffer ($2000-$3FFF, indeterminate SRAM at power-on), and only
; THEN flip to HIRES. Flipping first would display 8 KB of SRAM junk for as
; long as the clear takes (~0.1 s — very visible). Page 2 ($4000) is left
; untouched: init selects PAGE1; double-buffer programs clear page 2 through
; gen2_set_draw_page/gen2_hgr_clear before ever showing it.
; No zero-page use (the loop is 32 unrolled absolute-indexed stores), so this
; is safe from bare asm sketches and from the cc65 runtime alike.
gen2_hgr_init_clear:
_gen2_hgr_init_clear:
        lda GEN2_TEXTON      ; park the display on TEXT while we scrub
        lda GEN2_PAGE1
        lda #$00
        tax
@clr:
.repeat 32, I
        sta $2000 + (I * $100), x
.endrepeat
        inx
        bne @clr
        jmp gen2_hgr_init    ; now flip: GRAPHICS + HIRES + PAGE1 + full

; --- polite exit: restore the monitor-visible mode ----------------------------
; Leaves the card on TEXT + PAGE1 so whatever runs next (Wozmon, BASIC) is
; visible on the GEN2 monitor. Toggles only + RTS — pair it with the caller's
; own JMP WOZMON (see the header idiom).
gen2_text_restore:
_gen2_text_restore:
        lda GEN2_TEXTON
        lda GEN2_PAGE1
        rts

.endif
