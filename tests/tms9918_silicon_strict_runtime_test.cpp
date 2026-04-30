// TMS9918 Silicon Strict (siliconStrictMode) runtime-toggle smoke test.
//
// Pins the user-facing behaviour of the new Hardware menu / CLI toggle:
// the TMS9918 must transition cleanly between "tolerant" and "silicon-strict"
// states without a chip reset, so the user can A/B compare a running program
// (typically Galaga via CodeTank) against real hardware behaviour.
//
// What's covered:
//   - Phase 1 (strict=false): back-to-back $CC00 writes all land in VRAM.
//   - Phase 2 (flip strict=true mid-flight, no reset): back-to-back writes
//     past the first one are dropped because no cycles have elapsed.
//   - Phase 3 (flip strict=false again): tolerance restored, all writes land.
//
// References:
//   dev/SILICONBUGS.md Bug N1 (~8 cycles between writes in Mode I + sprites)
//   tests/tms9918_sprite_status_test.cpp T8 (static drop, single phase)

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
// is always accepted, regardless of the current strict-mode flag.
void strictWriteControl(TMS9918& vdp, uint8_t value)
{
    vdp.advanceCycles(10);
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
    vdp.advanceCycles(10);
    vdp.writeData(value);
}

// Configure VDP in Mode 0 (Graphic I) with display on and sprite 0 active.
// requiredAccessCycles() returns 8 for this configuration, which is the
// pessimistic case the user wants to reproduce (Galaga's render_sprites).
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
    vdp.advanceCycles(10);
    // Read-ahead buffer: setReadAddress already pre-fetched, first readData
    // returns it; we want the byte AT addr, not at addr+1, so call once.
    return vdp.readData();
}

} // namespace

int main()
{
    TMS9918 vdp;
    vdp.reset();
    initActiveSpriteMode(vdp);

    // Phase 1: tolerant mode (default after reset). Spam 4 bytes back-to-back
    // at $1000..$1003 with zero cycle gap. All four MUST land.
    {
        vdp.setSiliconStrictMode(false);
        strictSetWriteAddress(vdp, 0x1000);
        // No advanceCycles between these four. canAcceptAccess() returns
        // true unconditionally when strict=false.
        vdp.writeData(0xA1);
        vdp.writeData(0xA2);
        vdp.writeData(0xA3);
        vdp.writeData(0xA4);

        mustBeTrue(readVramAt(vdp, 0x1000) == 0xA1, "Phase1: byte 0 lands");
        mustBeTrue(readVramAt(vdp, 0x1001) == 0xA2, "Phase1: byte 1 lands");
        mustBeTrue(readVramAt(vdp, 0x1002) == 0xA3, "Phase1: byte 2 lands");
        mustBeTrue(readVramAt(vdp, 0x1003) == 0xA4, "Phase1: byte 3 lands");
    }

    // Phase 2: flip to strict mode mid-flight, no reset. After
    // strictSetWriteAddress, cyclesSinceIoAccess is 0 (the second control
    // write reset it). Open the window once with advanceCycles, do the
    // first writeData (accepted), then 3 back-to-back writeData with zero
    // gap (each must be dropped). VRAM at $1011..$1013 keeps its reset
    // value (0x00).
    {
        vdp.setSiliconStrictMode(true);
        strictSetWriteAddress(vdp, 0x1010);
        vdp.advanceCycles(10);  // open the window for write 0
        vdp.writeData(0xB1);    // accepted: gap = 10 cycles
        vdp.writeData(0xB2);    // dropped: gap = 0
        vdp.writeData(0xB3);    // dropped
        vdp.writeData(0xB4);    // dropped

        mustBeTrue(readVramAt(vdp, 0x1010) == 0xB1, "Phase2: byte 0 lands (window open)");
        mustBeTrue(readVramAt(vdp, 0x1011) == 0x00, "Phase2: byte 1 dropped");
        mustBeTrue(readVramAt(vdp, 0x1012) == 0x00, "Phase2: byte 2 dropped");
        mustBeTrue(readVramAt(vdp, 0x1013) == 0x00, "Phase2: byte 3 dropped");
    }

    // Phase 3: flip back to tolerant. The drop must stop immediately, again
    // without resetting the chip state. Same back-to-back pattern as Phase 1
    // at $1020..$1023.
    {
        vdp.setSiliconStrictMode(false);
        strictSetWriteAddress(vdp, 0x1020);
        vdp.writeData(0xC1);
        vdp.writeData(0xC2);
        vdp.writeData(0xC3);
        vdp.writeData(0xC4);

        mustBeTrue(readVramAt(vdp, 0x1020) == 0xC1, "Phase3: byte 0 lands");
        mustBeTrue(readVramAt(vdp, 0x1021) == 0xC2, "Phase3: byte 1 lands");
        mustBeTrue(readVramAt(vdp, 0x1022) == 0xC3, "Phase3: byte 2 lands");
        mustBeTrue(readVramAt(vdp, 0x1023) == 0xC4, "Phase3: byte 3 lands");
    }

    // Sanity: getter must mirror the last setter call.
    vdp.setSiliconStrictMode(true);
    mustBeTrue(vdp.isSiliconStrictMode(), "isSiliconStrictMode reflects setter (true)");
    vdp.setSiliconStrictMode(false);
    mustBeTrue(!vdp.isSiliconStrictMode(), "isSiliconStrictMode reflects setter (false)");

    std::printf("tms9918_silicon_strict_runtime: all 12 assertions passed\n");
    return 0;
}
