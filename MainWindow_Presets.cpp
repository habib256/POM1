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
    //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI  RAM  BASIC
    {
        "Apple-1 bare 4 K (July 1976)",
        "Pre-ACI original: 6502, 4 KB RAM, PIA 6821, Integer BASIC, WOZ Monitor. No cassette, no expansion.",
        false, false, false, false, false, false, false,
        false, false, false, 4, BasicType::Integer,
        { {"Apple 1 Screen", {10,61}, {0,0}} }, 1
    },
    {
        "Woz Apple 1 (1976)",
        "Original bare board: 6502, 8 KB RAM, PIA 6821, Integer BASIC, WOZ Monitor. No expansion cards.",
        false, false, false, false, false, false, false,
        false, false, true, 8, BasicType::Integer,
        { {"Apple 1 Screen", {10,61}, {0,0}} }, 1
    },
    {
        "Replica 1 (Briel)",
        "Vince Briel's modern recreation: Integer BASIC, Krusader assembler, ACI cassette.",
        false, false, false, false, false, false, false,
        true, false, true, 32, BasicType::Integer,
        { {"Apple 1 Screen", {10,61}, {0,0}} }, 1
    },
    {
        "Replica 1 + CFFA1",
        "Replica 1 with CFFA1 CompactFlash storage, Applesoft Lite, Terminal Card.",
        false, false, false, false, false, false, true,
        false, true, true, 32, BasicType::ApplesoftLite,
        { {"Apple 1 Screen", {10,61}, {0,0}} }, 1
    },
    {
        "Uncle Bernie's Apple 1",
        "Uncle Bernie's GEN2 280x192 HGR color graphics, Integer BASIC, ACI cassette.",
        true, false, false, false, false, false, false,
        false, false, true, 32, BasicType::Integer,
        {
            {"Apple 1 Screen",                 {10,  61}, {0,   0}},
            {"Uncle Bernie's GEN2 HGR Graphic Card", {624, 61}, {576, 420}},
        }, 2
    },
    {
        "P-LAB Apple 1",
        "Full P-LAB expansion: Applesoft Lite, microSD, A1-SID sound, TMS9918 graphics, I/O & RTC, Wi-Fi modem, terminal.",
        false, true, true, true, true, true, true,
        false, false, true, 32, BasicType::ApplesoftLite,
        {
            {"Apple 1 Screen",               {10,  61},  {0,   0}},
            {"P-LAB Graphic Card (TMS9918)", {640, 61},  {784, 612}},
            {"P-LAB Wi-Fi Modem",            {640, 495}, {340, 260}},
            {"P-LAB Terminal Card",          {10,  510}, {360, 280}},
            {"P-LAB I/O Board & RTC",        {740, 495}, {380, 280}},
        }, 5
    },
    {
        "POM1 Apple 1 Fantasy Computer",
        "56 KB RAM, Applesoft Lite, microSD + A1-SID + Wi-Fi modem + Terminal Card. Graphic cards off by default.",
        false, true, true, false, false, true, true,
        false, false, true, 56, BasicType::ApplesoftLite,
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

    // Full hard reset: clear memory, reload default ROMs, reset all peripherals
    emulation->hardReset();
    loadedPrograms.clear();
    loadedRoms.clear();

    // RAM size for this preset. The address space stays 64 KB; the value is
    // forwarded to Memory so out-of-range accesses past ramKB*1024 can be
    // flagged in the status bar without blocking the program.
    presetRamKB = cfg.ramKB;
    emulation->setPresetRamKB(cfg.ramKB);

    // Apple Cassette Interface — unplugged only for the bare-4K preset.
    emulation->setACIEnabled(cfg.aci);

    // Hardware flags (enabled = plugged in, show = window open)
    graphicsCardEnabled = cfg.graphicsCard;
    showGraphicsCard    = false;

    microSDEnabled = cfg.microSD;
    emulation->setMicroSDEnabled(cfg.microSD);

    cffa1Enabled = cfg.cffa1;
    emulation->setCFFA1Enabled(cfg.cffa1);

    sidEnabled = cfg.sid;
    emulation->setSIDEnabled(cfg.sid);

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
        } else {
            ok = emulation->reloadBasic(error);
            if (ok) loadedRoms.push_back({"Integer BASIC", 0xE000, 0xEFFF});
            if (emulation->reloadWozMonitor(error))
                loadedRoms.push_back({"Woz Monitor", 0xFF00, 0xFFFF});
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
