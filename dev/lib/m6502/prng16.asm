; ============================================================================
; prng16.asm -- 16-bit Galois LFSR ($B400 tap), shared by arcade games
; ============================================================================
; Promoted from inline copies in 2 projects (mutualization Tier 2.2). State
; lives in `prng_lo / prng_hi` — same canonical name as `prng8.asm`, so a
; project that uses both LFSRs shares one state pair physically (you'd only
; pick one in practice, but the unified naming lets `dev/lib/apple1/zp.inc`
; declare the slots once and have either lib find them).
;
; Reserves its own ZP slot pair via `.ifndef` guard with the standard
; alias-on-tight-ZP convention:
;
;     prng_lo = my_lo
;     prng_hi = my_hi
;     .include "prng16.asm"
;
; Or, simpler, `.include "zp.inc"` once at the top of your project and the
; slots are pre-declared at $06/$07.
;
;   prng16  -- 16-bit Galois LFSR, polynomial $B400 (taps 16, 14, 13, 11).
;              Returns A = new prng_lo. Mutates prng_lo + prng_hi.
;              Caller must seed the state to nonzero — zeroed state stays
;              zero forever.
;
; A semantically equivalent routine `roll_lfsr` (no return value, uses
; lfsr_lo/lfsr_hi instead of prng_lo/prng_hi) lives in
; `dev/lib/m6502/math.asm`. The two LFSRs are conceptually similar but
; physically distinct state — math.asm's `lfsr_*` lives in BSS (the
; LINEBUF segment for projects that follow its convention), prng16's
; `prng_*` lives in ZEROPAGE for fast access in tight game loops.
; ============================================================================

.ifndef prng_lo
.segment "ZEROPAGE"
prng_lo:        .res 1
prng_hi:        .res 1
.endif

.segment "CODE"

prng16:
        LSR     prng_hi
        ROR     prng_lo
        BCC     @done
        LDA     prng_hi
        EOR     #$B4
        STA     prng_hi
@done:  LDA     prng_lo
        RTS
