// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MainWindow_Presets.cpp — table of machine presets and the
// applyMachineConfig / applyPendingLayout / getPreset* methods of
// MainWindow_ImGui that consume it. Kept in its own TU so the preset table
// is one self-contained chunk; this is the natural future migration point
// to load presets from an external JSON/YAML file.

#include "MainWindow_ImGui.h"
#include "MainWindow_Internal.h"
#include "POM1Build.h"
#include "Logger.h"
#include "CodeBench.h"   // codeBench_->loadStarterForTargetIfClean() in DevBench presets
#include "Pom1BenchHost.h" // benchHost_->targetFor / selectTargetExplicit for the chooser's language launch
#include "ProcessUtil.h" // bench::executableDir() for exe-relative ini_defaults/

#include "imgui.h"
#include "imgui_internal.h"
#include "IconsFontAwesome6.h"
#include "PomRenderer.h"   // POM1 icon + Apple-50 logo textures on the chooser

#if !POM1_IS_WASM
#include <GLFW/glfw3.h>
#else
#include <emscripten.h>   // EM_ASM — FS.syncfs flush of the IDBFS-backed ini/
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>

namespace pom1::mainwindow::detail {

// Parmigiani's golden rule — "one board at a time".
// Claudio PARMIGIANI (P-LAB) insists that on real Apple-1 hardware you plug
// ONE P-LAB expansion card at a time, never several. The 6502 bus has no
// arbitration and many cards overlap address windows by design (SID vs.
// TMS9918 at $CC00, A1-IO vs. GEN2 HGR at $2000-$200F, Juke-Box claiming
// $4000-$BFFF, etc.). The two "Multiplexing Fantasy" entries below plug
// several P-LAB cards simultaneously on purpose — the name "Fantasy" flags
// that these are emulator-only configurations that cannot exist on real
// silicon. Every other preset respects the golden rule; mutual-exclusion
// logic in applyMachineConfig / setXxxEnabled mirrors real bus conflicts
// (SID ↔ SID-SE, TMS9918 ↔ SID-SE, GEN2 ↔ A1-IO, Juke-Box ↔ CFFA1/microSD/
// Krusader/Wi-Fi Modem). See CLAUDE.md for the rationale.
//
// Daughterboard rule: CodeTank is NOT an expansion card — it's a daughter-
// board that physically piggybacks the TMS9918 Graphic Card. Never set
// codeTank=true with tms9918=false in a preset; Memory::setCodeTankEnabled
// auto-plugs the host anyway, but a malformed preset would advertise an
// impossible real-hardware configuration.
// Layout design notes:
//   - Every preset uses the same canonical POM1 Fantasy frame:
//         Apple 1 Screen at (10, 61) size (843, 701)  ← LEFT column
//         Right column: x=858, width=338, y range 61..764
//         Resulting GLFW window: 1206 × 807 (matches last preset).
//   - Right column is split top/bottom for every preset. The TOP slot
//     (y=61, height≈223) carries the tutorial most relevant to the
//     preset; the BOTTOM slot (y=288, height≈476) carries the
//     peripheral's own visualisation panel (or a second useful window
//     if the card has no dedicated panel: CFFA1, microSD, SID).
//   - Last preset (POM1 Multiplexing Fantasy) is the shipped "default"
//     preset — its layout MUST stay byte-identical to the shipped
//     screenshot reference (the README mentions it). Don't touch.
//     Every other preset mirrors its geometry. First-time use writes
//     ini/imgui_preset_NN.ini + ini/preset_NN.size (NN = index); subsequent launches
//     load from there.
//   - Preset 10 (P-LAB Multiplexing Fantasy) is the only exception
//     that departs from the tutorial+peripheral template: it's the
//     "everything plugged" fantasy and stacks 3 peripherals in the
//     right column instead of a tutorial.
const MachineConfig kMachinePresets[] = {
    //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI  RAM  BASIC              SID-SE
    // ── Development benches (indices 0-2) ─────────────────────────────────────
    // The profiles the in-app DevBench loads when you pick a (language x machine)
    // target (kP1Targets[].preset in Pom1BenchHost.cpp). Each MIRRORS the machine
    // config of an existing preset (cards + RAM + BASIC): CC65 = "ACI & BASIC
    // cassette" (8 KB dual-bank + ACI + Integer cassette), TMS9918 = "TMS9918
    // (CodeTank)" (8 KB + TMS9918 + CodeTank), GEN2 = "GEN2 HGR Color" (48 KB +
    // GEN2 + ACI). Listed FIRST in the Presets menu; the array still ends with
    // POM1 Fantasy so "default = last" holds.
    {   // 0 — cc65 / asm text development
        "Apple-1 CC65 Development Bench",
        "Development bench for cc65 (C / 6502 asm) Apple-1 text programs. Same "
        "machine config as 'Apple-1 with ACI & BASIC cassette': 8 KB dual-bank RAM "
        "(4 KB at $0000-$0FFF + 4 KB at $E000-$EFFF), ACI, Integer BASIC ready to "
        "load from cassette, WOZ Monitor. The in-app DevBench loads this profile "
        "for the Apple-1 asm/C targets.",
        false, false, false, false, false, false, false,
        /*pr40*/ false,
        false, false, /*aci*/ true, /*ramKB*/ 8, BasicType::IntegerCassette,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*codeTank*/ false, CodeTank::Jumper::Lower16, /*codeTankRom*/ nullptr,
        /*gt6144*/ false,
        /*iecCard*/ false,
        {
            {"Apple 1 Screen", {10, 61}, {843, 701}},
        }, 1
    },
    {   // 1 — TMS9918 + CodeTank graphic development
        "Apple-1 TMS9918 Development Bench",
        "Development bench for P-LAB TMS9918 graphics. Same machine config as "
        "'P-LAB Apple-1 with TMS9918 (CodeTank daughterboard)': 8 KB dual-bank RAM, "
        "TMS9918A VDP ($CC00/$CC01) + the CodeTank 28c256 ROM daughterboard "
        "($4000-$7FFF) so the DevBench can flash built programs as a CodeTank dev "
        "cartridge. The in-app DevBench loads this profile for the TMS9918 asm/C targets.",
        false, false, false, true, false, false, false,
        /*pr40*/ false,
        false, false, /*aci*/ false, /*ramKB*/ 8, BasicType::None,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*codeTank*/ true, CodeTank::Jumper::Lower16,
        /*codeTankRom*/ "roms/codetank/Codetank_ARCADE.rom",
        /*gt6144*/ false,
        /*iecCard*/ false,
        {
            {"Apple 1 Screen",               {10,  61}, {843, 701}},
            {"P-LAB Graphic Card (TMS9918)", {858, 61}, {338, 300}},
        }, 2
    },
    {   // 2 — Uncle Bernie's GEN2 HGR graphic development
        "Apple-1 GEN2 HGR Development Bench",
        "Development bench for Uncle Bernie's GEN2 280x192 HGR colour card. Same "
        "machine config as 'Uncle Bernie's Apple-1 with GEN2 HGR Color': 48 KB RAM "
        "(the GEN2 card's DRAM doubles as the RAM expansion; HGR pages $2000/$4000 "
        "are RAM-backed), GEN2 HGR + ACI plugged. The in-app DevBench loads this "
        "profile for the GEN2 HGR asm/C targets.",
        true, false, false, false, false, false, false,
        /*pr40*/ false,
        false, false, /*aci*/ true, /*ramKB*/ 48, BasicType::None,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*codeTank*/ false, CodeTank::Jumper::Lower16, /*codeTankRom*/ nullptr,
        /*gt6144*/ false,
        /*iecCard*/ false,
        {
            {"Apple 1 Screen",                       {10,  61}, {843, 701}},
            {"Uncle Bernie's GEN2 HGR Graphic Card", {858, 60}, {338, 264}},
            {"GEN2 Video Workbench (Photo)",         {862, 329}, {342, 354}},
        }, 3
    },
    {
        "Bare Apple-1 (July 1976)",
        "Pre-ACI original: 6502, 4 KB RAM, PIA 6821, WOZ Monitor.",
        false, false, false, false, false, false, false,
        /*pr40*/ false,
        false, false, false, 4, BasicType::None,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*codeTank*/ false, CodeTank::Jumper::Lower16, /*codeTankRom*/ nullptr,
        /*gt6144*/ false,
        /*iecCard*/ false,
        // Right column carries two 1976-era photos of Woz + Jobs with
        // the Apple-1 — the portrait (standing Woz, seated Jobs) on
        // top and the landscape demo-session shot below. Fitting
        // companion to the bare July-1976 machine, replacing the
        // previously empty right column.
        {
            {"Apple 1 Screen",                  {10,  61},  {843, 701}},
            {"Woz & Jobs (1976)",               {859, 61},  {337, 497}},
            {"Apple-1 Demo Session (1976)",     {858, 516}, {338, 245}},
        }, 3
    },
    {
        "Apple-1 with ACI & BASIC cassette (October 1976)",
        "Original bare board with the ACI cassette expansion card: 6502, "
        "8 KB RAM (4 KB at $0000-$0FFF + 4 KB at $E000-$EFFF — Parmigiani's "
        "standard dual-bank layout), PIA 6821, Integer BASIC cassette ready "
        "to load into the upper 4 KB RAM bank, WOZ Monitor.",
        false, false, false, false, false, false, false,
        /*pr40*/ false,
        false, false, true, 8, BasicType::IntegerCassette,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*codeTank*/ false, CodeTank::Jumper::Lower16, /*codeTankRom*/ nullptr,
        /*gt6144*/ false,
        /*iecCard*/ false,
        {
            {"Apple 1 Screen",           {10,  61},  {843, 701}},
            {"Tutorial: Cassette (ACI)", {858, 61},  {338, 223}},
            {"Apple-1 Cassette Deck",    {858, 288}, {338, 476}},
        }, 3
    },
    {
        "Apple-1 + SWTPC GT-6144 Graphic Terminal (1976)",
        "First commercial Apple-1 graphics card: Southwest Technical Products' GT-6144 "
        "(1976, $98.50). Standalone 64x96 monochrome framebuffer on 6x Intel 2102 SRAM "
        "chips, write-only I/O port at $D00A. Boots on the October-1976 Apple-1 "
        "footprint (8 KB RAM + ACI + BASIC cassette), which is the machine Woz used "
        "when demonstrating the GT-6144 in Interface Age. Includes Steve Jobs' "
        "PR-40 printer interface in Mixed switch mode. Power-on framebuffer state "
        "is visible SRAM bistable noise; programs clear it before drawing. See "
        "GT6144.h for the 4-phase command protocol.",
        false, false, false, false, false, false, false,
        /*pr40*/ true,
        false, false, /*aci*/ true, /*ramKB*/ 8, BasicType::None,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*codeTank*/ false, CodeTank::Jumper::Lower16, /*codeTankRom*/ nullptr,
        /*gt6144*/ true,
        /*iecCard*/ false,
        {
            {"Apple 1 Screen",                 {10,  61},  {843, 701}},
            // 4:3 content lives inside whatever size we give the window;
            // GL_NEAREST stretches the 64x96 texture horizontally 2x.
            {"SWTPC GT-6144 Graphic Terminal", {856, 58},  {338, 220}},
            {"SWTPC PR-40 Printer",            {856, 282}, {338, 220}},
            {"Tutorial: SWTPC GT-6144",        {856, 506}, {339, 256}},
        }, 4
    },
    {
        "Replica-1 with ACI & Krusader (Briel 2003)",
        "Vince Briel's modern recreation. 8 KB dual-bank RAM (4 KB at "
        "$0000-$0FFF + 4 KB at $E000-$EFFF — Parmigiani's standard layout, same "
        "as 99 % of Originals). Krusader assembler and ACI cassette; "
        "Integer BASIC can be loaded from cassette when needed.",
        false, false, false, false, false, false, false,
        /*pr40*/ false,
        true, false, true, 8, BasicType::None,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*codeTank*/ false, CodeTank::Jumper::Lower16, /*codeTankRom*/ nullptr,
        /*gt6144*/ false,
        /*iecCard*/ false,
        {
            {"Apple 1 Screen",        {10,  61},  {843, 701}},
            {"Tutorial: Krusader",    {858, 61},  {338, 223}},
            {"Apple-1 Cassette Deck", {858, 288}, {338, 476}},
        }, 3
    },
    {
        "Replica-1 with CFFA1 & Applesoft Lite (Dreher 2007)",
        "Replica-1 with CFFA1 CompactFlash storage, Applesoft Lite. "
        "8 KB dual-bank RAM (4 KB at $0000-$0FFF + 4 KB at $E000-$EFFF — "
        "Parmigiani's standard layout). Applesoft Lite spans $E000-$FFFF in "
        "the CFFA1 build, so the high bank holds the BASIC ROM.",
        false, false, false, false, false, false, false,
        /*pr40*/ false,
        false, true, false, 8, BasicType::ApplesoftLite,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*codeTank*/ false, CodeTank::Jumper::Lower16, /*codeTankRom*/ nullptr,
        /*gt6144*/ false,
        /*iecCard*/ false,
        {
            // CFFA1 has no dedicated window (transparent storage); pair
            // the storage tutorial with the BASIC tutorial since the
            // preset boots Applesoft Lite by default.
            {"Apple 1 Screen",                {10,  61},  {843, 701}},
            {"Tutorial: CFFA1 CompactFlash",  {858, 61},  {338, 299}},
            {"Tutorial: Applesoft Lite",      {858, 364}, {338, 400}},
        }, 3
    },
    {
        "P-LAB Apple-1 with microSD & Applesoft Lite (April 2022)",
        "P-LAB microSD Storage Card, Applesoft Lite. 8 KB dual-bank RAM "
        "(4 KB at $0000-$0FFF + 4 KB at $E000-$EFFF — Parmigiani's standard). "
        "Applesoft Lite is a separate ROM at $6000-$7FFF (cold/warm: "
        "6000R / 6003R); the $E000-$EFFF high bank stays free RAM.",
        false, true, false, false, false, false, false,
        /*pr40*/ false,
        false, false, false, 8, BasicType::ApplesoftLite,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*codeTank*/ false, CodeTank::Jumper::Lower16, /*codeTankRom*/ nullptr,
        /*gt6144*/ false,
        /*iecCard*/ false,
        {
            // microSD is also transparent storage — no dedicated panel.
            // Show both the storage tutorial and the Applesoft one since
            // the microSD preset boots into Applesoft Lite.
            {"Apple 1 Screen",           {10,  61},  {843, 701}},
            {"Tutorial: microSD",        {858, 61},  {338, 327}},
            {"Tutorial: Applesoft Lite", {858, 389}, {338, 375}},
        }, 3
    },
    {   //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI
        "P-LAB Apple-1 with TMS9918 (CodeTank daughterboard)",
        "P-LAB Graphic Card (TMS9918A VDP) with the CodeTank 28c256 ROM daughterboard "
        "(Codetank_ARCADE.rom) at $4000-$7FFF, no BASIC (CodeTank ROM is the program). "
        "Replica/Originals dual-bank RAM: 4 KB at $0000-$0FFF + 4 KB at $E000-$EFFF "
        "(Parmigiani's standard 8 KB layout — same as 99% of Originals; with no BASIC "
        "loaded the upper bank is free RAM). The CodeTank piggybacks the Graphic Card "
        "on real P-LAB silicon - it has no edge connector. Type 4000R: Lower jumper "
        "boots the 3-game menu (1=Galaga, 2=Sokoban, 3=Snake); Upper jumper "
        "runs TMS LOGO V2.6 directly.",
        false, false, false, true, false, false, false,
        /*pr40*/ false,
        false, false, false, 8, BasicType::None,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*codeTank*/ true, CodeTank::Jumper::Lower16,
        /*codeTankRom*/ "roms/codetank/Codetank_ARCADE.rom",
        /*gt6144*/ false,
        /*iecCard*/ false,
        {
            // Factory layout matches ini_defaults/imgui_preset_09.ini (also
            // seeded as build/ini/ when pre-generating preset layouts).
            {"Apple 1 Screen",                {4,   60},  {404, 342}},
            {"P-LAB CodeTank Library",        {4,   200}, {630, 597}},
            {"P-LAB Graphic Card (TMS9918)",  {410, 61},  {795, 628}},
            {"Memory Map Bar (Horizontal)",   {2,   690}, {1202, 105}},
        }, 4
    },
    {   //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI
        "P-LAB Apple-1 Multiplexing Fantasy",
        "Emulator-only fantasy: plugs A1-SID, TMS9918 (+ CodeTank), I/O & RTC, "
        "Wi-Fi modem, and Terminal Card all at once. Violates Claudio Parmigiani's golden "
        "rule \"one board at a time\" - impossible on real Apple-1 silicon (the 6502 bus has "
        "no arbitration, and several of these cards share overlapping address windows). "
        "The microSD stays unplugged even here: its Applesoft Lite EEPROM window "
        "($6000-$7FFF) sits inside the CodeTank ROM ($4000-$7FFF) — plug it from the "
        "Hardware menu and the CodeTank pops out. Provided for convenience only.",
        false, false, true, true, true, true, true,
        /*pr40*/ false,
        false, false, false, 64, BasicType::Integer,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*codeTank*/ true, CodeTank::Jumper::Lower16,
        /*codeTankRom*/ "roms/codetank/Codetank_ARCADE.rom",
        /*gt6144*/ false,
        /*iecCard*/ false,
        {
            // P-LAB Fantasy departs from the tutorial+peripheral template:
            // the right column stacks three cards so the user can see
            // TMS9918 + Modem + I/O at once. Terminal Card stays plugged
            // but hidden (open via the Hardware menu if needed). PR-40
            // is intentionally unplugged in every default preset — plug
            // it from the toolbar when needed.
            {"Apple 1 Screen",                 {11,  60},  {843, 701}},
            // Right column: live TMS9918 framebuffer viewer on top,
            // static P-LAB TMS9918 PCB photo beneath. The I/O Board &
            // RTC and Wi-Fi Modem cards are still plugged (cfg.a1ioRtc
            // / cfg.wifiModem) so their state updates at runtime, but
            // their windows stay closed by default — the user can open
            // them from the Hardware menu when needed. Positions below
            // match the ini the user has been iterating on.
            {"P-LAB Graphic Card (TMS9918)",   {861, 72},  {344, 286}},
            {"P-LAB TMS9918 Card (Photo)",     {862, 393}, {342, 354}},
        }, 3
    },
    {
        "Uncle Bernie's Apple-1 with GEN2 HGR Color (April 2026)",
        "Uncle Bernie's GEN2 280x192 HGR color graphics — his real release "
        "setup. The card carries 48 KB of its own DRAM and doubles as a RAM "
        "expansion (spec Q9: $0000-$BFFF on the card via the VMA write-"
        "through latch, plus $E000-$EFFF on the motherboard — Bernie quotes "
        "54 KB total). Both HGR pages ($2000/$4000) and both TEXT/LORES "
        "pages ($0400/$0800) are RAM-backed. The ACI is plugged alongside — "
        "the release board is designed to coexist with it (Q7: the PCB even "
        "has a cutout for the ACI jacks), and Apple II ports keep their "
        "$C030 SPEAKER accesses for sound through the ACI TAPE OUT.",
        true, false, false, false, false, false, false,
        /*pr40*/ false,
        false, false, true, 48, BasicType::None,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*codeTank*/ false, CodeTank::Jumper::Lower16, /*codeTankRom*/ nullptr,
        /*gt6144*/ false,
        /*iecCard*/ false,
        {
            {"Apple 1 Screen",                       {10,  61},  {843, 701}},
            {"Uncle Bernie's GEN2 HGR Graphic Card", {858, 60},  {338, 180}},
            {"GEN2 Video Workbench (Photo)",         {862, 245}, {342, 240}},
            {"Tutorial: Uncle Bernie's GEN2 HGR",    {858, 490}, {339, 271}},
        }, 4
    },
    {
        "POM1 Apple-1 Multiplexing Fantasy (2026)",
        "Emulator-only fantasy (violates Parmigiani's golden rule \"one board at a time\"): "
        "64 KB RAM, Applesoft Lite, ACI + microSD + A1-SID + Wi-Fi modem + Terminal Card. "
        "Graphic cards and the PR-40 printer off by default — plug them from the toolbar. "
        "ACI plugged by default so the cassette deck can load/save tapes. Boots with the "
        "Cassette Deck + Welcome panels already open to the right of the Apple 1 screen; "
        "your layout customisations persist under ini/imgui_preset_12.ini "
        "(plus ini/preset_12.size for the OS window frame).",
        false, true, true, false, false, true, true,
        /*pr40*/ false,
        false, false, true, 64, BasicType::ApplesoftLite,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*codeTank*/ false, CodeTank::Jumper::Lower16, /*codeTankRom*/ nullptr,
        /*gt6144*/ false,
        /*iecCard*/ false,
        {
            // Positions / sizes match the shipped POM1 Fantasy screenshot
            // so the first launch (no saved ini/imgui_preset_12.ini yet)
            // snaps straight to that layout.
            {"Apple 1 Screen",         {10,  61},  {843, 701}},
            {"Welcome",                {858, 61},  {338, 223}},
            {"Apple-1 Cassette Deck",  {858, 288}, {338, 476}},
        }, 3
    },
};

const int kMachinePresetCount = static_cast<int>(sizeof(kMachinePresets) / sizeof(kMachinePresets[0]));

ImVec2 computePresetLayoutExtent(const MachineConfig& cfg, ImVec2 appleScreenFallbackSize)
{
    ImVec2 extent(0.0f, 0.0f);
    for (int i = 0; i < cfg.layoutCount; ++i) {
        const auto& p = cfg.layout[i];
        float w = p.size.x;
        float h = p.size.y;
        if ((w <= 0.0f || h <= 0.0f) && std::string_view(p.name) == "Apple 1 Screen") {
            if (w <= 0.0f) w = appleScreenFallbackSize.x;
            if (h <= 0.0f) h = appleScreenFallbackSize.y;
        }
        if (w <= 0.0f || h <= 0.0f) continue;
        extent.x = std::max(extent.x, p.pos.x + w);
        extent.y = std::max(extent.y, p.pos.y + h);
    }
    return extent;
}

} // namespace pom1::mainwindow::detail

