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

struct MachineConfig {
    const char* name;
    const char* description;
    bool graphicsCard, microSD, sid, tms9918, a1ioRtc, wifiModem, terminalCard;
    bool pr40Printer;   // SWTPC PR-40 (Jobs Oct. 1976 Interface Age hack)
    bool krusader;
    bool cffa1;
    bool aci;                   // Apple Cassette Interface (false for pre-ACI Bare 4K)
    int  ramKB;                 // Usable RAM in kilobytes (8 = standard dual-bank Apple-1)
    BasicType basicType;
    // A1-AUDIO Special Edition: Claudio Parmigiani's 10-unit A1-AUDIO card
    // (https://p-l4b.github.io/A1-AUDIO/). Same MOS 6581/8580 chip as the
    // prototype A1-SID, but the register window lives at $CC00-$CC1F instead
    // of $C800-$CFFF. Collides with TMS9918 at $CC00/$CC01 — the preset
    // layer enforces mutual exclusivity with `tms9918`.
    bool sidSpecialEdition;
    // P-LAB Apple-1 Juke-Box (Claudio Parmigiani & Jacopo Rosselli). When
    // true, the preset plugs the Juke-Box card at $4000-$BFFF or
    // $8000-$BFFF (per `jukeBoxJumper`), loads `roms/jukebox.rom` with the
    // chip-mode stored in `jukeBoxChipMode`, and claims the Px/Sx bank
    // latch at $CA00. Mutually exclusive with CFFA1, microSD, Krusader,
    // Wi-Fi Modem and A1-SID — the preset layer enforces that.
    bool jukeBox;
    JukeBox::Jumper jukeBoxJumper;
    JukeBox::ChipMode jukeBoxChipMode;
    // P-LAB CodeTank — 28c256 ROM daughterboard at $4000-$7FFF that
    // physically piggybacks the TMS9918 Graphic Card on real P-LAB
    // hardware. It has no edge connector and no on-board address decoder,
    // so it cannot exist standalone: enabling codeTank auto-plugs TMS9918
    // (see Memory::setCodeTankEnabled), and disabling TMS9918 cascade-
    // unplugs CodeTank. Mutually exclusive with the Juke-Box (overlapping
    // ROM window).
    bool codeTank;
    CodeTank::Jumper codeTankJumper;
    // Optional — when non-empty, the named ROM file is loaded into the
    // CodeTank card on plug. Empty falls back to the default probe path
    // (roms/codetank/Codetank_ARCADE.rom, then the legacy roms/codetank.rom).
    const char* codeTankRomPath;
    // SWTPC GT-6144 Graphic Terminal (1976) — write-only 64x96 mono framebuffer
    // at $D00A. No bus conflicts with other cards at that address.
    bool gt6144;
    // P-LAB IEC daughterboard for the microSD Storage Card. Drives the
    // Commodore IEC serial bus on unused 65C22 pins (PORTB bits 2-6) via
    // an SN7406 inverter. Backed by a virtual 1541 mounted from
    // disks/iec/dev8.d64. Daughterboard only — requires microSD enabled.
    bool iecCard;
    // Uncle Bernie's Extended ACI — the $C500-$C5FF page of the improved
    // Gen-2 cassette interface (Applefritter, august 2026). Daughter page of
    // the ACI's own PROM pair, so it cannot exist without `aci`: enabling it
    // cascade-plugs the ACI (see Memory::setExtendedACIEnabled) and unplugging
    // the ACI cascade-unplugs it. ON wherever the ACI is plugged, EXCEPT the two
    // historically faithful 1976 machines (#4 ACI & BASIC cassette, #5 GT-6144)
    // and the CC65 bench (#0), which mirrors #4 exactly — see
    // preset_ram_profiles_smoke. So: on for #2 (GEN2 bench), #6 (Briel Krusader),
    // #11 (GEN2 HGR Color) and #12 (POM1 Fantasy).
    bool extendedAci;
    MachineWindowPlacement layout[8];
    int layoutCount;
};

extern const MachineConfig kMachinePresets[];
extern const int kMachinePresetCount;

/// Number of entries in kMachinePresets[]. Free-function form so the CLI can
/// read the table without linking the UI; MainWindow_ImGui::getPresetCount()
/// forwards here.
int machinePresetCount();

/// Name of preset `index`, or nullptr when out of range.
const char* machinePresetName(int index);

// Named indices into kMachinePresets[] that other subsystems depend on by
// position (DevBench targets in Pom1BenchHost's kP1Targets[], the reverse
// applyMachineConfig auto-open). Anchoring them here means a preset reorder is
// a one-line edit instead of a silent DevBench breakage across scattered
// `t.preset == N` comparisons. Pinned by preset_ram_profiles_smoke.
inline constexpr int kPresetCC65Bench      = 0;   // Apple-1 CC65 Development Bench
inline constexpr int kPresetTMS9918Bench   = 1;   // Apple-1 TMS9918 Development Bench
inline constexpr int kPresetGen2Bench      = 2;   // Apple-1 GEN2 HGR Development Bench
inline constexpr int kPresetIntegerCassette = 4;  // Apple-1 with ACI & Integer BASIC cassette
inline constexpr int kPresetMicroSD        = 8;   // P-LAB microSD + Applesoft Lite
inline constexpr int kPresetTMS9918Card    = 9;   // P-LAB Apple-1 with TMS9918 + CodeTank
inline constexpr int kPresetGen2Color      = 11;  // Uncle Bernie's GEN2 HGR Color
// POM1 Multiplexing Fantasy is always the LAST preset (invariant — see
// applyMachineConfig / the "default = last" contract). Use kMachinePresetCount-1.

} // namespace pom1

#endif // POM1_MACHINE_PRESETS_H
