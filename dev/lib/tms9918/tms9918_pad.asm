; ============================================================================
; tms9918_pad.asm -- silicon-strict padding helpers for TMS9918 VDP access
; ============================================================================
; Two tiny RTS-based delay subroutines used by the silicon_strict_patch.py
; tool and by the WRT_DATA_* macros in tms9918.inc.
;
; Why JSR-to-RTS instead of NOPs?
;   NOP        = 2 cycles in 1 byte (ratio 2 c/B)
;   JSR + RTS  = 12 cycles in 3 bytes at the call site, helper itself = 1 byte
;                (ratio 4 c/B at the call site — twice as dense as NOP)
;
; **Hardened contract (May 2026)**: the silicon-strict floor is now 24 cycles
; (was 16). The "1 slot per 16 VDP cycles" worst case in Mode I + sprites is
; still the underlying spec, but games that ride right on the edge of 16c
; (Galaga sprite tables, LOGO turtle redraw) overflow under unfavourable
; phase alignment. The new pad24 helper delivers a 24c idle at the call site
; (4 c/B density, same as pad12), so back-to-back STA VDP_DATA pairs land
; with a 28c gap from one bus access to the next — well over the slot
; period regardless of phase.
;
; Reference: dev/SILICONBUGS.md Bug N°1 §2 (paranoid 24c contract).
; ============================================================================

.export tms9918_pad12
.export tms9918_pad24

.segment "CODE"

; Burns 12 cycles (= 6c JSR + 6c RTS), 3 bytes at the call site, helper
; itself = 1 byte. Kept for niche callers that genuinely want only 12c idle
; (status-register polls, address-latch settling) — most VDP store sites
; should use pad24 below.
tms9918_pad12:
        rts                     ; 6c, JSR adds 6c, total = 12c

; Burns 24 cycles (= 6c JSR + 12c pad12 + 6c RTS), 3 bytes at the call site,
; helper itself = 4 bytes. Standard idle pad for STA/STA pairs in Mode I +
; sprites under the hardened 24c contract. STA1 (4c) + JSR pad24 (24c) +
; STA2 (4c) → 28c from end of STA1's bus access to end of STA2's, which
; clears the worst-case slot period at any CPU↔VDP phase alignment.
tms9918_pad24:
        jsr     tms9918_pad12   ; 6c JSR + 12c pad12 = 18c so far
        rts                     ; +6c RTS, total = 24c at caller's JSR site