// ---------------------------------------------------------------------------
// MainWindow_ImGui method implementations
// ---------------------------------------------------------------------------

namespace {
using namespace pom1::mainwindow::detail;

std::string findFirstExistingPath(std::initializer_list<const char*> candidates)
{
    for (const char* path : candidates) {
        if (path && std::filesystem::exists(path)) return path;
    }
    return {};
}
}

void MainWindow_ImGui::applyPendingLayout(const char* windowName)
{
    // Use FirstUseEver so preset-driven positions only apply when the
    // window has no saved state in the active preset's ini yet (see
    // savePresetLayout / loadPresetLayout for the ini/imgui_preset_NN.ini
    // + ini/preset_NN.size sidecar pair). After the first boot the user's
    // drags/resizes are persisted there and take precedence on subsequent
    // loads of the same preset (delete that file to restore the default).
    // Within a session, the condition also means that re-applying the same
    // preset leaves the user's customisations intact.
    // During a layout reset (resetActivePresetLayout) force the factory
    // position/size onto live windows with ImGuiCond_Always and KEEP the entry
    // so it re-applies every frame until layoutResetForceFrames drains; normal
    // switches use FirstUseEver and consume the entry once.
    const bool force = layoutResetForceFrames > 0;
    const ImGuiCond cond = force ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
    for (auto it = pendingLayout.begin(); it != pendingLayout.end(); ++it) {
        if (it->name == windowName) {
            ImGui::SetNextWindowPos(it->pos, cond);
            if (it->size.x > 0.0f)
                ImGui::SetNextWindowSize(it->size, cond);
            if (!force)
                pendingLayout.erase(it);
            return;
        }
    }
}

// Default OS-window size for a preset: the layout bounding box (with a fallback
// extent for the font-metric-dependent Apple 1 screen), floored at the POM1
// Fantasy frame so switching presets never shrinks the window. Shared by
// applyMachineConfig (first-use sizing) and resetActivePresetLayout.
void MainWindow_ImGui::defaultOsWindowSize(int presetIndex, int& outW, int& outH) const
{
    outW = windowedWidth;
    outH = windowedHeight;
    if (presetIndex < 0 || presetIndex >= kMachinePresetCount) return;
    const MachineConfig& cfg = kMachinePresets[presetIndex];

    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
    const ImVec2 charSize = ImGui::CalcTextSize("M");
    ImGui::PopFont();
    const ImVec2 cell = Screen_ImGui::computeApple1CellDimensions(charSize);
    const float sw = cell.x * Screen_ImGui::kApple1Columns * screen->scale
                     + kApple1ImGuiWinPadW;
    const float sh = cell.y * Screen_ImGui::kApple1Rows * screen->scale
                     + kApple1ImGuiWinPadH;
    const ImVec2 extent = computePresetLayoutExtent(cfg, ImVec2(sw, sh));
    const float rightPad  = 10.0f;
    const float bottomPad = kStatusBarBandHeight + kApple1WindowDecorationSlop;
    int glfwW = static_cast<int>(sw) + kApple1GlfwExtraW;
    int glfwH = static_cast<int>(std::ceil(sh + apple1LayoutVerticalChrome()));
    if (extent.x > 0.0f && extent.y > 0.0f) {
        glfwW = std::max(glfwW, static_cast<int>(std::ceil(extent.x + rightPad)));
        glfwH = std::max(glfwH, static_cast<int>(std::ceil(extent.y + bottomPad)));
    }
    // Floor at the last preset (POM1 Fantasy) extent — the canonical frame.
    const ImVec2 fantasyExtent = computePresetLayoutExtent(
        kMachinePresets[kMachinePresetCount - 1], ImVec2(sw, sh));
    if (fantasyExtent.x > 0.0f && fantasyExtent.y > 0.0f) {
        glfwW = std::max(glfwW, static_cast<int>(std::ceil(fantasyExtent.x + rightPad)));
        glfwH = std::max(glfwH, static_cast<int>(std::ceil(fantasyExtent.y + bottomPad)));
    }
    outW = glfwW;
    outH = glfwH;
}

#if POM1_IS_WASM
// WASM canvas size for a preset — the browser analog of defaultOsWindowSize,
// but deliberately WITHOUT the POM1 Fantasy floor: on the web the canvas IS
// the whole app surface, so a bare Apple-1 profile should give a small canvas
// and a fully-loaded profile a large one (that is what "adapt to each profile"
// means here). Uses the preset's *declared* layout table so the size is right
// the instant the profile changes, even though the card windows only plug in
// ~15 frames later (pendingCardEnableFrames) — waiting on the live ImGui
// bounding box would lag the switch.
void MainWindow_ImGui::computeWasmCanvasSize(int presetIndex, int& outW, int& outH) const
{
    outW = wasmCanvasPixelW;
    outH = wasmCanvasPixelH;
    if (presetIndex < 0 || presetIndex >= kMachinePresetCount) return;
    const MachineConfig& cfg = kMachinePresets[presetIndex];

    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
    const ImVec2 charSize = ImGui::CalcTextSize("M");
    ImGui::PopFont();
    const ImVec2 cell = Screen_ImGui::computeApple1CellDimensions(charSize);
    const float sw = cell.x * Screen_ImGui::kApple1Columns * screen->scale
                     + kApple1ImGuiWinPadW;
    const float sh = cell.y * Screen_ImGui::kApple1Rows * screen->scale
                     + kApple1ImGuiWinPadH;
    const ImVec2 extent = computePresetLayoutExtent(cfg, ImVec2(sw, sh));

    // Base = Apple-1 screen window + chrome (same as the old per-frame path).
    int w = static_cast<int>(sw) + kApple1GlfwExtraW;
    int h = static_cast<int>(std::ceil(sh + apple1LayoutVerticalChrome()));
    if (extent.x > 0.0f && extent.y > 0.0f) {
        w = std::max(w, static_cast<int>(std::ceil(extent.x + 8.0f)));
        h = std::max(h, static_cast<int>(std::ceil(extent.y + 8.0f)));
    }
    // Same clamps the per-frame path used: floor for usability, cap for safety.
    outW = std::min(std::max(w, 320), 4096);
    outH = std::min(std::max(h, 240), 4096);
}
#endif

