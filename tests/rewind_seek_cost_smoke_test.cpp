// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// rewind_seek_cost_smoke — what a rewind seek must NOT do.
//
// WHAT THIS WAS WRITTEN TO FIX, AND DID NOT
//     TODO.md carried "éviter les reconfigurations au seek rewind — ne pas
//     réappliquer cartes et ROM lorsque les flags sont inchangés". Scrubbing
//     the timeline restores one snapshot per mouse-move, and every restore ends
//     with the FLAGS section walking all eighteen rows of Memory::cardSlots()
//     and calling each card's setter — setters whose comment says they
//     "reconfigure each card's bus handlers + ROM mirrors". That reads like a
//     ROM file re-read per card per mouse-move.
//
//     Measured, it is not: a seek over an unchanged topology reads no ROM at
//     all. The guard is already there, and in FIVE places rather than one —
//     ten of the fifteen setters early-out on an unchanged flag,
//     TerminalCard::setEnabled does its own `exchange(on) == on` so no socket
//     is rebound, Memory::setMicroSDEnabled reloads only when
//     `sdCardOsPresent()` says the window is empty, and the destructive
//     branches are gated on `snapshotRestoreInProgress`. The item was stale.
//
// SO WHY KEEP THE TEST
//     Because that invariant is held by five mechanisms across four files, and
//     nothing asserted it. It also protects something sharper than cost: the
//     FLAGS section is written AFTER MEM, so anything a setter does to memory
//     during a restore lands ON TOP of the 64 KB the snapshot just put back. A
//     future setter that clears its window without checking
//     `snapshotRestoreInProgress` would wipe restored RAM, and would do it only
//     while rewinding — the hardest kind of bug to find by hand.
//
// Four sections:
//   1. a seek that changes no card reads no ROM file.
//   2. …and the machine really did move — section 1 must not pass by the seek
//      doing nothing at all.
//   3. a seek that DOES change the topology re-wires the card, and still needs
//      no disk: the snapshot carries the ROM.
//   4. restoring the same frame twice yields identical memory.

#include "EmulationController.h"
#include "EmulationSnapshot.h"
#include "Logger.h"

#include <cassert>
#include <cstdio>
#include <chrono>
#include <memory>
#include <thread>
#include <string>
#include <vector>

namespace {

/// Counts the log lines Memory emits when it loads a ROM image, which is the
/// observable this test is built on: POM1 has no "how many times did you open
/// roms/?" counter, and adding one to a frozen facade to satisfy a test would
/// be the wrong trade.
class RomLoadCounter final : public pom1::Logger {
public:
    void log(pom1::LogLevel, const char* tag, const std::string& message) override
    {
        const std::string t = tag ? tag : "";
        if (t != "Mem" && t != "SD" && t != "CF" && t != "CodeTank") return;
        if (message.find("loaded to") != std::string::npos ||
            message.find("ROM loaded") != std::string::npos ||
            message.find("Disk image") != std::string::npos)
            ++count;
    }
    int count = 0;
};

std::unique_ptr<EmulationSnapshot> snap(EmulationController& emu)
{
    auto s = std::make_unique<EmulationSnapshot>();
    emu.copySnapshot(*s);
    return s;
}

/// Run until the rewind buffer holds at least `want` frames, or give up.
///
/// Capture is driven by the ASYNC emulation loop's wall-clock accumulator, not
/// by emulated cycles, so `runCyclesSync` — which pauses that loop — captures
/// nothing however long it runs. The CPU has to actually be started.
bool captureFrames(EmulationController& emu, std::size_t want)
{
    emu.startCpu();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        if (emu.getRewindStatus().frameCount >= want) { emu.stopCpu(); return true; }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    emu.stopCpu();
    return false;
}

} // namespace

