// TMS9918 Silicon Strict (siliconStrictMode) runtime-toggle smoke test.
//
// Pins the user-facing behaviour of the Hardware menu / CLI toggle:
// the TMS9918 must transition cleanly between "tolerant" and "silicon-strict"
// states without a chip reset, so the user can A/B compare a running program
// (typically Galaga via CodeTank) against real hardware behaviour.
//
// Hardened 40c contract — passing POM1 strict ⇒ silicon-safe (cf.
// dev/SILICONBUGS.md Bug N°1). Per-mode thresholds:
//   Mode I + sprites active        : 40 cycles  (was 24, May 2026 second bump)
//   Mode I sprites OFF / Multicolor:  6 cycles
//   Text mode                      :  4 cycles
//   Display blanked / VBlank       :  2 cycles
//
// What's covered:
//   - Phase 1 (strict=false): back-to-back $CC00 writes all land in VRAM.
//   - Phase 2 (strict=true, Mode I + sprites): writes need ≥ 40-cycle gap;
//     a 39-cycle gap drops, a 40-cycle gap is accepted.
//   - Phase 3 (flip strict=false again): tolerance restored, all writes land.
//   - Phase 4 (strict=true, text mode): 4-cycle window.
//   - Phase 5 (strict=true, multicolor): 6-cycle window.
//   - Phase 6 (strict=true, display blanked): 2-cycle window.

#include "TMS9918.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

void mustBeTrue(bool cond, const char* msg)
{
    if (!cond) {
        std::fprintf(stderr, "ASSERT FAILED: %s\n", msg);
        std::exit(1);
    }
}

// Strict-aware control / data helpers: advance enough cycles so the access
// is always accepted, regardless of the current strict-mode flag. 44c covers
// the worst-case (40c Mode I + sprites) with margin.
void strictWriteControl(TMS9918& vdp, uint8_t value)
{
    vdp.advanceCycles(44);
    vdp.writeControl(value);
}

void strictSetWriteAddress(TMS9918& vdp, uint16_t addr)
{
    strictWriteControl(vdp, (uint8_t)(addr & 0xFF));
    strictWriteControl(vdp, (uint8_t)(0x40 | ((addr >> 8) & 0x3F)));
}

void strictSetReadAddress(TMS9918& vdp, uint16_t addr)
{
    strictWriteControl(vdp, (uint8_t)(addr & 0xFF));
    strictWriteControl(vdp, (uint8_t)((addr >> 8) & 0x3F));
}

void strictWriteData(TMS9918& vdp, uint8_t value)
{
    vdp.advanceCycles(44);
    vdp.writeData(value);
}

// Configure VDP in Mode 0 (Graphic I) with display on and sprite 0 active.
// requiredAccessCycles() returns 40 for this configuration (Galaga's
// render_sprites worst case under the hardened contract).
void initActiveSpriteMode(TMS9918& vdp)
{
    // R1 = 0xC0 -> display on (bit 6), 16K (bit 7), Mode 0, no sprite mag, no IRQ.
    strictWriteControl(vdp, 0xC0);
    strictWriteControl(vdp, 0x81);
    // R5 = 0x06 -> SAT base = $0300.
    strictWriteControl(vdp, 0x06);
    strictWriteControl(vdp, 0x85);
    // SAT[0] = (Y=$50, X=$00, name=1, color=$0F): an active sprite, not the
    // terminator. requiredAccessCycles() walks SAT looking for any non-$D0
    // entry to decide spritesActive=true.
    strictSetWriteAddress(vdp, 0x0300);
    strictWriteData(vdp, 0x50);
    strictWriteData(vdp, 0x00);
    strictWriteData(vdp, 0x01);
    strictWriteData(vdp, 0x0F);
}

uint8_t readVramAt(TMS9918& vdp, uint16_t addr)
{
    strictSetReadAddress(vdp, addr);
    vdp.advanceCycles(44);
    return vdp.readData();
}

// Phase helpers: drive a write at a precise gap and observe whether it
// landed (accepted) or stayed at zero (dropped).
void writeAtGap(TMS9918& vdp, uint16_t addr, uint8_t value, int gapCycles)
{
    strictSetWriteAddress(vdp, addr);
    vdp.advanceCycles(gapCycles);
    vdp.writeData(value);
}

} // namespace

