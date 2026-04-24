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

#include "imgui.h"
#include "imgui_internal.h"

#if !POM1_IS_WASM
#include <GLFW/glfw3.h>
#endif

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
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
// Layout design notes:
//   - Every preset uses the same canonical POM1 Fantasy frame:
//         Apple 1 Screen at (10, 61) size (843, 701)  ← LEFT column
//         Right column: x=858, width=338, y range 61..764
//         Resulting GLFW window: 1206 × 807 (matches preset 14).
//   - Right column is split top/bottom for every preset. The TOP slot
//     (y=61, height≈223) carries the tutorial most relevant to the
//     preset; the BOTTOM slot (y=288, height≈476) carries the
//     peripheral's own visualisation panel (or a second useful window
//     if the card has no dedicated panel: CFFA1, microSD, SID).
//   - Preset 14 (POM1 Multiplexing Fantasy) is the shipped "default"
//     preset — its layout MUST stay byte-identical to the shipped
//     screenshot reference (the README mentions it). Don't touch.
//     Every other preset mirrors its geometry. First-time use writes
//     ini/imgui_preset_14.ini + ini/preset_14.size; subsequent launches
//     load from there.
//   - Preset 12 (P-LAB Multiplexing Fantasy) is the only exception
//     that departs from the tutorial+peripheral template: it's the
//     "everything plugged" fantasy and stacks 3 peripherals in the
//     right column instead of a tutorial.
const MachineConfig kMachinePresets[] = {
    //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI  RAM  BASIC              SID-SE
    {
        "Bare Apple-1 (July 1976)",
        "Pre-ACI original: 6502, 4 KB RAM, PIA 6821, WOZ Monitor.",
        false, false, false, false, false, false, false,
        /*pr40*/ false,
        false, false, false, 4, BasicType::None,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*gt6144*/ false,
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
        "Apple-1 with ACI & Integer BASIC (October 1976)",
        "Original bare board with the ACI cassette expansion card: 6502, 8 KB RAM, PIA 6821, Integer BASIC, WOZ Monitor.",
        false, false, false, false, false, false, false,
        /*pr40*/ false,
        false, false, true, 8, BasicType::Integer,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*gt6144*/ false,
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
        "footprint (8 KB RAM + ACI + Integer BASIC), which is the machine Woz used "
        "when demonstrating the GT-6144 in Interface Age. Power-on framebuffer state "
        "is visible SRAM bistable noise; programs clear it before drawing. See the "
        "Hardware Reference window and the top comment of GT6144.h for the 4-phase "
        "command protocol.",
        false, false, false, false, false, false, false,
        /*pr40*/ false,
        false, false, /*aci*/ true, /*ramKB*/ 8, BasicType::Integer,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*gt6144*/ true,
        {
            {"Apple 1 Screen",                 {10,  61},  {843, 701}},
            // 4:3 content lives inside whatever size we give the window;
            // GL_NEAREST stretches the 64x96 texture horizontally 2x.
            {"SWTPC GT-6144 Graphic Terminal", {856, 58},  {338, 277}},
            {"Tutorial: SWTPC GT-6144",        {856, 337}, {339, 425}},
        }, 3
    },
    {
        "Replica-1 with ACI, Krusader & Integer BASIC (Briel 2003)",
        "Vince Briel's modern recreation: Integer BASIC, Krusader assembler, ACI cassette.",
        false, false, false, false, false, false, false,
        /*pr40*/ false,
        true, false, true, 32, BasicType::Integer,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*gt6144*/ false,
        {
            {"Apple 1 Screen",        {10,  61},  {843, 701}},
            {"Tutorial: Krusader",    {858, 61},  {338, 223}},
            {"Apple-1 Cassette Deck", {858, 288}, {338, 476}},
        }, 3
    },
    {
        "Replica-1 with CFFA1 & Applesoft Lite (Dreher 2007)",
        "Replica 1 with CFFA1 CompactFlash storage, Applesoft Lite.",
        false, false, false, false, false, false, false,
        /*pr40*/ false,
        false, true, false, 32, BasicType::ApplesoftLite,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*gt6144*/ false,
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
        "P-LAB microSD Storage Card, Applesoft Lite.",
        false, true, false, false, false, false, false,
        /*pr40*/ false,
        false, false, false, 32, BasicType::ApplesoftLite,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*gt6144*/ false,
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
        "P-LAB Apple-1 with A1-SID Sound Card ($C800-$CFFF)",
        "P-LAB A1-SID Sound Card (MOS 6581/8580), Integer BASIC. Registers at $C800-$CFFF.",
        false, false, true, false, false, false, false,
        /*pr40*/ false,
        false, false, false, 32, BasicType::Integer,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*gt6144*/ false,
        {
            // A1-SID has no dedicated window (audio-only). Tutorial fills
            // the right column full height so there's room for the full
            // register-poke example.
            {"Apple 1 Screen",                  {10,  61}, {843, 701}},
            {"Tutorial: A1-SID / A1-AUDIO SE", {858, 61}, {338, 703}},
        }, 2
    },
    {   //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI
        "P-LAB Apple-1 with A1-AUDIO Special Edition ($CC00-$CC1F)",
        "Claudio Parmigiani's A1-AUDIO Special Edition (10 units): MOS 6581/8580 at $CC00-$CC1F. "
        "Mutually exclusive with TMS9918 (same $CC00/$CC01 window).",
        false, false, false, false, false, false, false,
        /*pr40*/ false,
        false, false, false, 32, BasicType::Integer,
        /*sidSE*/ true,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*gt6144*/ false,
        {
            {"Apple 1 Screen",                 {10,  61}, {843, 701}},
            {"Tutorial: A1-SID / A1-AUDIO SE", {858, 61}, {338, 703}},
        }, 2
    },
    {   //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI
        "P-LAB Apple-1 with TMS9918 Graphic Card",
        "P-LAB Graphic Card (TMS9918A VDP), Integer BASIC.",
        false, false, false, true, false, false, false,
        /*pr40*/ false,
        false, false, false, 32, BasicType::Integer,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*gt6144*/ false,
        {
            {"Apple 1 Screen",               {10,  61},  {843, 701}},
            {"P-LAB Graphic Card (TMS9918)", {857, 61},  {344, 286}},
            {"Tutorial: P-LAB TMS9918",      {859, 353}, {340, 408}},
        }, 3
    },
    {   //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI
        "P-LAB Apple-1 with I/O Board & RTC",
        "P-LAB A1-IO Board & RTC (DS3231, DS18B20, analog/digital I/O), Integer BASIC.",
        false, false, false, false, true, false, false,
        /*pr40*/ false,
        false, false, false, 32, BasicType::Integer,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*gt6144*/ false,
        {
            {"Apple 1 Screen",              {10,  61},  {843, 701}},
            {"P-LAB I/O Board & RTC",       {858, 61},  {340, 268}},
            {"Tutorial: P-LAB A1-IO & RTC", {859, 332}, {340, 431}},
        }, 3
    },
    {   //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI
        "P-LAB Apple-1 with Wi-Fi Modem BBS",
        "P-LAB MODEM BBS (65C51 ACIA, ESP8266 AT, TCP/TELNET), Integer BASIC.",
        false, false, false, false, false, true, false,
        /*pr40*/ false,
        false, false, false, 32, BasicType::Integer,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*gt6144*/ false,
        {
            {"Apple 1 Screen",            {10,  61},  {843, 701}},
            {"P-LAB Wi-Fi Modem",         {858, 61},  {339, 238}},
            {"Tutorial: Wi-Fi Modem BBS", {858, 303}, {344, 459}},
        }, 3
    },
    {   //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI
        "P-LAB Apple-1 with Juke-Box (16 kB RAM)",
        "Minimal Juke-Box configuration: 32 kB EEPROM ROM library at $4000-$BFFF, "
        "16 kB contiguous RAM, no ACI cassette. The Juke-Box's EEPROM replaces "
        "tape loading entirely. Type BD00R from the Woz Monitor to launch the "
        "Program Manager (& prompt). Apple Integer BASIC is pre-loaded at "
        "$E000 so L<letter> on a BASIC program followed by B works directly; "
        "the LA command still reloads BASIC from the EEPROM if needed.",
        false, false, false, false, false, false, false,
        /*pr40*/ false,
        false, false, /*aci*/ false, /*ramKB*/ 16, BasicType::Integer,
        /*sidSE*/ false,
        /*jukeBox*/ true, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*gt6144*/ false,
        {
            {"Apple 1 Screen",           {10,  61},  {843, 701}},
            {"P-LAB Juke-Box",           {857, 61},  {344, 321}},
            {"Tutorial: P-LAB Juke-Box", {857, 386}, {345, 378}},
        }, 3
    },
    {   //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI
        "P-LAB Apple-1 Multiplexing Fantasy",
        "Emulator-only fantasy: plugs microSD, A1-SID, TMS9918, I/O & RTC, Wi-Fi modem, "
        "and Terminal Card all at once. Violates Claudio Parmigiani's golden rule "
        "\"one board at a time\" - impossible on real Apple-1 silicon (the 6502 bus has "
        "no arbitration, and several of these cards share overlapping address windows). "
        "Provided for convenience only.",
        false, true, true, true, true, true, true,
        /*pr40*/ false,
        false, false, false, 64, BasicType::ApplesoftLite,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*gt6144*/ false,
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
        "Uncle Bernie's GEN2 280x192 HGR color graphics, Integer BASIC.",
        true, false, false, false, false, false, false,
        /*pr40*/ false,
        false, false, false, 32, BasicType::Integer,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*gt6144*/ false,
        {
            {"Apple 1 Screen",                       {10,  61},  {843, 701}},
            {"Uncle Bernie's GEN2 HGR Graphic Card", {858, 60},  {338, 264}},
            {"Tutorial: Uncle Bernie's GEN2 HGR",    {858, 329}, {339, 432}},
        }, 3
    },
    {
        "POM1 Apple-1 Multiplexing Fantasy (2026)",
        "Emulator-only fantasy (violates Parmigiani's golden rule \"one board at a time\"): "
        "64 KB RAM, Applesoft Lite, microSD + A1-SID + Wi-Fi modem + Terminal Card. "
        "Graphic cards and the PR-40 printer off by default — plug them from the toolbar. "
        "ACI unplugged so the cassette deck acts as a plain audio player. Boots with the "
        "Cassette Deck + Welcome panels already open to the right of the Apple 1 screen; "
        "your layout customisations persist under ini/imgui_preset_14.ini "
        "(plus ini/preset_14.size for the OS window frame).",
        false, true, true, false, false, true, true,
        /*pr40*/ false,
        false, false, false, 64, BasicType::ApplesoftLite,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32, JukeBox::ChipMode::Flash,
        /*gt6144*/ false,
        {
            // Positions / sizes match the shipped POM1 Fantasy screenshot
            // so the first launch (no saved ini/imgui_preset_14.ini yet)
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
    for (auto it = pendingLayout.begin(); it != pendingLayout.end(); ++it) {
        if (it->name == windowName) {
            ImGui::SetNextWindowPos(it->pos, ImGuiCond_FirstUseEver);
            if (it->size.x > 0.0f)
                ImGui::SetNextWindowSize(it->size, ImGuiCond_FirstUseEver);
            pendingLayout.erase(it);
            return;
        }
    }
}

void MainWindow_ImGui::applyMachineConfig(int presetIndex)
{
    if (presetIndex < 0 || presetIndex >= kMachinePresetCount) return;
    const MachineConfig& cfg = kMachinePresets[presetIndex];

    // Save the OUTGOING preset's layout (ImGui window positions + GLFW
    // window size) before we swap anything. Skipped on boot (activePreset
    // is -1) and on self-reapply (same index — nothing to migrate).
    if (activePresetIndex >= 0 && activePresetIndex != presetIndex) {
        savePresetLayout(activePresetIndex);
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
    showScreenConfig         = false;
    showMemoryConfig         = false;
    showLoadDialog           = false;
    showLoadTapeDialog       = false;
    showCassetteControl      = false;
    showCassetteDeck         = false;
    showMemoryMapGrid        = false;
    showMemoryBar            = false;
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
    emulation->setMicroSDEnabled(false);
    emulation->setCFFA1Enabled(false);
    emulation->setTMS9918Enabled(false);
    emulation->setA1IO_RTCEnabled(false);
    emulation->setWiFiModemEnabled(false);
    emulation->setJukeBoxEnabled(false);
    emulation->setPR40Enabled(false);
    emulation->setGT6144Enabled(false);
#if !POM1_IS_WASM
    emulation->setTerminalCardEnabled(false);
#endif

    // Skip the full hard reset on the very first invocation — at that
    // point Memory::Memory() has just run initMemory() (default ROMs +
    // peripheral resets) so hardReset() would redo the exact same work
    // (double BASIC/WOZ/ACI/SD loads, multiple TerminalCard re-listens).
    // From the second call onward (preset switch, user action) the
    // hardReset is mandatory: we need to wipe RAM, reload default ROMs,
    // and reset every peripheral before applying the new preset.
    if (presetAppliedOnce) {
        emulation->hardReset();
    } else {
        presetAppliedOnce = true;
    }
    loadedPrograms.clear();
    loadedRoms.clear();

    // RAM size for this preset. The address space stays 64 KB; the value is
    // forwarded to Memory so out-of-range accesses past ramKB*1024 can be
    // flagged in the status bar without blocking the program.
    presetRamKB = cfg.ramKB;
    emulation->setPresetRamKB(cfg.ramKB);

    // UI flags reflect the preset's target state immediately (the menu
    // checkmarks and toolbar chips are driven by these). The actual
    // peripheral plug is queued below via pendingXxxEnable and fires
    // kCardEnableDeferFrames frames later from render() once the CPU
    // has been executing for ~200 ms.
    aciEnabled               = cfg.aci;
    graphicsCardEnabled      = cfg.graphicsCard;
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
#if !POM1_IS_WASM
    terminalCardEnabled      = cfg.terminalCard;
    showTerminalCard         = false;
#endif
    pr40Enabled              = cfg.pr40Printer;
    showPR40                 = false;
    gt6144Enabled            = cfg.gt6144;
    showGT6144               = false;
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
#if !POM1_IS_WASM
        else if (n == "P-LAB Terminal Card")                  showTerminalCard = true;
#endif
        else if (n == "SWTPC PR-40 Printer")                  showPR40         = true;
        else if (n == "SWTPC GT-6144 Graphic Terminal")       showGT6144       = true;
        else if (n == "Apple-1 Cassette Deck")                showCassetteDeck = true;
        else if (n == "Welcome")                              showWelcome      = true;
        else if (n == "Woz & Jobs (1976)")                    showWozJobsPhoto = true;
        else if (n == "Apple-1 Demo Session (1976)")          showWozJobsRectPhoto = true;
        else if (n == "P-LAB TMS9918 Card (Photo)")           showTmsBoardPhoto = true;
        // Tutorial windows — names MUST match the titles used in
        // renderTutorialXxxWindow() calls (MainWindow_Dialogs.cpp).
        else if (n == "Tutorial: Integer BASIC")              showTutorialIntegerBasic = true;
        else if (n == "Tutorial: Applesoft Lite")                  showTutorialApplesoft    = true;
        else if (n == "Tutorial: microSD")                    showTutorialMicroSD      = true;
        else if (n == "Tutorial: Cassette (ACI)")             showTutorialCassette     = true;
        else if (n == "Tutorial: Wi-Fi Modem BBS")            showTutorialModemBBS     = true;
        else if (n == "Tutorial: SWTPC GT-6144")              showTutorialGT6144       = true;
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
#if !POM1_IS_WASM
    pendingTerminalCardEnable   = cfg.terminalCard;
#else
    pendingTerminalCardEnable   = false;
#endif
    pendingPr40Enable           = cfg.pr40Printer;
    pendingGT6144Enable         = cfg.gt6144;
    pendingCassetteAudioActive  = true;
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
            // ($E000-$FFFF) and clobber Integer BASIC + Woz Monitor even
            // when the preset wanted the microSD variant at $6000.
            if (cfg.microSD && !cfg.cffa1) {
                ok = emulation->reloadApplesoftLiteSDCard(error);
                if (ok) {
                    loadedRoms.push_back({"Integer BASIC", 0xE000, 0xEFFF});
                    loadedRoms.push_back({"Applesoft Lite (P-LAB microSD)", 0x6000, 0x7FFF});
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
        } else {
            // BasicType::None — pre-October-1976 bare Apple-1 has only the Woz Monitor.
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
        // preset's layout table. Needs a fallback extent for the Apple 1
        // screen itself (whose default size depends on font metrics).
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
        // Every preset boots with at least POM1 Fantasy's canonical frame
        // (1206 x 807 = Apple 1 Screen 843x701 + right column 338 wide +
        // chrome), so switching between presets never shrinks the OS
        // window. Presets whose layout naturally demands more keep the
        // extent-driven size above. The minimum is computed from the
        // last preset in the array (POM1 Fantasy) so the canonical
        // reference stays tied to the preset table.
        const ImVec2 fantasyExtent = computePresetLayoutExtent(
            kMachinePresets[kMachinePresetCount - 1], ImVec2(sw, sh));
        if (fantasyExtent.x > 0.0f && fantasyExtent.y > 0.0f) {
            glfwW = std::max(glfwW, static_cast<int>(std::ceil(fantasyExtent.x + rightPad)));
            glfwH = std::max(glfwH, static_cast<int>(std::ceil(fantasyExtent.y + bottomPad)));
        }
        glfwSetWindowSize(window, glfwW, glfwH);
    }
#endif

    activePresetIndex = presetIndex;
    setStatusMessage(std::string("Preset: ") + cfg.name, 3.0f);
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

bool loadSizeFile(int idx, int& w, int& h)
{
    std::ifstream f(sizePathForPreset(idx));
    return bool(f && (f >> w >> h)) && w > 0 && h > 0;
}

bool saveSizeFile(int idx, int w, int h)
{
    std::error_code ec;
    std::filesystem::create_directories("ini", ec);
    std::ofstream f(sizePathForPreset(idx));
    if (!f) return false;
    f << w << ' ' << h << '\n';
    return bool(f);
}

} // namespace

void MainWindow_ImGui::savePresetLayout(int idx) const
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
    ImGui::SaveIniSettingsToDisk(iniPath.c_str());
    pom1::log().debug("Layout",
        "Saved preset " + std::to_string(idx) + " → " + iniPath);
#if !POM1_IS_WASM
    if (window) {
        int w = 0, h = 0;
        glfwGetWindowSize(window, &w, &h);
        if (w > 0 && h > 0) saveSizeFile(idx, w, h);
    }
#endif
}

bool MainWindow_ImGui::loadPresetLayout(int idx)
{
    if (idx < 0 || idx >= kMachinePresetCount) return false;
    const std::string path = iniPathForPreset(idx);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return false;
    ImGui::LoadIniSettingsFromDisk(path.c_str());
#if !POM1_IS_WASM
    if (window) {
        int w = 0, h = 0;
        if (loadSizeFile(idx, w, h)) {
            glfwSetWindowSize(window, w, h);
        }
    }
#endif
    return true;
}
