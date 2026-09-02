// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MachinePresets.h — the machine-preset table and its accessors, with NO UI
// dependency whatsoever.
//
// Why this header exists at all: `kMachinePresets[]` is pure data (which cards
// are plugged, how much RAM, which BASIC) that the CLI, the DevBench target
// table and the Hardware → Preset menu all read. It used to live inside
// `MainWindow_Internal.h` / `MainWindow_Presets.cpp`, so `CliDispatcher.cpp`
// had to `#include "MainWindow_ImGui.h"` for exactly two static accessors —
// which dragged the whole UI layer (ImGui + GLFW + every render path) into
// anything that linked the CLI parser, and that is precisely why `parseCli()`
// was the one July-2026 test hole that stayed unfilled. Splitting the data out
// makes the parser unit-testable against a handful of TUs.
//
// Consequence for anyone editing the table: this TU must stay UI-free. It may
// include peripheral headers (they are plain hardware models) but never
// `imgui.h`, `GLFW/glfw3.h` or any `MainWindow_*` header. `MachineWindowPlacement`
// therefore carries `PresetVec2` instead of `ImVec2`; the UI side converts at
// the single place that needs it (`computePresetLayoutExtent`, still in
// `MainWindow_Presets.cpp` where the ImGui geometry lives).
//
// Future migration target: load the table from an external presets.json.

#ifndef POM1_MACHINE_PRESETS_H
#define POM1_MACHINE_PRESETS_H

#include <cstdint>
#include <string>

#include "CardTopology.h"
#include "CardTypes.h"
#include "CodeTank.h"
#include "JukeBox.h"

namespace pom1 {

// UI-free stand-in for ImVec2. Layout literals in the table are brace-
// initialised (`{10, 61}`), so this is a drop-in aggregate.
struct PresetVec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct MachineWindowPlacement {
    const char* name;
    PresetVec2 pos;
    PresetVec2 size;  // (0,0) = no size override
};

enum class BasicType { None, Integer, IntegerCassette, ApplesoftLite };

struct JukeBoxPresetOptions {
    JukeBox::Jumper jumper = JukeBox::Jumper::RAM16_ROM32;
    JukeBox::ChipMode chipMode = JukeBox::ChipMode::Flash;
};

struct CodeTankPresetOptions {
    CodeTank::Jumper jumper = CodeTank::Jumper::Lower16;
    const char* romPath = nullptr;
};

/// Stable preset identity. Values deliberately match the historical menu and
/// on-disk layout indices; append only.
enum class PresetId : uint8_t {
    CC65Bench = 0,
    TMS9918Bench,
    Gen2Bench,
    BareJuly1976,
    IntegerCassette,
    Gt6144,
    ReplicaKrusader,
    Cffa1Applesoft,
    MicroSDApplesoft,
    TmsCodeTank,
    PLabFantasy,
    Gen2Color,
    Pom1Fantasy,
    Count,
    Invalid = 0xFF,
};

inline constexpr PresetId kDefaultPresetId = PresetId::Pom1Fantasy;
constexpr int presetIndex(PresetId id) { return static_cast<int>(id); }

struct MachineConfig {
    const char* name;
    const char* description;
    CardSet cards;
    // Krusader is a ROM payload, not an expansion card, so it deliberately
    // stays outside CardSet.
    bool krusader;
    int  ramKB;                 // Usable RAM in kilobytes (8 = standard dual-bank Apple-1)
    BasicType basicType;
    // A1-AUDIO Special Edition: Claudio Parmigiani's 10-unit A1-AUDIO card
    // (https://p-l4b.github.io/A1-AUDIO/). Same MOS 6581/8580 chip as the
    // prototype A1-SID, but the register window lives at $CC00-$CC1F instead
    // of $C800-$CFFF. Collides with TMS9918 at $CC00/$CC01 — the preset
    // layer enforces mutual exclusivity with `tms9918`.
    // P-LAB Apple-1 Juke-Box (Claudio Parmigiani & Jacopo Rosselli). When
    // true, the preset plugs the Juke-Box card at $4000-$BFFF or
    // $8000-$BFFF (per `jukeBoxJumper`), loads `roms/jukebox.rom` with the
    // chip-mode stored in `jukeBoxChipMode`, and claims the Px/Sx bank
    // latch at $CA00. Mutually exclusive with CFFA1, microSD, Krusader,
    // Wi-Fi Modem and A1-SID — the preset layer enforces that.
    JukeBoxPresetOptions jukeBox;
    // P-LAB CodeTank — 28c256 ROM daughterboard at $4000-$7FFF that
    // physically piggybacks the TMS9918 Graphic Card on real P-LAB
    // hardware. It has no edge connector and no on-board address decoder,
    // so it cannot exist standalone: enabling codeTank auto-plugs TMS9918
    // (see Memory::setCodeTankEnabled), and disabling TMS9918 cascade-
    // unplugs CodeTank. Mutually exclusive with the Juke-Box (overlapping
    // ROM window).
    // Optional — when non-empty, the named ROM file is loaded into the
    // CodeTank card on plug. Empty falls back to the default probe path
    // (roms/codetank/Codetank_ARCADE.rom, then the legacy roms/codetank.rom).
    CodeTankPresetOptions codeTank;
    // Uncle Bernie's Extended ACI — the $C500-$C5FF page of the improved
    // Gen-2 cassette interface (Applefritter, august 2026). Daughter page of
    // the ACI's own PROM pair, so it cannot exist without `aci`: enabling it
    // cascade-plugs the ACI (see Memory::setExtendedACIEnabled) and unplugging
    // the ACI cascade-unplugs it. ON wherever the ACI is plugged, EXCEPT the two
    // historically faithful 1976 machines (#4 ACI & BASIC cassette, #5 GT-6144)
    // and the CC65 bench (#0), which mirrors #4 exactly — see
    // preset_ram_profiles_smoke. So: on for #2 (GEN2 bench), #6 (Briel Krusader),
    // #11 (GEN2 HGR Color) and #12 (POM1 Fantasy).
    MachineWindowPlacement layout[8];
    int layoutCount;

