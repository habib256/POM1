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
; VRAM access timing — silicon-strict back-to-back gating (hardened 40c).
;
; Real TMS9918A silicon opens a CPU access window only every ~6 µs in Mode I
; with sprites active (cf. dev/SILICONBUGS.md Bug N°1). Phase alignment +
; warm-NMOS drift can shrink the visible window past the original 8c margin
; — the May 2026 hardening ramp went 16c → 24c → 40c after Galaga sprite
; tables and LOGO turtle redraws kept showing artefacts at every previous
; level. The current 40c floor leaves comfortable margin at any phase.
;
; POM1 enforces 40c when Hardware → Silicon Strict is ON (default for every
; preset except Multiplexing Fantasy). Writes that arrive too fast are
; silently DROPPED — exactly like silicon under unfavorable phase alignment.
;
; Padding strategy: prefer `JSR tms9918_pad40` over NOPs (defined in
; tms9918_pad.asm). 3 bytes for 40c idle vs 20 NOPs for the same 40c — 6.67×
; denser at the call site.
;
;   STA VDP_DATA          ; 4c
;   JSR tms9918_pad40     ; 40c (3 bytes) → next write lands 40 c later
;
; Use WRT_DATA_REG when A is already loaded with the byte to push (typical
; sprite-attribute and tile-stream loops). Use WRT_DATA_VAL for "load
; immediate, then push" — same 40c gap to the next write.
; ---------------------------------------------------------------------------
; Note: WRT_DATA_REG / WRT_DATA_VAL reference tms9918_pad40; callers using
; these macros must `.import tms9918_pad40` themselves (the patcher does
; this automatically when injecting JSR tms9918_pad40 — see SILICONBUGS.md).

.macro WRT_DATA_REG
        STA     VDP_DATA        ; 4c
        JSR     tms9918_pad40   ; 40c, 3 bytes — next write lands 40c later
.endmacro

.macro WRT_DATA_VAL val
        LDA     #val            ; 2c
        STA     VDP_DATA        ; 4c
        JSR     tms9918_pad40   ; 40c, 3 bytes — next write lands 40c later
.endmacro

.endif  ; _TMS9918_INC_LOADED_
