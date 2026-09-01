// Exercise the three production concurrency roles without an ImGui or audio
// backend: EmulationController owns the producer thread, one synthetic render
// thread consumes immutable snapshots, and one synthetic callback thread mixes
// the real registered audio sources while topology changes underneath both.

#include "EmulationController.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

int main()
{
    EmulationController emu(nullptr, /*initializeAudioHardware=*/false);
    emu.setExecutionSpeedCyclesPerFrame(1000000);

    pom1::CardConfigurationRequest sidConfig;
    sidConfig.cards.add(pom1::CardId::Sid);
    pom1::CardConfigurationRequest jukeConfig;
    jukeConfig.cards.add(pom1::CardId::JukeBox);
    if (!emu.applyCardConfiguration(sidConfig)) {
        std::fprintf(stderr, "initial SID configuration failed\n");
        return 1;
    }
    // uint8_t{} rather than bare literals: the parameter is
    // vector<pair<uint8_t,uint8_t>>, and MSVC /W4 reports the int->uint8_t
    // narrowing inside <utility>'s pair constructor (C4244), where no cast at
    // this call site would otherwise appear.
    emu.pokeSidRegisters({{uint8_t{0},  uint8_t{0x34}}, {uint8_t{1}, uint8_t{0x12}},
                          {uint8_t{5},  uint8_t{0x11}}, {uint8_t{6}, uint8_t{0xF0}},
                          {uint8_t{24}, uint8_t{0x0F}}, {uint8_t{4}, uint8_t{0x21}}});
    emu.startCpu();

    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> renderPasses{0};
    std::atomic<uint64_t> audioCallbacks{0};
    std::atomic<uint64_t> invalidSnapshots{0};
    std::atomic<uint64_t> renderChecksum{0};
    std::atomic<uint64_t> finiteAudioSamples{0};

    std::thread renderThread([&] {
        EmulationSnapshot snapshot;
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        while (!stop.load(std::memory_order_acquire)) {
            emu.copySnapshot(snapshot);
            if (snapshot.memory.size() != 65536 ||
                (snapshot.sidEnabled && snapshot.jukeBoxEnabled)) {
                invalidSnapshots.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            uint64_t hash = snapshot.programCounter;
            for (size_t address = 0; address < snapshot.memory.size(); address += 257)
                hash = (hash * 131) ^ snapshot.memory[address];
            hash ^= static_cast<uint64_t>(snapshot.gen2VideoEvents.size());
            renderChecksum.fetch_xor(hash, std::memory_order_relaxed);
            renderPasses.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::thread audioThread([&] {
        std::array<float, 128> samples{};
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        while (!stop.load(std::memory_order_acquire)) {
            emu.mixAudio(samples.data(), static_cast<int>(samples.size()));
            for (float sample : samples) {
                if (std::isfinite(sample))
                    finiteAudioSamples.fetch_add(1, std::memory_order_relaxed);
            }
            audioCallbacks.fetch_add(1, std::memory_order_relaxed);
        }
    });

    start.store(true, std::memory_order_release);
    int stressMs = 350;
    if (const char* configured = std::getenv("POM1_CONCURRENCY_STRESS_MS")) {
        const long parsed = std::strtol(configured, nullptr, 10);
        if (parsed >= 100 && parsed <= 60000) stressMs = static_cast<int>(parsed);
    }
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(stressMs);
    unsigned swap = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto& request = (swap++ & 1U) ? sidConfig : jukeConfig;
        if (!emu.applyCardConfiguration(request)) {
            std::fprintf(stderr, "live topology swap %u failed\n", swap);
            stop.store(true, std::memory_order_release);
            renderThread.join();
            audioThread.join();
            return 1;
        }
        emu.writeMemory(static_cast<uint16_t>(0x0200 + (swap & 0xFFU)),
                        static_cast<uint8_t>(swap));
    }

    stop.store(true, std::memory_order_release);
    renderThread.join();
    audioThread.join();
    emu.stopCpu();
    const pom1::RealtimeDiagnostics realtime = emu.getRealtimeDiagnostics();

    const uint64_t renders = renderPasses.load(std::memory_order_relaxed);
    const uint64_t callbacks = audioCallbacks.load(std::memory_order_relaxed);
    const uint64_t finite = finiteAudioSamples.load(std::memory_order_relaxed);
    if (invalidSnapshots.load(std::memory_order_relaxed) != 0 || renders < 10 ||
        callbacks < 10 || finite != callbacks * 128 ||
        realtime.stateLockAcquisitions == 0 ||
        realtime.audioCallbacks != callbacks ||
        // maxStateWaitNs is a CATASTROPHE bound (5 s = a hang), not a quality
        // one. It was 500 ms, and that number could not survive contact with a
        // second machine: it fired on a healthy GitHub Linux runner at 1.66 s
        // while max HOLD stayed at 16 ms — nobody held the lock too long, a
        // waiter simply lost the scheduler. Measured here on an idle 8-core
        // Mac it already ranged 57-350 ms against that 500 ms line.
        //
        // Worse, it does not detect what it was there for. Disabling the
        // PriorityMutex yield in EmulationController::emulationLoop and running
        // eight times tripped the 500 ms bound ONCE: low power, high false
        // positive rate. What the yield actually buys shows up in PROGRESS of
        // the contending thread — topology swaps completed in the same window,
        // median ~117 with the yield against ~35 without, over eight runs each.
        // The distributions overlap on a single run (116 vs 45), so that is not
        // a threshold yet; it is the measurement a future gate should be built
        // on, with enough samples to place a line.
        //
        // What stays gated is what POM1 itself controls, and both have room:
        // max HOLD measured 18-68 ms against 100, and the audio callback 7-33
        // µs against 50 ms.
        realtime.maxStateWaitNs > 5000000000ULL ||
        realtime.maxStateHoldNs > 100000000ULL ||
        realtime.maxAudioCallbackNs > 50000000ULL) {
        std::fprintf(stderr,
                     "concurrency gate failed: renders=%llu callbacks=%llu "
                     "finite=%llu invalid=%llu state-wait=%lluns "
                     "state-hold=%lluns callback=%lluns\n",
                     static_cast<unsigned long long>(renders),
                     static_cast<unsigned long long>(callbacks),
                     static_cast<unsigned long long>(finite),
                     static_cast<unsigned long long>(
                         invalidSnapshots.load(std::memory_order_relaxed)),
                     static_cast<unsigned long long>(realtime.maxStateWaitNs),
                     static_cast<unsigned long long>(realtime.maxStateHoldNs),
                     static_cast<unsigned long long>(realtime.maxAudioCallbackNs));
        return 1;
    }

    std::printf("concurrent_frontends_smoke: OK (%u swaps, %llu renders, "
                "%llu callbacks, max wait/hold/callback=%llu/%llu/%llu ns, "
                "SID under/over=%llu/%llu, cassette under/over=%llu/%llu, "
                "checksum=%llu)\n",
                swap, static_cast<unsigned long long>(renders),
                static_cast<unsigned long long>(callbacks),
                static_cast<unsigned long long>(realtime.maxStateWaitNs),
                static_cast<unsigned long long>(realtime.maxStateHoldNs),
                static_cast<unsigned long long>(realtime.maxAudioCallbackNs),
                static_cast<unsigned long long>(realtime.sidUnderruns),
                static_cast<unsigned long long>(realtime.sidOverflows),
                static_cast<unsigned long long>(realtime.cassetteUnderruns),
                static_cast<unsigned long long>(realtime.cassetteOverflows),
                static_cast<unsigned long long>(
                    renderChecksum.load(std::memory_order_relaxed)));
    return 0;
}