void MainWindow_ImGui::applyMachineConfig(int presetIndex)
{
    if (presetIndex < 0 || presetIndex >= kMachinePresetCount) return;
    const MachineConfig& cfg = kMachinePresets[presetIndex];

    // Save the OUTGOING preset's layout (ImGui window positions + GLFW
    // window size) before we swap anything. Skipped on boot (activePreset
    // is -1) and on self-reapply (same index — nothing to migrate).
    if (activePresetIndex >= 0 && activePresetIndex != presetIndex) {
        savePresetLayout(activePresetIndex);
        // Évite d'écrire la géométrie du preset sortant dans le .ini entrant.
        memoryBarLastGeomValid = false;
        memoryBarHLastGeomValid = false;
    }

    // Reset transient UI state (dialogs, tutorial windows, help viewers,
    // Memory Viewer / Debugger / Memory Map Grid, card windows) so windows
    // that were open under the outgoing preset don't stay open under
    // the incoming one — the incoming preset's layout table is the sole
    // source of truth for what's visible. Each `showXxx` is then
    // re-enabled by the layout-driven auto-show loop further below, if
    // the new preset declares that panel.
    showAbout                = false;
    showSpecialThanks        = false;
    showHardwareReference    = false;
    showSoftwareReference    = false;
    showWelcome              = false;
    showTutorialIntegerBasic = false;
    showTutorialApplesoft    = false;
    showTutorialMicroSD      = false;
    showTutorialCassette     = false;
    showTutorialModemBBS     = false;
    showTutorialGT6144       = false;
    showTutorialPR40         = false;
    showTutorialTMS9918      = false;
    showTutorialA1IORTC      = false;
    showTutorialSID          = false;
    showTutorialGEN2HGR      = false;
    showTutorialCFFA1        = false;
    showTutorialJukeBox      = false;
    showTutorialTerminalCard = false;
    showTutorialKrusader     = false;
    showWozJobsPhoto         = false;
    showWozJobsRectPhoto     = false;
    showTmsBoardPhoto        = false;
    showGen2WorkbenchPhoto   = false;
    showKeyboardPhoto        = false;
    showWozPhoto             = false;
    showCopsonApple1Photo    = false;
    showHappyWozPhoto        = false;
    showPlabTms9918Photo     = false;
    showScreenConfig         = false;
    showMemoryConfig         = false;
    showLoadDialog           = false;
    showLoadTapeDialog       = false;
    showCassetteDeck         = false;
    showCodeTankLibrary      = false;
    codeTankPendingWozRunAt  = 0.0;
    bringTms9918WindowToFront = false;
    showMemoryMapGrid        = false;
    showMemoryBar            = false;
    showMemoryBarH           = false;
    // showMemoryViewer / showDebugger are kept across switches — they're
    // debug tools that users actively work with, not preset-bound panels.

    // Clear ImGui's accumulated window settings before loading the
    // incoming preset's ini. Without this, each per-preset ini grows on
    // every save to include every window ever seen across any preset
    // (TMS9918 entries end up in the Bare Apple-1 ini, etc.) because
    // LoadIniSettingsFromDisk merges into an already-populated settings
    // store.
    //
    // Use ImGui::ClearIniSettings() rather than SettingsWindows.clear()
    // directly: the public API also resets each ImGuiWindow's
    // SettingsOffset back to -1. Clearing just the chunk stream leaves
    // live windows pointing at freed memory (ImVector::clear frees the
    // buffer), and FindWindowSettingsByWindow follows the stale offset
    // via ptr_from_offset on the next save/read — segfault on the second
    // preset switch.
    ImGui::ClearIniSettings();

    // Show POM1 banner only for the last preset (POM1 Fantasy)
    screen->setShowBanner(presetIndex == kMachinePresetCount - 1);

    // Unplug EVERY expansion card up front — cleans any lingering state
    // from the previous preset (sources off the mixer, chips + rings
    // fully reset, TCP listeners closed, bus handles disabled). The
    // actual re-plug is deferred below via pendingCardEnableFrames so
    // each card starts ~15 frames after the CPU has been running: when
    // cards are re-plugged in the same frame as applyMachineConfig, the
    // peripherals latch onto the mixer / bus before the CPU has issued
    // any cycle and can miss their first register writes (silent SID,
    // silent cassette playback, dead WiFi modem etc.) until a manual
    // toggle. Deferring past a few thousand CPU cycles fixes that
    // uniformly for every card.
    emulation->setSIDEnabled(false);
    emulation->setSIDSpecialEditionEnabled(false);
    emulation->setACIEnabled(false);
    emulation->deactivateCassetteAudioSource();
    emulation->setIECCardEnabled(false);
    emulation->setMicroSDEnabled(false);
    emulation->setCFFA1Enabled(false);
    emulation->setTMS9918Enabled(false);
    emulation->setA1IO_RTCEnabled(false);
    emulation->setWiFiModemEnabled(false);
    emulation->setJukeBoxEnabled(false);
    emulation->setCodeTankEnabled(false);
    emulation->setPR40Enabled(false);
    emulation->setGT6144Enabled(false);
#if !POM1_IS_WASM
    emulation->setTerminalCardEnabled(false);
#endif

    // Silicon-fidelity power-on state (RAM size + VRAM/RAM cold-boot noise) is
    // consumed by the hardReset() below — resetMemory() seeds main RAM and
    // TMS9918::reset() seeds VRAM. ARM these BEFORE the reset, or the boot frame
    // shows the lenient bistable VRAM and the silicon-faithful noise only lands
    // on a later manual reset (user-visible bug: TMS sprites that depend on the
    // uninitialised-SAT noise rendered too cleanly on silicon presets at boot).
    const bool fantasyPreset =
        std::string_view(cfg.name).find("Fantasy") != std::string_view::npos;
    presetRamKB = cfg.ramKB;
    emulation->setPresetRamKB(cfg.ramKB);
    emulation->setVramNoiseOnReset(!fantasyPreset);
    emulation->setSystemRamNoiseOnReset(!fantasyPreset);

    // Skip the full hard reset on the very first invocation — at that
    // point Memory::Memory() has just run initMemory() (default ROMs +
    // peripheral resets) so hardReset() would redo the exact same work
    // (double BASIC/WOZ/ACI/SD loads, multiple TerminalCard re-listens).
    // From the second call onward (preset switch, user action) the
    // hardReset is mandatory: we need to wipe RAM, reload default ROMs,
    // and reset every peripheral before applying the new preset.
    // Exception: a SILICON preset on the very first call still needs ONE reset
    // so the just-armed power-on noise replaces the constructor's lenient seed
    // (Fantasy first-boot keeps skipping it — no power-on noise wanted there).
    // DevBench profiles (indices 0-2) are a compile-and-run workflow, not a
    // cold-boot demo: skip the ~3 s power-on scenarization so the reset lands on
    // a cleared screen immediately.
    const bool isDevBench = (presetIndex >= 0 && presetIndex <= 2);
    if (presetAppliedOnce) {
        emulation->hardReset(/*animateBoot=*/!isDevBench);
    } else {
        if (!fantasyPreset)
            emulation->hardReset(/*animateBoot=*/!isDevBench);
        presetAppliedOnce = true;
    }
    loadedPrograms.clear();
    loadedRoms.clear();

    // RAM size for this preset + fantasyPreset were applied above (before the
    // boot reset, so the power-on noise lands on the first frame). Continue
    // arming the rest of the silicon-fidelity bundle.
    emulation->setSiliconStrictMode(!fantasyPreset);
    siliconStrictModeEnabled = !fantasyPreset;
    emulation->setOutOfRangeStrictMode(!fantasyPreset);
    oorStrictModeEnabled = !fantasyPreset;
    // Silicon Strict is an all-or-nothing master switch: a non-Fantasy preset
    // ARMS every silicon-fidelity knob at once, Fantasy (the default, last
    // preset) DISARMS them all. After the preset lands the user can flip any
    // individual knob in the Silicon Strict window and that override sticks
    // until the next preset switch or master toggle. The same bundle is armed
    // by the master button in MainWindow_HardwareWindows.cpp — keep them in
    // sync. DRAM refresh (4/65 CPU steal) is part of the bundle, so a Silicon
    // preset reproduces the real-DRAM beam-race drift out of the box.
    dramRefreshEnabled = !fantasyPreset;
    emulation->setDramRefreshEnabled(!fantasyPreset);
    vramNoiseOnResetEnabled = !fantasyPreset;        // armed before the boot reset above
    systemRamNoiseOnResetEnabled = !fantasyPreset;   // armed before the boot reset above
    // NMOS decimal ADC/SBC flag bug: original-chip behaviour on strict presets,
    // 65C02-corrected on the (fantasy) Multiplexing presets.
    emulation->setCpuDecimalBugNMOS(!fantasyPreset);
    cpuDecimalBugEnabled = !fantasyPreset;
    // GEN2 HGR random power-on state. Same Fantasy-OFF rule as siliconStrictMode;
    // when the card plugs (deferred 15 frames below), Memory consults this flag
    // to decide between random / documented latch + DRAM + scanner phase.
    emulation->setGen2RandomPowerOn(!fantasyPreset);
    gen2RandomPowerOnEnabled = !fantasyPreset;
    // Keep the four individual Silicon Strict Inspector checkboxes in sync with
    // the master power-on flag (setGen2RandomPowerOn flips all four together).
    gen2RandomLatchEnabled        = !fantasyPreset;
    gen2RandomFloatingBusEnabled  = !fantasyPreset;
    gen2RandomScannerPhaseEnabled = !fantasyPreset;
    gen2RandomDramNoiseEnabled    = !fantasyPreset;

    // UI flags reflect the preset's target state immediately (the menu
    // checkmarks and toolbar chips are driven by these). The actual
    // peripheral plug is queued below via pendingXxxEnable and fires
    // kCardEnableDeferFrames frames later from render() once the CPU
    // has been executing for ~200 ms.
    aciEnabled               = cfg.aci;
    graphicsCardEnabled      = cfg.graphicsCard;
    emulation->setHgrFramebufferAttached(graphicsCardEnabled);
    showGraphicsCard         = false;
    microSDEnabled           = cfg.microSD;
    cffa1Enabled             = cfg.cffa1;
    sidEnabled               = cfg.sid;
    sidSpecialEditionEnabled = cfg.sidSpecialEdition;
    tms9918Enabled           = cfg.tms9918;
    showTMS9918              = false;
    a1ioRtcEnabled           = cfg.a1ioRtc;
    showA1IO_RTC             = false;
    wifiModemEnabled         = cfg.wifiModem;
    showWiFiModem            = false;
    jukeBoxEnabled           = cfg.jukeBox;
    jukeBoxJumper            = cfg.jukeBoxJumper;
    jukeBoxChipMode          = cfg.jukeBoxChipMode;
    showJukeBox              = false;
    codeTankEnabled          = cfg.codeTank;
    codeTankJumper           = cfg.codeTankJumper;
#if !POM1_IS_WASM
    terminalCardEnabled      = cfg.terminalCard;
    showTerminalCard         = false;
#endif
    pr40Enabled              = cfg.pr40Printer;
    showPR40                 = false;
    gt6144Enabled            = cfg.gt6144;
    showGT6144               = false;
    iecCardEnabled           = cfg.iecCard;
    showIECCard              = false;
    showCassetteDeck         = false;
    showWelcome              = false;

    // Layout-driven auto-show: every panel named in the preset's layout
    // table opens on load. This replaces the old per-flag "show on preset
    // apply" rules (hardcoded showCassetteDeck / showWelcome for the
    // default POM1 preset, and the cfg.pr40Printer / cfg.gt6144 shortcuts
    // used previously). Keeping the decision in the layout table means
    // every preset can independently declare which panels to open, and
    // POM1 Fantasy — whose layout lists "Apple 1 Screen" + "Welcome" +
    // "Apple-1 Cassette Deck" — still boots with the same three windows
    // as before.
    for (int i = 0; i < cfg.layoutCount; ++i) {
        const std::string_view n = cfg.layout[i].name;
        // Peripheral / auxiliary windows
        if      (n == "Uncle Bernie's GEN2 HGR Graphic Card") showGraphicsCard = true;
        else if (n == "P-LAB Graphic Card (TMS9918)")         showTMS9918      = true;
        else if (n == "P-LAB I/O Board & RTC")                showA1IO_RTC     = true;
        else if (n == "P-LAB Wi-Fi Modem")                    showWiFiModem    = true;
        else if (n == "P-LAB Juke-Box")                       showJukeBox      = true;
        else if (n == "P-LAB CodeTank Library" || n == "P-LAB CodeTank")
            showCodeTankLibrary = true;
#if !POM1_IS_WASM
        else if (n == "P-LAB Terminal Card")                  showTerminalCard = true;
#endif
        else if (n == "SWTPC PR-40 Printer")                  showPR40         = true;
        else if (n == "SWTPC GT-6144 Graphic Terminal")       showGT6144       = true;
        else if (n == "IEC Disk")                             showIECCard      = true;
        else if (n == "Apple-1 Cassette Deck")                showCassetteDeck = true;
        else if (n == "Welcome")                              showWelcome      = true;
        else if (n == "Memory Map Bar (Horizontal)")          showMemoryBarH   = true;
        else if (n == "Woz & Jobs (1976)")                    showWozJobsPhoto = true;
        else if (n == "Apple-1 Demo Session (1976)")          showWozJobsRectPhoto = true;
        else if (n == "P-LAB TMS9918 Card (Photo)")           showTmsBoardPhoto = true;
        else if (n == "GEN2 Video Workbench (Photo)")         showGen2WorkbenchPhoto = true;
        else if (n == "Apple-1 ASCII Keyboard")               showKeyboardPhoto = true;
        else if (n == "Steve Wozniak (Photo)")                showWozPhoto = true;
        // Tutorial windows — names MUST match the titles used in
        // renderTutorialXxxWindow() calls (MainWindow_Dialogs.cpp).
        else if (n == "Tutorial: Integer BASIC")              showTutorialIntegerBasic = true;
        else if (n == "Tutorial: Applesoft Lite")                  showTutorialApplesoft    = true;
        else if (n == "Tutorial: microSD")                    showTutorialMicroSD      = true;
        else if (n == "Tutorial: Cassette (ACI)")             showTutorialCassette     = true;
        else if (n == "Tutorial: Wi-Fi Modem BBS")            showTutorialModemBBS     = true;
        else if (n == "Tutorial: SWTPC GT-6144")              showTutorialGT6144       = true;
        else if (n == "Tutorial: IEC")                        showTutorialIECCard      = true;
        else if (n == "Tutorial: SWTPC PR-40 Printer")        showTutorialPR40         = true;
        else if (n == "Tutorial: P-LAB TMS9918")              showTutorialTMS9918      = true;
        else if (n == "Tutorial: P-LAB A1-IO & RTC")          showTutorialA1IORTC      = true;
        else if (n == "Tutorial: A1-SID / A1-AUDIO SE")       showTutorialSID          = true;
        else if (n == "Tutorial: Uncle Bernie's GEN2 HGR")    showTutorialGEN2HGR      = true;
        else if (n == "Tutorial: CFFA1 CompactFlash")         showTutorialCFFA1        = true;
        else if (n == "Tutorial: P-LAB Juke-Box")             showTutorialJukeBox      = true;
        else if (n == "Tutorial: P-LAB Terminal Card")        showTutorialTerminalCard = true;
        else if (n == "Tutorial: Krusader")                   showTutorialKrusader     = true;
        // "Apple 1 Screen" is always visible and has no show flag.
    }

    // Stash deferred plug intents. Every card that needs to be on for
    // this preset is queued here; the single pendingCardEnableFrames
    // counter below drives them all. Cassette audio source is always
    // re-plugged — CassetteDevice exists independently of the ACI and
    // produces the audible tape output through the mixer.
    pendingAciEnable            = cfg.aci;
    pendingMicroSDEnable        = cfg.microSD;
    pendingCffa1Enable          = cfg.cffa1;
    pendingSidEnable            = cfg.sid;
    pendingSidSEEnable          = cfg.sidSpecialEdition;
    pendingTms9918Enable        = cfg.tms9918;
    pendingA1ioRtcEnable        = cfg.a1ioRtc;
    pendingWifiModemEnable      = cfg.wifiModem;
    pendingJukeBoxEnable        = cfg.jukeBox;
    pendingJukeBoxJumper        = cfg.jukeBoxJumper;
    pendingJukeBoxChipMode      = cfg.jukeBoxChipMode;
    pendingCodeTankEnable       = cfg.codeTank;
    pendingCodeTankJumper       = cfg.codeTankJumper;
    pendingCodeTankRomPath      = (cfg.codeTankRomPath ? cfg.codeTankRomPath : std::string());
#if !POM1_IS_WASM
    pendingTerminalCardEnable   = cfg.terminalCard;
#else
    pendingTerminalCardEnable   = false;
#endif
    pendingPr40Enable           = cfg.pr40Printer;
    pendingGT6144Enable         = cfg.gt6144;
    pendingIECCardEnable        = cfg.iecCard;
    pendingCassetteAudioActive  = true;
    pendingPresetTapePath.clear();
    pendingPresetTapeForceProgramMode = false;
    pendingPresetTapeAutoPlay = false;
    if (cfg.basicType == BasicType::IntegerCassette) {
        pendingPresetTapePath = findFirstExistingPath({
            "cassettes/BASIC.aci", "../cassettes/BASIC.aci", "../../cassettes/BASIC.aci",
            "cassettes/BASIC.ogg", "../cassettes/BASIC.ogg", "../../cassettes/BASIC.ogg",
        });
        pendingPresetTapeForceProgramMode = true;
        if (pendingPresetTapePath.empty()) {
            pom1::log().warn("POM1",
                "Integer BASIC cassette asset not found (expected cassettes/BASIC.aci or BASIC.ogg)");
        }
    } else if (presetIndex == kMachinePresetCount - 1) {
        // POM1 Multiplexing Fantasy (2026) — shipped default; deck opens with
        // Woz's talk inserted (Play is user-driven). No other preset preloads it.
        pendingPresetTapePath = findFirstExistingPath({
            "cassettes/WOZ_talk.mp3", "../cassettes/WOZ_talk.mp3",
            "../../cassettes/WOZ_talk.mp3",
        });
        if (pendingPresetTapePath.empty()) {
            pom1::log().warn("POM1",
                "WOZ_talk.mp3 not found (expected cassettes/WOZ_talk.mp3)");
        }
    }
    pendingCardEnableFrames     = kCardEnableDeferFrames;

    // Load the appropriate BASIC ROM for this preset and track in loadedRoms
    {
        std::string error;
        bool ok = false;
        if (cfg.basicType == BasicType::ApplesoftLite) {
            // Pick the variant from the preset config, not from Memory's
            // current peripheral state. Card plugs are deferred by
            // kCardEnableDeferFrames, so at this point microSDEnabled /
            // cffa1Enabled are still false — calling the generic
            // reloadApplesoftLite() would dispatch to the CFFA1 variant
            // ($E000-$FFFF) and clobber the high RAM bank + Woz Monitor
            // when the preset wanted the microSD variant at $6000.
            if (cfg.microSD && !cfg.cffa1) {
                ok = emulation->reloadApplesoftLiteSDCard(error);
                if (ok) {
                    if (fantasyPreset) {
                        ok = emulation->reloadBasic(error);
                        if (ok) loadedRoms.push_back({"Integer BASIC", 0xE000, 0xEFFF});
                    } else {
                        emulation->unloadBasic();
                    }
                    loadedRoms.push_back({"Applesoft Lite (P-LAB microSD)", 0x6000, 0x7FFF});
                    if (ok && emulation->reloadWozMonitor(error))
                        loadedRoms.push_back({"Woz Monitor", 0xFF00, 0xFFFF});
                }
            } else {
                ok = emulation->reloadApplesoftLiteCFFA1(error);
                if (ok) {
                    loadedRoms.push_back({"Applesoft Lite (CFFA1)", 0xE000, 0xFFFF});
                }
            }
        } else if (cfg.basicType == BasicType::Integer) {
            ok = emulation->reloadBasic(error);
            if (ok) loadedRoms.push_back({"Integer BASIC", 0xE000, 0xEFFF});
            if (emulation->reloadWozMonitor(error))
                loadedRoms.push_back({"Woz Monitor", 0xFF00, 0xFFFF});
        } else if (cfg.basicType == BasicType::IntegerCassette) {
            // October 1976 hardware shipped BASIC on cassette, not preloaded
            // in RAM. Leave $E000-$EFFF as high-bank RAM; the preset inserts
            // the BASIC tape in the deck and the user loads it through ACI.
            emulation->unloadBasic();
            ok = emulation->reloadWozMonitor(error);
            if (ok)
                loadedRoms.push_back({"Woz Monitor", 0xFF00, 0xFFFF});
        } else {
            // BasicType::None — realistic presets leave $E000-$EFFF as RAM.
            emulation->unloadBasic();
            ok = emulation->reloadWozMonitor(error);
            if (ok) loadedRoms.push_back({"Woz Monitor", 0xFF00, 0xFFFF});
        }
        if (!ok) {
            setStatusMessage(error, 5.0f);
        }
    }

    // Krusader assembler at $A000-$BFFF (mutually exclusive with microSD/CFFA1)
    if (cfg.krusader) {
        std::string error;
        if (emulation->reloadKrusader(error) == true)
            loadedRoms.push_back({"Krusader", 0xA000, 0xBFFF});
    }

    // CFFA1 CompactFlash firmware at $9000-$AFFF
    if (cfg.cffa1) {
        std::string error;
        if (emulation->reloadCFFA1Rom(error))
            loadedRoms.push_back({"CFFA1 Firmware", 0x9000, 0xAFDF});
    }

    // Populate pending layout positions. These are `FirstUseEver` hints
    // applied in applyPendingLayout(); they only take effect for windows
    // that don't already have an entry in the loaded ini file below.
    pendingLayout.clear();
    for (int i = 0; i < cfg.layoutCount; i++) {
        const auto& p = cfg.layout[i];
        pendingLayout.push_back({p.name, p.pos, p.size});
    }

    if (cfg.jukeBox)
        evictMemoryMapRegionsForJukeBox();

    // Try loading the INCOMING preset's saved layout. When found, ImGui
    // merges the file's window states into the live context — windows that
    // were positioned by hand in a previous session snap back. When no file
    // exists (first time this preset is used), the pendingLayout defaults
    // above provide the initial arrangement and the GLFW OS window is
    // sized to contain it via computePresetLayoutExtent.
    const bool layoutLoaded = loadPresetLayout(presetIndex);
#if !POM1_IS_WASM
    if (!layoutLoaded && window) {
        // No saved size — derive a default OS-window bounding box from the
        // preset's layout table (factored so resetActivePresetLayout reuses it).
        int glfwW = 0, glfwH = 0;
        defaultOsWindowSize(presetIndex, glfwW, glfwH);
        glfwSetWindowSize(window, glfwW, glfwH);
    }
#else
    // WASM: resize the HTML canvas to the incoming profile — ONCE, here, on the
    // profile change (not every frame). We store the target in wasmCanvasPixelW/H;
    // the main loop (main_imgui.cpp) issues emscripten_set_canvas_element_size
    // only when the element size actually differs, so no profile change means no
    // resize. In browser fullscreen the main loop uses the CSS viewport instead
    // and reads this value again only when fullscreen is exited.
    (void)layoutLoaded;
    computeWasmCanvasSize(presetIndex, wasmCanvasPixelW, wasmCanvasPixelH);
#endif

    activePresetIndex = presetIndex;

    // Indices 0-2 are the DevBench profiles (CC65 / TMS9918 / GEN2 HGR). When
    // the user picks one from the Presets menu, open the POM1 Bench and load
    // the matching ASM starter sketch. DevBench preset N maps identically to
    // ASM target N in kP1Targets[] (Pom1BenchHost.cpp). If the editor is
    // dirty, keep the user's code and just log a notice — they can pick New
    // from the toolbar to force-reset. Skipped when applyMachineConfig is
    // being driven BY the Bench's own target picker (otherwise picking a C
    // target — which also maps to preset 0/1/2 — would wipe the C sketch back
    // to asm); see Pom1BenchHost::onTargetSelected.
    if (presetIndex >= 0 && presetIndex <= 2 && !suppressDevBenchAutoload) {
        ensureBench();
        showBench = true;
        if (!codeBench_->loadStarterForTargetIfClean(presetIndex)) {
            pom1::log().info("DevBench",
                             std::string("Preset switched to '") + cfg.name +
                             "' but the Bench sketch was modified - kept "
                             "user's code (use New in the Bench to reset).");
        }
    }

    setStatusMessage(std::string("Preset: ") + cfg.name, 3.0f);
}

