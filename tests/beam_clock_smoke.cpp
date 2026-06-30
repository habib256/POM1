// BeamClock smoke test — the shared cycle→(line,x) raster-beam mapping (Étape 3
// of the TMS9918 beam/CPU sync). Pure and standalone: it constructs the TMS9918
// NTSC geometry and pins the mapping that renderBeamCatchUp / syncSpriteScanToBeam
// (and a future GEN2 adoption) rely on.
#include "BeamClock.h"

#include <cstdio>

static int failures = 0;
static void check(bool c, const char* m)
{
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++failures; }
}

int main()
{
    // TMS9918A NTSC geometry — matches TMS9918::beamGeometry().
    const pom1::BeamGeometry g{
        /*cyclesPerFrame*/ 17062, /*totalLines*/ 262, /*activeLines*/ 192,
        /*activeWidth*/    256,   /*ticksPerCpuCycle*/ 21, /*ticksPerLine*/ 1368,
        /*activeLeftTick*/ 258,   /*ticksPerPixel*/ 4 };

    // Frame start.
    {
        const pom1::BeamPos p = pom1::beamPosAt(g, 0);
        check(p.line == 0 && p.x == 0, "frameCycle 0 -> (0,0)");
    }
    // Mid-line 80 — the exact split point Phases G/I depend on.
    {
        const pom1::BeamPos p = pom1::beamPosAt(g, 5240);
        check(p.line == 80, "frameCycle 5240 -> line 80");
        check(p.x == 98,    "frameCycle 5240 -> x 98");
    }
    // VBlank region: line clamps to activeLines, x = 0.
    {
        const pom1::BeamPos p = pom1::beamPosAt(g, 14000);
        check(p.line == 192 && p.x == 0, "VBlank cycle -> (192,0)");
    }
    // End-of-frame and over-clamp.
    {
        const pom1::BeamPos p = pom1::beamPosAt(g, g.cyclesPerFrame);
        check(p.line == 192 && p.x == 0, "end-of-frame -> (192,0)");
        const pom1::BeamPos q = pom1::beamPosAt(g, 99999);
        check(q.line == 192 && q.x == 0, "over-frame clamps");
    }
    // Negative clamps to frame start.
    {
        const pom1::BeamPos p = pom1::beamPosAt(g, -50);
        check(p.line == 0 && p.x == 0, "negative clamps to (0,0)");
    }
    // Monotonic line + x in range across the whole frame.
    {
        int prevLine = 0;
        for (int c = 0; c <= g.cyclesPerFrame; c += 7) {
            const pom1::BeamPos p = pom1::beamPosAt(g, c);
            check(p.line >= prevLine,                    "line monotonic non-decreasing");
            check(p.x >= 0 && p.x <= g.activeWidth,      "x within [0,256]");
            check(p.line >= 0 && p.line <= g.activeLines, "line within [0,192]");
            prevLine = p.line;
        }
    }

    if (failures) {
        std::fprintf(stderr, "beam_clock_smoke: %d failures\n", failures);
        return 1;
    }
    std::printf("beam_clock_smoke: all checks passed\n");
    return 0;
}
