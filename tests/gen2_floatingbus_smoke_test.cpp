// GEN2 release video scanner / floating-bus oracle.
//
// Phase 1 of the GEN2 beam-racing back-port (TODO.md). Pins the MAME
// `scanner_address` arithmetic ported into Gen2VideoScanner so later phases
// (the $C250-$C257 MSB blank flag, beam-raced rendering) build on a verified
// address generator instead of a hand-tuned one.
//
// The expected addresses below were computed by hand from the MAME formula for
// each (frameCycle, DisplayState) tuple, so a transcription error in the bit
// packing fails the test rather than silently matching whatever the code emits.
//
// Reference derivations (addend0 = 0x0D is the scanner's built-in lead-in):
//   * frameCycle 0  → v_clock 0, h_clock 0  (HBL, h<25)
//       sum = 0x0D → low bits 0x68
//       HGR page 1 → 0x2000 | 0x68         = 0x2068
//       HGR page 2 → 0x4000 | 0x68         = 0x4068
//       TEXT page 1 → 0x0400 | 0x1000(phantom) | 0x68 = 0x1468
//   * frameCycle 25 → v_clock 0, h_clock 25 (first displayed byte, line 0)
//       h_state 24 → addend1 3, sum (0x0D+3) & 0xF = 0
//       HGR page 1 → 0x2000 ; HGR page 2 → 0x4000 ; TEXT page 1 → 0x0400
//   * frameCycle 160*65+25 = 10425 → v_clock 160 (bottom text band: v_4 & v_2)
//       MIXED forces HGR off → text addressing → 0x0650 (inside $0400-$07FF)
//
// Self-contained: no ImGui, no CPU, no Memory — drives Gen2VideoScanner directly.

#include "Gen2VideoScanner.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { \
    std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

#define CHECK_ADDR(got, want, msg) do { \
    const uint16_t g_ = (got); const uint16_t w_ = (want); \
    if (g_ != w_) { \
        std::printf("FAIL: %s (got $%04X, want $%04X)\n", msg, g_, w_); \
        ++failures; } } while (0)

using DS = Gen2VideoScanner::DisplayState;

DS hgr(bool page2) { DS d; d.textMode = false; d.hiRes = true; d.page2 = page2; return d; }
DS text(bool page2) { DS d; d.textMode = true; d.hiRes = false; d.page2 = page2; return d; }

}  // namespace

int main()
{
    // -- Timing constants -----------------------------------------------------
    CHECK(Gen2VideoScanner::kCyclesPerLine == 65, "65 cycles per scanline");
    CHECK(Gen2VideoScanner::kLinesPerFrame == 262, "262 scanlines per frame");
    CHECK(Gen2VideoScanner::kCyclesPerFrame == 17030, "17030 cycles per frame");

    // -- Scanner address oracle (exact, hand-derived) -------------------------
    CHECK_ADDR(Gen2VideoScanner::scannerAddress(0, hgr(false)), 0x2068,
               "HGR page1 @ cycle 0 (lead-in)");
    CHECK_ADDR(Gen2VideoScanner::scannerAddress(0, hgr(true)), 0x4068,
               "HGR page2 @ cycle 0 (lead-in)");
    CHECK_ADDR(Gen2VideoScanner::scannerAddress(25, hgr(false)), 0x2000,
               "HGR page1 @ cycle 25 (first displayed byte)");
    CHECK_ADDR(Gen2VideoScanner::scannerAddress(25, hgr(true)), 0x4000,
               "HGR page2 @ cycle 25 (first displayed byte)");
    CHECK_ADDR(Gen2VideoScanner::scannerAddress(25, text(false)), 0x0400,
               "TEXT page1 @ cycle 25 (first displayed char)");
    CHECK_ADDR(Gen2VideoScanner::scannerAddress(0, text(false)), 0x1468,
               "TEXT page1 @ cycle 0 (HBL phantom row $1000)");

    // -- HGR page select: only the base differs, low bits identical -----------
    for (uint64_t c : {0ULL, 25ULL, 100ULL, 9001ULL}) {
        const uint16_t p1 = Gen2VideoScanner::scannerAddress(c, hgr(false));
        const uint16_t p2 = Gen2VideoScanner::scannerAddress(c, hgr(true));
        CHECK((p1 & 0x1FFF) == (p2 & 0x1FFF), "HGR page2 keeps low 13 bits of page1");
        CHECK((p1 & 0xE000) == 0x2000, "HGR page1 sits in $2000-$3FFF");
        CHECK((p2 & 0xE000) == 0x4000, "HGR page2 sits in $4000-$5FFF");
    }

    // -- Active-video HGR addresses stay inside the framebuffer ---------------
    // Skip the lead-in/HBL cycles (h_clock < 25); displayed bytes must land in
    // page 1 ($2000-$3FFF).
    for (uint64_t line = 0; line < 192; ++line) {
        for (uint64_t h = 25; h < 65; ++h) {
            const uint64_t cyc = line * 65 + h;
            const uint16_t a = Gen2VideoScanner::scannerAddress(cyc, hgr(false));
            CHECK((a & 0xE000) == 0x2000, "displayed HGR byte in page 1");
        }
    }

    // -- Mixed mode: bottom 4 text rows fall back to text addressing ----------
    {
        const uint64_t cyc = 160 * 65 + 25;   // v_clock 160, first displayed col
        DS m = hgr(false); m.mixedMode = true;
        const uint16_t a = Gen2VideoScanner::scannerAddress(cyc, m);
        CHECK_ADDR(a, 0x0650, "MIXED bottom-4-rows reads text page1");
        CHECK((a & 0xE000) == 0x0000 && a >= 0x0400 && a < 0x0800,
              "MIXED bottom row inside text page1 $0400-$07FF, not HGR");
        // The top of the screen in the same MIXED frame is still HGR.
        const uint16_t top = Gen2VideoScanner::scannerAddress(0 * 65 + 25, m);
        CHECK((top & 0xE000) == 0x2000, "MIXED top rows still HGR page1");
    }

    // -- Floating-bus byte read + counter advance/wrap ------------------------
    {
        std::vector<uint8_t> mem(0x10000, 0x00);
        for (int a = 0x2000; a <= 0x3FFF; ++a) mem[a] = 0xA1;   // HGR page 1
        for (int a = 0x4000; a <= 0x5FFF; ++a) mem[a] = 0xB2;   // HGR page 2

        Gen2VideoScanner sc;
        CHECK(sc.peekVideoCycle() == 0, "fresh scanner at frame phase 0");
        sc.advanceCycles(25);                 // land on first displayed byte
        CHECK(sc.peekVideoCycle() == 25, "advanceCycles moves frame phase");

        sc.setDisplayState(hgr(false));
        CHECK(sc.floatingBus(mem.data()) == 0xA1, "floating bus samples HGR page1 (0xA1)");
        sc.setDisplayState(hgr(true));
        CHECK(sc.floatingBus(mem.data()) == 0xB2, "floating bus samples HGR page2 (0xB2)");

        // Wrap: one full frame plus 7 cycles returns to phase 7.
        Gen2VideoScanner w;
        w.advanceCycles(Gen2VideoScanner::kCyclesPerFrame + 7);
        CHECK(w.peekVideoCycle() == 7, "video cycle wraps modulo frame length");
    }

    if (failures == 0) std::printf("OK gen2_floatingbus_smoke\n");
    return failures == 0 ? 0 : 1;
}
