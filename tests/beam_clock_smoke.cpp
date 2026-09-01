// BeamClock smoke test — the shared cycle→(line,x) raster-beam mapping (Étape 3
// of the TMS9918 beam/CPU sync). Pure and standalone: it constructs the TMS9918
// NTSC geometry and pins the mapping that renderBeamCatchUp / syncSpriteScanToBeam
// (and a future GEN2 adoption) rely on.
#include <initializer_list>
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

    // ---------------------------------------------------------------
    // rawLineAt / lineTickAt — the ordinals BEHIND beamPosAt.
    //
    // These exist because a renderer and a JOURNAL want different things from
    // the same beam. beamPosAt answers "what pixel is lit", so it clamps the
    // line onto the active region and zeroes x outside it. The GEN2 journal
    // sorts its soft-switch events by (line, column) — VBlank events included,
    // and those are precisely the ones that set the state the next frame starts
    // from. Collapsing them all to column 0 left their order to an unstable
    // sort. That defect was introduced and caught the same afternoon, by the
    // exhaustive sweep in gen2_beam_geometry_smoke.
    // ---------------------------------------------------------------
    {
        // Sweep the whole frame and watch rawLineAt change. This asserts the
        // properties WITHOUT assuming a line-start formula: with a frame that
        // does not divide evenly into its lines (this geometry does not — the
        // GEN2's 17030/262 = 65 does, which is why its adoption reduces
        // byte-for-byte), `line * cyclesPerLine` is not the exact inverse of
        // rawLineAt, and a first draft of this section that assumed it was
        // produced 204 false failures.
        {
            int prevLine = -1;
            int prevTick = -1;
            int lineStarts = 0;
            for (int c = 0; c <= g.cyclesPerFrame; ++c) {
                const int line = pom1::rawLineAt(g, c);
                const int tick = pom1::lineTickAt(g, c);
                check(line >= 0 && line <= g.totalLines, "rawLineAt within the frame");
                check(tick >= 0,                          "lineTickAt is never negative");
                if (line != prevLine) {
                    // A new scanline: the ordinal RESTARTS. Not necessarily at
                    // exactly 0 — when the frame does not divide evenly into its
                    // lines, the floor'd line-start can sit one cycle before the
                    // first cycle that actually maps to the line, so the first
                    // tick is 0 or one cycle's worth. That is beamPosAt's
                    // original arithmetic, inherited deliberately rather than
                    // "fixed": the TMS9918 renderer is calibrated on it, and the
                    // GEN2 (whose 17030/262 = 65 divides evenly) always gets 0.
                    // What an ordering key needs is that it resets, and it does.
                    check(line == prevLine + 1 || prevLine == -1,
                          "rawLineAt advances one line at a time");
                    check(tick <= g.ticksPerCpuCycle,
                          "a new scanline restarts the ordinal at the line's start");
                    if (prevLine >= 0)
                        check(tick < prevTick, "the ordinal drops at a line boundary");
                    ++lineStarts;
                } else {
                    check(tick > prevTick, "lineTickAt strictly increases across a line");
                }
                prevLine = line;
                prevTick = tick;
            }
            check(lineStarts == g.totalLines + 1,
                  "the sweep crosses every scanline exactly once");
        }

        // rawLineAt keeps VBlank lines distinct where beamPosAt folds them —
        // the distinction the GEN2 journal needs, since it sorts VBlank events
        // by column and they set the state the next frame starts from.
        {
            int vblCycles = 0;
            for (int c = 0; c <= g.cyclesPerFrame; ++c) {
                const int raw = pom1::rawLineAt(g, c);
                if (raw < g.activeLines) continue;
                ++vblCycles;
                check(pom1::beamPosAt(g, c).line == g.activeLines,
                      "beamPosAt folds every VBlank line onto activeLines");
                check(pom1::beamPosAt(g, c).x == 0, "x is zero outside the picture");
                // …while the ordinal underneath still tells the lines apart.
                check(raw <= g.totalLines, "rawLineAt keeps the VBlank line number");
            }
            check(vblCycles > 0, "this geometry has a VBlank to test");
        }

        // beamPosAt must remain expressible in terms of both — the refactor that
        // introduced them must not have changed what it answers.
        for (int c = 0; c <= g.cyclesPerFrame; ++c) {
            const pom1::BeamPos p = pom1::beamPosAt(g, c);
            const int raw = pom1::rawLineAt(g, c);
            check(p.line == (raw < g.activeLines ? raw : g.activeLines),
                  "beamPosAt's line is rawLineAt, clamped");
            if (p.line < g.activeLines) {
                int expect = (pom1::lineTickAt(g, c) - g.activeLeftTick) / g.ticksPerPixel;
                if (expect < 0) expect = 0;
                if (expect > g.activeWidth) expect = g.activeWidth;
                check(p.x == expect, "beamPosAt's x is lineTickAt, mapped and clamped");
            }
        }
    }

    if (failures) {
        std::fprintf(stderr, "beam_clock_smoke: %d failures\n", failures);
        return 1;
    }
    std::printf("beam_clock_smoke: all checks passed\n");
    return 0;
}
