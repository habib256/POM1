; ============================================================================
; dungeon.asm -- procedural-generation primitives for grid-based 6502 games
; ============================================================================
;
; Today this file holds the smallest, truly grid-agnostic helper from the
; TMS_Rogue dungeon generator: `rand_mod`. The bigger BSP-light pattern
; (room placement, L-corridor with marker tiles, 4-cardinal-neighbour door
; classification, three-pass corridor finalisation) still lives inline in
; `sketchs/tms9918/game_rogue/TMS_Rogue.asm` because it bakes in two
; rogue-specific assumptions:
;
;   1. Logical grid is 16-wide so `index → (col, row)` collapses to one
;      `AND #$0F` + four `LSR` bit-shifts.  A generic version would need
;      a divmod by an arbitrary cols count — slower or table-driven.
;
;   2. Tile codes (`TILE_WALL`, `TILE_EMPTY`, `TILE_CORR`, `TILE_CORR_DROP`,
;      `TILE_DOOR`) are read directly as constants. A generic version
;      would parametrise these via `.import` or pre-`.include` aliases.
;
; Promotion will land here when the second consumer shows up — see the
; "Rule of Three" note in dev/lib/games/README.md.  In the meantime the hooks
; for that promotion are documented in the `gen_dungeon` / `finalize_doors`
; comment blocks of TMS_Rogue.asm.
;
; ----------------------------------------------------------------------------
; Caller responsibilities -- declare these BEFORE `.include "dungeon.asm"`
; ----------------------------------------------------------------------------
;
; ZP scratch (declare in your `.zeropage` block, .include "prng16.asm"
; before this file so the LFSR state is available):
;
;   prng_lo, prng_hi   = 16-bit Galois LFSR state (from prng16.asm)
;   tmp                = 1-byte caller-owned scratch (rand_mod stomps it)
;
; ============================================================================

; ----------------------------------------------------------------------------
; rand_mod -- uniform pseudorandom integer in [0, A).
;   In:  A = max (must be > 0; A=0 is undefined behaviour)
;   Out: A = result in [0, max)
;   Clobbers: tmp; advances prng_lo / prng_hi via prng16.
;
; Implementation: pull a fresh 16-bit LFSR sample (low byte → A) then
; reduce by repeated subtraction.  Fast for our typical ranges (max
; ≤ 16) — averaging ~8 iterations.  For larger ranges or higher
; throughput, swap in a divmod-style reducer.
;
; The repeated-subtract form has a small bias when 256 mod max ≠ 0
; (some residues are slightly more likely).  In practice the bias is
; under 1 % for max ≤ 16 and invisible to gameplay.
; ----------------------------------------------------------------------------

.segment "CODE"

rand_mod:
        STA     tmp
        JSR     prng16
@lp:    CMP     tmp
        BCC     @done
        SEC
        SBC     tmp
        JMP     @lp
@done:  RTS
