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
; **Active contract: 12 cycles** (JSR tms9918_pad12), full stop. That is the
; 12c minimum gap (the pad alone), or 16c measured STA->STA (4c store + 12c
; pad) between back-to-back STA VDP_DATA — comfortably above the openMSX
; slot-table model's ~7.5c silicon worst case. 12c is the floor and the
; default: the WRT_DATA_* macros in tms9918.inc and all lib code use pad12.
;
; tms9918_pad24 stays as an optional cushion. tms9918_pad40 is LEGACY — a
; paranoid 40c holdover from the superseded "hardening ramp"; the openMSX
; model made it unnecessary. Kept only because TMS_Rogue's upload loops and
; the silicon_strict patcher's hardened mode still call it; do NOT use it in
; new code (slated for removal once those migrate to pad12).
;
; Reference: dev/Programming_TMS9918.md §17, dev/lib/tms9918/tms9918.inc.
; ============================================================================

.include "tms9918.inc"

.export tms9918_pad12
.export tms9918_pad24
.export tms9918_pad40
.export vdp_display_off
.export vdp_display_on

.segment "CODE"

; Burns 12 cycles (= 6c JSR + 6c RTS), 3 bytes at the call site, helper
; itself = 1 byte. Kept for niche callers that genuinely want only 12c idle
; (status-register polls, address-latch settling).
;
; INVARIANT — flag-transparent: pad12 is a bare RTS, so it leaves every
; processor flag untouched. Hot loops place `JSR tms9918_pad12` BETWEEN a
; CPX/CMP and its branch (see sprite_triangle.asm and tms9918_text.asm) and
; rely on the compare's Z/C surviving the call. Any future pad12 body MUST
; stay flag-transparent or those loops break silently.
tms9918_pad12:
        rts                     ; 6c, JSR adds 6c, total = 12c

; Burns 24 cycles (= 6c JSR + 12c pad12 + 6c RTS), 3 bytes at the call site,
; helper itself = 4 bytes. Optional cushion above the 12c contract — the
; default everywhere is pad12.
tms9918_pad24:
        jsr     tms9918_pad12   ; 6c JSR + 12c pad12 = 18c so far
        rts                     ; +6c RTS, total = 24c at caller's JSR site

; Burns 40 cycles (= 6c JSR + 12c pad12 + 12c pad12 + 4c (NOP×2) + 6c RTS),
; 3 bytes at the call site, helper itself = 7 bytes.
;
; LEGACY — do NOT use in new code. This is the paranoid 40c holdover from the
; superseded "hardening ramp"; the 12c contract (pad12) is the floor now. It
; survives only because TMS_Rogue's upload loops and the silicon_strict
; patcher's hardened mode still reference it. Slated for removal once those
; are migrated to pad12.
tms9918_pad40:
        jsr     tms9918_pad12   ; +12c → 18c so far
        jsr     tms9918_pad12   ; +12c → 30c so far
        nop                     ; +2c  → 32c
        nop                     ; +2c  → 34c
        rts                     ; +6c RTS, total = 40c at caller's JSR site

; ============================================================================
; Display gate helpers (R1 register manipulation).
; ============================================================================
; vdp_display_off:  R1 = $80  (blanked, 16K, no IRQ, sprite 8x8 unmag).
; vdp_display_on:   R1 = $C0  (display ON, 16K, sprite 8x8 unmag — matches
;                              vdp1_regs[1] and vdp2_regs[1] defaults).
;
; Use these to bracket any VRAM burst that runs after init_vdp_g{1,2} has
; left display ON. Strict-mode active-display slot density (Gfx12 ~19
; slots/line) drops bytes from tight loops even with pad12 between writes;
; blanking flips POM1 to slotsMsx1ScreenOff (~107 slots/line, ~2c apart)
; where bursts always fit. See doc/TMS9918-SPRITE_INIT.md § 6.4.
;
; Callers that need a non-default ON value (e.g. R1 = $C2 for 16x16 sprites,
; or $C3 for 16x16+mag) must do their own write and skip vdp_display_on.
; ============================================================================

; Fall-through trick: vdp_display_on loads A=$C0 then BIT $0080 swallows the
; LDA #$80 of vdp_display_off (BIT abs is 3 bytes, $2C opcode + 2 byte addr).
; Both paths converge on the shared STA / pad / STA / pad / RTS tail.
; Saves ~12 bytes vs two independent helpers.
vdp_display_on:
        LDA     #$C0
        .byte   $2C             ; BIT abs — opcode-swallows the next LDA #imm
vdp_display_off:
        LDA     #$80
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$81            ; cmd = $80 | reg 1 (write to R1)
        STA     VDP_CTRL
        JSR     tms9918_pad12
        RTS
