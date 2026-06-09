#ifndef GEN2VIDEOSCANNER_H
#define GEN2VIDEOSCANNER_H

#include <cstdint>

/**
 * Gen2VideoScanner — cycle-accurate video address generator for Uncle
 * Bernie's GEN2 *release* color graphics card (back-port POM2 → POM1,
 * Phase 1 of the beam-racing roadmap; see TODO.md).
 *
 * The original POM1 GEN2 model (GraphicsCard.cpp) is a *passive* end-of-frame
 * rasteriser: it reads the framebuffer at $2000-$3FFF once per frame and never
 * tracks where the electron beam is at a given CPU cycle. The release card
 * relies on the beam position for two things the prototype could not do:
 *
 *   1. The *floating bus* — every cycle the video scanner is fetching one DRAM
 *      byte for refresh/display. Reads of an undriven address return that byte.
 *      Copy-protection and demos seed RNGs from it, so it must be bit-exact.
 *   2. The H-blank / V-blank MSB flag Bernie exposes on $C250-$C257 reads
 *      (Phase 2) — derived from the same (h_clock, v_clock) counter this class
 *      maintains.
 *
 * This module owns ONLY the timing + address arithmetic. It performs no MMIO
 * and holds no framebuffer of its own — `floatingBus()` indexes into the full
 * 64 KB address space the caller (Memory) owns. Phase 2 wires the soft switches
 * that drive `DisplayState`; Phase 3 reuses `peekVideoCycle()` for beam-raced
 * rendering.
 *
 * `scannerAddress()` is a verbatim port of POM2's `Memory::floatingBus()`
 * (itself MAME `apple2video.cpp:124-201 scanner_address`), stripped of the
 * Apple IIe-only inputs (80STORE, iieMode) the Apple-1 GEN2 never has. The
 * Apple II / II+ H-blank "phantom row" ($1000 in text mode) is kept — the GEN2
 * models the original Apple II video counter, not the IIe fix.
 */
class Gen2VideoScanner
{
public:
    // Apple II NTSC video timing — verbatim POM2 / MAME constants.
    static constexpr uint64_t kCyclesPerLine  = 65;   // CPU cycles per scanline
    static constexpr uint64_t kLinesPerFrame  = 262;  // NTSC scanlines (0..261)
    static constexpr uint64_t kCyclesPerFrame = kCyclesPerLine * kLinesPerFrame; // 17030

    // GEN2 display mode latch. Power-on default mirrors the Apple II reset
    // state: TEXT on, PAGE1, HIRES + MIXED off (Phase 2 drives this from the
    // $C250-$C257 soft switches).
    struct DisplayState {
        bool textMode  = true;
        bool mixedMode = false;
        bool page2     = false;
        bool hiRes     = false;
    };

    // Advance the video counter by `cycles` CPU cycles (driven from
    // Memory::advanceCycles when the card is plugged).
    void advanceCycles(uint64_t cycles) { cycleCounter += cycles; }

    // Reset the counter to a deterministic frame phase (used on cold plug so
    // headless tests and beam-race rendering start from a known position).
    void resetCycle() { cycleCounter = 0; }

    // Raw free-running counter (monotonic).
    uint64_t cycle() const { return cycleCounter; }

    // Position within the current frame, [0, kCyclesPerFrame). Exposed for
    // headless tests and the Phase 2 H/V-blank flag.
    uint64_t peekVideoCycle() const { return cycleCounter % kCyclesPerFrame; }

    void setDisplayState(const DisplayState& s) { display = s; }
    const DisplayState& displayState() const { return display; }

    // Pure MAME `scanner_address` oracle: the 16-bit DRAM address the video
    // scanner fetches at frame-position `frameCycle` under `ds`. No memory
    // access — the headless gen2_floatingbus_smoke test pins this directly.
    static uint16_t scannerAddress(uint64_t frameCycle, const DisplayState& ds);

    // Floating-bus byte the scanner is presenting at the current cycle.
    // `mem` is the full 64 KB address space (the scanner indexes the text
    // pages $0400-$0BFF or the HGR pages $2000-$5FFF).
    uint8_t floatingBus(const uint8_t* mem) const {
        return mem[scannerAddress(peekVideoCycle(), display)];
    }

private:
    uint64_t     cycleCounter = 0;
    DisplayState display{};
};

#endif // GEN2VIDEOSCANNER_H