// Boot profile chooser — a full-viewport preset selector shown before any other
// UI at startup (see the boot gate in render()). Picking a profile applies it
// via applyBootConfig() and dismisses the chooser. Preset indices come from the
// named kPreset* constants (MainWindow_Internal.h); POM1 Fantasy is always the
// last preset (kMachinePresetCount - 1).
void MainWindow_ImGui::renderProfileChooser()
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(vp->Size, ImGuiCond_Always);
    // Dark, focused backdrop so the selector reads as a boot screen.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(10, 12, 18, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::Begin("##ProfileChooser", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);

    const float winW = ImGui::GetWindowWidth();
    // Three side-by-side columns (Machines / Create / Development). Clamp to the
    // viewport so a small OS window at boot never pushes the columns off-screen.
    const float colW = std::min(1080.0f, winW - 40.0f);
    const float indent = (winW - colW) * 0.5f;

    // Anchor the column near the top of the viewport (small margin), rather
    // than vertically centered.
    ImGui::Dummy(ImVec2(0.0f, 24.0f));

    auto centeredScaledText = [&](const char* txt, float scale, ImU32 col) {
        ImGui::SetWindowFontScale(scale);
        const float tw = ImGui::CalcTextSize(txt).x;
        ImGui::SetCursorPosX((winW - tw) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(txt);
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.0f);
    };
    // A chooser button carries an action: apply a machine preset, or launch a
    // graphical language (a DevBench BASIC/LOGO target). -1 fields = "not this".
    int chosenPreset      = -1;
    int chosenLang        = -1;   // DevBench language axis (2=BASIC, 3=LOGO)
    int chosenMachine     = -1;   // DevBench machine axis (see kP1Machines[])
    int chosenPaint       = -1;   // 0 = HGR Painter, 1 = TMS9918 Painter
    int chosenAudio       = -1;   // 0 = Beeper SFX, 1 = SID Tracker
    int chosenGame        = -1;   // kChooserGames[] index (CodeTank cartridges)
    // A profile row: accent-coloured button (width w) + a dim wrapped description.
    // onClick writes the chosen action; the caller runs it after ImGui::End().
    auto actionButton = [&](const char* label, const char* desc,
                            ImU32 base, ImU32 hov, ImU32 act, float w, float h,
                            const std::function<void()>& onClick) {
        ImGui::PushStyleColor(ImGuiCol_Button, base);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, act);
        if (ImGui::Button(label, ImVec2(w, h))) onClick();
        ImGui::PopStyleColor(3);
        if (desc && *desc) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(133, 143, 163, 255));
            ImGui::TextWrapped("%s", desc);
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
        }
    };
    auto presetButton = [&](const char* label, const char* desc, int preset,
                            ImU32 base, ImU32 hov, ImU32 act, float w, float h) {
        actionButton(label, desc, base, hov, act, w, h,
                     [&, preset]() { chosenPreset = preset; });
    };
    auto langButton = [&](const char* label, const char* desc, int lang, int machine,
                          ImU32 base, ImU32 hov, ImU32 act, float w, float h) {
        actionButton(label, desc, base, hov, act, w, h,
                     [&, lang, machine]() { chosenLang = lang; chosenMachine = machine; });
    };
    auto columnHeader = [&](const char* txt) {
        ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 1.0f), "%s", txt);
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    };

    // Title — a red apple glyph next to the "POM1" wordmark, the whole row
    // centred. Drawn as two coloured segments at the same big font scale (a
    // single Text can't mix colours), positioned on one baseline.
    const float titleScale = 1.8f;
    ImGui::SetWindowFontScale(titleScale);
    const ImVec2 appleSz = ImGui::CalcTextSize(ICON_FA_APPLE_WHOLE);
    const float  titleGap = ImGui::CalcTextSize(" ").x;
    const ImVec2 wordSz = ImGui::CalcTextSize("POM1");
    ImGui::SetWindowFontScale(1.0f);
    const float rowW = appleSz.x + titleGap + wordSz.x;
    const float rowY = ImGui::GetCursorPosY();
    const float rowX = (winW - rowW) * 0.5f;
    ImGui::SetCursorPos(ImVec2(rowX, rowY));
    ImGui::SetWindowFontScale(titleScale);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(214, 44, 44, 255));   // red apple
    ImGui::TextUnformatted(ICON_FA_APPLE_WHOLE);
    ImGui::PopStyleColor();
    ImGui::SetCursorPos(ImVec2(rowX + appleSz.x + titleGap, rowY));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(236, 240, 248, 255));
    ImGui::TextUnformatted("POM1");
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);

    ImGui::Dummy(ImVec2(0.0f, 1.0f));
    centeredScaledText("Choose an Apple-1 profile", 1.0f, IM_COL32(150, 158, 172, 255));
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    // Top: the flagship "everything plugged" fantasy machine (always last
    // preset) — amber accent, flanked by the POM1 app icon on each side. The
    // row (icon · button · icon) spans colW; the button shrinks to leave room
    // for the icons so the whole thing stays centred. Its caption is drawn
    // centred BELOW the button so it reads as a headline for the selector.
    ensureAppIconTexture();
    const bool haveIcon = appIconTexture && appIconWidth > 0 && appIconHeight > 0;
    const float fIcon = 52.0f;
    const float fGap = 16.0f;
    const float fBtnH = 62.0f;
    const float fBtnW = colW - (haveIcon ? 2.0f * (fIcon + fGap) : 0.0f);
    const float fRowX = (winW - colW) * 0.5f;
    const float fRowY = ImGui::GetCursorPosY();
    auto drawFantasyIcon = [&](float x) {
        ImGui::SetCursorPos(ImVec2(x, fRowY + (fBtnH - fIcon) * 0.5f));
        ImGui::Image(pom1::renderer()->asImTextureID(appIconTexture), ImVec2(fIcon, fIcon));
    };
    if (haveIcon) drawFantasyIcon(fRowX);
    ImGui::SetCursorPos(ImVec2(fRowX + (haveIcon ? fIcon + fGap : 0.0f), fRowY));
    presetButton(ICON_FA_STAR "  POM1 FANTASY Apple-1  " ICON_FA_STAR,
                 /*desc*/ nullptr,
                 kMachinePresetCount - 1,
                 IM_COL32(150, 108, 30, 255), IM_COL32(190, 142, 46, 255),
                 IM_COL32(120, 86, 22, 255), fBtnW, fBtnH);
    if (haveIcon) drawFantasyIcon(fRowX + fIcon + fGap + fBtnW + fGap);
    ImGui::SetCursorPosY(fRowY + fBtnH);
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    centeredScaledText("Everything plugged in at once — sound, Wi-Fi, microSD and terminal. "
                       "The all-in-one machine to explore POM1.",
                       1.0f, IM_COL32(133, 143, 163, 255));
    ImGui::Dummy(ImVec2(0.0f, 14.0f));

    // Four columns, each answering "what do you want to do?":
    //   Play     (red)   — insert a CodeTank release cartridge and play (auto
    //                       plugs TMS9918 + CodeTank, jumper set, 4000R typed).
    //   Machines (teal)  — boot a faithful single-card Apple-1 and use it.
    //   Create   (violet)— write graphics programs in BASIC or LOGO (opens the
    //                       Bench with the interpreter live + a starter demo).
    //   Develop  (green) — hand-write 6502 assembly or C in the DevBench.
    // Machine-axis values in the Create column are the kP1Machine* constants
    // (Pom1BenchHost.h), pinned to kP1Machines[] by a static_assert.
    const ImU32 gB = IM_COL32(140, 45, 45, 255), gH = IM_COL32(180, 62, 62, 255),  gA = IM_COL32(110, 34, 34, 255);
    const ImU32 mB = IM_COL32(28, 68, 92, 255),  mH = IM_COL32(42, 96, 126, 255), mA = IM_COL32(20, 52, 74, 255);
    const ImU32 vB = IM_COL32(74, 46, 96, 255),  vH = IM_COL32(102, 64, 132, 255), vA = IM_COL32(56, 34, 74, 255);
    const ImU32 dB = IM_COL32(34, 74, 46, 255),  dH = IM_COL32(48, 100, 64, 255),  dA = IM_COL32(26, 56, 36, 255);
    if (indent > 0.0f) ImGui::SetCursorPosX(indent);
    if (ImGui::BeginTable("##profilecols", 4, ImGuiTableFlags_SizingStretchSame, ImVec2(colW, 0.0f))) {
        ImGui::TableNextRow();

        // Column 1 — the CodeTank release cartridges (red accent). One click =
        // cartridge in the slot, jumper set, machine reset, 4000R auto-typed.
        ImGui::TableNextColumn();
        columnHeader(ICON_FA_GAMEPAD "  Play");
        const float gw = ImGui::GetContentRegionAvail().x;
        auto gameButton = [&](const char* label, const char* desc, int game) {
            actionButton(label, desc, gB, gH, gA, gw, 46.0f,
                         [&, game]() { chosenGame = game; });
            ImGui::Dummy(ImVec2(0.0f, 8.0f));
        };
        gameButton("ARCADE — 3 games",
                   "Galaga, Sokoban and Snake from the ARCADE cartridge menu.", 0);
        gameButton("TETRIS",
                   "Nino Porcino's Tetris (CLASSICS cartridge).", 1);
        gameButton("CHESS",
                   "Graphical chess vs AI or 2-player (CLASSICS cartridge).", 2);
        gameButton("DEMOS — 6 demos",
                   "Life, Mandel, Plasma, Vague, Nyan and the sprite Animals.", 3);

        // Column 2 — faithful single-card machines (teal accent).
        ImGui::TableNextColumn();
        columnHeader(ICON_FA_MICROCHIP "  Machines");
        const float mw = ImGui::GetContentRegionAvail().x;
        presetButton("Apple-1 — Integer BASIC",
                     "The original 1976 board with cassette. Type Integer BASIC or load "
                     "programs from tape.",
                     kPresetIntegerCassette, mB, mH, mA, mw, 46.0f);
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        presetButton("GEN2 HGR — Color",
                     "Uncle Bernie's 280x192 color graphics card, 48 KB RAM. Run the HGR "
                     "demos and games.",
                     kPresetGen2Color, mB, mH, mA, mw, 46.0f);
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        presetButton("TMS9918 — CodeTank",
                     "P-LAB video chip with the CodeTank game cartridge — sprites, tiles "
                     "and three built-in games.",
                     kPresetTMS9918Card, mB, mH, mA, mw, 46.0f);

        // Column 3 — graphical languages (violet accent). Each boots the
        // interpreter live on its card and opens the Bench with a matching
        // demo. Compact form: one dim label per language + two half-width
        // card buttons (HGR / TMS9918); the long blurbs moved to tooltips.
        ImGui::TableNextColumn();
        columnHeader(ICON_FA_PALETTE "  Create");
        const float vw = ImGui::GetContentRegionAvail().x;
        auto langPair = [&](const char* name, const char* what, int lang,
                            int machineHgr, int machineTms,
                            const char* tipHgr, const char* tipTms) {
            ImGui::TextColored(ImVec4(0.72f, 0.66f, 0.82f, 1.0f), "%s", name);
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::TextColored(ImVec4(0.52f, 0.56f, 0.64f, 1.0f), "%s", what);
            const float half = (vw - 8.0f) * 0.5f;
            ImGui::PushID(name);
            langButton("HGR", nullptr, lang, machineHgr, vB, vH, vA, half, 38.0f);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tipHgr);
            ImGui::SameLine(0.0f, 8.0f);
            langButton("TMS9918", nullptr, lang, machineTms, vB, vH, vA, half, 38.0f);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tipTms);
            ImGui::PopID();
        };
        langPair("Applesoft", "graphics BASIC", /*BASIC*/ 2,
                 kP1MachineApplesoftGen2, kP1MachineApplesoftTms,
                 "Color-graphics BASIC (HGR, HPLOT, HCOLOR) on Uncle Bernie's GEN2 card.\n"
                 "Opens the editor with a demo ready to run.",
                 "The same graphics BASIC driving the P-LAB TMS9918 chip.\n"
                 "Opens the editor with a demo ready to run.");
        ImGui::Dummy(ImVec2(0.0f, 12.0f));
        langPair("LOGO", "turtle graphics", /*LOGO*/ 3,
                 kP1MachineLogoGen2, kP1MachineLogoTms,
                 "Turtle graphics (LOGO V2.6) on the GEN2 HGR card.\n"
                 "Draw with TO, REPEAT, FD and TR.",
                 "Turtle graphics (LOGO V2.6) on the P-LAB TMS9918 card.\n"
                 "Draw with TO, REPEAT, FD and TR.");

        // Column 4 — DevBench profiles (green accent).
        ImGui::TableNextColumn();
        columnHeader(ICON_FA_WRENCH "  Develop");
        const float dw = ImGui::GetContentRegionAvail().x;
        presetButton("Assembly / C — Text",
                     "DevBench for hand-written 6502 assembly or C on the plain text "
                     "Apple-1 (cc65 toolchain).",
                     kPresetCC65Bench, dB, dH, dA, dw, 46.0f);
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        presetButton("Assembly / C — HGR",
                     "DevBench targeting Uncle Bernie's GEN2 HGR color card (cc65 "
                     "assembly or C).",
                     kPresetGen2Bench, dB, dH, dA, dw, 46.0f);
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        presetButton("Assembly / C — TMS9918",
                     "DevBench targeting the P-LAB TMS9918 card via the CodeTank "
                     "cartridge (cc65 assembly or C).",
                     kPresetTMS9918Bench, dB, dH, dA, dw, 46.0f);

        ImGui::EndTable();
    }

    // Media tools: direct access to the Paint editors (HGR / TMS9918) and the
    // audio editors (Beeper SFX / SID Tracker) on ONE centred row. Each plugs its
    // matching card and opens the editor — mirroring the Tools-menu actions. Paint
    // buttons keep the amber accent, audio buttons the magenta one.
    ImGui::Dummy(ImVec2(0.0f, 18.0f));
    centeredScaledText(ICON_FA_PAINTBRUSH "  Studio — Paint & Audio editors", 1.0f, IM_COL32(140, 153, 179, 255));
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    {
        // Clamp to the viewport like the columns above — the chooser window has
        // NoScrollbar, so an unclamped 4-button row would clip past the right
        // edge (unreachable buttons) on OS windows narrower than ~1000 px.
        const float ebGap = 14.0f, ebH = 44.0f;
        const float ebW = std::min(240.0f, (std::min(1080.0f, winW - 40.0f) - 3.0f * ebGap) / 4.0f);
        const float rowW = 4.0f * ebW + 3.0f * ebGap;
        if (winW > rowW) ImGui::SetCursorPosX((winW - rowW) * 0.5f);
        const ImU32 pB = IM_COL32(150, 96, 34, 255), pH = IM_COL32(190, 128, 52, 255), pA = IM_COL32(120, 76, 26, 255);
        const ImU32 aB = IM_COL32(120, 60, 96, 255), aH = IM_COL32(152, 78, 124, 255), aA = IM_COL32(96, 48, 76, 255);
        ImGui::PushStyleColor(ImGuiCol_Button, pB);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pH);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, pA);
        if (ImGui::Button(ICON_FA_PAINTBRUSH "  HGR Painter", ImVec2(ebW, ebH))) chosenPaint = 0;
        ImGui::SameLine(0.0f, ebGap);
        if (ImGui::Button(ICON_FA_PAINTBRUSH "  TMS9918 Painter", ImVec2(ebW, ebH))) chosenPaint = 1;
        ImGui::PopStyleColor(3);
        ImGui::SameLine(0.0f, ebGap);
        ImGui::PushStyleColor(ImGuiCol_Button, aB);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, aH);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, aA);
        if (ImGui::Button(ICON_FA_VOLUME_HIGH "  Beeper SFX", ImVec2(ebW, ebH))) chosenAudio = 0;
        ImGui::SameLine(0.0f, ebGap);
        if (ImGui::Button(ICON_FA_MUSIC "  SID Tracker", ImVec2(ebW, ebH))) chosenAudio = 1;
        ImGui::PopStyleColor(3);
    }

    // Opt-in startup preference: when checked, the machine profile picked
    // below is written to ini/startup and the chooser is skipped on the next
    // launches (Settings > Show profile chooser at startup re-enables it).
    // Applies to the machine-profile buttons only — the activity launchers
    // (language / paint / audio / game) are one-off compositions.
    {
        const char* rememberLabel = "Always start with the profile I pick (skip this screen)";
        const float chkW = ImGui::CalcTextSize(rememberLabel).x
                         + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
        ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetWindowWidth() - chkW) * 0.5f));
        ImGui::Checkbox(rememberLabel, &chooserRememberChoice_);
    }

    // Footer anchored near the bottom of the viewport: a dim, centred nod to
    // Apple's 50th anniversary (1976-2026).
    const char* footer = "Celebrating 50 years of Apple  —  1976-2026";
    const float footY = ImGui::GetWindowHeight() - ImGui::GetTextLineHeight() - 20.0f;
    if (footY > ImGui::GetCursorPosY()) ImGui::SetCursorPosY(footY);
    centeredScaledText(footer, 1.0f, IM_COL32(120, 128, 144, 255));

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // Enter with nothing else picked = boot the flagship default (POM1 FANTASY),
    // exactly like clicking the big amber button — a one-keystroke "just start".
    if (chosenPreset < 0 && chosenLang < 0 && chosenPaint < 0 && chosenAudio < 0 &&
        chosenGame < 0 &&
        (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)))
        chosenPreset = kMachinePresetCount - 1;

    // Run the picked action after ImGui::End() (every path dismisses the chooser).
    if (chosenPreset >= 0) {
        if (chooserRememberChoice_)
            writeStartupPreset(chosenPreset);
        applyBootConfig(chosenPreset);
        showProfileChooser = false;
    } else if (chosenLang >= 0) {
        launchLanguageFromChooser(chosenLang, chosenMachine);
        showProfileChooser = false;
    } else if (chosenPaint >= 0) {
        launchPaintEditorFromChooser(/*tms=*/chosenPaint == 1);
        showProfileChooser = false;
    } else if (chosenAudio >= 0) {
        launchAudioEditorFromChooser(/*sid=*/chosenAudio == 1);
        showProfileChooser = false;
    } else if (chosenGame >= 0) {
        launchGameFromChooser(chosenGame);
        showProfileChooser = false;
    }
}

