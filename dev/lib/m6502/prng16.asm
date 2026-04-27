; ============================================================================
; prng16.asm -- 16-bit Galois LFSR ($B400 tap), shared by arcade games
; ============================================================================
; Promoted from inline copies in 2 projects (mutualization Tier 2.2). Reserves
; its own ZP slot pair (seed_lo / seed_hi) via `.ifndef` guard so a ZP-tight
; caller can alias them to existing scratch slots before the .include:
;
;     seed_lo = my_lo
;     seed_hi = my_hi
;     .include "prng16.asm"
;
;   prng16  -- 16-bit Galois LFSR, polynomial $B400 (taps 16, 14, 13, 11).
;              Returns A = new seed_lo. Mutates seed_lo + seed_hi.
;              Caller must seed the state to nonzero — zeroed state stays
;              zero forever.
;
; A semantically equivalent routine `roll_lfsr` (no return value, uses
; lfsr_lo/lfsr_hi instead of seed_lo/seed_hi) lives in
; `dev/lib/m6502/math.asm` for projects already integrated with that
; module's full ZP convention (e.g. TMS_Logo).
; ============================================================================

.ifndef seed_lo
.segment "ZEROPAGE"
seed_lo:        .res 1
seed_hi:        .res 1
.endif

.segment "CODE"

prng16:
        LSR     seed_hi
        ROR     seed_lo
        BCC     @done
        LDA     seed_hi
        EOR     #$B4
        STA     seed_hi
@done:  LDA     seed_lo
        RTS
