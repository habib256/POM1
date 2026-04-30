; ============================================================================
; tms9918.inc -- shared hardware equates for the P-LAB TMS9918 Graphic Card
; ============================================================================
; Include alongside apple1.inc when targeting the P-LAB Apple-1 TMS9918 card.
; Pure equates, no code, no segments. Idempotent — outer .ifndef guard so
; tms9918m1.asm and tms9918m2.asm (which both include this file) can
; coexist with a project that also includes it directly.
;
; cc65 include path: dev/lib/tms9918/
; ============================================================================

.ifndef _TMS9918_INC_LOADED_
_TMS9918_INC_LOADED_ = 1

; --- P-LAB TMS9918 Graphic Card --------------------------------------------
VDP_DATA = $CC00        ; VRAM data port  (read/write, auto-increments)
VDP_CTRL = $CC01        ; VDP register / address port (write twice = setup)

; Common register-write helpers expect the value first, then the register
; index ORed with $80 in CTRL — see tms9918m2.asm:init_vdp_g2 for an example.

; ---------------------------------------------------------------------------
; VRAM access timing — silicon-strict back-to-back gating.
;
; Real TMS9918A silicon opens a CPU access window only every ~6 µs in Mode I
; with sprites active (cf. dev/SILICONBUGS.md Bug N°1). At 1.022 MHz that's
; ~6 cycles; with the ~2 µs preparation margin, the safe gap is ≥ 8 cycles
; between two VRAM writes. POM1 enforces this when the Hardware → Silicon
; Strict toggle is on (default for every preset except Multiplexing Fantasy):
; writes that arrive too fast are silently DROPPED, exactly like on silicon.
;
; The macros below pad two consecutive `STA VDP_DATA` so the gap is ≥ 8 c:
;   STA VDP_DATA          ; 4c
;   NOP / NOP             ; 2c + 2c → next write lands 8 c later
;
; Use WRT_DATA_REG when A is already loaded with the byte to push (typical
; sprite-attribute and tile-stream loops). Use WRT_DATA_VAL for "load
; immediate, then push" — the LDA #imm itself is 2c so the trailing NOP
; count drops by one.
;
; Indexed addressing (`STA VDP_DATA,X` with X=0) costs +1 cycle — pair it
; with a single NOP if you prefer style over a NOP-pair.
; ---------------------------------------------------------------------------
.macro WRT_DATA_REG
        STA     VDP_DATA        ; 4c
        NOP                     ; 2c
        NOP                     ; 2c — next write lands 8c later
.endmacro

.macro WRT_DATA_VAL val
        LDA     #val            ; 2c
        STA     VDP_DATA        ; 4c
        NOP                     ; 2c — next write lands 8c later
.endmacro

.endif  ; _TMS9918_INC_LOADED_