// Boot a CodeTank release cartridge straight from the chooser's Play column:
// apply the TMS9918 (CodeTank) preset, swap the queued cartridge + jumper for
// the picked game (the pendings ride the deferred plug rail, and setting them
// AFTER applyBootConfig also wins over a --codetank-rom/-jumper CLI override,
// like the audio branch does for its card), then auto-type 4000R once the
// cold boot settles — the same UX as the CodeTank Library window's Run.
void MainWindow_ImGui::launchGameFromChooser(int game)
{
    static const struct { const char* rom; CodeTank::Jumper jumper; } kChooserGames[] = {
        { "roms/codetank/Codetank_ARCADE.rom",   CodeTank::Jumper::Lower16 },  // Galaga/Sokoban/Snake menu
        { "roms/codetank/Codetank_CLASSICS.rom", CodeTank::Jumper::Lower16 },  // Tetris
        { "roms/codetank/Codetank_CLASSICS.rom", CodeTank::Jumper::Upper16 },  // Chess
        { "roms/codetank/Codetank_DEMOS.rom",    CodeTank::Jumper::Lower16 },  // demo menu
    };
    constexpr int kChooserGameCount =
        static_cast<int>(sizeof(kChooserGames) / sizeof(kChooserGames[0]));
    if (game < 0 || game >= kChooserGameCount) return;

    applyBootConfig(kPresetTMS9918Card);
    pendingCodeTankRomPath = kChooserGames[game].rom;
    pendingCodeTankJumper  = kChooserGames[game].jumper;
    codeTankJumper         = kChooserGames[game].jumper;
    // ~3 s: covers the deferred card plug (~0.2 s) + the power-on scenario,
    // mirroring the CodeTank Library's kCodeTankColdBootSeconds.
    codeTankPendingWozRunAt = ImGui::GetTime() + 3.0;
}

// Open a pixel-art editor straight from the chooser. Plug the matching graphics
// machine (its Paint editor needs a live card to draw into) and raise the editor
// window — mirroring the Tools-menu actions that plug the card on open.
void MainWindow_ImGui::launchPaintEditorFromChooser(bool tms)
{
    if (tms) {
        applyBootConfig(kPresetTMS9918Card);
        tms9918Enabled = true;
        pendingTms9918Enable = true;
        showTMS9918 = true;
        showTMSPaintEditor = true;
    } else {
        applyBootConfig(kPresetGen2Color);
        graphicsCardEnabled = true;
        showGraphicsCard = true;
        emulation->setHgrFramebufferAttached(true);
        showHGRPaintEditor = true;
    }
}

// Open an audio editor straight from the chooser. Boot a plain Apple-1 with the
// ACI (kPresetIntegerCassette) as the base machine, then raise the editor
// window. Card plugs ride the deferred rail (pending* flags, plugged by
// finalizePendingCardPlugs ~15 frames after the reset) like
// launchPaintEditorFromChooser — a same-frame plug is the documented
// silent-card-on-boot condition, and the deferred finalize would otherwise
// honour a CLI --enable sid-se/jukebox override queued by applyBootConfig and
// evict the SID the user just asked for.
void MainWindow_ImGui::launchAudioEditorFromChooser(bool sid)
{
    applyBootConfig(kPresetIntegerCassette);
    if (sid) {
        sidEnabled = true;
        pendingSidEnable = true;
        // A1-SID evicts its bus rivals (A1-AUDIO SE shares $C800-$CFFF, the
        // Juke-Box latch sits at $CA00) — clear their pendings too so the
        // finalize pass can't plug one and evict the SID right back.
        sidSpecialEditionEnabled = false;
        pendingSidSEEnable = false;
        jukeBoxEnabled = false;
        pendingJukeBoxEnable = false;
        showSidTracker = true;
    } else {
        // kPresetIntegerCassette carries the ACI (cfg.aci queues the deferred
        // plug), but a persistent CLI `--disable aci` override is re-applied
        // inside every applyBootConfig — re-assert the pendings after the call
        // (like the SID branch above) so the editor never falls back on its
        // render guard's same-frame emergency plug (silent-card-on-boot).
        aciEnabled = true;
        pendingAciEnable = true;
        showSfxEditor = true;
    }
}

// Boot straight into a graphical language environment from the chooser. These
// are DevBench "targets" (Applesoft-on-video or LOGO), not plain machine
// presets: selectTargetExplicit() switches to the target's machine preset
// (window layout + OS window size via applyMachineConfig) AND cold-starts the
// in-ROM interpreter to its prompt, then we drop the target's starter demo into
// the Bench editor so Run works immediately. Resolving the target through
// targetFor() (not a literal kP1Targets[] index) keeps this robust against a
// target-table reorder and against the WASM build's trimmed table.
void MainWindow_ImGui::launchLanguageFromChooser(int benchLang, int benchMachine)
{
    ensureBench();
    const int target = benchHost_->targetFor(benchLang, benchMachine);
    if (target < 0) {
        setStatusMessage("This graphical language target is unavailable", 3.0f);
        return;
    }
    showBench = true;
    codeBench_->prepareTargetWithStarter(target);
    applyBootCliOverrides();
}

// GUI-free preset application for --headless. Mirrors the machine-config
// essence of applyMachineConfig() — RAM size, strict modes, card plugs, BASIC
// ROM, Krusader — but without ImGui (no ini/layout/window) and without the
// 15-frame deferred plug (that only existed to dodge silent-card-on-boot for
// audio; headless has no audio device, so cards plug immediately). Cascades
// (CodeTank→TMS9918, IEC→microSD) and mutex evictions are handled inside the
// EmulationController setters; the preset config is already mutex-consistent.
void MainWindow_ImGui::applyHeadlessConfig(EmulationController& emu, int presetIndex)
{
    if (presetIndex < 0 || presetIndex >= kMachinePresetCount) return;
    const MachineConfig& cfg = kMachinePresets[presetIndex];
    pom1::log().info("POM1", std::string("headless preset: ") + cfg.name +
                     " (" + std::to_string(cfg.ramKB) + "K)");

    emu.setPresetRamKB(cfg.ramKB);
    const bool fantasy = std::string_view(cfg.name).find("Fantasy") != std::string_view::npos;
    emu.setSiliconStrictMode(!fantasy);
    emu.setOutOfRangeStrictMode(!fantasy);
    // Headless = tests / golden-image regression / CI: force the deterministic
    // GEN2 cold state (documented latch + zeroed DRAM + cycleCounter=0 + fixed
    // xorshift seed) regardless of the preset's Fantasy bit. The GUI path
    // mirrors !fantasyPreset because users want hardware fidelity there;
    // headless callers want reproducibility (--dump-gen2-frame /
    // gfx_regress_gen2_testcard depend on it).
    emu.setGen2RandomPowerOn(false);

    // Jumpers before the cards that read them.
    emu.setJukeBoxJumper(cfg.jukeBoxJumper);
    emu.setJukeBoxChipMode(cfg.jukeBoxChipMode);
    emu.setCodeTankJumper(cfg.codeTankJumper);

    // Plug cards (base cards before daughterboards).
    emu.setHgrFramebufferAttached(cfg.graphicsCard);
    emu.setTMS9918Enabled(cfg.tms9918);
    emu.setCodeTankEnabled(cfg.codeTank);
    emu.setSIDEnabled(cfg.sid);
    emu.setSIDSpecialEditionEnabled(cfg.sidSpecialEdition);
    emu.setMicroSDEnabled(cfg.microSD);
    emu.setIECCardEnabled(cfg.iecCard);
    emu.setCFFA1Enabled(cfg.cffa1);
    emu.setJukeBoxEnabled(cfg.jukeBox);
    emu.setWiFiModemEnabled(cfg.wifiModem);
    emu.setA1IO_RTCEnabled(cfg.a1ioRtc);
    emu.setPR40Enabled(cfg.pr40Printer);
    emu.setGT6144Enabled(cfg.gt6144);
    emu.setACIEnabled(cfg.aci);
#if !POM1_IS_WASM
    emu.setTerminalCardEnabled(cfg.terminalCard);
#endif

    // BASIC ROM — variant picked from cfg (not live flags), mirroring
    // applyMachineConfig. Errors are logged by the reload* facades.
    std::string err;
    if (cfg.basicType == BasicType::ApplesoftLite) {
        if (cfg.microSD && !cfg.cffa1) {
            emu.reloadApplesoftLiteSDCard(err);
            if (fantasy) emu.reloadBasic(err); else emu.unloadBasic();
            emu.reloadWozMonitor(err);
        } else {
            emu.reloadApplesoftLiteCFFA1(err);
        }
    } else if (cfg.basicType == BasicType::Integer) {
        emu.reloadBasic(err);
        emu.reloadWozMonitor(err);
    } else {
        // None / IntegerCassette: $E000-$EFFF stays RAM (cassette BASIC loads later).
        emu.unloadBasic();
        emu.reloadWozMonitor(err);
    }
    if (cfg.krusader) emu.reloadKrusader(err);
}

