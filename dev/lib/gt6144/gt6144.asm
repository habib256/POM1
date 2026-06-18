; ============================================================================
; gt6144.asm -- SWTPC GT-6144 plotter primitives
; ============================================================================
; Intended to mutualise the clear + plot + control-opcode patterns inlined in
; dev/projects/gt6144/gt6144_demo_hello and gt6144_demo_life. Each routine is a
; tight few-instruction wrapper around the $D00A protocol — no ZP usage, small
; footprint. STATUS: not yet adopted — both demos still carry inline copies;
; migrate them onto this module (see README) or retire it.
;
;   gt_clear     -- paint every pixel OFF (64*96 = 6144 latch+commit pairs)
;   gt_plot_on   -- A=x (0..63), Y=y (0..95) → set pixel ON at (x,y)
;   gt_plot_off  -- A=x (0..63), Y=y (0..95) → set pixel OFF at (x,y)
;   gt_blank     -- hide the framebuffer
;   gt_unblank   -- show the framebuffer
;   gt_invert    -- video inversion ON (visual XOR; SRAM untouched)
;   gt_normal    -- cancel inversion
;
; Caller responsibility: gt6144.inc included (this module includes it
; itself for the equates).
;
; No ZP. All routines clobber A only (Y unchanged for plot routines —
; caller can keep Y across multiple plots at the same row).
; ============================================================================

.include "gt6144.inc"

.segment "CODE"

; ----------------------------------------------------------------------------
; gt_clear -- 64 columns × 96 rows = 6144 OFF pairs.
;             ~12300 cycles at 1 MHz = ~12 ms (fast enough to be invisible).
;             Clobbers A, X, Y.
; ----------------------------------------------------------------------------
gt_clear:
        LDX     #0
@xl:    STX     GT_PORT             ; latch X (X < 64), pixel OFF
        LDY     #128
@yl:    STY     GT_PORT             ; commit (X, Y-128) = OFF
        INY
        CPY     #224
        BNE     @yl
        INX
        CPX     #64
        BNE     @xl
        RTS

; ----------------------------------------------------------------------------
; gt_plot_on -- A=x (0..63), Y=y (0..95). Plot ON at (x, y).
;               Clobbers A. Y is preserved (handy across runs at same row).
; ----------------------------------------------------------------------------
gt_plot_on:
        ORA     #GT_LATCH_ON        ; bit 6 = 1: latch X with pixel ON
        STA     GT_PORT
        TYA
        ORA     #GT_COMMIT          ; bit 7 = 1: commit Y
        STA     GT_PORT
        RTS

; ----------------------------------------------------------------------------
; gt_plot_off -- A=x (0..63), Y=y (0..95). Plot OFF at (x, y).
; ----------------------------------------------------------------------------
gt_plot_off:
        STA     GT_PORT             ; latch X, pixel OFF (bit 6 = 0)
        TYA
        ORA     #GT_COMMIT
        STA     GT_PORT
        RTS

; ----------------------------------------------------------------------------
; Control-opcode wrappers (single-byte writes to $D00A).
; ----------------------------------------------------------------------------
gt_blank:
        LDA     #GT_OP_BLANK
        STA     GT_PORT
        RTS

gt_unblank:
        LDA     #GT_OP_UNBLANK
        STA     GT_PORT
        RTS

gt_invert:
        LDA     #GT_OP_INVERT
        STA     GT_PORT
        RTS

gt_normal:
        LDA     #GT_OP_NORMAL
        STA     GT_PORT
        RTS
