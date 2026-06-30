#pragma once
#include <cstdint>

// BeamClock — shared NTSC raster-beam position seam for POM1's cycle-accurate
// video engines. It maps a within-frame CPU cycle (optionally plus the in-flight
// instruction offset, for sub-instruction accuracy) to the beam's (line, x).
//
// This is the foundation Étape 0-2 of the TMS9918 beam/CPU sync already compute
// inline (renderBeamCatchUp / syncSpriteScanToBeam); factoring it here makes the
// cycle→(h,v) mapping a single reusable unit — the "BeamClock + renderUntil(beam)"
// socle the TODO calls out as shared between the TMS9918 and the GEN2 beam engine
// (and POM2). GEN2 keeps its own absolute-cycle journal today; it can adopt this
// geometry once the journal/replay path is unified (Étape 3 remainder).
//
// Pure header, depends only on <cstdint> — so it is unit-testable standalone and
// carries no peripheral coupling.

namespace pom1 {

// NTSC raster geometry, all in the engine's native units. The caller fills it
// from its own constants (TMS9918 supplies kCyclesPerFrame, 262 lines, 1368
// ticks/line, 21 ticks/cycle, the active-left tick and 4 ticks/pixel).
struct BeamGeometry {
    int cyclesPerFrame;    // CPU cycles per NTSC frame (59.94 Hz)
    int totalLines;        // total scanlines incl. VBlank (262 NTSC)
    int activeLines;       // visible scanlines (192)
    int activeWidth;       // visible pixels per line (256)
    int ticksPerCpuCycle;  // VDP ticks per CPU cycle (21)
    int ticksPerLine;      // VDP ticks per scanline (1368)  [reserved / symmetry]
    int activeLeftTick;    // tick within a line where the visible area begins
    int ticksPerPixel;     // VDP ticks per visible pixel (4)
};

// Beam position: line ∈ [0, totalLines] (clamped to activeLines for the active
// region), x ∈ [0, activeWidth] (0 outside the active region / VBlank).
struct BeamPos { int line; int x; };

// Map a within-frame CPU cycle to the beam (line, x). frameCycle is clamped to
// [0, cyclesPerFrame]. The line uses the same totalLines-division the per-line
// scan/render cadence uses (so a beam line agrees with `frameCycle*262/frame`);
// x is the active column derived from the within-line VDP tick.
inline BeamPos beamPosAt(const BeamGeometry& g, int frameCycle)
{
    if (frameCycle < 0) frameCycle = 0;
    if (frameCycle > g.cyclesPerFrame) frameCycle = g.cyclesPerFrame;

    const int totalLine = static_cast<int>(
        static_cast<int64_t>(frameCycle) * g.totalLines / g.cyclesPerFrame);
    const int line = (totalLine < g.activeLines) ? totalLine : g.activeLines;

    int x = 0;
    if (line < g.activeLines) {
        const int lineStartCycle = static_cast<int>(
            static_cast<int64_t>(totalLine) * g.cyclesPerFrame / g.totalLines);
        const int ticksIntoLine = (frameCycle - lineStartCycle) * g.ticksPerCpuCycle;
        x = (ticksIntoLine - g.activeLeftTick) / g.ticksPerPixel;
        if (x < 0) x = 0;
        if (x > g.activeWidth) x = g.activeWidth;
    }
    return { line, x };
}

}  // namespace pom1
