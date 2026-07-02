; ============================================================================
; tms9918_helpers.asm -- silicon-strict mutualised VDP-store helpers
; ============================================================================
; Standalone tiny module (no zero-page imports, no other lib dependencies)
; that every TMS9918 project links to dodge the 200+ inline NOPs that the
; pure inline silicon-strict patcher would otherwise insert.
;
; Mutualises:
;   vdp_write_a      -- caller does `LDA #imm` then `JSR vdp_write_a`,
;                       saving 2 bytes per site vs. inline `LDA #imm /
;                       STA VDP_DATA / NOP / NOP`.
;   vdp_set_write_xy -- caller does `LDX #lo / LDY #hi / JSR vdp_set_write_xy`,
;                       saving 4 bytes per site vs. inline 2-byte address
;                       write with NOP padding.
;
; Both helpers integrate the silicon-strict NOP padding so callers don't
; have to. Contract (pad18 era): the helper's internal NOPs + the JSR/RTS
; round-trips of the canonical caller pattern give a ≥ 20c STA→STA gap —
; above the ~16c active-display floor validated on real silicon (cf.
; sketchs/doc/Programming_TMS9918.md §17, §25 and tms9918_pad.asm).
;
; Sized to ~12 bytes total in ROM. ~204 sites across all TMS9918 projects
; refactor through these two entries (tools/silicon_strict_refactor.py).
; ============================================================================

        .import tms9918_pad18  ; silicon-strict pad18-v4 (helper from tms9918_pad.asm)
.include "tms9918.inc"

.export vdp_write_a, vdp_set_write_xy

.segment "CODE"

; ----------------------------------------------------------------------------
; vdp_write_a: write A to VDP_DATA with silicon-strict NOP padding.
;   Caller pattern:
;       LDA #imm           (2c, 2 bytes)
;       JSR vdp_write_a    (6c, 3 bytes)  ← 5 bytes total, vs 7 inline.
;   Inputs:  A = byte to push to VDP_DATA.
;   Output:  A preserved (STA doesn't change it). X, Y preserved.
;   Cycle accounting: STA (4c) + NOP×2 (4c) + RTS (6c) = 14c; with the
;   canonical caller pattern (LDA #imm + JSR) the next VDP store sees a
;   22c STA→STA gap — the pad18 contract, clearing the ~16c silicon floor
;   with margin. (Was a single NOP = 20c gap, above the floor but below
;   the repo contract.)
; ----------------------------------------------------------------------------
vdp_write_a:
        STA     VDP_DATA
        NOP
        NOP
        RTS

; ----------------------------------------------------------------------------
; vdp_set_write_xy: prep VRAM auto-incrementing write at address (X=lo, Y=hi).
;   Sets the write flag (bit 6) on the high byte automatically.
;   Caller pattern:
;       LDX #lo                 (2c, 2 bytes)
;       LDY #hi                 (2c, 2 bytes)
;       JSR vdp_set_write_xy    (6c, 3 bytes)  ← 7 bytes, vs 11 inline.
;   The two STA VDP_CTRL writes are spaced ≥ 8c apart by the TYA + ORA
;   bridge plus a NOP. Trailing RTS gives ≥ 8c before the caller's next
;   VDP store.
;   Clobbers: A. X, Y consumed.
; ----------------------------------------------------------------------------
vdp_set_write_xy:
        TXA
        STA     VDP_CTRL
        NOP
        TYA
        ORA     #$40            ; bit 6 = write
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (back-to-back VDP store)
        STA     VDP_CTRL
        RTS
