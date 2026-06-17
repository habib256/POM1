#ifndef GEN2VIDEOSCANNER_H
#define GEN2VIDEOSCANNER_H

#include <cstdint>

/**
 * Gen2VideoScanner — cycle-accurate video timing core for Uncle Bernie's
 * GEN2 *release* color graphics card (back-port POM2 → POM1; see TODO.md).
 *
 * Phase 1 (shipped): video cycle counter + MAME `scanner_address` floating
 * bus. Phase 2 (this file): the release card's soft-switch latch, the HST0
 * H/V-blank flag and the noise generator for the low 7 bits of a $C25x
 * read. Phase 3 consumes `Event` records for beam-raced rendering.
 *
 * Spec source: Bernie's PDF `doc/reference/ColorGraphicsCard_doc_for_Arnaud.pdf`,
 * transcribed in `doc/GEN2_RELEASE_questions.md` (Q1-Q10 all RESOLVED
 * 2026-06-12). Key decided semantics:
 *
 *   * Soft switches $C250-$C257 are READ-ONLY: a read toggles the addressed
 *     switch AND returns HST0 in D7; writes are ignored by design (a write
 *     would clash the card's D7 bus driver). Decode is
 *     `SEL = $Cxxx & !A11 & A9 & A4` — the 8 switches mirror every 8
 *     locations across $C2xx/$C3xx/$C6xx/$C7xx wherever A4 = 1.
 *   * HST0 = 1 while blanking (H-blank OR V-blank), 0 during live video,
 *     EXCEPT a 0 notch during the 3-cycle color-burst window (hcnt 13-15).
 *     `hst0State()` below is Bernie's behavioural C model, ported verbatim.
 *   * Low 7 bits of a $C25x read = floating data bus, intentionally
 *     unreliable; Bernie recommends emulators return random noise there so
 *     software never grows a dependency on it. `nextNoise()` is a xorshift32.
 *   * Timing: 65 CPU cycles/line; 262 lines @ 60 Hz or 312 @ 50 Hz (the
 *     card has a vertical-rate jumper; NTSC color either way). Lines 0-191
 *     are live, 192+ are VBL; visible bytes at hcnt 25-64.
 *   * Power-on latch state is INDETERMINATE on real PLDs and Apple-1 RESET
 *     never touches it — software must initialise the switches. POM1 uses a
 *     fixed documented cold state (GRAPHICS + HIRES + PAGE1 + MIX off) so
 *     legacy POM1 HGR programs that predate the soft switches still display,
 *     and never resets it on Apple-1 RESET (per spec).
 *
 * This module owns ONLY timing + address arithmetic + the mode latch. It
 * performs no MMIO and holds no framebuffer — Memory registers the $C2xx
 * PeripheralBus handler and owns the per-frame event journal.
 *
 * `scannerAddress()` is a verbatim port of POM2's `Memory::floatingBus()`
 * (itself MAME `apple2video.cpp:124-201 scanner_address`), stripped of the
 * Apple IIe-only inputs (80STORE, iieMode) the Apple-1 GEN2 never has. The
 * Apple II / II+ H-blank "phantom row" ($1000 in text mode) is kept — the
 * GEN2 models the original Apple II video counter, not the IIe fix.
 */
class Gen2VideoScanner
{
public:
    // Apple II NTSC video timing — verbatim POM2 / MAME constants.
    static constexpr uint64_t kCyclesPerLine  = 65;   // CPU cycles per scanline
    static constexpr uint64_t kLinesPerFrame  = 262;  // 60 Hz scanlines (0..261)
    static constexpr uint64_t kLinesPerFrame50Hz = 312;  // 50 Hz vertical option
    static constexpr uint64_t kCyclesPerFrame = kCyclesPerLine * kLinesPerFrame; // 17030
    static constexpr int      kVisibleLines   = 192;  // lines 0..191 are live

    // GEN2 display mode latch ($C250-$C257; 1:1 port of Apple II $C050-$C057).
    // Cold state = GRAPHICS + HIRES + PAGE1 (documented arbitrary pick — the
    // real PLD latch powers up indeterminate and RESET never touches it, so
    // any fixed value is spec-conformant; this one keeps every pre-Phase-2
    // POM1 HGR program rendering without an init sequence).
    struct DisplayState {
        bool textMode  = false;
        bool mixedMode = false;
        bool page2     = false;
        bool hiRes     = true;

        bool operator==(const DisplayState& o) const {
            return textMode == o.textMode && mixedMode == o.mixedMode
                && page2 == o.page2 && hiRes == o.hiRes;
        }
        bool operator!=(const DisplayState& o) const { return !(*this == o); }
    };