int main()
{
    int assertions = 0;

    // ----------------------------------------------------------------------
    // Phase 1: tolerant mode (default after reset). Spam 4 bytes back-to-back
    // at $1000..$1003 with zero cycle gap. All four MUST land.
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp;
        vdp.reset();
        initActiveSpriteMode(vdp);
        vdp.setSiliconStrictMode(false);
        strictSetWriteAddress(vdp, 0x1000);
        vdp.writeData(0xA1);
        vdp.writeData(0xA2);
        vdp.writeData(0xA3);
        vdp.writeData(0xA4);

        mustBeTrue(readVramAt(vdp, 0x1000) == 0xA1, "Phase1: byte 0 lands"); ++assertions;
        mustBeTrue(readVramAt(vdp, 0x1001) == 0xA2, "Phase1: byte 1 lands"); ++assertions;
        mustBeTrue(readVramAt(vdp, 0x1002) == 0xA3, "Phase1: byte 2 lands"); ++assertions;
        mustBeTrue(readVramAt(vdp, 0x1003) == 0xA4, "Phase1: byte 3 lands"); ++assertions;
    }

    // ----------------------------------------------------------------------
    // Phase 2: strict mode, Mode I + sprites → 40-cycle window.
    // gap = 39 → drop; gap = 40 → accept.
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp;
        vdp.reset();
        initActiveSpriteMode(vdp);
        vdp.setSiliconStrictMode(true);

        writeAtGap(vdp, 0x1010, 0xB1, 40);  // accept (boundary)
        writeAtGap(vdp, 0x1011, 0xB2, 39);  // drop (boundary -1)
        writeAtGap(vdp, 0x1012, 0xB3, 40);  // accept (boundary)
        writeAtGap(vdp, 0x1013, 0xB4, 100); // accept (way over)

        mustBeTrue(readVramAt(vdp, 0x1010) == 0xB1, "Phase2: gap=40 accepted"); ++assertions;
        mustBeTrue(readVramAt(vdp, 0x1011) == 0x00, "Phase2: gap=39 dropped"); ++assertions;
        mustBeTrue(readVramAt(vdp, 0x1012) == 0xB3, "Phase2: gap=40 boundary accepted"); ++assertions;
        mustBeTrue(readVramAt(vdp, 0x1013) == 0xB4, "Phase2: gap=100 accepted"); ++assertions;
    }

    // ----------------------------------------------------------------------
    // Phase 3: tolerant restored, no chip reset. Same back-to-back as Phase 1.
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp;
        vdp.reset();
        initActiveSpriteMode(vdp);
        vdp.setSiliconStrictMode(true);
        // (... drop a couple to dirty the dropped-counter ...)
        vdp.writeData(0x99);
        vdp.writeData(0x99);
        vdp.setSiliconStrictMode(false);
        strictSetWriteAddress(vdp, 0x1020);
        vdp.writeData(0xC1);
        vdp.writeData(0xC2);
        vdp.writeData(0xC3);
        vdp.writeData(0xC4);

        mustBeTrue(readVramAt(vdp, 0x1020) == 0xC1, "Phase3: byte 0 lands"); ++assertions;
        mustBeTrue(readVramAt(vdp, 0x1021) == 0xC2, "Phase3: byte 1 lands"); ++assertions;
        mustBeTrue(readVramAt(vdp, 0x1022) == 0xC3, "Phase3: byte 2 lands"); ++assertions;
        mustBeTrue(readVramAt(vdp, 0x1023) == 0xC4, "Phase3: byte 3 lands"); ++assertions;
    }

    // ----------------------------------------------------------------------
    // Phase 4: strict + text mode (M1=1) → 4-cycle window.
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp;
        vdp.reset();
        // R1 = 0xD0 -> display on (bit 6), 16K (bit 7), Mode 1 (M1=bit 4)
        strictWriteControl(vdp, 0xD0);
        strictWriteControl(vdp, 0x81);
        vdp.setSiliconStrictMode(true);

        writeAtGap(vdp, 0x1100, 0xD1, 4);   // accept (boundary)
        writeAtGap(vdp, 0x1101, 0xD2, 3);   // drop  (boundary -1)
        writeAtGap(vdp, 0x1102, 0xD3, 10);  // accept

        mustBeTrue(readVramAt(vdp, 0x1100) == 0xD1, "Phase4 text: gap=4 accepted"); ++assertions;
        mustBeTrue(readVramAt(vdp, 0x1101) == 0x00, "Phase4 text: gap=3 dropped"); ++assertions;
        mustBeTrue(readVramAt(vdp, 0x1102) == 0xD3, "Phase4 text: gap=10 accepted"); ++assertions;
    }

    // ----------------------------------------------------------------------
    // Phase 5: strict + multicolor (M2=1) → 6-cycle window.
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp;
        vdp.reset();
        // R1 = 0xC8 -> display on, 16K, Mode 3 (M2=bit 3)
        strictWriteControl(vdp, 0xC8);
        strictWriteControl(vdp, 0x81);
        vdp.setSiliconStrictMode(true);

        writeAtGap(vdp, 0x1200, 0xE1, 6);   // accept (boundary)
        writeAtGap(vdp, 0x1201, 0xE2, 5);   // drop  (boundary -1)
        writeAtGap(vdp, 0x1202, 0xE3, 10);  // accept

        mustBeTrue(readVramAt(vdp, 0x1200) == 0xE1, "Phase5 multicolor: gap=6 accepted"); ++assertions;
        mustBeTrue(readVramAt(vdp, 0x1201) == 0x00, "Phase5 multicolor: gap=5 dropped"); ++assertions;
        mustBeTrue(readVramAt(vdp, 0x1202) == 0xE3, "Phase5 multicolor: gap=10 accepted"); ++assertions;
    }

    // ----------------------------------------------------------------------
    // Phase 6: strict + display blanked (R1 bit 6 = 0) → 16-cycle window.
    // Mirrors the Mode I+sprites worst case (1 slot/16 VDP + STA + margin).
    // Strict means strict — no per-mode escape hatch. The auto-patcher
    // handles cross-JSR boundaries via VDP-tail routine detection.
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp;
        vdp.reset();
        // R1 = 0x80 -> 16K, display OFF (bit 6 = 0), Mode 0
        strictWriteControl(vdp, 0x80);
        strictWriteControl(vdp, 0x81);
        vdp.setSiliconStrictMode(true);

        writeAtGap(vdp, 0x1300, 0xF1, 16);  // accept (boundary)
        writeAtGap(vdp, 0x1301, 0xF2, 15);  // drop  (boundary -1)
        writeAtGap(vdp, 0x1302, 0xF3, 8);   // drop  (LDA abs / STA / LDA abs / STA)
        writeAtGap(vdp, 0x1303, 0xF4, 6);   // drop  (LDA #imm bridge)

        mustBeTrue(readVramAt(vdp, 0x1300) == 0xF1, "Phase6 blank: gap=16 accepted"); ++assertions;
        mustBeTrue(readVramAt(vdp, 0x1301) == 0x00, "Phase6 blank: gap=15 dropped"); ++assertions;
        mustBeTrue(readVramAt(vdp, 0x1302) == 0x00, "Phase6 blank: gap=8 dropped"); ++assertions;
        mustBeTrue(readVramAt(vdp, 0x1303) == 0x00, "Phase6 blank: gap=6 dropped"); ++assertions;
    }

    // ----------------------------------------------------------------------
    // Phase 7: cross-JSR boundary at VBlank gate (Snake draw_hud regression).
    //
    // Models the sequence around `JSR emit_3digit_vdp` in TMS_Snake.asm:
    //   STA VDP_DATA      ; callee's last opcode (4c) → counter reset to 4
    //   RTS               ; (6c) → counter at 10
    //   JSR tms9918_pad40 ; (40c) → counter at 50         ← patcher-injected
    //   LDA #$17          ; (2c) → counter at 52
    //   STA VDP_CTRL      ; check counter (52 ≥ 16) → ACCEPT
    //   JSR tms9918_pad40 ; (40c) → counter at 4+40 = 44   ← patcher-injected
    //   LDA #$58          ; (2c) → counter at 46
    //   STA VDP_CTRL      ; check counter (46 ≥ 16) → ACCEPT
    //
    // Without the patcher's cross-JSR pads, the counter at each STA VDP_CTRL
    // would be 12c (10c RTS-bridge + 2c LDA), exactly matching the
    // user-observed gap=12 drops in Snake at strict 16c-VBlank.
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp;
        vdp.reset();
        // R1 = 0xC0 -> display ON, 16K, Mode 0, sprites 8x8
        strictWriteControl(vdp, 0xC0);
        strictWriteControl(vdp, 0x81);
        // Drive frameCycleCounter past kActiveDisplayCycles so we're in VBlank.
        vdp.advanceCycles(13000);
        vdp.setSiliconStrictMode(true);

        // Set up VRAM write address = $1000.
        strictSetWriteAddress(vdp, 0x1000);

        // ---- callee's last STA VDP_DATA ----
        vdp.advanceCycles(44);            // saturated entry to writeData
        vdp.writeData(0xCA);              // ACCEPT, counter resets to 0 inside
        vdp.advanceCycles(4);             // STA opcode's own 4c

        // ---- bridge: RTS (6c) + JSR pad40 (40c) + LDA #$17 (2c) = 48c ----
        vdp.advanceCycles(6 + 40 + 2);
        vdp.writeControl(0x17);           // 1st byte of addr pair, counter=52 → ACCEPT
        vdp.advanceCycles(4);             // STA VDP_CTRL opcode's own 4c

        // ---- bridge: JSR pad40 (40c) + LDA #$58 (2c) = 42c ----
        vdp.advanceCycles(40 + 2);
        vdp.writeControl(0x58);           // 2nd byte, counter=46 → ACCEPT, addr = $1817
        vdp.advanceCycles(4);

        // Final data write to confirm address landed correctly.
        vdp.advanceCycles(40 + 2);        // mirror Snake's @hi_lp: JSR pad40 + LDA hud_hi_str,X
        vdp.writeData(0xAB);

        mustBeTrue(readVramAt(vdp, 0x1817) == 0xAB,
                   "Phase7 cross-JSR: pad40 cushion lets the post-RTS address-set + data-write land at the intended VRAM address"); ++assertions;

        // Negative control: same pattern WITHOUT the cross-JSR pad → drop.
        // Bridge becomes RTS (6c) + LDA #imm (2c) = 8c, gap = 12c < 16c VBlank.
        vdp.reset();
        strictWriteControl(vdp, 0xC0);
        strictWriteControl(vdp, 0x81);
        vdp.advanceCycles(13000);
        vdp.setSiliconStrictMode(true);
        strictSetWriteAddress(vdp, 0x2000);
        vdp.advanceCycles(44);
        vdp.writeData(0xDE);              // accepted
        vdp.advanceCycles(4);             // STA's 4c
        vdp.advanceCycles(6 + 2);         // RTS + LDA only — NO pad40
        vdp.writeControl(0x17);           // gap=12 < 16 → DROP
        // Latch is still 0 from last accepted writeControl in the strictWriteControl
        // path. Even after the dropped $17, latch stays 0. So the next byte
        // write — even if accepted — is interpreted as a fresh 1st byte, the
        // address is never set, and the subsequent writeData lands at the
        // chip's *previous* vramAddr (= $2000). VRAM[$2017] therefore stays 0.
        vdp.advanceCycles(44);
        vdp.writeControl(0x58);           // accepted but as 1st byte of new pair
        vdp.advanceCycles(44);
        vdp.writeData(0xEE);              // lands at $2000 (or wherever vramAddr is)
        mustBeTrue(readVramAt(vdp, 0x2017) == 0x00,
                   "Phase7 negative: without cross-JSR pad, the dropped addr-set leaves VRAM[$2017] unwritten"); ++assertions;
    }

    // Sanity: getter must mirror the last setter call.
    {
        TMS9918 vdp;
        vdp.setSiliconStrictMode(true);
        mustBeTrue(vdp.isSiliconStrictMode(), "isSiliconStrictMode reflects setter (true)"); ++assertions;
        vdp.setSiliconStrictMode(false);
        mustBeTrue(!vdp.isSiliconStrictMode(), "isSiliconStrictMode reflects setter (false)"); ++assertions;
    }

    std::printf("tms9918_silicon_strict_runtime: all %d assertions passed\n", assertions);
    return 0;
}