int MainWindow_ImGui::getPresetCount()
{
    return kMachinePresetCount;
}

const char* MainWindow_ImGui::getPresetName(int index)
{
    if (index < 0 || index >= kMachinePresetCount) return nullptr;
    return kMachinePresets[index].name;
}

// ---------------------------------------------------------------------------
// Per-preset layout persistence — each preset index has its own ini file in
// ini/imgui_preset_NN.ini (ImGui-managed window positions/sizes) plus a
// sidecar ini/preset_NN.size (text "W H" for the GLFW OS window). Swapping
// presets saves the outgoing profile's layout and loads the incoming one.
// The directory is created on demand the first time a save occurs.
// ---------------------------------------------------------------------------

namespace {

std::string iniPathForPreset(int idx)
{
    char buf[48];
    std::snprintf(buf, sizeof(buf), "ini/imgui_preset_%02d.ini", idx);
    return std::string(buf);
}

std::string sizePathForPreset(int idx)
{
    char buf[48];
    std::snprintf(buf, sizeof(buf), "ini/preset_%02d.size", idx);
    return std::string(buf);
}

std::string windowsPathForPreset(int idx)
{
    char buf[48];
    std::snprintf(buf, sizeof(buf), "ini/preset_%02d.windows", idx);
    return std::string(buf);
}

/** Shipped under repo `ini_defaults/` (tracked in git) — the curated factory
 *  layout baseline for every preset on a fresh, config-less install. Resolved
 *  cwd-relative first (dev: repo root or `build/`), then exe-relative so the
 *  packaged builds find it regardless of working directory:
 *    Windows   <exe>/ini_defaults/
 *    macOS     <exe>/../Resources/ini_defaults/    (Contents/MacOS → Resources)
 *    AppImage  <exe>/../share/POM1/ini_defaults/   (usr/bin → usr/share/POM1)
 *  Mirrors the cc65 bundle resolution. */
std::string findIniDefaultsFile(const char* basename)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const char* prefixes[] = {
        "ini_defaults/",
        "../ini_defaults/",
        "../../ini_defaults/",
        "../../../ini_defaults/",
    };
    for (const char* pre : prefixes) {
        const std::string p = std::string(pre) + basename;
        if (fs::is_regular_file(p, ec))
            return p;
    }
    const std::string exeDir = bench::executableDir();
    if (!exeDir.empty()) {
        const fs::path e(exeDir);
        const fs::path cands[] = {
            e / "ini_defaults" / basename,
            e.parent_path() / "Resources" / "ini_defaults" / basename,
            e.parent_path() / "share" / "POM1" / "ini_defaults" / basename,
        };
        for (const fs::path& c : cands)
            if (fs::is_regular_file(c, ec))
                return c.string();
    }
    return {};
}

bool copyIniDefaultsFileTo(const char* basename, const std::string& destPath)
{
    namespace fs = std::filesystem;
    const std::string src = findIniDefaultsFile(basename);
    if (src.empty()) return false;
    std::error_code ec;
    if (fs::path parent = fs::path(destPath).parent_path(); !parent.empty())
        fs::create_directories(parent, ec);
    fs::copy_file(src, destPath, fs::copy_options::none, ec);
    return !ec;
}

// The OS-window sidecar grew from "W H" to "W H X Y M F" (position +
// maximized + fullscreen) in juillet 2026. The extra fields are optional on
// read, so a legacy 2-field file still loads (position untouched, windowed).
struct OsWindowGeom {
    int  w = 0, h = 0;
    bool havePos = false;
    int  x = 0, y = 0;
    bool maximized = false;
    bool fullscreen = false;
};

bool loadSizeFile(int idx, OsWindowGeom& g)
{
    std::ifstream f(sizePathForPreset(idx));
    // Reject a corrupt sidecar: parse failure, non-positive, or absurd dims that
    // would be handed straight to glfwSetWindowSize. 16384 comfortably exceeds
    // any real display while ruling out garbage like "2000000000".
    if (!(f && (f >> g.w >> g.h)) ||
        g.w <= 0 || g.h <= 0 || g.w > 16384 || g.h > 16384)
        return false;
    int x = 0, y = 0, m = 0, fs = 0;
    if (f >> x >> y) {
        // Same sanity bound as the size, but positions may be negative
        // (left/upper monitor in a multi-head setup).
        if (x >= -16384 && x <= 16384 && y >= -16384 && y <= 16384) {
            g.havePos = true;
            g.x = x;
            g.y = y;
        }
        if (f >> m >> fs) {
            g.maximized  = (m != 0);
            g.fullscreen = (fs != 0);
        }
    }
    return true;
}

bool saveSizeFile(int idx, const OsWindowGeom& g)
{
    std::error_code ec;
    std::filesystem::create_directories("ini", ec);
    std::ofstream f(sizePathForPreset(idx));
    if (!f) return false;
    f << g.w << ' ' << g.h;
    // Factory defaults (pregenerate) carry no position — write the legacy
    // 2-field form so restoring them leaves the OS free to place the window.
    if (g.havePos) {
        f << ' ' << g.x << ' ' << g.y << ' '
          << (g.maximized ? 1 : 0) << ' ' << (g.fullscreen ? 1 : 0);
    }
    f << '\n';
    return bool(f);
}

#if !POM1_IS_WASM
// Clamp a restored window rect so its title bar stays reachable on SOME
// monitor. Guards against layouts saved on a since-unplugged second screen
// (the classic "my window restored off-screen" trap). If the rect overlaps
// no monitor's work area by at least kMinVisible px, snap it into the
// primary monitor's work area.
void clampToVisibleMonitor(GLFWwindow* /*window*/, OsWindowGeom& g)
{
    if (!g.havePos) return;
    constexpr int kMinVisible = 64;
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    for (int i = 0; i < count; ++i) {
        int mx = 0, my = 0, mw = 0, mh = 0;
        glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
        const int ovX = std::min(g.x + g.w, mx + mw) - std::max(g.x, mx);
        const int ovY = std::min(g.y + g.h, my + mh) - std::max(g.y, my);
        if (ovX >= kMinVisible && ovY >= kMinVisible)
            return;                          // visible enough — keep as saved
    }
    GLFWmonitor* primary = glfwGetPrimaryMonitor();
    if (!primary) { g.havePos = false; return; }
    int mx = 0, my = 0, mw = 0, mh = 0;
    glfwGetMonitorWorkarea(primary, &mx, &my, &mw, &mh);
    g.w = std::min(g.w, mw);
    g.h = std::min(g.h, mh);
    g.x = mx + std::max(0, (mw - g.w) / 2);
    g.y = my + std::max(0, (mh - g.h) / 2);
}
#endif

// WASM: ini/ is mounted on IDBFS (see shell.html preRun) — writes only reach
// IndexedDB after an FS.syncfs flush. Debounced JS-side so bursts of saves
// coalesce into one IndexedDB transaction.
void syncIniToIdbfs()
{
#if POM1_IS_WASM
    EM_ASM({
        if (typeof FS === 'undefined' || !FS.syncfs) return;
        if (Module.pom1IniSyncPending) return;
        Module.pom1IniSyncPending = true;
        setTimeout(function() {
            Module.pom1IniSyncPending = false;
            FS.syncfs(false, function(err) {
                if (err) console.warn('POM1 ini sync failed:', err);
            });
        }, 250);
    });
#endif
}

/** Flush Memory Map Bar geometry into ImGui .ini settings before SaveIniSettingsToDisk.
 *  1) Prefer the live ImGuiWindow when it exists (same frame ordering issues as before).
 *  2) Else use the last Pos/Size captured while the bar was visible — so shutdown or
 *     preset switch after closing the bar still persists position in imgui_preset_NN.ini. */
void syncMemoryBarIniToDisk(const char* windowName,
                            bool storedGeomValid,
                            const ImVec2& storedPos,
                            const ImVec2& storedSize)
{
    ImGuiContext& g = *GImGui;
    if (ImGuiWindow* window = ImGui::FindWindowByName(windowName)) {
        if (window->Flags & ImGuiWindowFlags_NoSavedSettings)
            return;
        ImGuiWindowSettings* settings = ImGui::FindWindowSettingsByWindow(window);
        if (!settings) {
            settings = ImGui::CreateNewWindowSettings(window->Name);
            window->SettingsOffset = g.SettingsWindows.offset_from_ptr(settings);
        }
        if (settings->ID != window->ID)
            return;
        settings->Pos = ImVec2ih(window->Pos);
        settings->Size = ImVec2ih(window->SizeFull);
        settings->IsChild = (window->Flags & ImGuiWindowFlags_ChildWindow) != 0;
        settings->Collapsed = window->Collapsed;
        settings->WantDelete = false;
        return;
    }
    if (!storedGeomValid || storedSize.x <= 0.0f || storedSize.y <= 0.0f)
        return;

    const ImGuiID id = ImHashStr(windowName);
    ImGuiWindowSettings* settings = ImGui::FindWindowSettingsByID(id);
    if (!settings)
        settings = ImGui::CreateNewWindowSettings(windowName);
    if (settings->ID != id)
        return;
    settings->Pos = ImVec2ih(storedPos);
    settings->Size = ImVec2ih(storedSize);
    settings->IsChild = false;
    settings->Collapsed = false;
    settings->WantDelete = false;
}

} // namespace

void MainWindow_ImGui::savePresetLayout(int idx)
{
    if (idx < 0 || idx >= kMachinePresetCount) return;
    std::error_code ec;
    std::filesystem::create_directories("ini", ec);
    if (ec) {
        pom1::log().warn("Layout",
            "create_directories(ini) failed: " + ec.message());
        return;
    }
    const std::string iniPath = iniPathForPreset(idx);
    syncMemoryBarIniToDisk("Memory Map Bar", memoryBarLastGeomValid,
                           memoryBarLastPos, memoryBarLastSize);
    syncMemoryBarIniToDisk("Memory Map Bar (Horizontal)", memoryBarHLastGeomValid,
                           memoryBarHLastPos, memoryBarHLastSize);
    ensureWindowSettingsHandler();  // so SaveIniSettingsToDisk emits [POM1Windows]
    ImGui::SaveIniSettingsToDisk(iniPath.c_str());  // geometry + window presence
    // Presence now lives in the .ini; drop any migrated legacy sidecar.
    std::filesystem::remove(windowsPathForPreset(idx), ec);
    pom1::log().debug("Layout",
        "Saved preset " + std::to_string(idx) + " → " + iniPath);
#if !POM1_IS_WASM
    if (window) {
        // Persist the WINDOWED rect (windowedPos/Width members, tracked every
        // frame while the window is neither maximized nor fullscreen — see
        // render()) plus the maximized/fullscreen flags, so a maximized or
        // fullscreen session restores with a sane underlying windowed rect.
        OsWindowGeom g;
        const bool maxed = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) != 0;
        if (!maxed && !fullscreen) {
            glfwGetWindowSize(window, &g.w, &g.h);
            glfwGetWindowPos(window, &g.x, &g.y);
        } else {
            g.w = windowedWidth;
            g.h = windowedHeight;
            g.x = windowedPosX;
            g.y = windowedPosY;
        }
        g.havePos    = true;
        g.maximized  = maxed;
        g.fullscreen = fullscreen;
        if (g.w > 0 && g.h > 0) saveSizeFile(idx, g);
    }
#endif
    syncIniToIdbfs();
}

bool MainWindow_ImGui::loadPresetLayout(int idx)
{
    if (idx < 0 || idx >= kMachinePresetCount) return false;
    const std::string path = iniPathForPreset(idx);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return false;
    ensureWindowSettingsHandler();   // register before parsing presence
    iniHadPresenceSection_ = false;
    ImGui::LoadIniSettingsFromDisk(path.c_str());  // geometry + [POM1Windows] presence
    // Migrate a pre-existing ini/preset_NN.windows sidecar (or an ini_defaults/
    // seed) only when the .ini itself carried no presence section. Overrides the
    // card-driven defaults applyMachineConfig set; savePresetLayout then folds it
    // into the .ini and removes the sidecar.
    if (!iniHadPresenceSection_)
        loadWindowFlags(idx);
#if !POM1_IS_WASM
    if (window) {
        OsWindowGeom g;
        if (loadSizeFile(idx, g)) {
            clampToVisibleMonitor(window, g);
            const bool isFullscreenNow = glfwGetWindowMonitor(window) != nullptr;
            if (g.fullscreen) {
                // Restore straight into fullscreen; keep the windowed rect in
                // the tracking members so leaving fullscreen lands right.
                windowedWidth  = g.w;
                windowedHeight = g.h;
                if (g.havePos) { windowedPosX = g.x; windowedPosY = g.y; }
                GLFWmonitor* monitor = glfwGetPrimaryMonitor();
                const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
                if (monitor && mode) {
                    glfwSetWindowMonitor(window, monitor, 0, 0,
                                         mode->width, mode->height, mode->refreshRate);
                    fullscreen = true;
                }
            } else {
                if (isFullscreenNow) {
                    // Previous preset left us fullscreen — drop back to windowed.
                    glfwSetWindowMonitor(window, nullptr,
                                         g.havePos ? g.x : windowedPosX,
                                         g.havePos ? g.y : windowedPosY,
                                         g.w, g.h, 0);
                    fullscreen = false;
                } else {
                    if (g.havePos) glfwSetWindowPos(window, g.x, g.y);
                    glfwSetWindowSize(window, g.w, g.h);
                }
                if (g.maximized)
                    glfwMaximizeWindow(window);
                else if (glfwGetWindowAttrib(window, GLFW_MAXIMIZED))
                    glfwRestoreWindow(window);
            }
        }
    }
#endif
    return true;
}

