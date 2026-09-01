// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// PresetDecisions.h — what applying a preset MEANS, pure.
//
// Fourth seam of the family (Apple1KeyMap / FullscreenExpand / WindowGeometry /
// StagedCardConfiguration / LayoutDecisions): no ImGui, no GLFW, no MainWindow,
// so every rule here is reachable from a test binary that links only the preset
// table itself.
//
// WHY THIS EXISTS. `MachinePresets.h` already owns the DATA and `CardTopology.h`
// already owns the card POLICY; what stayed in `applyMachineConfig()` was the
// composition — and three of its rules were each stated TWICE, in two files, with
// a comment asking the reader to keep the copies in sync:
//
//   - the silicon-fidelity bundle, once on the preset path and once on the
//     Silicon Strict master button (twelve flags, "keep them in sync");
//   - the "where does Applesoft Lite live?" predicate, once to pick the ROM
//     profile the controller loads and once to build the inventory the Memory
//     Map shows — a copy that can make the map describe a machine that is not
//     the one running;
//   - "is this a DevBench profile?", spelled as an index range in one place and
//     as three PresetId comparisons in another.
//
// A rule stated twice is a rule that can disagree with itself. Each one below is
// stated once, returned as a value, and asserted against all thirteen shipped
// presets by preset_decisions_smoke.

#ifndef POM1_PRESET_DECISIONS_H
#define POM1_PRESET_DECISIONS_H

#include <cstdint>
#include <vector>

#include "MachineCoordinator.h"   // CardConfigurationRequest::SystemRomProfile
#include "MachinePresets.h"