    // One soft-switch flip, journaled by Memory for the beam-raced replay
    // (POM2 `Memory::VideoEvent`, minus the IIe-only kinds). `emuCycle` is
    // the scanner's monotonic cycle at the flip (instruction-internal cycles
    // included) — the raster position is re-derived from it at render time.
    enum class EventKind : uint8_t { TextMode = 0, MixedMode = 1, Page2 = 2, HiRes = 3 };
    struct Event {
        uint64_t  emuCycle = 0;
        EventKind kind     = EventKind::TextMode;
        bool      value    = false;
    };

    // Advance the video counter by `cycles` CPU cycles (driven from
    // Memory::advanceCycles when the card is plugged).
    void advanceCycles(uint64_t cycles) { cycleCounter += cycles; }

    // Reset the counter to a deterministic frame phase (used on cold plug so
    // headless tests and beam-race rendering start from a known position).
    void resetCycle() { cycleCounter = 0; }

    // Raw free-running counter (monotonic).
    uint64_t cycle() const { return cycleCounter; }
    // Snapshot restore (see Memory's "GEN2VID" section).
    void setCycle(uint64_t c) { cycleCounter = c; }

    // 50/60 Hz vertical-rate jumper. NTSC color either way; only the VBL
    // length (and therefore the frame period and HST0 cadence) changes.
    void setFiftyHz(bool on) { linesPerFrame = on ? kLinesPerFrame50Hz : kLinesPerFrame; }
    bool isFiftyHz() const { return linesPerFrame == kLinesPerFrame50Hz; }
    uint64_t cyclesPerFrame() const { return kCyclesPerLine * linesPerFrame; }

    // Position within the current frame, [0, cyclesPerFrame()). Exposed for
    // headless tests and the HST0 H/V-blank flag.
    uint64_t peekVideoCycle() const { return cycleCounter % cyclesPerFrame(); }

    void setDisplayState(const DisplayState& s) { display = s; }
    const DisplayState& displayState() const { return display; }

    // ── HST0 — Bernie's H/V-blank flag (read back in D7 of a $C25x read) ──
    //
    // Verbatim behavioural model from the PDF (Appendix 1, Listing 2):
    // `line` 0..261 @ 60 Hz / 0..311 @ 50 Hz, `hcnt` 0..64. Returns 1 while
    // blanking, 0 in live video, with the 3-cycle color-burst 0 notch
    // (hcnt 13-15) carved out of H-blank — robust software double-samples
    // and ORs the two reads to mask the notch (Bernie's Listing 1).
    static int hst0State(int line, int hcnt);

    // HST0 sampled at an absolute scanner cycle (mod frame internally).
    int hst0At(uint64_t absCycle) const {
        const uint64_t c = absCycle % cyclesPerFrame();
        return hst0State(static_cast<int>(c / kCyclesPerLine),
                         static_cast<int>(c % kCyclesPerLine));
    }
    int hst0() const { return hst0At(cycleCounter); }

    // Low-7-bit garbage for $C25x reads — the floating data bus the release
    // card leaves undriven. Bernie's PDF explicitly recommends random noise
    // here (post #6: "I recommend to put random data there") so no software
    // grows a dependency. Deterministic xorshift32 keeps tests reproducible.
    uint8_t nextNoise() {
        noiseState ^= noiseState << 13;
        noiseState ^= noiseState >> 17;
        noiseState ^= noiseState << 5;
        return static_cast<uint8_t>(noiseState);
    }

    // Pure MAME `scanner_address` oracle: the 16-bit DRAM address the video
    // scanner fetches at frame-position `frameCycle` under `ds`. No memory
    // access — the headless gen2_floatingbus_smoke test pins this directly.
    // `lines` selects the 60 Hz (262) or 50 Hz (312) vertical counter wrap.
    static uint16_t scannerAddress(uint64_t frameCycle, const DisplayState& ds,
                                   uint64_t lines = kLinesPerFrame);

    // Floating-bus byte the scanner is presenting at the current cycle.
    // `mem` is the full 64 KB address space (the scanner indexes the text
    // pages $0400-$0BFF or the HGR pages $2000-$5FFF).
    uint8_t floatingBus(const uint8_t* mem) const {
        return mem[scannerAddress(peekVideoCycle(), display, linesPerFrame)];
    }

private:
    uint64_t     cycleCounter  = 0;
    uint64_t     linesPerFrame = kLinesPerFrame;
    uint32_t     noiseState    = 0x1D872B41u;   // arbitrary non-zero seed
    DisplayState display{};
};

#endif // GEN2VIDEOSCANNER_H