// ---------------------------------------------------------------------------
// Window registry — single source of truth for every dismissable window.
//
// Adding a window means adding ONE row here. Presence persistence (the
// [POM1Windows][Open] section the settings handler writes into each preset
// .ini) and persistentWindowFlags() are both derived from this table, so the
// open/closed state of tutorials, peripheral panels and info/photo windows is
// covered automatically — no hand-maintained allow-list to drift out of sync.
// Only the transient file/config dialogs opt out (persistPresence=false):
// re-opening a half-finished "Save As" on a preset switch would be a bug, so
// they are excluded by data, explicitly, rather than by silent omission.
//
// `key` is the stable persistence token (never rename it — it is what lands in
// the .ini); `title` documents the ImGui Begin() title ImGui already keys the
// window's geometry by. `kind` is informational/grouping only today.
// ---------------------------------------------------------------------------
const std::vector<MainWindow_ImGui::WindowDescriptor>&
MainWindow_ImGui::windowRegistry()
{
    using K  = WindowKind;
    using MW = MainWindow_ImGui;
    static const std::vector<WindowDescriptor> kReg = {
        // key                    title                                        show flag                    kind            persist
        // ── Tools ───────────────────────────────────────────────────────────────────────────────────────────────────────────
        { "Bench",                "POM1 Bench",                                &MW::showBench,              K::Tool,        true  },
        { "Telemetry",            "Telemetry Side Channel",                    &MW::showTelemetry,          K::Tool,        true  },
        { "TMS9918Inspector",     "TMS9918 VDP Inspector",                     &MW::showTMS9918Inspector,   K::Tool,        true  },
        { "SiliconStrict",        "Silicon Strict Inspector",                  &MW::showSiliconStrictWindow,K::Tool,        true  },
        { "MemoryViewer",         "Memory Viewer",                             &MW::showMemoryViewer,       K::Tool,        true  },
        { "Debugger",             "CPU Debug Console",                         &MW::showDebugger,           K::Tool,        true  },
        { "RewindTimeline",       "State Rewind",                              &MW::showRewindTimeline,     K::Tool,        true  },
        { "MemoryMapGrid",        "Memory Map Grid",                           &MW::showMemoryMapGrid,      K::Tool,        true  },
        { "MemoryBar",            "Memory Map Bar",                            &MW::showMemoryBar,          K::Tool,        true  },
        { "MemoryBarH",           "Memory Map Bar (Horizontal)",               &MW::showMemoryBarH,         K::Tool,        true  },
        // ── Peripheral / card panels ──────────────────────────────────────────────────────────────────────────────────────────
        { "CassetteDeck",         "Apple-1 Cassette Deck",                     &MW::showCassetteDeck,       K::Peripheral,  true  },
        { "GraphicsCard",         "Uncle Bernie's GEN2 HGR Graphic Card",      &MW::showGraphicsCard,       K::Peripheral,  true  },
        { "HGRPaintEditor",       "HGR Paint Editor",                          &MW::showHGRPaintEditor,     K::Tool,        true  },
        { "HGRSpriteEditor",      "HGR Sprite Editor",                         &MW::showHGRSpriteEditor,    K::Tool,        true  },
        { "TMSPaintEditor",       "TMS9918 Paint Editor",                      &MW::showTMSPaintEditor,     K::Tool,        true  },
        { "TMSSpriteEditor",      "TMS9918 Sprite Editor",                     &MW::showTMSSpriteEditor,    K::Tool,        true  },
        { "SfxEditor",            "Beeper SFX Editor",                         &MW::showSfxEditor,          K::Tool,        true  },
        { "SidTracker",           "SID Tracker",                               &MW::showSidTracker,         K::Tool,        true  },
        { "TMS9918",              "P-LAB Graphic Card (TMS9918)",              &MW::showTMS9918,            K::Peripheral,  true  },
        { "GT6144",               "SWTPC GT-6144 Graphic Terminal",            &MW::showGT6144,             K::Peripheral,  true  },
        { "IECCard",              "IEC Disk",                                  &MW::showIECCard,            K::Peripheral,  true  },
        { "WiFiModem",            "P-LAB Wi-Fi Modem",                         &MW::showWiFiModem,          K::Peripheral,  true  },
        { "TerminalCard",         "P-LAB Terminal Card",                       &MW::showTerminalCard,       K::Peripheral,  true  },
        { "PR40",                 "SWTPC PR-40 Printer",                       &MW::showPR40,               K::Peripheral,  true  },
        { "A1IO_RTC",             "P-LAB I/O Board & RTC",                     &MW::showA1IO_RTC,           K::Peripheral,  true  },
        { "JukeBox",              "P-LAB Juke-Box",                            &MW::showJukeBox,            K::Peripheral,  true  },
        { "CodeTankLibrary",      "P-LAB CodeTank Library",                    &MW::showCodeTankLibrary,    K::Peripheral,  true  },
        // ── Tutorials (Help > Tutorials) ──────────────────────────────────────────────────────────────────────────────────────
        { "TutorialIntegerBasic", "Tutorial: Integer BASIC",                   &MW::showTutorialIntegerBasic, K::Tutorial,  true },
        { "TutorialApplesoft",    "Tutorial: Applesoft Lite",                  &MW::showTutorialApplesoft,  K::Tutorial,    true  },
        { "TutorialMicroSD",      "Tutorial: microSD",                         &MW::showTutorialMicroSD,    K::Tutorial,    true  },
        { "TutorialCassette",     "Tutorial: Cassette (ACI)",                  &MW::showTutorialCassette,   K::Tutorial,    true  },
        { "TutorialModemBBS",     "Tutorial: Wi-Fi Modem BBS",                 &MW::showTutorialModemBBS,   K::Tutorial,    true  },
        { "TutorialGT6144",       "Tutorial: SWTPC GT-6144",                   &MW::showTutorialGT6144,     K::Tutorial,    true  },
        { "TutorialIECCard",      "Tutorial: IEC",                             &MW::showTutorialIECCard,    K::Tutorial,    true  },
        { "TutorialPR40",         "Tutorial: SWTPC PR-40 Printer",             &MW::showTutorialPR40,       K::Tutorial,    true  },
        { "TutorialTMS9918",      "Tutorial: P-LAB TMS9918",                   &MW::showTutorialTMS9918,    K::Tutorial,    true  },
        { "TutorialA1IORTC",      "Tutorial: P-LAB A1-IO & RTC",               &MW::showTutorialA1IORTC,    K::Tutorial,    true  },
        { "TutorialSID",          "Tutorial: A1-SID / A1-AUDIO SE",            &MW::showTutorialSID,        K::Tutorial,    true  },
        { "TutorialGEN2HGR",      "Tutorial: Uncle Bernie's GEN2 HGR",         &MW::showTutorialGEN2HGR,    K::Tutorial,    true  },
        { "TutorialCFFA1",        "Tutorial: CFFA1 CompactFlash",              &MW::showTutorialCFFA1,      K::Tutorial,    true  },
        { "TutorialJukeBox",      "Tutorial: P-LAB Juke-Box",                  &MW::showTutorialJukeBox,    K::Tutorial,    true  },
        { "TutorialTerminalCard", "Tutorial: P-LAB Terminal Card",             &MW::showTutorialTerminalCard, K::Tutorial,  true },
        { "TutorialKrusader",     "Tutorial: Krusader",                        &MW::showTutorialKrusader,   K::Tutorial,    true  },
        // ── Info / credits / photos ───────────────────────────────────────────────────────────────────────────────────────────
        { "About",                "About POM1",                                &MW::showAbout,              K::Info,        true  },
        { "SpecialThanks",        "Ports & acknowledgements",                  &MW::showSpecialThanks,      K::Info,        true  },
        { "HardwareReference",    "Hardware Reference",                        &MW::showHardwareReference,  K::Info,        true  },
        { "SoftwareReference",    "Software Reference",                        &MW::showSoftwareReference,  K::Info,        true  },
        { "ShortcutsHelp",        "Keyboard Shortcuts",                        &MW::showShortcutsHelp,      K::Info,        true  },
        { "Welcome",              "Welcome",                                   &MW::showWelcome,            K::Info,        true  },
        { "WozJobsPhoto",         "Woz & Jobs (1976)",                         &MW::showWozJobsPhoto,       K::Info,        true  },
        { "WozJobsRectPhoto",     "Apple-1 Demo Session (1976)",               &MW::showWozJobsRectPhoto,   K::Info,        true  },
        { "TmsBoardPhoto",        "P-LAB TMS9918 Card (Photo)",                &MW::showTmsBoardPhoto,      K::Info,        true  },
        { "Gen2WorkbenchPhoto",   "GEN2 Video Workbench (Photo)",              &MW::showGen2WorkbenchPhoto, K::Info,        true  },
        { "KeyboardPhoto",        "Apple-1 ASCII Keyboard",                    &MW::showKeyboardPhoto,      K::Peripheral,  true  },
        { "WozPhoto",             "Steve Wozniak (Photo)",                     &MW::showWozPhoto,           K::Info,        true  },
        { "CopsonApple1Photo",    "Apple-1 (Copson) Photo",                    &MW::showCopsonApple1Photo,  K::Info,        true  },
        { "HappyWozPhoto",        "Apple-1 Happy Woz (Photo)",                 &MW::showHappyWozPhoto,      K::Info,        true  },
        { "PlabTms9918Photo",     "P-LAB TMS9918 Board (Photo)",               &MW::showPlabTms9918Photo,   K::Info,        true  },
        // ── Transient dialogs — NOT persisted (would re-pop a file/config op) ─────────────────────────────────────────────────
        { "ScreenConfig",         "Display Settings",                          &MW::showScreenConfig,       K::Dialog,      false },
        { "CrtSettings",          "CRT Effects",                               &MW::showCrtSettings,        K::Dialog,      false },
        { "MemoryConfig",         "Memory Settings",                           &MW::showMemoryConfig,       K::Dialog,      false },
        { "LoadDialog",           "Load Program",                              &MW::showLoadDialog,         K::Dialog,      false },
        { "LoadTapeDialog",       "Load Tape",                                 &MW::showLoadTapeDialog,     K::Dialog,      false },
        { "SaveDialog",           "Save Memory",                               &MW::showSaveDialog,         K::Dialog,      false },
        { "SaveTapeDialog",       "Save Tape",                                 &MW::showSaveTapeDialog,     K::Dialog,      false },
        { "LoadSnapshotDialog",   "Load Snapshot",                             &MW::showLoadSnapshotDialog, K::Dialog,      false },
        { "SaveSnapshotDialog",   "Save Snapshot",                             &MW::showSaveSnapshotDialog, K::Dialog,      false },
        // Boot/relaunch profile selector — a full-viewport overlay, not a saved
        // window. Persisting it would re-open the chooser over the machine on
        // every load. Listed (persist=false) so the completeness audit accounts
        // for every show* flag rather than silently omitting this one.
        { "ProfileChooser",       "Profile Chooser",                           &MW::showProfileChooser,     K::Dialog,      false },
    };
    return kReg;
}

std::vector<std::pair<const char*, bool*>> MainWindow_ImGui::persistentWindowFlags()
{
    // Derived from windowRegistry(): every row whose presence is part of a
    // saved profile, as a {stable key → this instance's live show* flag} pair.
    std::vector<std::pair<const char*, bool*>> out;
    for (const auto& d : windowRegistry())
        if (d.persistPresence)
            out.push_back({ d.key, &(this->*d.show) });
    return out;
}

// ---------------------------------------------------------------------------
// Presence persistence — folded into the preset .ini via a custom ImGui
// settings handler. ImGui's own ini stores each window's position/size/
// collapsed state but NOT whether the app has it *open* (that lives in the
// show* bools). This handler writes a [POM1Windows][Open] section into the
// same file, so geometry + presence round-trip together through Load/Save-
// IniSettingsFromDisk. Replaces the former ini/preset_NN.windows sidecar.
// ---------------------------------------------------------------------------
void* MainWindow_ImGui::windowSettings_ReadOpen(ImGuiContext*, ImGuiSettingsHandler* h, const char* name)
{
    // Single entry: [POM1Windows][Open]. Returning non-null makes ImGui feed
    // each following line to ReadLine. Record that the .ini carried presence
    // so loadPresetLayout skips the legacy-sidecar migration.
    auto* self = static_cast<MainWindow_ImGui*>(h->UserData);
    if (self && std::strcmp(name, "Open") == 0) {
        self->iniHadPresenceSection_ = true;
        return self;
    }
    return nullptr;
}

void MainWindow_ImGui::windowSettings_ReadLine(ImGuiContext*, ImGuiSettingsHandler* h, void* entry, const char* line)
{
    auto* self = static_cast<MainWindow_ImGui*>(entry ? entry : h->UserData);
    if (!self) return;
    const char* eq = std::strchr(line, '=');
    if (!eq) return;
    const std::string key(line, static_cast<std::size_t>(eq - line));
    const char c = eq[1];
    const bool val = (c == '1' || c == 't' || c == 'T' || c == 'y' || c == 'Y');
    for (const auto& wf : self->persistentWindowFlags())
        if (key == wf.first) { *wf.second = val; break; }
}

void MainWindow_ImGui::windowSettings_WriteAll(ImGuiContext*, ImGuiSettingsHandler* h, ImGuiTextBuffer* out_buf)
{
    auto* self = static_cast<MainWindow_ImGui*>(h->UserData);
    if (!self) return;
    out_buf->appendf("[%s][Open]\n", h->TypeName);
    for (const auto& wf : self->persistentWindowFlags())
        out_buf->appendf("%s=%d\n", wf.first, *wf.second ? 1 : 0);
    out_buf->append("\n");
}

void MainWindow_ImGui::ensureWindowSettingsHandler()
{
    if (windowSettingsHandlerRegistered_) return;
    if (ImGui::GetCurrentContext() == nullptr) return;        // need a live context
    if (ImGui::FindSettingsHandler("POM1Windows") != nullptr) {
        windowSettingsHandlerRegistered_ = true;              // already added this session
        return;
    }
    ImGuiSettingsHandler ini_handler;                         // ctor zero-inits the optional fns
    ini_handler.TypeName   = "POM1Windows";
    ini_handler.TypeHash   = ImHashStr("POM1Windows");
    ini_handler.ReadOpenFn = windowSettings_ReadOpen;
    ini_handler.ReadLineFn = windowSettings_ReadLine;
    ini_handler.WriteAllFn = windowSettings_WriteAll;
    ini_handler.UserData   = this;
    ImGui::AddSettingsHandler(&ini_handler);                  // ImGui copies it by value
    windowSettingsHandlerRegistered_ = true;
}

