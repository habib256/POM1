; ============================================================================
; tms9918_pad.asm -- silicon-strict padding helpers for TMS9918 VDP access
; ============================================================================
; Three RTS-based delay subroutines used by the silicon_strict_patch.py
; tool and by the WRT_DATA_* macros in tms9918.inc.
;
; Why JSR-to-RTS (+ a few NOPs) instead of a NOP sled?
;   NOP        = 2 cycles in 1 byte (ratio 2 c/B)
;   JSR + RTS  = 12 cycles in 3 bytes at the call site, helper itself = 1 byte
;                (ratio 4 c/B at the call site — twice as dense as NOP)
;
; **Active contract: 18 cycles** (JSR tms9918_pad18). That is an 18c gap (the
; pad alone), or 22c measured STA->STA (4c store + 18c pad) between back-to-back
; STA VDP_DATA. Real TMS9918A silicon (validated on Claudio Parmigiani's
; Replica-1) drops CPU writes spaced under ~16c during active Mode I/II
; display — far stricter than the openMSX slot-table's ~7.5c estimate. The old
; 12c pad gave only a 16c STA->STA gap, sitting *exactly* at the silicon floor
; (no margin); pad18's 22c clears it. POM1 models the floor in TMS9918.cpp
; (kMinActiveDrainCycles = 16). The WRT_DATA_* macros in tms9918.inc and all
; lib code use pad18; `tms9918_pad12` survives as a legacy alias that now
; resolves to pad18 (so any un-migrated call site still gets the safe 18c).
;
; tms9918_pad24 stays as an optional cushion. tms9918_pad40 is LEGACY — a
; paranoid holdover from the superseded "hardening ramp". Kept only because
; TMS_Rogue's upload loops and the silicon_strict patcher's hardened mode still
; call it; do NOT use it in new code (slated for removal once those migrate).
;
; Reference: sketchs/doc/Programming_TMS9918.md §17, dev/lib/tms9918/tms9918.inc.
; ============================================================================

.include "tms9918.inc"

.export tms9918_pad18
.export tms9918_pad12            ; legacy alias -> tms9918_pad18
.export tms9918_pad24
.export tms9918_pad40
.export vdp_display_off
.export vdp_display_on

.segment "CODE"

; Burns 18 cycles (= 6c JSR + 6c NOP×3 + 6c RTS), 3 bytes at the call site,
; helper itself = 4 bytes. This is THE silicon-strict default — a 22c STA->STA
; gap that clears the real chip's ~16c active-display floor with margin.
;
; INVARIANT — flag-transparent: NOP and RTS leave every processor flag
; untouched. Hot loops place `JSR tms9918_pad18` BETWEEN a CPX/CMP and its
; branch (see sprite_triangle.asm and tms9918_text.asm) and rely on the
; compare's Z/C surviving the call. Any future pad body MUST stay
; flag-transparent (NOP-only filler) or those loops break silently.
tms9918_pad18:
        nop                     ; +2c
        nop                     ; +2c
        nop                     ; +2c
        rts                     ; +6c RTS; JSR adds 6c → total = 18c

; Legacy 12c name. The whole codebase moved to the 18c contract; the historical
; `JSR tms9918_pad12` call sites now resolve here so they stay silicon-safe
; without a forced edit. New code should call tms9918_pad18 directly.
tms9918_pad12 = tms9918_pad18

; Burns 30 cycles (= 6c JSR + 18c pad18 + 6c RTS), 3 bytes at the call site.
; Optional cushion above the 18c contract — the default everywhere is pad18.
tms9918_pad24:
        jsr     tms9918_pad18   ; 6c JSR + 18c pad18 = 24c so far
        rts                     ; +6c RTS, total = 30c at caller's JSR site

; Burns 48 cycles (= 6c JSR + 18c pad18 + 18c pad18 + 6c RTS), 3 bytes at the
; call site.
;
; LEGACY — do NOT use in new code. Paranoid holdover from the superseded
; "hardening ramp"; the 18c contract (pad18) is the floor now. Survives only
; because TMS_Rogue's upload loops and the silicon_strict patcher's hardened
; mode still reference it. Slated for removal once those are migrated.
tms9918_pad40:
        jsr     tms9918_pad18   ; +18c → 24c so far
        jsr     tms9918_pad18   ; +18c → 42c so far
        rts                     ; +6c RTS, total = 48c at caller's JSR site

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
; where bursts always fit. See sketchs/doc/TMS9918-SPRITE_INIT.md § 6.4.
;
; Callers that need a non-default ON value (e.g. R1 = $C2 for 16x16 sprites,
; or $C3 for 16x16+mag) must do their own write and skip vdp_display_on.
; ============================================================================

; Fall-through trick: vdp_display_on loads A=$C0 then BIT $0080 swallows the
; LDA #$80 of vdp_display_off (BIT abs is 3 bytes, $2C opcode + 2 byte addr).
; Both paths converge on the shared STA / pad / STA / pad / RTS tail.
; Saves ~12 bytes vs two independent helpers.
;
; SILICON WARNING (openMSX/dvik, modelled by POM1 since juillet 2026): an R1
; register write ALSO loads the VRAM address counter (1st byte → low bits,
; register-write 2nd byte → high bits). NEVER call these between a
; vdp_set_write/vdp_set_read and the data stream it primes — always re-set
; the VRAM address after the display flip.
vdp_display_on:
        LDA     #$C0
        .byte   $2C             ; BIT abs — opcode-swallows the next LDA #imm
vdp_display_off:
        LDA     #$80
        STA     VDP_CTRL
        JSR     tms9918_pad18
        LDA     #$81            ; cmd = $80 | reg 1 (write to R1)
        STA     VDP_CTRL
        JSR     tms9918_pad18
        RTS
