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

#include <string>

namespace pom1::mainwindow::detail {

const MachineConfig kMachinePresets[] = {
    //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI  RAM  BASIC              SID-SE
    {
        "Bare Apple-1 (July 1976)",
        "Pre-ACI original: 6502, 4 KB RAM, PIA 6821, WOZ Monitor.",
        false, false, false, false, false, false, false,
        false, false, false, 4, BasicType::None,
        /*sidSE*/ false,
        { {"Apple 1 Screen", {10,61}, {0,0}} }, 1
    },
    {
        "Apple-1 with ACI & Integer BASIC (October 1976)",
        "Original bare board with the ACI cassette expansion card: 6502, 8 KB RAM, PIA 6821, Integer BASIC, WOZ Monitor.",
        false, false, false, false, false, false, false,
        false, false, true, 8, BasicType::Integer,
        /*sidSE*/ false,
        { {"Apple 1 Screen", {10,61}, {0,0}} }, 1
    },
    {
        "Replica-1 with ACI, Krusader & Integer BASIC (Briel 2003)",
        "Vince Briel's modern recreation: Integer BASIC, Krusader assembler, ACI cassette.",
        false, false, false, false, false, false, false,
        true, false, true, 32, BasicType::Integer,
        /*sidSE*/ false,
        { {"Apple 1 Screen", {10,61}, {0,0}} }, 1
    },
    {
        "Replica-1 with CFFA1 & Applesoft Lite (Dreher 2007)",
        "Replica 1 with CFFA1 CompactFlash storage, Applesoft Lite.",
        false, false, false, false, false, false, false,
        false, true, false, 32, BasicType::ApplesoftLite,
        /*sidSE*/ false,
        { {"Apple 1 Screen", {10,61}, {0,0}} }, 1
    },
    {
        "P-LAB Apple-1 with microSD & Applesoft Lite (April 2022)",
        "P-LAB microSD Storage Card, Applesoft Lite.",
        false, true, false, false, false, false, false,
        false, false, false, 32, BasicType::ApplesoftLite,
        /*sidSE*/ false,
        {
            {"Apple 1 Screen", {10, 61}, {0, 0}},
        }, 1
    },
    {   //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI
        "P-LAB Apple-1 with A1-SID Sound Card ($C800-$CFFF)",
        "P-LAB A1-SID Sound Card (MOS 6581/8580), Integer BASIC. Registers at $C800-$CFFF.",
        false, false, true, false, false, false, false,
        false, false, false, 32, BasicType::Integer,
        /*sidSE*/ false,
        {
            {"Apple 1 Screen", {10, 61}, {0, 0}},
        }, 1
    },
    {   //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI
        "P-LAB Apple-1 with A1-AUDIO Special Edition ($CC00-$CC1F)",
        "Claudio Parmigiani's A1-AUDIO Special Edition (10 units): MOS 6581/8580 at $CC00-$CC1F. "
        "Mutually exclusive with TMS9918 (same $CC00/$CC01 window).",
        false, false, false, false, false, false, false,
        false, false, false, 32, BasicType::Integer,
        /*sidSE*/ true,
        {
            {"Apple 1 Screen", {10, 61}, {0, 0}},
        }, 1
    },
    {   //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI
        "P-LAB Apple-1 with TMS9918 Graphic Card",
        "P-LAB Graphic Card (TMS9918A VDP), Integer BASIC.",
        false, false, false, true, false, false, false,
        false, false, false, 32, BasicType::Integer,
        /*sidSE*/ false,
        {
            {"Apple 1 Screen",               {10,  61}, {0,   0}},
            {"P-LAB Graphic Card (TMS9918)", {640, 61}, {784, 612}},
        }, 2
    },
    {   //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI
        "P-LAB Apple-1 with I/O Board & RTC",
        "P-LAB A1-IO Board & RTC (DS3231, DS18B20, analog/digital I/O), Integer BASIC.",
        false, false, false, false, true, false, false,
        false, false, false, 32, BasicType::Integer,
        /*sidSE*/ false,
        {
            {"Apple 1 Screen",        {10,  61},  {0,   0}},
            {"P-LAB I/O Board & RTC", {640, 61},  {380, 280}},
        }, 2
    },
    {   //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI
        "P-LAB Apple-1 with Wi-Fi Modem BBS",
        "P-LAB MODEM BBS (65C51 ACIA, ESP8266 AT, TCP/TELNET), Integer BASIC.",
        false, false, false, false, false, true, false,
        false, false, false, 32, BasicType::Integer,
        /*sidSE*/ false,
        {
            {"Apple 1 Screen",    {10,  61},  {0,   0}},
            {"P-LAB Wi-Fi Modem", {640, 61},  {340, 260}},
        }, 2
    },
    {
        "Uncle Bernie's Apple-1 with GEN2 HGR Color (April 2026)",
        "Uncle Bernie's GEN2 280x192 HGR color graphics, Integer BASIC.",
        true, false, false, false, false, false, false,
        false, false, false, 32, BasicType::Integer,
        /*sidSE*/ false,
        {
            {"Apple 1 Screen",                 {10,  61}, {0,   0}},
            {"Uncle Bernie's GEN2 HGR Graphic Card", {624, 61}, {576, 420}},
        }, 2
    },
    {   //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI
        "P-LAB Apple-1 Multiplexing Fantasy",
        "All P-LAB expansion cards: microSD, A1-SID, TMS9918, I/O & RTC, Wi-Fi modem, Terminal Card.",
        false, true, true, true, true, true, true,
        false, false, false, 64, BasicType::ApplesoftLite,
        /*sidSE*/ false,
        {
            {"Apple 1 Screen",               {10,  61},  {0,   0}},
            {"P-LAB Graphic Card (TMS9918)", {640, 61},  {784, 612}},
            {"P-LAB Wi-Fi Modem",            {640, 495}, {340, 260}},
            {"P-LAB Terminal Card",          {10,  510}, {360, 280}},
            {"P-LAB I/O Board & RTC",        {740, 495}, {380, 280}},
        }, 5
    },
    {
        "POM1 Apple-1 Multiplexing Fantasy (2026)",
        "64 KB RAM, Applesoft Lite, microSD + A1-SID + Wi-Fi modem + Terminal Card. Graphic cards off by default.",
        false, true, true, false, false, true, true,
        false, false, true, 64, BasicType::ApplesoftLite,
        /*sidSE*/ false,
        {
            {"Apple 1 Screen",        {10,  61},  {0,   0}},
            {"P-LAB Wi-Fi Modem",     {640, 495}, {340, 260}},
            {"P-LAB Terminal Card",   {10,  510}, {360, 280}},
        }, 3
    },
};

const int kMachinePresetCount = static_cast<int>(sizeof(kMachinePresets) / sizeof(kMachinePresets[0]));

} // namespace pom1::mainwindow::detail

