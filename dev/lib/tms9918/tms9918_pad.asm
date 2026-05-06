; ============================================================================
; tms9918_pad.asm -- silicon-strict padding helpers for TMS9918 VDP access
; ============================================================================
; Three RTS-based delay subroutines used by the silicon_strict_patch.py
; tool and by the WRT_DATA_* macros in tms9918.inc.
;
; Why JSR-to-RTS instead of NOPs?
;   NOP        = 2 cycles in 1 byte (ratio 2 c/B)
;   JSR + RTS  = 12 cycles in 3 bytes at the call site, helper itself = 1 byte
;                (ratio 4 c/B at the call site — twice as dense as NOP)
;
; **Hardened contract (May 2026 v2)**: the silicon-strict floor is now 40
; cycles (was 24, originally 16). Real silicon's "1 slot per 16 VDP cycles"
; worst case is the underlying spec, but at warm-NMOS phase corners and with
; CPU↔VDP drift the visible window shrinks past 24c — Galaga sprite tables
; still corrupted at 24c and LOGO BIRD/Demo* turtle redraws still showed
; pixel artefacts. The pad40 helper delivers a 40c idle at the call site
; (call-site density 6.67 c/B), so back-to-back STA VDP_DATA pairs land with
; a 48c gap — well over the slot period at any phase alignment.
;
; Reference: dev/SILICONBUGS.md Bug N°1 §2 (paranoid 40c contract).
; ============================================================================

.export tms9918_pad12
.export tms9918_pad24
.export tms9918_pad40

.segment "CODE"

; Burns 12 cycles (= 6c JSR + 6c RTS), 3 bytes at the call site, helper
; itself = 1 byte. Kept for niche callers that genuinely want only 12c idle
; (status-register polls, address-latch settling).
tms9918_pad12:
        rts                     ; 6c, JSR adds 6c, total = 12c

; Burns 24 cycles (= 6c JSR + 12c pad12 + 6c RTS), 3 bytes at the call site,
; helper itself = 4 bytes. Kept for backward compat with the May 2026 v1
; rollout — current patcher emits pad40 instead.
tms9918_pad24:
        jsr     tms9918_pad12   ; 6c JSR + 12c pad12 = 18c so far
        rts                     ; +6c RTS, total = 24c at caller's JSR site

; Burns 40 cycles (= 6c JSR + 12c pad12 + 12c pad12 + 4c (NOP×2) + 6c RTS),
; 3 bytes at the call site, helper itself = 7 bytes. Standard idle pad for
; STA/STA pairs in Mode I + sprites under the hardened 40c contract. STA1
; (4c) + JSR pad40 (40c) + STA2 (4c) → 44c from end of STA1's bus access to
; STA2's, which clears the worst-case slot period at any CPU↔VDP phase
; alignment with comfortable margin.
tms9918_pad40:
        jsr     tms9918_pad12   ; +12c → 18c so far
        jsr     tms9918_pad12   ; +12c → 30c so far
        nop                     ; +2c  → 32c
        nop                     ; +2c  → 34c
        rts                     ; +6c RTS, total = 40c at caller's JSR site
