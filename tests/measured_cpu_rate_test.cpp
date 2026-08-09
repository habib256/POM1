// measured_cpu_rate_test.cpp -- pin EmulationController::getMeasuredCpuHz().
//
// The status bar used to display `executionSpeed * 60`, which is the TARGET the
// pacer aims for, not what the host delivered: a machine running at half speed
// read exactly like one running on time, and MAX speed showed no number at all.
//
// The measurement's one subtle requirement is WHERE the wall-clock is charged.
// runEmulationSlice returns early whenever the cycle budget isn't full yet --
// the normal case at x1, where the host is orders of magnitude faster than an
// Apple-1 and the loop spends most of its time asleep. Accumulating elapsed
// time only in the slices that actually ran the CPU would measure the bursts
// and report tens of MHz as the sustained rate. This test would catch that:
// x1 must read ~1 MHz, not ~50.
//
// The bands are deliberately loose -- this is a wall-clock measurement on a
// possibly-loaded machine. They only have to separate "measured" from "target"
// and from the burst-rate bug, both of which are an order of magnitude away.

#include "TMS9918.h"      // IWYU pragma: keep
#include "WiFiModem.h"    // IWYU pragma: keep
#include "TerminalCard.h" // IWYU pragma: keep
#include "A1IO_RTC.h"     // IWYU pragma: keep
#include "PR40Printer.h"  // IWYU pragma: keep
#include "CpuClock.h"
#include "EmulationController.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace {

double measureFor(EmulationController& emu, int cyclesPerFrame, double seconds)
{
    emu.setExecutionSpeedCyclesPerFrame(cyclesPerFrame);
    emu.startCpu();
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    return emu.getMeasuredCpuHz();
}

} // namespace

int main()
{
    // The controller starts its emulation thread from the constructor and runs
    // immediately. No ROM is loaded: memory is zeros, so the 6502 executes BRK
    // forever -- which is fine, cycles are cycles. The measurement is about the
    // PACE, not about what is being executed.
    EmulationController emu(nullptr);

    // ---- x1: the pacer holds the CPU at the Apple-1's real clock ------------
    const double target1x =
        static_cast<double>(POM1_CPU_CYCLES_PER_FRAME_1X_60HZ) * 60.0;   // ~1.023 MHz
    const double hz1x = measureFor(emu, POM1_CPU_CYCLES_PER_FRAME_1X_60HZ, 1.5);
    std::printf("  x1  : target %.0f Hz, measured %.0f Hz\n", target1x, hz1x);
    if (hz1x <= 0.0) {
        std::fprintf(stderr, "  → no measurement at all (0 Hz) while the CPU was running\n");
        return 1;
    }
    if (hz1x > target1x * 1.5) {
        std::fprintf(stderr,
                     "  → x1 measured %.0f Hz, far above the %.0f Hz target. The window is "
                     "charging only the slices that ran the CPU, so it is reporting the "
                     "burst rate as the sustained one.\n", hz1x, target1x);
        return 1;
    }
    if (hz1x < target1x * 0.2) {
        std::fprintf(stderr,
                     "  → x1 measured %.0f Hz, less than a fifth of the %.0f Hz target. "
                     "Either the host is heavily loaded or cycles are being dropped from "
                     "the accumulator.\n", hz1x, target1x);
        return 1;
    }

    // ---- MAX: no pacing, so the reading must follow the host ----------------
    // This is the case the status bar could not express at all before (it just
    // said "Max"), and the one where the measurement is the only real number.
    const double hzMax = measureFor(emu, 1000000, 1.5);
    std::printf("  Max : measured %.2f MHz\n", hzMax / 1e6);
    if (hzMax <= target1x * 1.5) {
        std::fprintf(stderr,
                     "  → MAX speed measured %.0f Hz, no faster than x1. The reading is not "
                     "following the speed selector.\n", hzMax);
        return 1;
    }

    // ---- Stopped CPU reads as stopped --------------------------------------
    // A stale rate left behind by the last running window would make a paused
    // machine look busy, and the status bar suppresses its "(real ...)" warning
    // on the strength of this being 0.
    emu.stopCpu();
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    const double hzStopped = emu.getMeasuredCpuHz();
    std::printf("  Stop: measured %.0f Hz\n", hzStopped);
    if (hzStopped != 0.0) {
        std::fprintf(stderr,
                     "  → a stopped CPU still reports %.0f Hz; the window is not being "
                     "refreshed while parked.\n", hzStopped);
        return 1;
    }

    std::printf("measured_cpu_rate_smoke: OK (x1 %.0f Hz, Max %.2f MHz, stopped 0 Hz)\n",
                hz1x, hzMax / 1e6);
    return 0;
}