// ---------------------------------------------------------------------------
// MainWindow_ImGui method implementations
// ---------------------------------------------------------------------------

namespace {
using namespace pom1::mainwindow::detail;
}

void MainWindow_ImGui::applyPendingLayout(const char* windowName)
{
    for (auto it = pendingLayout.begin(); it != pendingLayout.end(); ++it) {
        if (it->name == windowName) {
            ImGui::SetNextWindowPos(it->pos, ImGuiCond_Always);
            if (it->size.x > 0.0f)
                ImGui::SetNextWindowSize(it->size, ImGuiCond_Always);
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

    // Unplug both SID variants up front — cleans any lingering state from
    // the previous preset (source off the mixer, chip + ring fully reset).
    // The ACTUAL re-plug is deferred below via pendingSidEnableFrames so
    // the SID starts a few frames after the CPU has been running: plugging
    // the SID in the same frame as the preset apply catches the chip
    // before it can pick up audible writes, and the first tune loaded
    // stays silent until the user manually toggles the card. Deferring
    // the plug past the frame lets the CPU settle and fixes that.
    emulation->setSIDEnabled(false);
    emulation->setSIDSpecialEditionEnabled(false);

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

    // Apple Cassette Interface — unplugged only for the bare-4K preset.
    aciEnabled = cfg.aci;
    emulation->setACIEnabled(cfg.aci);

    // Hardware flags (enabled = plugged in, show = window open)
    graphicsCardEnabled = cfg.graphicsCard;
    showGraphicsCard    = false;

    microSDEnabled = cfg.microSD;
    emulation->setMicroSDEnabled(cfg.microSD);

    cffa1Enabled = cfg.cffa1;
    emulation->setCFFA1Enabled(cfg.cffa1);

    // SID plug is deferred: setSIDEnabled(false) ran in the unplug block
    // above; the actual plug (if the preset wants the card) fires a few
    // frames later from render() via pendingSidEnableFrames. See the
    // comment on that field in MainWindow_ImGui.h for the rationale.
    // Both SID variants share the same deferred-plug slot — they can't be
    // plugged together (one chip, mutually exclusive MachineConfig fields).
    sidEnabled = cfg.sid;
    sidSpecialEditionEnabled = cfg.sidSpecialEdition;
    pendingSidEnable = cfg.sid;
    pendingSidSEEnable = cfg.sidSpecialEdition;
    pendingSidEnableFrames = (cfg.sid || cfg.sidSpecialEdition) ? kSidEnableDeferFrames : 0;

    tms9918Enabled = cfg.tms9918;
    emulation->setTMS9918Enabled(cfg.tms9918);
    showTMS9918 = false;

    a1ioRtcEnabled = cfg.a1ioRtc;
    emulation->setA1IO_RTCEnabled(cfg.a1ioRtc);
    showA1IO_RTC = false;

    wifiModemEnabled = cfg.wifiModem;
    emulation->setWiFiModemEnabled(cfg.wifiModem);
    showWiFiModem = false;

#if !POM1_IS_WASM
    // Terminal Card requires a TCP server — desktop only.
    terminalCardEnabled = cfg.terminalCard;
    emulation->setTerminalCardEnabled(cfg.terminalCard);
    showTerminalCard = false;
#endif

    // Load the appropriate BASIC ROM for this preset and track in loadedRoms
    {
        std::string error;
        bool ok = false;
        if (cfg.basicType == BasicType::ApplesoftLite) {
            ok = emulation->reloadApplesoftLite(error);
            if (ok) {
                if (cfg.microSD && !cfg.cffa1) {
                    loadedRoms.push_back({"Integer BASIC", 0xE000, 0xEFFF});
                    loadedRoms.push_back({"Applesoft Lite (P-LAB microSD)", 0x6000, 0x7FFF});
                    loadedRoms.push_back({"Woz Monitor", 0xFF00, 0xFFFF});
                } else {
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