    CardSet enabledCards() const { return cards; }
    bool hasCard(CardId id) const { return cards.contains(id); }
};

extern const MachineConfig kMachinePresets[];
extern const int kMachinePresetCount;

// ── The preset REGISTRY ─────────────────────────────────────────────────────
//
// `kMachinePresets[]` is the thirteen machines POM1 ships, and it stays exactly
// that: a `constexpr`-shaped table, indices 0-12, one `PresetId` each, parsed as
// TEXT by preset_ram_profiles_smoke and indexed by name from
// `kPresetCC65Bench` and friends. None of that moves.
//
// What the registry adds is EXTERNAL machines — preset files (src/PresetFile.h)
// the user wrote — appended after the built-ins and addressable by index like
// any other preset, so the Hardware menu, the profile chooser, `--preset` and
// the DevBench all reach them through one accessor instead of two code paths.
//
// Three rules the rest of the codebase depends on:
//
//   * BUILT-INS KEEP THEIR INDICES. External presets start at
//     `kMachinePresetCount` and never renumber a shipped one. `ini/preset_NN.size`
//     and `ini/imgui_preset_NN.ini` are keyed by index, so a shipped profile's
//     saved layout must not migrate onto a different machine because the user
//     dropped a file in `presets/`.
//   * "DEFAULT = LAST" IS DEAD, and `kDefaultPresetId` replaces it. It was true
//     only while the table was the whole world; with one external preset
//     registered, `count - 1` is a user's file rather than POM1 Fantasy. The one
//     site that relied on it is fixed.
//   * `presetIdFromIndex()` RETURNS `Invalid` FOR AN EXTERNAL INDEX, because an
//     external preset has no stable identity — nothing may key behaviour off a
//     `PresetId` it did not check. `machinePresetMode()` is what answers the one
//     question `isFantasyPreset()` used to, for both kinds.
//
// Registration order IS index order. `PresetLoader` sorts by filename so a given
// `presets/` directory always yields the same indices.

/// Total presets addressable by index: the built-in table plus every registered
/// external machine. Free-function form so the CLI can read it without linking
/// the UI; MainWindow_ImGui::getPresetCount() forwards here.
int machinePresetCount();

/// The machine at `index`, built-in or external, or nullptr when out of range.
/// THIS is what a caller holding a user-supplied index must use —
/// `kMachinePresets[index]` is only safe for an index that came from a named
/// constant.
const MachineConfig* machinePresetAt(int index);

/// Name of preset `index`, or nullptr when out of range.
const char* machinePresetName(int index);

/// True when `index` names a registered external machine rather than a table row.
bool machinePresetIsExternal(int index);

/// The bus mode preset `index` runs in. Built-ins answer from their `PresetId`;
/// an external preset carries its own, because it has none. Every caller that
/// used to ask `isFantasyPreset(presetIdFromIndex(i))` must ask this instead —
/// that composition silently answers "strict" for an external index.
TopologyMode machinePresetMode(int index);

/// Append an external machine. `cfg`'s `name`, `description` and
/// `codeTank.romPath` are COPIED into stable storage and rewired, so the caller
/// may destroy its source. Returns the new index, or -1 when the registry is
/// full (`kMaxExternalPresets`).
int registerExternalPreset(const MachineConfig& cfg, TopologyMode mode);

/// Drop every external machine. Built-ins are untouched.
void clearExternalPresets();

/// Bound on the registry. A menu is a list a human reads; past this the
/// directory is not a preset collection but a mistake.
inline constexpr int kMaxExternalPresets = 64;

PresetId presetIdFromIndex(int index);
const MachineConfig* machinePreset(PresetId id);
bool isFantasyPreset(PresetId id);

/// Validates stable identities, dependencies and strict/fantasy conflicts.
/// Returns false with a concise diagnostic for malformed built-in data.
bool validateMachinePresets(std::string& error);

// Named indices into kMachinePresets[] that other subsystems depend on by
// position (DevBench targets in Pom1BenchHost's kP1Targets[], the reverse
// applyMachineConfig auto-open). Anchoring them here means a preset reorder is
// a one-line edit instead of a silent DevBench breakage across scattered
// `t.preset == N` comparisons. Pinned by preset_ram_profiles_smoke.
inline constexpr int kPresetCC65Bench = presetIndex(PresetId::CC65Bench);
inline constexpr int kPresetTMS9918Bench = presetIndex(PresetId::TMS9918Bench);
inline constexpr int kPresetGen2Bench = presetIndex(PresetId::Gen2Bench);
inline constexpr int kPresetIntegerCassette = presetIndex(PresetId::IntegerCassette);
inline constexpr int kPresetMicroSD = presetIndex(PresetId::MicroSDApplesoft);
inline constexpr int kPresetTMS9918Card = presetIndex(PresetId::TmsCodeTank);
inline constexpr int kPresetGen2Color = presetIndex(PresetId::Gen2Color);

} // namespace pom1

#endif // POM1_MACHINE_PRESETS_H
