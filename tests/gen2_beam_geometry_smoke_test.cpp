// GEN2 adopts the shared beam clock — exhaustively, or not at all.
//
// The two cycle-accurate video engines each computed cycle→(line, x) their own
// way: the TMS9918 through pom1::beamPosAt (BeamClock.h), the GEN2 through a
// private formula in GraphicsCard::frameCycleToPos, ported verbatim from POM2's
// Apple2Display. BeamClock.h's own header named the gap — "GEN2 keeps its own
// absolute-cycle journal today; it can adopt this geometry once the
// journal/replay path is unified" — and this is that adoption.
//
// The claim is EXACT EQUIVALENCE, not approximation, so the test is exhaustive:
// every cycle of a whole 262-line frame and of a whole 312-line frame, both
// compared against the historical formula RE-DERIVED HERE rather than called.
// Re-deriving is the point — asking the new code to agree with the new code
// proves nothing, and a comment claiming the reduction is exact is not evidence.
//
// Covered:
//   §1  the geometry is internally coherent, and says what the card is;
//   §2  262-line frame: all 17 030 cycles agree with the historical formula;
//   §3  312-line frame (the card's 50 Hz vertical jumper): all 20 280;
//   §4  absolute cycles beyond one frame wrap, they do not clamp;
//   §5  the two behaviours the reduction had to preserve, stated directly:
//       VBL collapses onto line 192, and the visible window opens at cycle 25.
//
// The beam-RACING behaviour built on top of this — vertical splits, horizontal
// mid-scanline splits, the floating bus — is pinned by gen2_beam_race_smoke,
// gen2_horizontal_split_smoke and gen2_floatingbus_smoke. Those are what prove
// the beam works; this proves it still computes the same positions after being
// re-pointed at the shared clock.

#include "GraphicsCard.h"
#include "Gen2VideoScanner.h"
#include "BeamClock.h"

#include <algorithm>
#include <cassert>
#include <cstdio>

namespace {

// The formula GraphicsCard::frameCycleToPos carried before the adoption,
// transcribed from the pre-change source. Independent of everything under test.
GraphicsCard::RasterPos historicalPos(uint64_t emuCycle, uint64_t linesPerFrame)
{
    constexpr int kHiresHeight = 192;
    const uint64_t rawLine = (emuCycle / Gen2VideoScanner::kCyclesPerLine) % linesPerFrame;
    const int scanline = rawLine < static_cast<uint64_t>(kHiresHeight)
                             ? static_cast<int>(rawLine)
                             : kHiresHeight;
    const int hpos = static_cast<int>(emuCycle % Gen2VideoScanner::kCyclesPerLine);
    const int byteCol = std::clamp(hpos - 25, 0, 40);
    return {scanline, byteCol};
}

void sweepWholeFrame(uint64_t lines)
{
    const uint64_t cyclesPerFrame = Gen2VideoScanner::kCyclesPerLine * lines;
    for (uint64_t c = 0; c < cyclesPerFrame; ++c) {
        const GraphicsCard::RasterPos want = historicalPos(c, lines);
        const GraphicsCard::RasterPos got  = GraphicsCard::frameCycleToPos(c, lines);
        if (want.scanline != got.scanline || want.byteCol != got.byteCol) {
            std::printf("FAIL cycle %llu of a %llu-line frame: "
                        "historical (%d,%d) vs beam clock (%d,%d)\n",
                        (unsigned long long)c, (unsigned long long)lines,
                        want.scanline, want.byteCol, got.scanline, got.byteCol);
            assert(false && "the BeamClock reduction is not exact");
        }
    }
}

} // namespace