bool MainWindow_ImGui::loadWindowFlags(int idx)
{
    if (idx < 0 || idx >= kMachinePresetCount) return false;
    std::ifstream f(windowsPathForPreset(idx));
    if (!f) return false;
    // key → "1"/"0", applied only to keys present in the file (a partial file
    // leaves the rest at the applyMachineConfig default).
    const auto flags = persistentWindowFlags();
    std::string line;
    while (std::getline(f, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const char c = (eq + 1 < line.size()) ? line[eq + 1] : '0';
        const bool val = (c == '1' || c == 't' || c == 'T' || c == 'y' || c == 'Y');
        for (const auto& wf : flags)
            if (key == wf.first) { *wf.second = val; break; }
    }
    return true;
}

// Settings → "Reset Window Layout": drop the active preset's saved ImGui ini +
// .size sidecar and the live ImGui window settings, re-seed the factory
// positions, force them onto the live windows for a couple of frames, and
// resize the OS window to the preset's default extent. (Placed after the
// iniPathForPreset/sizePathForPreset helpers so they are in scope.)
void MainWindow_ImGui::resetActivePresetLayout()
{
    const int idx = activePresetIndex;
    if (idx < 0 || idx >= kMachinePresetCount) return;
    const MachineConfig& cfg = kMachinePresets[idx];

    ImGui::ClearIniSettings();                 // wipe live window settings store
    std::error_code ec;
    std::filesystem::remove(iniPathForPreset(idx), ec);
    std::filesystem::remove(sizePathForPreset(idx), ec);
    std::filesystem::remove(windowsPathForPreset(idx), ec);  // drop saved open/closed set
    memoryBarLastGeomValid  = false;
    memoryBarHLastGeomValid = false;

    // Curated factory layout: when ini_defaults/ ships a snapshot for this
    // preset (DevBench 0-2, CodeTank 9), re-seed and load it so reset restores
    // the FULL reviewed arrangement — every window's geometry, the open/closed
    // set and the OS window size — rather than the sparse hard-coded
    // kMachinePresets[].layout. loadPresetLayout's LoadIniSettingsFromDisk
    // force-applies geometry to live windows and seeds late-materialised ones
    // at creation, exactly like a normal preset switch.
    char iniBase[48], sizeBase[48];
    std::snprintf(iniBase, sizeof(iniBase), "imgui_preset_%02d.ini", idx);
    std::snprintf(sizeBase, sizeof(sizeBase), "preset_%02d.size", idx);
    copyIniDefaultsFileTo(sizeBase, sizePathForPreset(idx));  // OS window size sidecar
    if (copyIniDefaultsFileTo(iniBase, iniPathForPreset(idx)) && loadPresetLayout(idx)) {
        pendingLayout.clear();
        layoutResetForceFrames = 0;             // ini already force-applied via ApplyAll
        setStatusMessage("Window layout reset to factory default", 2.5f);
        return;
    }

    // No curated seed — rebuild from the hard-coded layout table and force it
    // onto live windows for a couple of frames.
    pendingLayout.clear();
    for (int i = 0; i < cfg.layoutCount; ++i) {
        const auto& p = cfg.layout[i];
        pendingLayout.push_back({p.name, p.pos, p.size});
    }
    layoutResetForceFrames = 2;                 // apply with Always next frame(s)

#if !POM1_IS_WASM
    if (window) {
        int glfwW = 0, glfwH = 0;
        defaultOsWindowSize(idx, glfwW, glfwH);
        glfwSetWindowSize(window, glfwW, glfwH);
    }
#endif
    setStatusMessage("Window layout reset to preset default", 2.5f);
}

// Settings → "Reset ALL Presets' Layouts": delete every preset's saved ImGui
// ini + .size, re-seed fresh factory files for all of them, and reset the
// active preset's live windows now. Other presets snap to their defaults the
// next time they are loaded.
void MainWindow_ImGui::resetAllPresetLayouts()
{
    std::error_code ec;
    for (int i = 0; i < kMachinePresetCount; ++i) {
        std::filesystem::remove(iniPathForPreset(i), ec);
        std::filesystem::remove(sizePathForPreset(i), ec);
        std::filesystem::remove(windowsPathForPreset(i), ec);
    }
    resetActivePresetLayout();             // wipes live state + active files, resizes OS window
    pregenerateMissingPresetLayouts();     // re-write factory ini/.size for all presets
    setStatusMessage("All preset window layouts reset to factory default", 3.0f);
}

// ---------------------------------------------------------------------------
// pregenerateMissingPresetLayouts -- write out default ini/imgui_preset_NN.ini
// + ini/preset_NN.size for every preset that doesn't have one yet, using the
// hard-coded `kMachinePresets[i].layout` defaults (window name + pos + size).
// For any index, when `ini_defaults/imgui_preset_NN.ini` and
// `ini_defaults/preset_NN.size` ship under the tracked `ini_defaults/` dir,
// those curated files are copied instead — they are the canonical factory
// defaults (keep in sync with kMachinePresets[NN]). Shipped for all 13 presets
// (0-12): the curated layouts under ini_defaults/ are the default baseline for
// every profile on a fresh, config-less install.
//
// Called once at boot. Ensures the ini/ directory is fully populated even
// before the user visits each preset, so that:
//   1. Defaults are always discoverable and editable on disk.
//   2. The ini files act as a versioned snapshot of the canonical layouts.
//   3. Users can hand-edit any preset's layout without having to first launch
//      it (handy for kiosk / repo-shipping setups).
//
// Does NOT overwrite existing ini files — user customisations are preserved.
// ---------------------------------------------------------------------------
void MainWindow_ImGui::pregenerateMissingPresetLayouts()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories("ini", ec);
    if (ec) return;

    int generated = 0;
    for (int idx = 0; idx < kMachinePresetCount; ++idx) {
        // Seed the open/closed-window sidecar from ini_defaults/ when a curated
        // one ships there — lets a saved arrangement become the default baseline
        // on a fresh (config-less) install. Never overwrites a user's own.
        const std::string winPath = windowsPathForPreset(idx);
        if (!fs::exists(winPath, ec)) {
            char winBase[48];
            std::snprintf(winBase, sizeof(winBase), "preset_%02d.windows", idx);
            copyIniDefaultsFileTo(winBase, winPath);
        }

        const std::string iniPath = iniPathForPreset(idx);
        const std::string sizePath = sizePathForPreset(idx);
        const bool iniExists  = fs::exists(iniPath, ec);
        const bool sizeExists = fs::exists(sizePath, ec);
        if (iniExists && sizeExists) continue;

        const MachineConfig& cfg = kMachinePresets[idx];

        // Write the .ini file with one [Window][...] section per layout entry.
        if (!iniExists) {
            char iniBase[48];
            std::snprintf(iniBase, sizeof(iniBase), "imgui_preset_%02d.ini", idx);
            bool seededFromDefaults = copyIniDefaultsFileTo(iniBase, iniPath);
            if (!seededFromDefaults) {
                std::ofstream f(iniPath);
                if (!f) continue;
                for (int i = 0; i < cfg.layoutCount; ++i) {
                    const MachineWindowPlacement& w = cfg.layout[i];
                    f << "[Window][" << w.name << "]\n";
                    f << "Pos=" << static_cast<int>(w.pos.x) << ','
                                << static_cast<int>(w.pos.y) << '\n';
                    if (w.size.x > 0.0f && w.size.y > 0.0f) {
                        f << "Size=" << static_cast<int>(w.size.x) << ','
                                      << static_cast<int>(w.size.y) << '\n';
                    }
                    f << '\n';
                }
            }
        }

        // Write the .size file with the bounding box of the layout, plus a
        // small floor matching the canonical Fantasy preset (last entry) so
        // that no preset starts smaller than the reference frame.
        if (!sizeExists) {
            char sizeBase[48];
            std::snprintf(sizeBase, sizeof(sizeBase), "preset_%02d.size", idx);
            bool seededFromDefaults = copyIniDefaultsFileTo(sizeBase, sizePath);
            if (!seededFromDefaults) {
                // Need a fallback Apple-1 screen size — use the spec's size if
                // present, else a reasonable 843x701 baseline.
                ImVec2 fallback(843.0f, 701.0f);
                for (int i = 0; i < cfg.layoutCount; ++i) {
                    if (std::string(cfg.layout[i].name) == "Apple 1 Screen"
                        && cfg.layout[i].size.x > 0.0f) {
                        fallback = cfg.layout[i].size;
                        break;
                    }
                }
                ImVec2 extent = pom1::mainwindow::detail::computePresetLayoutExtent(cfg, fallback);
                int w = static_cast<int>(std::ceil(extent.x + 10.0f));
                int h = static_cast<int>(std::ceil(extent.y + 60.0f));
                // Floor at Fantasy preset extent (last entry — reference frame)
                const MachineConfig& fantasy = kMachinePresets[kMachinePresetCount - 1];
                ImVec2 fext = pom1::mainwindow::detail::computePresetLayoutExtent(fantasy, fallback);
                if (fext.x > 0.0f) w = std::max(w, static_cast<int>(std::ceil(fext.x + 10.0f)));
                if (fext.y > 0.0f) h = std::max(h, static_cast<int>(std::ceil(fext.y + 60.0f)));
                OsWindowGeom g;
                g.w = w;
                g.h = h;   // no position: factory default leaves placement to the OS
                saveSizeFile(idx, g);
            }
        }
        ++generated;
    }
    if (generated > 0) {
        pom1::log().info("Layout",
            "Pre-generated " + std::to_string(generated) +
            " preset layout file(s) under ini/");
    }
}

// ---------------------------------------------------------------------------
// Debounced layout autosave. ImGui raises io.WantSaveIniSettings a few seconds
// after any window move/resize/collapse when io.IniFilename == nullptr; window
// presence (the show* flags) never touches ImGui's settings, so it is tracked
// by hashing the persistent-flag bitset. Either signal arms a short debounce,
// then the active preset's whole profile is saved. This is what makes a crash
// (desktop) or a tab close (WASM — the shutdown save never runs there) lose at
// most a few seconds of arrangement instead of the whole session.
// ---------------------------------------------------------------------------
void MainWindow_ImGui::maybeAutosaveLayout(float deltaTime)
{
    if (activePresetIndex < 0 || showProfileChooser) return;
    // Don't autosave while the deferred card plug (preset switch) is still in
    // flight — the incoming preset's windows are still being raised.
    if (pendingCardEnableFrames > 0) {
        presenceHashValid_ = false;
        layoutDirtyForSeconds_ = -1.0f;
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    bool dirty = false;
    if (io.WantSaveIniSettings) {
        io.WantSaveIniSettings = false;   // consumed — we own persistence
        dirty = true;
    }

    // FNV-1a over the presence bitset — presence changes don't mark ImGui's
    // settings dirty, so they need their own change detection.
    uint64_t h = 1469598103934665603ull;
    for (const auto& wf : persistentWindowFlags())
        h = (h ^ (*wf.second ? 0x9Eu : 0x3Bu)) * 1099511628211ull;
    if (!presenceHashValid_) {
        lastPresenceHash_ = h;
        presenceHashValid_ = true;
    } else if (h != lastPresenceHash_) {
        lastPresenceHash_ = h;
        dirty = true;
    }

    constexpr float kAutosaveDebounceSeconds = 2.0f;
    if (dirty)
        layoutDirtyForSeconds_ = 0.0f;
    else if (layoutDirtyForSeconds_ >= 0.0f)
        layoutDirtyForSeconds_ += deltaTime;

    if (layoutDirtyForSeconds_ >= kAutosaveDebounceSeconds) {
        layoutDirtyForSeconds_ = -1.0f;
        savePresetLayout(activePresetIndex);
    }
}

void MainWindow_ImGui::saveActivePresetLayoutNow()
{
    if (activePresetIndex >= 0)
        savePresetLayout(activePresetIndex);
    saveUiSettings();
    layoutDirtyForSeconds_ = -1.0f;
}

// ---------------------------------------------------------------------------
// Global UI settings — ini/ui.settings. Session-wide (NOT per preset): theme,
// HiDPI mode + manual scale. Tiny key=value file, rewritten on every change.
// On WASM it lives under the IDBFS ini/ mount like the preset layouts.
// ---------------------------------------------------------------------------
void MainWindow_ImGui::applyUiTheme(int theme)
{
    uiTheme_ = theme < 0 ? 0 : (theme > 2 ? 2 : theme);
    ImGuiStyle& style = ImGui::GetStyle();
    switch (uiTheme_) {
    case 1:
        ImGui::StyleColorsLight();
        style.FrameBorderSize = 0.0f;
        break;
    case 2: {
        // High contrast: dark base pushed to pure black/white with visible
        // borders — for low-vision use, not aesthetics.
        ImGui::StyleColorsDark();
        ImVec4* c = style.Colors;
        c[ImGuiCol_Text]           = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        c[ImGuiCol_TextDisabled]   = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
        c[ImGuiCol_WindowBg]       = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
        c[ImGuiCol_PopupBg]        = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
        c[ImGuiCol_Border]         = ImVec4(1.00f, 1.00f, 1.00f, 0.80f);
        c[ImGuiCol_FrameBg]        = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
        c[ImGuiCol_FrameBgActive]  = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
        c[ImGuiCol_TitleBg]        = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
        c[ImGuiCol_TitleBgActive]  = ImVec4(0.15f, 0.15f, 0.60f, 1.00f);
        c[ImGuiCol_MenuBarBg]      = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
        c[ImGuiCol_CheckMark]      = ImVec4(1.00f, 1.00f, 0.00f, 1.00f);
        c[ImGuiCol_Button]         = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
        c[ImGuiCol_ButtonHovered]  = ImVec4(0.90f, 0.90f, 0.00f, 1.00f);
        c[ImGuiCol_ButtonActive]   = ImVec4(1.00f, 1.00f, 0.30f, 1.00f);
        c[ImGuiCol_NavCursor]      = ImVec4(1.00f, 1.00f, 0.00f, 1.00f);
        style.FrameBorderSize = 1.0f;
        break;
    }
    default:
        ImGui::StyleColorsDark();
        style.FrameBorderSize = 0.0f;
        break;
    }
}

void MainWindow_ImGui::loadUiSettings()
{
    std::ifstream f("ini/ui.settings");
    if (!f) { applyUiTheme(uiTheme_); return; }
    std::string line;
    int   theme = uiTheme_;
    int   hidpiAuto = uiHiDpiAuto_ ? 1 : 0;
    float hidpiScale = uiHiDpiManualScale_;
    while (std::getline(f, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        pom1::CrtParams& c = crtEffects.params;
        try {
            if      (key == "theme")       theme      = std::stoi(val);
            else if (key == "hidpi_auto")  hidpiAuto  = std::stoi(val);
            else if (key == "hidpi_scale") hidpiScale = std::stof(val);
            else if (key == "crt_enabled")     crtEffects.enabled = (std::stoi(val) != 0);
            else if (key == "crt_brightness")  c.brightness        = std::stof(val);
            else if (key == "crt_contrast")    c.contrast          = std::stof(val);
            else if (key == "crt_saturation")  c.saturation        = std::stof(val);
            else if (key == "crt_hue")         c.hue               = std::stof(val);
            else if (key == "crt_sharpness")   c.sharpness         = std::stof(val);
            else if (key == "crt_persistence") c.persistence       = std::stof(val);
            else if (key == "crt_scanlines")   c.scanlines         = std::stof(val);
            else if (key == "crt_barrel")      c.barrel            = std::stof(val);
            else if (key == "crt_shadowmask") {
                int m = std::stoi(val);
                if (m < 0) m = 0; else if (m > 3) m = 3;
                c.shadowMask = static_cast<pom1::CrtParams::ShadowMask>(m);
            }
            else if (key == "crt_maskstrength") c.shadowMaskStrength = std::stof(val);
            else if (key == "crt_lumgain")      c.luminanceGain      = std::stof(val);
            else if (key == "crt_centerlight")  c.centerLighting     = std::stof(val);
            else if (key == "crt_gamma")        c.phosphorGamma      = std::stof(val);
        } catch (...) { /* ignore malformed line */ }
    }
    applyUiTheme(theme);
    uiHiDpiAuto_ = (hidpiAuto != 0);
    if (hidpiScale >= 0.75f && hidpiScale <= 3.0f)
        uiHiDpiManualScale_ = hidpiScale;
    if (!uiHiDpiAuto_) {
        ImGui::GetIO().FontGlobalScale = uiHiDpiManualScale_;
        uiHiDpiInit_ = true;   // don't let the dialog re-latch over the file
    }
}

void MainWindow_ImGui::saveUiSettings()
{
    std::error_code ec;
    std::filesystem::create_directories("ini", ec);
    std::ofstream f("ini/ui.settings");
    if (!f) return;
    f << "theme=" << uiTheme_ << '\n'
      << "hidpi_auto=" << (uiHiDpiAuto_ ? 1 : 0) << '\n'
      << "hidpi_scale=" << uiHiDpiManualScale_ << '\n';
    // Universal CRT effect stack — master toggle + shader knob set.
    const pom1::CrtParams& c = crtEffects.params;
    f << "crt_enabled="     << (crtEffects.enabled ? 1 : 0) << '\n'
      << "crt_brightness="  << c.brightness         << '\n'
      << "crt_contrast="    << c.contrast           << '\n'
      << "crt_saturation="  << c.saturation         << '\n'
      << "crt_hue="         << c.hue                << '\n'
      << "crt_sharpness="   << c.sharpness          << '\n'
      << "crt_persistence=" << c.persistence        << '\n'
      << "crt_scanlines="   << c.scanlines          << '\n'
      << "crt_barrel="      << c.barrel             << '\n'
      << "crt_shadowmask="  << static_cast<int>(c.shadowMask) << '\n'
      << "crt_maskstrength="<< c.shadowMaskStrength << '\n'
      << "crt_lumgain="     << c.luminanceGain      << '\n'
      << "crt_centerlight=" << c.centerLighting     << '\n'
      << "crt_gamma="       << c.phosphorGamma      << '\n';
    f.close();
    syncIniToIdbfs();
}

// ---------------------------------------------------------------------------
// Startup profile preference — ini/startup. Written by the Profile Chooser's
// "always start with this profile" checkbox (or the Settings menu toggle);
// read by render()'s boot gate. CLI --preset always wins over it.
// ---------------------------------------------------------------------------
bool MainWindow_ImGui::readStartupPreset(int& presetIndex)
{
    std::ifstream f("ini/startup");
    if (!f) return false;
    std::string line;
    int autoStart = 0, preset = -1;
    while (std::getline(f, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        try {
            if      (key == "auto")   autoStart = std::stoi(val);
            else if (key == "preset") preset    = std::stoi(val);
        } catch (...) { /* ignore malformed line */ }
    }
    if (autoStart != 1 || preset < 0 || preset >= kMachinePresetCount)
        return false;
    presetIndex = preset;
    return true;
}

void MainWindow_ImGui::writeStartupPreset(int presetIndex)
{
    std::error_code ec;
    if (presetIndex < 0) {
        std::filesystem::remove("ini/startup", ec);
    } else {
        std::filesystem::create_directories("ini", ec);
        std::ofstream f("ini/startup");
        if (f) f << "auto=1\npreset=" << presetIndex << '\n';
    }
    syncIniToIdbfs();
}

// ---------------------------------------------------------------------------
// UI keyboard-navigation mode (F10). While ON, ImGui owns the keyboard
// (Tab / arrows / Space / Enter navigate the UI) and the Apple-1 receives
// nothing; F10 toggles back. The status bar shows "UI NAV" while active.
// ---------------------------------------------------------------------------
void MainWindow_ImGui::setUiNavMode(bool on)
{
    uiNavMode_ = on;
    ImGuiIO& io = ImGui::GetIO();
    if (on)
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    else
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    setStatusMessage(on
        ? "UI keyboard navigation ON (F10 to return keys to the Apple-1)"
        : "Keyboard back to the Apple-1", 3.0f);
}
