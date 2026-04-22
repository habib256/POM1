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

#include "imgui.h"

#include <algorithm>
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
const MachineConfig kMachinePresets[] = {
    //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI  RAM  BASIC              SID-SE
    {
        "Bare Apple-1 (July 1976)",
        "Pre-ACI original: 6502, 4 KB RAM, PIA 6821, WOZ Monitor.",
        false, false, false, false, false, false, false,
        /*pr40*/ false,
        false, false, false, 4, BasicType::None,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32,
        /*gt6144*/ false,
        { {"Apple 1 Screen", {10,61}, {0,0}} }, 1
    },
    {
        "Apple-1 with ACI & Integer BASIC (October 1976)",
        "Original bare board with the ACI cassette expansion card: 6502, 8 KB RAM, PIA 6821, Integer BASIC, WOZ Monitor.",
        false, false, false, false, false, false, false,
        /*pr40*/ false,
        false, false, true, 8, BasicType::Integer,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32,
        /*gt6144*/ false,
        { {"Apple 1 Screen", {10,61}, {0,0}} }, 1
    },
    {
        "Apple-1 + SWTPC GT-6144 Graphic Terminal (1976)",
        "First commercial Apple-1 graphics card: Southwest Technical Products' GT-6144 "
        "(1976, $98.50). Standalone 64x96 monochrome framebuffer on 6x Intel 2102 SRAM "
        "chips, write-only I/O port at $D00A. Boots with Integer BASIC + ACI cassette. "
        "Power-on state is visible SRAM bistable noise; programs clear the framebuffer "
        "before drawing. See the Hardware Reference window and the top comment of "
        "GT6144.h for the 4-phase command protocol.",
        false, false, false, false, false, false, false,
        /*pr40*/ false,
        false, false, /*aci*/ true, /*ramKB*/ 32, BasicType::Integer,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32,
        /*gt6144*/ true,
        {
            {"Apple 1 Screen",                    {10,  61}, {0,   0}},
            // 656 x 516 = 4:3 raster (640x480) + ImGui chrome, matches the
            // stock TV / composite monitor the 64x96 matrix was designed for.
            {"SWTPC GT-6144 Graphic Terminal",    {640, 61}, {656, 516}},
        }, 2
    },
    {
        "Replica-1 with ACI, Krusader & Integer BASIC (Briel 2003)",
        "Vince Briel's modern recreation: Integer BASIC, Krusader assembler, ACI cassette.",
        false, false, false, false, false, false, false,
        /*pr40*/ false,
        true, false, true, 32, BasicType::Integer,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32,
        /*gt6144*/ false,
        { {"Apple 1 Screen", {10,61}, {0,0}} }, 1
    },
    {
        "Replica-1 with CFFA1 & Applesoft Lite (Dreher 2007)",
        "Replica 1 with CFFA1 CompactFlash storage, Applesoft Lite.",
        false, false, false, false, false, false, false,
        /*pr40*/ false,
        false, true, false, 32, BasicType::ApplesoftLite,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32,
        /*gt6144*/ false,
        { {"Apple 1 Screen", {10,61}, {0,0}} }, 1
    },
    {
        "P-LAB Apple-1 with microSD & Applesoft Lite (April 2022)",
        "P-LAB microSD Storage Card, Applesoft Lite.",
        false, true, false, false, false, false, false,
        /*pr40*/ false,
        false, false, false, 32, BasicType::ApplesoftLite,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32,
        /*gt6144*/ false,
        {
            {"Apple 1 Screen", {10, 61}, {0, 0}},
        }, 1
    },
    {   //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI
        "P-LAB Apple-1 with A1-SID Sound Card ($C800-$CFFF)",
        "P-LAB A1-SID Sound Card (MOS 6581/8580), Integer BASIC. Registers at $C800-$CFFF.",
        false, false, true, false, false, false, false,
        /*pr40*/ false,
        false, false, false, 32, BasicType::Integer,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32,
        /*gt6144*/ false,
        {
            {"Apple 1 Screen", {10, 61}, {0, 0}},
        }, 1
    },
    {   //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI
        "P-LAB Apple-1 with A1-AUDIO Special Edition ($CC00-$CC1F)",
        "Claudio Parmigiani's A1-AUDIO Special Edition (10 units): MOS 6581/8580 at $CC00-$CC1F. "
        "Mutually exclusive with TMS9918 (same $CC00/$CC01 window).",
        false, false, false, false, false, false, false,
        /*pr40*/ false,
        false, false, false, 32, BasicType::Integer,
        /*sidSE*/ true,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32,
        /*gt6144*/ false,
        {
            {"Apple 1 Screen", {10, 61}, {0, 0}},
        }, 1
    },
    {   //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI
        "P-LAB Apple-1 with TMS9918 Graphic Card",
        "P-LAB Graphic Card (TMS9918A VDP), Integer BASIC.",
        false, false, false, true, false, false, false,
        /*pr40*/ false,
        false, false, false, 32, BasicType::Integer,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32,
        /*gt6144*/ false,
        {
            {"Apple 1 Screen",               {10,  61}, {0,   0}},
            {"P-LAB Graphic Card (TMS9918)", {640, 61}, {784, 612}},
        }, 2
    },
    {   //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI
        "P-LAB Apple-1 with I/O Board & RTC",
        "P-LAB A1-IO Board & RTC (DS3231, DS18B20, analog/digital I/O), Integer BASIC.",
        false, false, false, false, true, false, false,
        /*pr40*/ false,
        false, false, false, 32, BasicType::Integer,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32,
        /*gt6144*/ false,
        {
            {"Apple 1 Screen",        {10,  61},  {0,   0}},
            {"P-LAB I/O Board & RTC", {640, 61},  {380, 280}},
        }, 2
    },
    {   //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI
        "P-LAB Apple-1 with Wi-Fi Modem BBS",
        "P-LAB MODEM BBS (65C51 ACIA, ESP8266 AT, TCP/TELNET), Integer BASIC.",
        false, false, false, false, false, true, false,
        /*pr40*/ false,
        false, false, false, 32, BasicType::Integer,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32,
        /*gt6144*/ false,
        {
            {"Apple 1 Screen",    {10,  61},  {0,   0}},
            {"P-LAB Wi-Fi Modem", {640, 61},  {340, 260}},
        }, 2
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
        /*jukeBox*/ true, JukeBox::Jumper::RAM16_ROM32,
        /*gt6144*/ false,
        {
            {"Apple 1 Screen",    {10,  61}, {0,   0}},
            {"P-LAB Juke-Box",    {640, 61}, {360, 260}},
        }, 2
    },
    {   //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI
        "P-LAB Apple-1 Multiplexing Fantasy",
        "Emulator-only fantasy: plugs microSD, A1-SID, TMS9918, I/O & RTC, Wi-Fi modem, "
        "and Terminal Card all at once. Violates Claudio Parmigiani's golden rule "
        "\"one board at a time\" - impossible on real Apple-1 silicon (the 6502 bus has "
        "no arbitration, and several of these cards share overlapping address windows). "
        "Provided for convenience only.",
        false, true, true, true, true, true, true,
        /*pr40*/ true,
        false, false, false, 64, BasicType::ApplesoftLite,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32,
        /*gt6144*/ false,
        {
            {"Apple 1 Screen",               {10,  61},  {0,   0}},
            {"P-LAB Graphic Card (TMS9918)", {640, 61},  {784, 612}},
            {"P-LAB Wi-Fi Modem",            {640, 495}, {340, 260}},
            {"P-LAB Terminal Card",          {10,  510}, {360, 280}},
            {"P-LAB I/O Board & RTC",        {740, 495}, {380, 280}},
        }, 5
    },
    {
        "Uncle Bernie's Apple-1 with GEN2 HGR Color (April 2026)",
        "Uncle Bernie's GEN2 280x192 HGR color graphics, Integer BASIC.",
        true, false, false, false, false, false, false,
        /*pr40*/ false,
        false, false, false, 32, BasicType::Integer,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32,
        /*gt6144*/ false,
        {
            {"Apple 1 Screen",                 {10,  61}, {0,   0}},
            {"Uncle Bernie's GEN2 HGR Graphic Card", {624, 61}, {576, 420}},
        }, 2
    },
    {
        "POM1 Apple-1 Multiplexing Fantasy (2026)",
        "Emulator-only fantasy (violates Parmigiani's golden rule \"one board at a time\"): "
        "64 KB RAM, Applesoft Lite, microSD + A1-SID + Wi-Fi modem + Terminal Card. "
        "Graphic cards and the PR-40 printer off by default — plug them from the toolbar. "
        "ACI unplugged so the cassette deck acts as a plain audio player. Boots with the "
        "Cassette Deck + Welcome panels already open to the right of the Apple 1 screen; "
        "your layout customisations are persisted to imgui.ini.",
        false, true, true, false, false, true, true,
        /*pr40*/ false,
        false, false, false, 64, BasicType::ApplesoftLite,
        /*sidSE*/ false,
        /*jukeBox*/ false, JukeBox::Jumper::RAM16_ROM32,
        /*gt6144*/ false,
        {
            // Positions / sizes mirror the canonical POM1 imgui.ini so the
            // first launch (no saved layout) snaps to the shipped screenshot.
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
    // window has no saved state in imgui.ini yet. After the first boot the
    // user's drags/resizes are persisted by ImGui and take precedence on
    // subsequent launches (deleting imgui.ini restores the preset layout).
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
    showJukeBox              = false;
#if !POM1_IS_WASM
    terminalCardEnabled      = cfg.terminalCard;
    showTerminalCard         = false;
#endif
    pr40Enabled              = cfg.pr40Printer;
    showPR40                 = cfg.pr40Printer;
    gt6144Enabled            = cfg.gt6144;
    showGT6144               = cfg.gt6144;

    // The default POM1 preset (last in the list) boots with the procedural
    // cassette deck and the Welcome panel already open to match the
    // canonical screenshot layout. Any other preset starts with these
    // auxiliary windows closed — the user opens them from the File / Help
    // menus when wanted. The Welcome panel only "greets" on that preset;
    // the user can still reopen it from Help > Welcome at any time.
    const bool isDefaultPreset = (presetIndex == kMachinePresetCount - 1);
    showCassetteDeck = isDefaultPreset;
    showWelcome      = isDefaultPreset;

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

    // Populate pending layout positions
    pendingLayout.clear();
    for (int i = 0; i < cfg.layoutCount; i++) {
        const auto& p = cfg.layout[i];
        pendingLayout.push_back({p.name, p.pos, p.size});
    }

    if (cfg.jukeBox)
        evictMemoryMapRegionsForJukeBox();

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
