// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// preset_registry_smoke — the dynamic preset table.
//
// `kMachinePresets[]` used to BE the table: thirteen rows, indices 0-12, and
// every consumer indexed straight into it. External preset files
// (src/PresetFile.h) made that too narrow, so the table became a registry —
// built-ins first, user machines appended. This pins the three properties the
// rest of the codebase leans on, and the two assumptions that had to die.
//
// Seven sections:
//   1. an empty registry is exactly the shipped table.
//   2. built-ins KEEP their indices when externals arrive — ini/preset_NN.size
//      is keyed by index, so a shipped profile's layout must not migrate.
//   3. an external preset is addressable like any other.
//   4. "default = last" is dead; kDefaultPresetId is what names the default.
//   5. presetIdFromIndex() says Invalid for an external index, and
//      machinePresetMode() is what answers instead — including for a FANTASY
//      external preset, which the old composition got silently wrong.
//   6. the registry copies its strings, so a caller may destroy its source.
//   7. the bound is enforced, and clearing restores the shipped table.

#include "MachinePresets.h"
#include "PresetFile.h"

#include <cassert>
#include <cstdio>
#include <string>

using namespace pom1;

namespace {

/// Register a machine described the way a user would, so the test exercises the
/// real path rather than a hand-built MachineConfig.
int registerFromText(const std::string& text)
{
    const presetfile::ParsedPreset parsed = presetfile::parsePreset(text, "test.preset");
    assert(parsed.ok);
    const MachineConfig cfg = parsed.toMachineConfig();
    return registerExternalPreset(cfg, parsed.mode);
}

const char* kStrictPreset =
    "pom1-preset 1\nname = External Strict\nram = 32\ncards = aci\n";
const char* kFantasyPreset =
    "pom1-preset 1\nname = External Fantasy\nram = 48\n"
    "cards = microsd, cffa1\nmode = fantasy\n";

} // namespace