namespace pom1::presets {

// ---------------------------------------------------------------------------
// 1. The silicon-fidelity bundle
// ---------------------------------------------------------------------------

/// Silicon Strict is an ALL-OR-NOTHING master switch: a non-Fantasy preset arms
/// every fidelity knob at once, Fantasy disarms them all. After the preset lands
/// the user can flip any individual knob in the Silicon Strict window and that
/// override sticks until the next preset switch or master toggle.
///
/// The four GEN2 sub-knobs ride along because the card's power-on state is one
/// decision: random latch + floating bus + scanner phase + DRAM noise are what
/// `setGen2RandomPowerOn` means, and the Inspector's four checkboxes only mirror
/// it. DRAM refresh (4/65 CPU steal) is in the bundle too, so a Silicon preset
/// reproduces the real-DRAM beam-race drift out of the box.
struct SiliconFidelity {
    bool siliconStrict          = false;
    bool outOfRangeStrict       = false;
    bool dramRefresh            = false;
    bool vramNoiseOnReset       = false;
    bool systemRamNoiseOnReset  = false;
    bool cpuDecimalBugNMOS      = false;
    bool gen2RandomPowerOn      = false;
    bool gen2RandomLatch        = false;
    bool gen2RandomFloatingBus  = false;
    bool gen2RandomScannerPhase = false;
    bool gen2RandomDramNoise    = false;
};

inline SiliconFidelity siliconFidelity(bool armed)
{
    SiliconFidelity f;
    f.siliconStrict          = armed;
    f.outOfRangeStrict       = armed;
    f.dramRefresh            = armed;
    f.vramNoiseOnReset       = armed;
    f.systemRamNoiseOnReset  = armed;
    f.cpuDecimalBugNMOS      = armed;
    f.gen2RandomPowerOn      = armed;
    f.gen2RandomLatch        = armed;
    f.gen2RandomFloatingBus  = armed;
    f.gen2RandomScannerPhase = armed;
    f.gen2RandomDramNoise    = armed;
    return f;
}

/// The bundle a preset arms. Fantasy — the shipped default, and the last entry
/// in the table — is the only one that disarms.
inline SiliconFidelity siliconFidelityForPreset(PresetId id)
{
    return siliconFidelity(!isFantasyPreset(id));
}

/// RAM ceiling the MASTER TOGGLE forces: a real Apple-1 has 8 KB of dual-bank
/// RAM ($0000-$0FFF + $E000-$EFFF) with $1000-$7FFF floating; fantasy gets the
/// anything-goes map. Applying a preset uses `MachineConfig::ramKB` instead —
/// the preset's own profile is more specific than the bundle's default.
inline int strictRamKB(bool armed) { return armed ? 8 : 64; }

// ---------------------------------------------------------------------------
// 2. Which ROMs a preset means
// ---------------------------------------------------------------------------

/// Applesoft Lite lives in the microSD card's RAM window ($6000-$7FFF) unless
/// the CFFA1 is what carries it ($E000-$FFFF). ONE predicate, because it decides
/// both the profile the controller loads and the inventory the Memory Map shows.
inline bool applesoftOnSdCard(const MachineConfig& cfg)
{
    return cfg.hasCard(CardId::MicroSD) && !cfg.hasCard(CardId::Cffa1);
}

using SystemRomProfile = CardConfigurationRequest::SystemRomProfile;

inline SystemRomProfile romProfileFor(const MachineConfig& cfg, bool fantasy)
{
    if (cfg.basicType == BasicType::ApplesoftLite) {
        if (!applesoftOnSdCard(cfg))
            return SystemRomProfile::ApplesoftCffa1;
        return fantasy ? SystemRomProfile::ApplesoftSdFantasy
                       : SystemRomProfile::ApplesoftSd;
    }
    if (cfg.basicType == BasicType::Integer)
        return SystemRomProfile::IntegerBasic;
    return SystemRomProfile::MonitorOnly;
}

/// One row of the Memory Map's "loaded ROMs" list.
struct RomSpan {
    const char* name;
    std::uint16_t start;
    std::uint16_t end;
};

/// Presentation metadata ONLY — the controller loads the ROMs from the profile
/// above. Derived from the same predicate so the two cannot drift.
inline std::vector<RomSpan> romInventoryFor(const MachineConfig& cfg, bool fantasy)
{
    std::vector<RomSpan> roms;
    if (cfg.basicType == BasicType::ApplesoftLite) {
        if (applesoftOnSdCard(cfg)) {
            roms.push_back({"Applesoft Lite (loaded in card RAM)", 0x6000, 0x7FFF});
            // Fantasy keeps Integer BASIC in its socket alongside — the strict
            // machine has one socket and cannot.
            if (fantasy)
                roms.push_back({"Integer BASIC", 0xE000, 0xEFFF});
            roms.push_back({"Woz Monitor", 0xFF00, 0xFFFF});
        } else {
            roms.push_back({"Applesoft Lite (CFFA1)", 0xE000, 0xFFFF});
        }
    } else {
        if (cfg.basicType == BasicType::Integer)
            roms.push_back({"Integer BASIC", 0xE000, 0xEFFF});
        roms.push_back({"Woz Monitor", 0xFF00, 0xFFFF});
    }
    if (cfg.krusader)
        roms.push_back({"Krusader", 0xE000, 0xFFFF});
    if (cfg.hasCard(CardId::Cffa1))
        roms.push_back({"CFFA1 Firmware", 0x9000, 0xAFDF});
    return roms;
}

// ---------------------------------------------------------------------------
// 3. Which tape a preset preloads
// ---------------------------------------------------------------------------

/// The deck rides its own timing rail (it exists independently of the ACI), so
/// this is a separate decision from the topology.
struct PresetTape {
    /// Candidate paths in preference order; the caller resolves the first that
    /// exists. Empty = this preset preloads no tape.
    std::vector<const char*> candidates;
    /// Force the pulse/program path rather than deck audio: this is cassette
    /// DATA the ACI has to read, not music.
    bool forceProgramMode = false;
    bool autoPlay = false;
    /// Logged when no candidate resolves. Null when nothing is expected.
    const char* missingWarning = nullptr;
};

inline PresetTape tapeFor(const MachineConfig& cfg, PresetId id)
{
    PresetTape t;
    if (cfg.basicType == BasicType::IntegerCassette) {
        t.candidates = {"cassettes/BASIC.aci", "cassettes/BASIC.ogg"};
        t.forceProgramMode = true;
        t.missingWarning =
            "Integer BASIC cassette asset not found (expected cassettes/BASIC.aci or BASIC.ogg)";
    } else if (id == kDefaultPresetId) {
        // POM1 Multiplexing Fantasy (2026) — the shipped default; its deck opens
        // with Woz's talk inserted, Play being user-driven. No other preset
        // preloads it, and it is deck AUDIO, not tape data.
        t.candidates = {"cassettes/WOZ_talk.mp3"};
        t.missingWarning = "WOZ_talk.mp3 not found (expected cassettes/WOZ_talk.mp3)";
    }
    return t;
}

// ---------------------------------------------------------------------------
// 4. The remaining per-preset predicates
// ---------------------------------------------------------------------------

/// Indices 0-2, named rather than spelled as a range: the DevBench profiles.
inline bool isDevBenchPreset(PresetId id)
{
    return id == PresetId::CC65Bench
        || id == PresetId::TMS9918Bench
        || id == PresetId::Gen2Bench;
}

/// A DevBench profile is a compile-and-run workflow, not a cold-boot demo: skip
/// the ~3 s power-on scenarization so the reset lands on a cleared screen.
inline bool animatesBoot(PresetId id) { return !isDevBenchPreset(id); }

/// The POM1 banner belongs to the shipped default profile alone.
inline bool showsBanner(PresetId id) { return id == kDefaultPresetId; }

/// The FIRST apply skips the destructive reset — `Memory::Memory()` has just run
/// `initMemory()` and a hard reset would redo it (double BASIC/WOZ/ACI/SD loads,
/// repeated TerminalCard re-listens). Exception: a SILICON preset still needs
/// that one reset even on the first call, so the just-armed power-on noise
/// replaces the constructor's lenient seed. Fantasy first-boot keeps skipping —
/// no power-on noise is wanted there.
inline bool coldResetOnApply(bool presetAppliedOnce, PresetId id)
{
    return presetAppliedOnce || !isFantasyPreset(id);
}

} // namespace pom1::presets

#endif // POM1_PRESET_DECISIONS_H
