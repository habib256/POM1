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

    // ---- Live topology changes: no UI frame and no visible CPU pause --------
    // Alternate two mutually-exclusive cards while the emulation thread runs
    // at MAX. The controller quiesces the core inside its critical section,
    // then restores the prior run state. Each call must publish only the
    // completed topology: never both cards, never neither, and never a stopped
    // CPU snapshot.
    pom1::CardConfigurationRequest sidConfig;
    sidConfig.cards.add(pom1::CardId::Sid);
    pom1::CardConfigurationRequest jukeConfig;
    jukeConfig.cards.add(pom1::CardId::JukeBox);
    for (int i = 0; i < 32; ++i) {
        const bool wantSid = (i & 1) == 0;
        const pom1::CardConfigurationResult result =
            emu.applyCardConfiguration(wantSid ? sidConfig : jukeConfig);
        if (!result) {
            std::fprintf(stderr, "  → live card transaction %d failed: %s\n",
                         i, result.message.c_str());
            return 1;
        }
        EmulationSnapshot snapshot;
        emu.copySnapshot(snapshot);
        if (!snapshot.cpuRunning || snapshot.sidEnabled != wantSid ||
            snapshot.jukeBoxEnabled == wantSid) {
            std::fprintf(stderr,
                         "  → incoherent snapshot after live transaction %d "
                         "(running=%d sid=%d juke=%d)\n",
                         i, snapshot.cpuRunning, snapshot.sidEnabled,
                         snapshot.jukeBoxEnabled);
            return 1;
        }
    }
    pom1::CardConfigurationRequest noCards;
    if (!emu.applyCardConfiguration(noCards)) {
        std::fprintf(stderr, "  → failed to detach live topology after stress\n");
        return 1;
    }

    // ---- Cold machine transaction: reset + topology, still one facade call --
    pom1::CardConfigurationRequest coldSidConfig;
    coldSidConfig.cards.add(pom1::CardId::Sid);
    coldSidConfig.coldReset = true;
    coldSidConfig.animateBoot = false;
    coldSidConfig.systemRomProfile =
        pom1::CardConfigurationRequest::SystemRomProfile::MonitorOnly;
    coldSidConfig.presetRamKB = 8;
    coldSidConfig.siliconStrict = true;
    coldSidConfig.outOfRangeStrict = true;
    coldSidConfig.vramNoiseOnReset = false;
    coldSidConfig.systemRamNoiseOnReset = false;
    coldSidConfig.cpuDecimalBugNMOS = true;
    coldSidConfig.dramRefresh = true;
    coldSidConfig.gen2RandomPowerOn = false;
    if (!emu.applyCardConfiguration(coldSidConfig)) {
        std::fprintf(stderr, "  → cold machine transaction failed\n");
        return 1;
    }
    EmulationSnapshot coldSnapshot;
    emu.copySnapshot(coldSnapshot);
    if (!coldSnapshot.cpuRunning || !coldSnapshot.sidEnabled ||
        coldSnapshot.memory.size() != 65536 || coldSnapshot.memory[0xFF00] == 0) {
        std::fprintf(stderr,
                     "  → incoherent cold transaction "
                     "(running=%d sid=%d memory=%zu monitor=%02X)\n",
                     coldSnapshot.cpuRunning, coldSnapshot.sidEnabled,
                     coldSnapshot.memory.size(),
                     coldSnapshot.memory.size() == 65536
                         ? coldSnapshot.memory[0xFF00] : 0);
        return 1;
    }
    if (emu.getPresetRamKB() != 8 ||
        !emu.isSiliconStrictMode() || !emu.isOutOfRangeStrictMode() ||
        !emu.isCpuDecimalBugNMOS() || !emu.isDramRefreshEnabled() ||
        emu.isVramNoiseOnReset() || emu.isSystemRamNoiseOnReset() ||
        emu.isGen2RandomPowerOn()) {
        std::fprintf(stderr,
                     "  → cold transaction did not apply its machine settings\n");
        return 1;
    }

    // ---- Stopped CPU reads as stopped --------------------------------------
    // A stale rate left behind by the last running window would make a paused
    // machine look busy, and the status bar suppresses its "(real ...)" warning
    // on the strength of this being 0.
    emu.stopCpu();
    pom1::CardConfigurationRequest pausedSidConfig;
    pausedSidConfig.cards.add(pom1::CardId::Sid);
    if (!emu.applyCardConfiguration(pausedSidConfig)) {
        std::fprintf(stderr, "  → card transaction failed while CPU was stopped\n");
        return 1;
    }
    EmulationSnapshot pausedSnapshot;
    emu.copySnapshot(pausedSnapshot);
    if (pausedSnapshot.cpuRunning || !pausedSnapshot.sidEnabled) {
        std::fprintf(stderr,
                     "  → stopped-state transaction changed run state "
                     "(running=%d sid=%d)\n",
                     pausedSnapshot.cpuRunning, pausedSnapshot.sidEnabled);
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    const double hzStopped = emu.getMeasuredCpuHz();
    std::printf("  Stop: measured %.0f Hz\n", hzStopped);
    if (hzStopped != 0.0) {
        std::fprintf(stderr,
                     "  → a stopped CPU still reports %.0f Hz; the window is not being "
                     "refreshed while parked.\n", hzStopped);
        return 1;
    }

    std::printf("measured_cpu_rate_smoke: OK (x1 %.0f Hz, Max %.2f MHz, "
                "32 live topology swaps, cold reset, paused topology swap, stopped 0 Hz)\n",
                hz1x, hzMax / 1e6);
    return 0;
}