int main()
{
    // ── 1. an empty registry is the shipped table ────────────────────────
    {
        clearExternalPresets();
        assert(machinePresetCount() == kMachinePresetCount);
        assert(machinePresetCount() == 13);          // the thirteen POM1 ships
        for (int i = 0; i < kMachinePresetCount; ++i) {
            assert(machinePresetAt(i) == &kMachinePresets[i]);
            assert(!machinePresetIsExternal(i));
        }
        assert(machinePresetAt(-1) == nullptr);
        assert(machinePresetAt(kMachinePresetCount) == nullptr);
        assert(machinePresetName(kMachinePresetCount) == nullptr);
        std::puts("  [PASS] 1. an empty registry is exactly the shipped table");
    }

    // ── 2. built-ins keep their indices ──────────────────────────────────
    {
        // The property `ini/preset_NN.size` and `ini/imgui_preset_NN.ini` depend
        // on: a user dropping a file in presets/ must not move a shipped
        // profile's saved window layout onto a different machine.
        std::string before[13];
        for (int i = 0; i < kMachinePresetCount; ++i) before[i] = machinePresetName(i);
        registerFromText(kStrictPreset);
        registerFromText(kFantasyPreset);
        for (int i = 0; i < kMachinePresetCount; ++i) {
            assert(before[i] == machinePresetName(i));
            assert(machinePresetAt(i) == &kMachinePresets[i]);
            assert(!machinePresetIsExternal(i));
        }
        std::puts("  [PASS] 2. built-ins keep their indices when externals arrive");
    }

    // ── 3. an external preset is addressable like any other ──────────────
    {
        assert(machinePresetCount() == kMachinePresetCount + 2);
        const int strictIdx  = kMachinePresetCount;
        const int fantasyIdx = kMachinePresetCount + 1;
        assert(machinePresetIsExternal(strictIdx));
        assert(machinePresetIsExternal(fantasyIdx));
        assert(!machinePresetIsExternal(machinePresetCount()));   // past the end

        const MachineConfig* strict = machinePresetAt(strictIdx);
        assert(strict);
        assert(std::string(strict->name) == "External Strict");
        assert(strict->ramKB == 32);
        assert(strict->hasCard(CardId::Aci));
        // Registration order IS index order.
        assert(std::string(machinePresetName(fantasyIdx)) == "External Fantasy");
        assert(machinePresetAt(fantasyIdx)->ramKB == 48);
        std::puts("  [PASS] 3. an external preset is addressable like any other");
    }

    // ── 4. "default = last" is dead ──────────────────────────────────────
    {
        // It was true only while the table was the whole world. With externals
        // registered, `count - 1` is a USER'S FILE. Anything that wants the
        // shipped default must name it.
        assert(machinePresetCount() - 1 != presetIndex(kDefaultPresetId));
        assert(machinePresetIsExternal(machinePresetCount() - 1));
        assert(std::string(machinePresetName(presetIndex(kDefaultPresetId)))
               == "POM1 Apple-1 Multiplexing Fantasy (2026)");
        // …and the shipped default is still the LAST BUILT-IN, which is the
        // invariant that actually mattered and still holds.
        assert(presetIndex(kDefaultPresetId) == kMachinePresetCount - 1);
        std::puts("  [PASS] 4. 'default = last' is dead; kDefaultPresetId names it");
    }

    // ── 5. identity, and the mode question ───────────────────────────────
    {
        const int strictIdx  = kMachinePresetCount;
        const int fantasyIdx = kMachinePresetCount + 1;
        // An external preset has NO stable identity, and says so.
        assert(presetIdFromIndex(strictIdx) == PresetId::Invalid);
        assert(presetIdFromIndex(fantasyIdx) == PresetId::Invalid);
        // Which is why `isFantasyPreset(presetIdFromIndex(i))` — the old way to
        // ask "does this machine multiplex?" — is WRONG for an external index:
        // it answers "strict" for a preset that says `mode = fantasy`. This is
        // the bug the accessor exists to prevent, asserted as a contrast.
        assert(!isFantasyPreset(presetIdFromIndex(fantasyIdx)));   // the wrong answer
        assert(machinePresetMode(fantasyIdx) == TopologyMode::Fantasy);  // the right one
        assert(machinePresetMode(strictIdx) == TopologyMode::Strict);
        // Built-ins keep answering from their identity, through the same call.
        assert(machinePresetMode(presetIndex(PresetId::Pom1Fantasy)) == TopologyMode::Fantasy);
        assert(machinePresetMode(presetIndex(PresetId::PLabFantasy)) == TopologyMode::Fantasy);
        assert(machinePresetMode(presetIndex(PresetId::BareJuly1976)) == TopologyMode::Strict);
        assert(machinePresetMode(presetIndex(PresetId::Gen2Color)) == TopologyMode::Strict);
        std::puts("  [PASS] 5. external presets have no PresetId; the mode accessor answers");
    }

    // ── 6. the registry owns its strings ─────────────────────────────────
    {
        clearExternalPresets();
        int index = -1;
        {
            // The ParsedPreset — and the MachineConfig borrowing its strings —
            // dies here. A registry that stored the pointers would dangle, and
            // `MachineConfig` is all `const char*`.
            const presetfile::ParsedPreset parsed = presetfile::parsePreset(
                "pom1-preset 1\nname = Temporary\ndescription = gone\n"
                "ram = 16\ncards = codetank\n"
                "codetank-rom = roms/codetank/Codetank_DEMOS.rom\n", "tmp.preset");
            assert(parsed.ok);
            const MachineConfig cfg = parsed.toMachineConfig();
            index = registerExternalPreset(cfg, parsed.mode);
        }
        assert(index == kMachinePresetCount);
        const MachineConfig* kept = machinePresetAt(index);
        assert(kept);
        assert(std::string(kept->name) == "Temporary");
        assert(std::string(kept->description) == "gone");
        assert(kept->codeTank.romPath);
        assert(std::string(kept->codeTank.romPath) == "roms/codetank/Codetank_DEMOS.rom");
        // Dependencies were closed by the parser, and survived registration.
        assert(kept->hasCard(CardId::Tms9918));
        // Window placement belongs to a shipped profile; an external preset
        // gets the default arrangement.
        assert(kept->layoutCount == 0);
        std::puts("  [PASS] 6. the registry copies the strings it is handed");
    }

    // ── 7. the bound, and clearing ───────────────────────────────────────
    {
        clearExternalPresets();
        for (int i = 0; i < kMaxExternalPresets; ++i) {
            const int idx = registerFromText(
                "pom1-preset 1\nname = Bulk " + std::to_string(i) + "\nram = 8\n");
            assert(idx == kMachinePresetCount + i);
        }
        assert(machinePresetCount() == kMachinePresetCount + kMaxExternalPresets);
        // Past the bound it REFUSES rather than growing: a menu is a list a
        // human reads, and past this the directory is a mistake, not a
        // collection.
        const presetfile::ParsedPreset one =
            presetfile::parsePreset("pom1-preset 1\nname = Extra\nram = 8\n", "x");
        assert(registerExternalPreset(one.toMachineConfig(), one.mode) == -1);
        assert(machinePresetCount() == kMachinePresetCount + kMaxExternalPresets);

        clearExternalPresets();
        assert(machinePresetCount() == kMachinePresetCount);
        for (int i = 0; i < kMachinePresetCount; ++i)
            assert(machinePresetAt(i) == &kMachinePresets[i]);
        std::puts("  [PASS] 7. the bound refuses; clearing restores the shipped table");
    }

    std::puts("preset_registry_smoke: all sections passed");
    return 0;
}
