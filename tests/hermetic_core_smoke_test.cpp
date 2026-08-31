// A core built for a test must touch nothing it was not given.
//
// This is the exit criterion of the host-services work, written down as an
// assertion. Measured before any of it: constructing a bare `Memory` took
// 205 ms, opened a real audio device, bound localhost:6502 and opened a 32 MB
// CompactFlash image — none of which any test asked for, and all of which
// happened again for every test binary in the suite.
//
// Two things made that so, and both are now expressible:
//
//   * resources were discovered by walking `../` from the working directory,
//     so a test inherited whatever the repo happened to contain.
//     `ResourceLocator::rootedAt(dir)` says "look ONLY here".
//
//   * the Terminal Card's TCP listener was opened from `reset()` rather than
//     from the plug, so it came up with the card unplugged — which it is by
//     default. A second core in the same process then warned "failed to bind
//     port 6502 (already in use?)", and terminal_card_smoke had to skip
//     reset() entirely to avoid fighting a running POM1.
//
//   * audio was a host service the core built for itself. `Memory` now takes
//     a `pom1::IAudioService` (AudioService.h) and only falls back to owning
//     an `AudioDevice` when nothing is injected — with the OS output OFF by
//     default, which is what a bare `Memory mem;` in 50 test files gets.
//     §4 builds a core on the in-memory double and watches it register.

#include "TMS9918.h"      // IWYU pragma: keep — Memory's unique_ptr members
#include "WiFiModem.h"    // IWYU pragma: keep
#include "TerminalCard.h" // IWYU pragma: keep
#include "A1IO_RTC.h"     // IWYU pragma: keep
#include "PR40Printer.h"  // IWYU pragma: keep
#include "Memory.h"
#include "M6502.h"
#include "AudioService.h"
#include "CassetteDevice.h"
#include "ResourceLocator.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int main()
{
    const fs::path sandbox = fs::temp_directory_path() / "pom1_hermetic_core_smoke";
    fs::remove_all(sandbox);
    fs::create_directories(sandbox);

    // ── §1 A core rooted at an empty directory finds nothing, and says so ────
    //
    // No ROMs, no sdcard/, no disks/, no cfcard/ — and crucially it does not
    // wander off to the repo, which chdir'ing alone could not prevent (the test
    // binary lives inside the tree, and resources are also looked for next to
    // the executable).
    {
        Memory mem(/*initializeAudioHardware=*/false,
                   pom1::ResourceLocator::rootedAt(sandbox));
        M6502 cpu(&mem);
        mem.initMemory();

        // The Woz Monitor is the one thing a machine cannot boot without, so a
        // missing file falls back to the built-in copy — REPORTED, never silent.
        const auto fallbacks = mem.romFallbacksUsed();
        bool sawMonitor = false;
        for (const auto& f : fallbacks) sawMonitor |= (f == "WozMonitor.rom");
        assert(sawMonitor && "an empty root must fall back, and report it");

        // Nothing was mounted from the repo.
        assert(!mem.isTerminalCardEnabled());
        assert(mem.resources().roots().size() == 1);
        assert(mem.resources().find("roms/WozMonitor.rom").empty());

        // The machine still runs: hermetic does not mean crippled.
        cpu.hardReset();
        cpu.run(1000);
    }

    // ── §2 No listening socket unless the card is plugged ───────────────────
    //
    // The proof is that a SECOND core comes up clean in the same process. When
    // the listener rode on reset(), this is where "failed to bind port 6502"
    // appeared.
    {
        Memory first(false, pom1::ResourceLocator::rootedAt(sandbox));
        first.initMemory();
        Memory second(false, pom1::ResourceLocator::rootedAt(sandbox));
        second.initMemory();

        assert(!first.getTerminalCard().isEnabled());
        assert(!second.getTerminalCard().isEnabled());
        TerminalCard::Snapshot s;
        first.getTerminalCard().copySnapshot(s);
        assert(!s.serverListening && "an unplugged Terminal Card must not listen");
        second.getTerminalCard().copySnapshot(s);
        assert(!s.serverListening);

        // A reset does not open one either — that was the whole defect.
        first.resetMemory();
        first.getTerminalCard().copySnapshot(s);
        assert(!s.serverListening && "resetMemory() must not open a socket");
    }

    // ── §3 A root that HAS data is used, and only that root ─────────────────
    //
    // The mirror of §1: prove the locator is actually consulted rather than
    // simply failing everywhere.
    {
        const fs::path stocked = sandbox / "stocked";
        fs::create_directories(stocked / "sdcard");
        fs::create_directories(stocked / "roms");
        // A 256-byte Monitor image of our own, distinguishable from the shipped
        // one: if the machine picks this up, resolution went through the root
        // we gave it.
        {
            std::ofstream rom(stocked / "roms" / "WozMonitor.rom", std::ios::binary);
            const std::vector<char> body(256, '\x42');
            rom.write(body.data(), static_cast<std::streamsize>(body.size()));
        }

        Memory mem(false, pom1::ResourceLocator::rootedAt(stocked));
        mem.initMemory();
        assert(mem.loadWozMonitor() == 0);
        assert(mem.romFallbacksUsed().empty() &&
               "a root that has the file must not trigger the built-in fallback");
        assert(mem.getMemoryPointer()[0xFF00] == 0x42 &&
               "the ROM must come from the injected root");
    }

    // ── §4 The audio service is given, not built ────────────────────────────
    //
    // The core registers its cassette on whatever service it was handed, and
    // hands it back before the source dies — the part that matters when the
    // service outlives the machine, which is exactly the shipped arrangement
    // (main_imgui.cpp owns the AudioDevice above the window).
    //
    // A NullAudioService is not "AudioDevice with the hardware off" — that one
    // still mixes and is what the audio tests use. This double has no mixer, no
    // scratch buffer and no threads, so a core built on it demonstrably brings
    // no audio machinery up with it.
    {
        pom1::NullAudioService audio;
        assert(audio.sourceCount() == 0);
        {
            Memory mem(/*initializeAudioHardware=*/false,
                       pom1::ResourceLocator::rootedAt(sandbox / "empty"), &audio);
            assert(&mem.audioService() == &audio &&
                   "the injected service must be the one the machine talks to");
            assert(!mem.audioService().isAvailable());

            const AudioSource* cassette = &mem.getCassetteDevice();
            assert(!audio.hasSource(cassette) &&
                   "the cassette rides its own activation rail, not construction");
            mem.activateCassetteAudioSource();
            assert(audio.hasSource(cassette) && audio.sourceCount() == 1);
        }
        assert(audio.sourceCount() == 0 &&
               "~Memory must unregister every source it added: an injected "
               "service outlives the machine and would keep a dangling pointer");
    }

    fs::remove_all(sandbox);
    std::printf("hermetic_core_smoke: OK\n");
    return 0;
}
