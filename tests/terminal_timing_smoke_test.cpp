// The Apple-1 display's busy timing — pom1::terminal (src/TerminalTiming.h).
//
// Every printing program spins on the Woz Monitor's `BIT $D012 / BMI`, so how
// PB7 is modelled decides how a printing program's cycles fall. Two models live
// side by side: the historical fixed countdown (default — every shipped program
// and golden image is validated on it) and the phase-locked FieldSync, which
// models the fact that Woz's terminal has no framebuffer and can only latch a
// character when the scan reaches the cursor.
//
// Covered:
//   §1  the field period is the raster period, not a derived 1/60 s;
//   §2  FixedDelay is exactly what POM1 has always done;
//   §3  THE property that makes FieldSync safe — two consecutive writes are
//       one field apart whatever the phase, so 60 characters per second is
//       preserved and only the distribution of the wait changes;
//   §4  a write on the boundary has missed the latch and waits a full field;
//   §5  the phase counter wraps, and survives absurd inputs.
//
// Links nothing: the model is a header of constexpr functions.

#include "TerminalTiming.h"

#include <cassert>
#include <initializer_list>
#include <cstdio>

using namespace pom1::terminal;

namespace {
// What Memory has always used: POM1_CPU_CLOCK_HZ / 60.
constexpr int kHistoricalDelay = 1022727 / 60;   // 17045
} // namespace

int main()
{
    // -----------------------------------------------------------------
    // §1 The field is the raster period.
    //
    // 65 CPU cycles per scanline × 262 lines, resolved with Uncle Bernie and
    // recorded in doc/GEN2_RELEASE.md. POM1's fixed delay derives 1/60 s from
    // the CPU clock instead and lands 15 cycles away — harmless while nothing
    // was phase-locked to it, which is precisely what FieldSync changes.
    // -----------------------------------------------------------------
    {
        static_assert(kFieldCycles == 65 * 262);
        static_assert(kFieldCycles == 17030);
        assert(kHistoricalDelay == 17045);
        assert(kHistoricalDelay - kFieldCycles == 15);
    }

    // -----------------------------------------------------------------
    // §2 FixedDelay is bit-for-bit the historical behaviour.
    //
    // It must not depend on the phase at all: that independence is the whole
    // reason the default is safe to leave in place.
    // -----------------------------------------------------------------
    {
        for (int phase = 0; phase < kFieldCycles; phase += 971) {
            assert(busyCyclesAfterWrite(BusyModel::FixedDelay, phase, kHistoricalDelay)
                   == kHistoricalDelay);
        }
        // A caller with no delay configured gets no busy, not a negative one.
        assert(busyCyclesAfterWrite(BusyModel::FixedDelay, 0, 0) == 0);
        assert(busyCyclesAfterWrite(BusyModel::FixedDelay, 0, -5) == 0);
    }

    // -----------------------------------------------------------------
    // §3 THE safety property.
    //
    // Under FieldSync the wait varies with the phase — that is the point — but
    // the INTERVAL BETWEEN CONSECUTIVE WRITES does not. A program that prints
    // as fast as the machine allows still prints exactly one character per
    // field, i.e. the documented 60 per second, whatever phase it started at
    // and however many cycles its poll loop wastes before writing again.
    //
    // Without this, switching models would silently change the throughput of
    // every printing program in the corpus.
    // -----------------------------------------------------------------
    {
        for (int startPhase = 0; startPhase < kFieldCycles; startPhase += 613) {
            for (int overhead : {0, 1, 7, 42, 500}) {
                // Write A lands at an arbitrary phase — wherever the program
                // happened to reach its first store.
                const int busyA = busyCyclesAfterWrite(BusyModel::FieldSync, startPhase, 0);

                // The busy always ends ON the latch point. That is the whole
                // model in one line: the wait is not measured from the write,
                // it is measured to the scan.
                assert(advanceFieldPhase(startPhase, busyA) == 0);

                // The program then burns `overhead` cycles and stores again.
                const int phaseB = advanceFieldPhase(startPhase, busyA + overhead);
                assert(phaseB == overhead);
                const int busyB = busyCyclesAfterWrite(BusyModel::FieldSync, phaseB, 0);
                assert(busyB == kFieldCycles - overhead);

                // …and again.
                const int phaseC = advanceFieldPhase(phaseB, busyB + overhead);
                assert(phaseC == overhead);

                // THE INVARIANT. From the second write onward the machine is in
                // lock-step with the scan: write-to-write is EXACTLY one field,
                // whatever phase the program started at and however many cycles
                // its poll loop burns. That is what preserves the documented 60
                // characters per second across the model change.
                const int spacingBC = busyB + overhead;
                assert(spacingBC == kFieldCycles);

                // The FIRST wait is the one that varies — deliberately. It is
                // the only observable difference from FixedDelay, and it is
                // what "the terminal streams, it does not buffer" means.
                assert(busyA == kFieldCycles - startPhase);
            }
        }
        // Stated once more without the loop, because it is the claim a reader
        // will want to check by hand: start anywhere, print twice, and the two
        // stores are one field apart.
        const int p0 = 9999 % kFieldCycles;
        const int b0 = busyCyclesAfterWrite(BusyModel::FieldSync, p0, 0);
        const int p1 = advanceFieldPhase(p0, b0);          // the latch point
        assert(p1 == 0);
        assert(busyCyclesAfterWrite(BusyModel::FieldSync, p1, 0) == kFieldCycles);
    }

    // -----------------------------------------------------------------
    // §4 A write on the boundary has just missed the latch.
    //
    // Returning 0 there would let a tight ECHO loop print without ever
    // spinning — a machine that prints infinitely fast once per field.
    // -----------------------------------------------------------------
    {
        assert(busyCyclesAfterWrite(BusyModel::FieldSync, 0, 0) == kFieldCycles);
        assert(busyCyclesAfterWrite(BusyModel::FieldSync, 1, 0) == kFieldCycles - 1);
        assert(busyCyclesAfterWrite(BusyModel::FieldSync, kFieldCycles - 1, 0) == 1);
        // Never zero, never negative, never longer than a field.
        for (int phase = 0; phase < kFieldCycles; phase += 337) {
            const int b = busyCyclesAfterWrite(BusyModel::FieldSync, phase, 0);
            assert(b > 0 && b <= kFieldCycles);
        }
    }

    // -----------------------------------------------------------------
    // §5 The phase counter.
    // -----------------------------------------------------------------
    {
        assert(advanceFieldPhase(0, 0) == 0);
        assert(advanceFieldPhase(0, 5) == 5);
        assert(advanceFieldPhase(kFieldCycles - 1, 1) == 0);
        assert(advanceFieldPhase(kFieldCycles - 1, 2) == 1);
        // A whole field is a no-op on the phase.
        assert(advanceFieldPhase(1234, kFieldCycles) == 1234);
        // Several fields at once — a long slice, or a fast-forward.
        assert(advanceFieldPhase(10, 5 * kFieldCycles + 7) == 17);
        // Degenerate inputs are answered, not crashed.
        assert(advanceFieldPhase(0, -1) == 0);
        assert(advanceFieldPhase(99, -1000) == 99);
        assert(busyCyclesAfterWrite(BusyModel::FieldSync, -5, 0) == kFieldCycles);
        assert(busyCyclesAfterWrite(BusyModel::FieldSync, kFieldCycles + 3, 0)
               == kFieldCycles - 3);
    }

    std::printf("terminal_timing_smoke: OK\n");
    return 0;
}