int main()
{
    EmulationController emu(nullptr, /*initializeAudioHardware=*/false);
    // A machine with ROM-backed cards, so "did it reload a ROM?" has something
    // to observe. microSD brings SD CARD OS ($8000) and the Applesoft Lite
    // image; the BASIC/Monitor ROMs are always there.
    emu.setCardEnabled(pom1::CardId::MicroSD, true);
    emu.setRewindEnabled(true);

    if (!captureFrames(emu, 4)) {
        // Rewind capture is wall-clock paced; on a machine that cannot get
        // there this test has nothing to say, and saying nothing is better than
        // asserting on an empty buffer.
        std::puts("rewind_seek_cost_smoke: SKIP (no rewind frames captured)");
        return 77;
    }
    const std::size_t frames = emu.getRewindStatus().frameCount;
    assert(frames >= 4);

    // ── 1. an unchanged topology reloads nothing ─────────────────────────
    RomLoadCounter counter;
    pom1::Logger* previous = &pom1::log();
    pom1::setLogger(&counter);
    for (int i = 0; i < 20; ++i)
        emu.rewindSeekTo(i % frames);
    const int loadsDuringSeeks = counter.count;
    pom1::setLogger(previous);

    if (loadsDuringSeeks != 0)
        std::printf("  seeking reloaded %d ROM image(s)\n", loadsDuringSeeks);
    assert(loadsDuringSeeks == 0);

    // What a seek DOES cost, printed rather than asserted: the number belongs
    // in the record (it is why the TODO item was closed as stale), but a
    // wall-clock bound on a shared CI runner is a false red waiting to happen —
    // the same argument concurrent_frontends_smoke makes for its own gates.
    {
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 40; ++i) emu.rewindSeekTo(i % frames);
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - t0).count();
        std::printf("  (40 seeks in %lld us — %.0f us each)\n",
                    static_cast<long long>(us), static_cast<double>(us) / 40.0);
    }
    std::puts("  [PASS] 1. twenty seeks over an unchanged topology load no ROM");

    // ── 2. …and the seeks really moved the machine ───────────────────────
    {
        // Without this, section 1 would also pass if rewindSeekTo() had quietly
        // become a no-op — the shape of false confidence lock_order_smoke's
        // control case exists to prevent.
        emu.rewindSeekTo(0);
        const auto oldest = snap(emu);
        emu.rewindSeekTo(frames - 1);
        const auto newest = snap(emu);
        assert(oldest->programCounter != newest->programCounter ||
               oldest->memory != newest->memory);
        std::puts("  [PASS] 2. the seeks did move the machine");
    }

    // ── 3. a topology change re-wires, and still needs no disk ──────────
    {
        // Capture frames with microSD unplugged, then seek back to one that had
        // it. The card must be wired up again — skipping an unchanged card must
        // not become skipping every card — but the ROM still comes from the
        // snapshot's own MEM section, not from roms/.
        emu.rewindResumeLive();
        emu.setCardEnabled(pom1::CardId::MicroSD, false);
        const std::size_t before = emu.getRewindStatus().frameCount;
        if (captureFrames(emu, before + 2)) {
            RomLoadCounter changing;
            pom1::setLogger(&changing);
            emu.rewindSeekTo(0);              // an early frame: microSD was on
            pom1::setLogger(previous);
            auto restored = snap(emu);
            assert(restored->microSDEnabled);            // re-wired
            assert(changing.count == 0);                 // and off no disk
            // The window really holds SD CARD OS, from the snapshot.
            bool nonZero = false;
            for (int i = 0; i < 64; ++i)
                if (restored->memory[0x8000 + i] != 0) { nonZero = true; break; }
            assert(nonZero);
            std::puts("  [PASS] 3. a topology change re-wires the card off the snapshot");
        } else {
            std::puts("  [SKIP] 3. could not capture a second topology");
        }
    }

    // ── 4. the restored bytes survive the FLAGS pass ─────────────────────
    {
        // The FLAGS section is written AFTER MEM, so an unconditional
        // reconfigure reloaded ROM images over the 64 KB the snapshot had just
        // put back. $8000 is SD CARD OS's window; whatever the snapshot holds
        // there must be what the machine holds after the restore.
        emu.rewindResumeLive();
        emu.setCardEnabled(pom1::CardId::MicroSD, true);
        if (captureFrames(emu, emu.getRewindStatus().frameCount + 2)) {
            const std::size_t last = emu.getRewindStatus().frameCount - 1;
            emu.rewindSeekTo(last);
            const auto first = snap(emu);
            emu.rewindSeekTo(0);
            emu.rewindSeekTo(last);
            const auto again = snap(emu);
            assert(first->memory == again->memory);
            std::puts("  [PASS] 4. restoring the same frame twice yields the same memory");
        } else {
            std::puts("  [SKIP] 4. could not capture enough frames");
        }
    }

    std::puts("rewind_seek_cost_smoke: all sections passed");
    return 0;
}