int main()
{
    // -----------------------------------------------------------------
    // §1 The geometry describes this card, in the shared vocabulary.
    // -----------------------------------------------------------------
    {
        Gen2VideoScanner s;
        const pom1::BeamGeometry g = s.beamGeometry();

        assert(g.cyclesPerFrame == 65 * 262 && g.cyclesPerFrame == 17030);
        assert(g.totalLines  == 262);
        assert(g.activeLines == 192);
        assert(g.activeWidth == 40 && "the GEN2's horizontal unit is the byte column");
        // A tick IS a cycle IS a byte column on this card — that identity is
        // exactly what makes the shared mapping reduce to the private one.
        assert(g.ticksPerCpuCycle == 1);
        assert(g.ticksPerPixel    == 1);
        assert(g.ticksPerLine     == 65);
        assert(g.activeLeftTick   == 25);
        // The visible window has to fit inside a line, with blanking either side.
        assert(g.activeLeftTick + g.activeWidth <= g.ticksPerLine);
        assert(g.activeLines <= g.totalLines);
        // The line division is exact for this card — 17030 / 262 = 65 with no
        // remainder — which is why the mapping is byte-identical rather than
        // merely close. (It is not exact for every geometry BeamClock serves.)
        assert(g.cyclesPerFrame % g.totalLines == 0);

        s.setFiftyHz(true);
        const pom1::BeamGeometry g50 = s.beamGeometry();
        assert(g50.totalLines == 312);
        assert(g50.cyclesPerFrame == 65 * 312 && g50.cyclesPerFrame == 20280);
        assert(g50.activeLines == 192 && "the 50 Hz jumper adds VBL lines, not picture");
        assert(g50.cyclesPerFrame % g50.totalLines == 0);
    }

    // -----------------------------------------------------------------
    // §2-§3 Every cycle of a whole frame, at both vertical rates.
    // -----------------------------------------------------------------
    sweepWholeFrame(Gen2VideoScanner::kLinesPerFrame);       // 262 → 17 030 cycles
    sweepWholeFrame(Gen2VideoScanner::kLinesPerFrame50Hz);   // 312 → 20 280 cycles

    // -----------------------------------------------------------------
    // §4 Absolute cycles wrap.
    //
    // The journal stores ABSOLUTE emulator cycles, so frameCycleToPos is asked
    // about values far past one frame. beamPosAt clamps rather than wraps, so
    // the modulo has to happen before the call — get that wrong and every event
    // after the first frame pins to the bottom-right corner.
    // -----------------------------------------------------------------
    {
        const uint64_t f = Gen2VideoScanner::kCyclesPerFrame;   // 17030
        for (uint64_t base : {f, 5 * f, 1000 * f}) {
            for (uint64_t off : {uint64_t{0}, uint64_t{1}, uint64_t{25}, uint64_t{64},
                                 uint64_t{65}, uint64_t{9999}, f - 1}) {
                const GraphicsCard::RasterPos a = GraphicsCard::frameCycleToPos(off);
                const GraphicsCard::RasterPos b = GraphicsCard::frameCycleToPos(base + off);
                assert(a.scanline == b.scanline && a.byteCol == b.byteCol &&
                       "position must be periodic in the frame, not clamped");
            }
        }
    }

    // -----------------------------------------------------------------
    // §5 The two behaviours the reduction had to preserve.
    // -----------------------------------------------------------------
    {
        // Horizontal blanking: the first 25 cycles of a line all land on byte
        // column 0, so a switch thrown in HBL governs the whole upcoming line.
        for (int hpos = 0; hpos <= 25; ++hpos)
            assert(GraphicsCard::frameCycleToPos(hpos).byteCol == 0);
        // Then one column per cycle, to the last visible byte.
        assert(GraphicsCard::frameCycleToPos(26).byteCol == 1);
        assert(GraphicsCard::frameCycleToPos(64).byteCol == 39);
        // Cycle 65 is the next line's HBL, not column 40.
        assert(GraphicsCard::frameCycleToPos(65).scanline == 1);
        assert(GraphicsCard::frameCycleToPos(65).byteCol == 0);

        // Vertical blanking collapses onto 192: lines 192..261 all report 192,
        // so the segment builder ignores them. They govern the NEXT frame,
        // whose start state already carries their effect.
        for (uint64_t line : {uint64_t{192}, uint64_t{193}, uint64_t{230}, uint64_t{261}}) {
            const uint64_t c = line * Gen2VideoScanner::kCyclesPerLine + 30;
            assert(GraphicsCard::frameCycleToPos(c).scanline == 192);
        }
        // …and the last live line still reports itself.
        assert(GraphicsCard::frameCycleToPos(191 * 65 + 30).scanline == 191);
    }

    std::printf("gen2_beam_geometry_smoke: OK (%d + %d cycles swept, exact)\n",
                65 * 262, 65 * 312);
    return 0;
}
