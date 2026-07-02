; ============================================================================
; tms9918_5strigger.asm -- 5th-sprite-overflow as mid-frame synchronisation
;                          primitive for the P-LAB TMS9918.
;
; Why:
;   The TMS9918 only has a frame /INT (no line interrupt), so the MSX/SG-1000
;   trick of scheduling mid-frame work via an IRQ is not possible regardless
;   of /INT wiring — even though P-LAB does wire /INT → /IRQ. The status
;   register, however, exposes another raster-following bit: bit 6 (5S =
;   "5th sprite overflow") sets the moment the chip discovers a 5th sprite
;   on the current scan line.
;
;   By placing 5 invisible sprites at a chosen Y, the program can spin on
;   bit 6 to trap the raster as it crosses that scan line — same idea as
;   Daniel Vik's "Waves" demo on MSX1, only with polling instead of IRQ.
;
;   Use cases on POM1:
;     * change the colour table mid-frame (palette split)
;     * swap the name table address (R2) for an instant top/bottom split
;     * upload extra patterns to VRAM during the active-display half-frame
;
; Cost:
;   Polling busy-loop = ~6 cycles per iteration (BIT VDP_CTRL + BVC).
;   Worst case = (192 - line) * 228c ≈ ~12 000c when line = 8 (top of
;   screen) and we just exited V-blank. Fine for a single split per frame.
;
; Caveats — read these once:
;   1. Reading $CC01 clears bits 5 (collision), 6 (5S) AND 7 (F) at once.
;      Call WAIT_VBLANK FIRST, then arm_5s_trigger, then wait_5s_trigger.
;      Don't poll WAIT_VBLANK between the two — bit 7 (F) clears bit 6
;      side-effect-wise on every read.
;
;   2. The 5S flag latches on the FIRST line where the 5th sprite is
;      found, and ONLY while F (bit 7) is clear — the silicon gates the
;      5th-sprite comparator on F (TI datasheet; openMSX). The read that
;      observes 5S also clears it (with F and C — a status read latch-
;      clears all three), so wait_5s_trigger's own poll both keeps F
;      drained and consumes the flag it returns on. If the 5 sprites
;      stay armed, later lines with 5+ sprites simply re-latch it.
;
;   3. The chip checks Y first; X-position and pattern do NOT matter for
;      counting. We use early-clock + colour 0 (transparent) so the
;      sprites are entirely invisible regardless of the pattern table
;      contents.
;
;   4. If the program already uses sprites for gameplay, calling this
;      primitive trashes SAT entries 0..5. Save/restore around the call,
;      OR reserve the first 5 SAT slots as "trigger sprites" with a
;      Y = $D0 default and bump them to a real Y only when arming.
;
;   5. The "displayed Y" is (Y_attribute + 1) mod 256 — TMS9918 Y=0 shows
;      on scan line 1, Y=255 shows on line 0. Pass the actual scan line
;      you want to TRAP (1..192) as the argument and we subtract 1
;      internally. Pass 0 for "last line" (Y attribute = $FF = -1).
;
;   6. POM1 with `siliconStrictMode = ON` (default) drops VRAM writes
;      < ~7.5 c apart in Mode I + sprites. We use the same JSR
;      tms9918_pad18 pattern as tms9918m1.asm — every consecutive
;      VDP_DATA / VDP_CTRL store is gated.
;
; Public symbols:
;   arm_5s_trigger   -- write 5 invisible sprites at scan line A
;   wait_5s_trigger  -- spin BIT $CC01 / BVC -- until 5S sets
;
; Cross-link:
;   To "disarm" the trigger after use, call `disable_sprites`
;   (exported by tms9918m1.asm or tms9918m2.asm) — writes Y=$D0 to
;   sprite #0 attribute, the chip stops the chain at that entry.
; ============================================================================

        .import tms9918_pad18
.include "tms9918.inc"

.export arm_5s_trigger, wait_5s_trigger

; --- BSS scratch (1 byte) ---
.segment "BSS"
_y_attr: .res 1

; ============================================================================
.segment "CODE"
; ============================================================================

; ----------------------------------------------------------------------------
; arm_5s_trigger -- place 5 invisible sprites at scan line A.
;   In:        A = target scan line (1..192; 0 → wraps to last line)
;   Out:       SAT[0..4] populated, SAT[5].Y = $D0 (chain terminator)
;   Clobbers:  A, X
; ----------------------------------------------------------------------------
arm_5s_trigger:
        ; Convert scan line → Y attribute: displayed at (Y+1) mod 256.
        SEC
        SBC #1
        STA _y_attr             ; cached across the 5-iteration loop

        ; Address VDP for write at $1B00 (sprite attribute table base).
        LDA #$00
        STA VDP_CTRL
        JSR tms9918_pad18       ; pad12 between back-to-back CTRL stores
        LDA #$5B                ; $1B | $40 = write-mode address $1B00
        STA VDP_CTRL
        JSR tms9918_pad18       ; cushion before first STA VDP_DATA

        ; Five SAT entries × 4 bytes each:
        ;   byte 0: Y = scan_line - 1 (cached in _y_attr)
        ;   byte 1: X = 0 (with bit 7 of byte 3 = early clock → x_screen = -32)
        ;   byte 2: pattern name = 0 (irrelevant — colour 0 is transparent)
        ;   byte 3: $80 = early clock + colour 0 (fully invisible)
        LDX #5
@s_lp:  LDA _y_attr             ; byte 0
        STA VDP_DATA
        JSR tms9918_pad18
        LDA #0                  ; byte 1
        STA VDP_DATA
        JSR tms9918_pad18
        LDA #0                  ; byte 2
        STA VDP_DATA
        JSR tms9918_pad18
        LDA #$80                ; byte 3
        STA VDP_DATA
        JSR tms9918_pad18
        DEX
        BNE @s_lp

        ; Sprite #5 = chain terminator (Y = $D0 stops the SAT scan).
        LDA #$D0
        STA VDP_DATA
        JSR tms9918_pad18
        RTS

; ----------------------------------------------------------------------------
; wait_5s_trigger -- spin until 5S flag (status register bit 6) is set.
;   Exploits BIT: bit 6 of the operand goes into the V CPU flag, so we
;   can branch with BVC. Cost: ~6c per loop iteration (4c BIT + 3c BVC
;   taken / 2c fall-through).
;   Side effects: each iteration ALSO consumes bits 5 (collision) and 7
;     (F = V-blank). Don't rely on those being intact after this returns.
;   In:        —
;   Clobbers:  flags only (A is preserved — BIT does not load A)
; ----------------------------------------------------------------------------
wait_5s_trigger:
@w:     BIT VDP_CTRL            ; N=bit7(F), V=bit6(5S)
        BVC @w                  ; loop while 5S still 0
        RTS
