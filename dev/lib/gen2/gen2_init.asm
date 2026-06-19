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
;   JSR gen2_hgr_init    GRAPHICS + HIRES + PAGE1 + full screen
;   JSR gen2_lores_init  GRAPHICS + LORES + PAGE1 + full screen
;
; C consumers link gen2_blit.s, which .include's this file as _gen2_hgr_init /
; _gen2_lores_init. Asm sketches: .include "gen2_init.asm" and JSR gen2_hgr_init.
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

.endif
