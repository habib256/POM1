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
; The "1 slot per 16 VDP cycles" worst case in Mode I + sprites means a 16-
; cycle gap between two STA VDP_DATA instructions covers 1 full slot period
; + STA latch + phase margin. STA1 (4c) + JSR pad12 (12c) + STA2 (4c) = 16c
; gap from end of STA1's bus access to end of STA2's bus access. Done.
;
; Reference: dev/SILICONBUGS.md Bug N°1 §2 (paranoid 16c contract).
; ============================================================================

.export tms9918_pad12

.segment "CODE"

; Burns 12 cycles (= 6c JSR + 6c RTS), 3 bytes at the call site, helper
; itself = 1 byte. The standard idle pad for STA/STA pairs in Mode I +
; sprites (gap = 4 + 12 + 4 = 16c when used as `STA / JSR pad12 / STA`).
;
; For longer pads, chain `JSR pad12 / JSR pad12` at the call site (24c, 6
; bytes) — still denser than NOP*12 (24c, 12 bytes).
tms9918_pad12:
        rts                     ; 6c, JSR adds 6c, total = 12c
