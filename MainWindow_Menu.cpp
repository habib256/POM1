// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MainWindow_Menu.cpp — implementations of the three big top-of-window
// renderers: the menu bar, the toolbar, and the bottom status bar.
// Extracted from MainWindow_ImGui.cpp to keep the original .cpp manageable.

#include "MainWindow_ImGui.h"
#include "MainWindow_Internal.h"
#include "POM1Build.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "IconsFontAwesome6.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>

namespace {
using namespace pom1::mainwindow::detail;
}

void MainWindow_ImGui::renderMenuBar()
{
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Load Memory", shortcutLabel(GLFW_KEY_O, GLFW_MOD_CONTROL))) {
                loadMemory();
            }
            if (ImGui::MenuItem("Save Memory", shortcutLabel(GLFW_KEY_S, GLFW_MOD_CONTROL))) {
                saveMemory();
            }
            ImGui::Separator();
            ImGui::MenuItem("Cassette Deck", nullptr, &showCassetteDeck);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Realistic procedural cassette deck.\nPiano keys, mechanical counter, live transport.");
            ImGui::MenuItem("Cassette Controls (classic)", nullptr, &showCassetteControl);
            ImGui::Separator();
            if (ImGui::MenuItem("Paste Code", shortcutLabel(GLFW_KEY_V, GLFW_MOD_CONTROL))) {
                pasteCode();
            }
#if !POM1_IS_WASM
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", shortcutLabel(GLFW_KEY_Q, GLFW_MOD_CONTROL))) {
                quit();
            }
#endif
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("CPU")) {
#if !POM1_IS_WASM
            ImGui::Text("CPU Speed:");
            if (ImGui::RadioButton("x1", executionSpeed == POM1_CPU_CYCLES_PER_FRAME_1X_60HZ)) {
                executionSpeed = POM1_CPU_CYCLES_PER_FRAME_1X_60HZ;
                emulation->setExecutionSpeedCyclesPerFrame(executionSpeed);
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("x2", executionSpeed == POM1_CPU_CYCLES_PER_FRAME_2X_60HZ)) {
                executionSpeed = POM1_CPU_CYCLES_PER_FRAME_2X_60HZ;
                emulation->setExecutionSpeedCyclesPerFrame(executionSpeed);
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Max", executionSpeed == 1000000)) {
                executionSpeed = 1000000;
                emulation->setExecutionSpeedCyclesPerFrame(executionSpeed);
            }
            ImGui::Separator();
#endif
            if (cpuRunning) {
                if (ImGui::MenuItem("Stop", shortcutLabel(GLFW_KEY_F6))) {
                    stopCpu();
                }
            } else {
                if (ImGui::MenuItem("Start", shortcutLabel(GLFW_KEY_F6))) {
                    startCpu();
                }
            }
            if (ImGui::MenuItem("Step", shortcutLabel(GLFW_KEY_F7))) {
                stepCpu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Soft Reset", shortcutLabel(GLFW_KEY_F5))) {
                reset();
            }
            if (ImGui::MenuItem("Hard Reset", shortcutLabel(GLFW_KEY_F5, GLFW_MOD_CONTROL))) {
                hardReset();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Debug Console", shortcutLabel(GLFW_KEY_F3))) {
                debugCpu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Settings")) {
            if (ImGui::MenuItem("Display Options")) {
                configScreen();
            }
            ImGui::Separator();
            ImGui::Text("Terminal Speed (chars/sec):");
            static int termSpeed = 60;
            ImGui::SetNextItemWidth(150);
            if (ImGui::SliderInt("##termspeed", &termSpeed, 0, 2000, termSpeed == 0 ? "Max" : "%d c/s")) {
                emulation->setTerminalSpeed(termSpeed);
            }
            ImGui::MenuItem("Keyboard autorepeat", nullptr, &keyboardAutorepeat);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Off (default): matches a real TTL keyboard - holding a key asserts STROBE once.\n"
                                  "On: OS autorepeat reaches the Apple 1 (useful when using POM1 as a terminal).");
            ImGui::Separator();
            ImGui::MenuItem("Memory Viewer", shortcutLabel(GLFW_KEY_F1), &showMemoryViewer);
            ImGui::MenuItem("Memory Map", shortcutLabel(GLFW_KEY_F2), &showMemoryMap);
            if (ImGui::MenuItem("Memory Options")) {
                configMemory();
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("A1-SID chip model")) {
                const bool is6581 = (uiSnapshot.sidChipModel == pom1::SID::ChipModel::MOS6581);
                if (ImGui::MenuItem("MOS 6581 (vintage, non-linear filter)", nullptr, is6581)) {
                    emulation->setSIDChipModel(pom1::SID::ChipModel::MOS6581);
                    setStatusMessage("A1-SID: MOS 6581 selected", 2.0f);
                }
                if (ImGui::MenuItem("CSG 8580 (cleaner revision)", nullptr, !is6581)) {
                    emulation->setSIDChipModel(pom1::SID::ChipModel::MOS8580);
                    setStatusMessage("A1-SID: CSG 8580 selected", 2.0f);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Hardware")) {
            if (ImGui::MenuItem("Woz ACI Cassette Interface", nullptr, &aciEnabled)) {
                emulation->setACIEnabled(aciEnabled);
                setStatusMessage(aciEnabled ? "Woz ACI plugged" : "Woz ACI unplugged", 2.0f);
            }
            if (ImGui::MenuItem("Uncle Bernie's GEN2 HGR Graphic Card", nullptr, &graphicsCardEnabled)) {
                if (graphicsCardEnabled) showGraphicsCard = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("P-LAB microSD Storage Card", nullptr, &microSDEnabled)) {
                emulation->setMicroSDEnabled(microSDEnabled);
                if (microSDEnabled) cffa1Enabled = false; // sync UI
            }
            if (ImGui::MenuItem("CFFA1 CompactFlash Card", nullptr, &cffa1Enabled)) {
                emulation->setCFFA1Enabled(cffa1Enabled);
                if (cffa1Enabled) microSDEnabled = false; // sync UI
            }
            if (ImGui::MenuItem("P-LAB A1-SID Sound Card (SID @ $C800)", nullptr, &sidEnabled)) {
                emulation->setSIDEnabled(sidEnabled);
                // Prototype and SE share the same MOS chip — plugging one
                // auto-unplugs the other on the backend; mirror that here.
                if (sidEnabled) sidSpecialEditionEnabled = false;
            }
            if (ImGui::MenuItem("A1-AUDIO Special Edition (SID @ $CC00)", nullptr, &sidSpecialEditionEnabled)) {
                emulation->setSIDSpecialEditionEnabled(sidSpecialEditionEnabled);
                if (sidSpecialEditionEnabled) {
                    // Mutually exclusive with the prototype SID and with TMS9918.
                    sidEnabled = false;
                    tms9918Enabled = false;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Claudio Parmigiani's 10-unit A1-AUDIO Special Edition.\n"
                                  "Same MOS 6581/8580 as the prototype, register window at $CC00-$CC1F.\n"
                                  "Mutually exclusive with P-LAB Graphic Card (TMS9918).");
            if (ImGui::MenuItem("P-LAB Graphic Card (TMS9918)", nullptr, &tms9918Enabled)) {
                emulation->setTMS9918Enabled(tms9918Enabled);
                if (tms9918Enabled) {
                    showTMS9918 = true;
                    sidSpecialEditionEnabled = false; // mutually exclusive
                }
            }
            if (ImGui::MenuItem("P-LAB I/O Board & RTC", nullptr, &a1ioRtcEnabled)) {
                emulation->setA1IO_RTCEnabled(a1ioRtcEnabled);
                if (a1ioRtcEnabled) showA1IO_RTC = true;
            }
#if !POM1_IS_WASM
            if (ImGui::MenuItem("P-LAB Terminal Card", nullptr, &terminalCardEnabled)) {
                emulation->setTerminalCardEnabled(terminalCardEnabled);
                if (terminalCardEnabled) showTerminalCard = true;
            }
#endif
            if (ImGui::MenuItem("P-LAB MODEM BBS", nullptr, &wifiModemEnabled)) {
                emulation->setWiFiModemEnabled(wifiModemEnabled);
                if (wifiModemEnabled) showWiFiModem = true;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Presets")) {
            auto presetItem = [&](int i) {
                char ramLabel[24];
                std::snprintf(ramLabel, sizeof(ramLabel), "%d KB RAM", kMachinePresets[i].ramKB);
                if (ImGui::MenuItem(kMachinePresets[i].name, ramLabel))
                    applyMachineConfig(i);
            };
            presetItem(0);   // Bare Apple-1
            presetItem(1);   // Apple-1 with ACI & Integer BASIC
            ImGui::Separator();
            presetItem(2);   // Replica-1 with ACI, Krusader
            presetItem(3);   // Replica-1 with CFFA1
            ImGui::Separator();
            // All P-LAB expansion cards grouped together
            for (int i = 4; i <= 9; ++i)
                presetItem(i);               // microSD, A1-SID, A1-AUDIO SE, TMS9918, I/O+RTC, Wi-Fi
            presetItem(11);                  // P-LAB Multiplexing Fantasy
            ImGui::Separator();
            presetItem(10);                  // Uncle Bernie's GEN2 HGR
            ImGui::Separator();
            presetItem(12);                  // POM1 Multiplexing Fantasy
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            ImGui::MenuItem("Hardware Reference", nullptr, &showHardwareReference);
            if (ImGui::BeginMenu("Special Thanks to")) {
                ImGui::MenuItem("Ports & acknowledgements", nullptr, &showSpecialThanks);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("About")) {
                about();
            }
            ImGui::EndMenu();
        }
}

void MainWindow_ImGui::renderToolbar()
{
    ImGuiIO& io = ImGui::GetIO();
    float menuBarHeight = ImGui::GetFrameHeight();
    float toolbarHeight = kToolbarBandHeight;

    ImGui::SetNextWindowPos(ImVec2(0, menuBarHeight));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, toolbarHeight));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
    if (ImGui::Begin("##Toolbar", nullptr, flags)) {

        ImVec4 activeColor(0.2f, 0.4f, 0.8f, 1.0f);
        ImVec2 btnSize(28, 24);
#if !POM1_IS_WASM
        const float mhzBtnPadX = ImGui::GetStyle().FramePadding.x * 2.0f;
        const float mhzBtnW =
            std::max(ImGui::CalcTextSize("x1").x, ImGui::CalcTextSize("x2").x) + mhzBtnPadX;
        const ImVec2 mhzBtnSize(mhzBtnW, 24.0f);
#endif

        // --- Chargement (premier) ---
        if (ImGui::Button(ICON_FA_FOLDER_OPEN, btnSize)) loadMemory();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Load (Ctrl+O)");

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,
            microSDEnabled ? ImVec4(0.2f, 0.4f, 0.8f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        if (ImGui::Button(ICON_FA_SD_CARD, btnSize)) {
            microSDEnabled = !microSDEnabled;
            emulation->setMicroSDEnabled(microSDEnabled);
            if (microSDEnabled) { cffa1Enabled = false; } // mutual exclusion
            setStatusMessage(microSDEnabled ? "P-LAB microSD Card plugged - type 8000R"
                                            : "P-LAB microSD Card unplugged", 2.0f);
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(microSDEnabled ? "P-LAB microSD Storage Card (click to unplug)"
                                             : "Plug P-LAB microSD Storage Card");
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,
            cffa1Enabled ? ImVec4(0.2f, 0.4f, 0.8f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        if (ImGui::Button(ICON_FA_HARD_DRIVE, btnSize)) {
            cffa1Enabled = !cffa1Enabled;
            emulation->setCFFA1Enabled(cffa1Enabled);
            if (cffa1Enabled) { microSDEnabled = false; } // mutual exclusion
            setStatusMessage(cffa1Enabled ? "CFFA1 CompactFlash plugged - type 9006R"
                                          : "CFFA1 CompactFlash unplugged", 2.0f);
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(cffa1Enabled ? "CFFA1 CompactFlash Card (click to unplug)"
                                           : "Plug CFFA1 CompactFlash Card");
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,
            aciEnabled ? ImVec4(0.2f, 0.4f, 0.8f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        if (ImGui::Button("##cassetteToolbar", btnSize)) {
            aciEnabled = !aciEnabled;
            emulation->setACIEnabled(aciEnabled);
            setStatusMessage(aciEnabled ? "Woz ACI plugged" : "Woz ACI unplugged", 2.0f);
        }
        drawToolbarCassetteIcon(ImGui::GetWindowDrawList(),
                                ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(aciEnabled ? "Woz ACI Cassette Interface (click to unplug)"
                                         : "Plug Woz ACI Cassette Interface");
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,
            sidEnabled ? ImVec4(0.2f, 0.4f, 0.8f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        if (ImGui::Button(ICON_FA_MUSIC, btnSize)) {
            sidEnabled = !sidEnabled;
            emulation->setSIDEnabled(sidEnabled);
            setStatusMessage(sidEnabled ? "P-LAB A1-SID plugged" : "P-LAB A1-SID unplugged", 2.0f);
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(sidEnabled ? "P-LAB A1-SID Sound Card (click to unplug)"
                                         : "Plug P-LAB A1-SID Sound Card");
        }

        ImGui::SameLine();
        if (graphicsCardEnabled && showGraphicsCard)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.8f, 1.0f));
        else if (!graphicsCardEnabled)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        else
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_Button]);
        if (ImGui::Button("##hgrToolbar", btnSize)) {
            if (!graphicsCardEnabled) {
                graphicsCardEnabled = true;
                showGraphicsCard = true;
                // Load demo HGR image at $2000 if available
                std::string demoPath;
                for (const auto& dir : {"software/hgr", "../software/hgr", "../../software/hgr"}) {
                    std::string p = std::string(dir) + "/GEN2.HGR.BIN";
                    if (std::filesystem::exists(p)) { demoPath = p; break; }
                }
                if (!demoPath.empty()) {
                    std::string error;
                    emulation->loadBinaryToRam(demoPath, 0x2000, error);
                    setStatusMessage("GEN2 plugged - demo image loaded at $2000", 3.0f);
                } else {
                    setStatusMessage("GEN2 plugged", 2.0f);
                }
            } else {
                showGraphicsCard = !showGraphicsCard;
                if (!showGraphicsCard) {
                    graphicsCardEnabled = false;
                    setStatusMessage("GEN2 unplugged", 2.0f);
                }
            }
        }
        drawToolbarTextLabel(ImGui::GetWindowDrawList(),
                               ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), "HGR");
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(graphicsCardEnabled ? "Bernie's GEN2 HGR (click to unplug)" : "Plug Uncle Bernie's GEN2 HGR Graphic Card");
        }

        ImGui::SameLine();
        if (tms9918Enabled && showTMS9918)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.8f, 1.0f));
        else if (!tms9918Enabled)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        else
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_Button]);
        if (ImGui::Button(ICON_FA_DISPLAY, btnSize)) {
            if (!tms9918Enabled) {
                tms9918Enabled = true;
                showTMS9918 = true;
                emulation->setTMS9918Enabled(true);
                setStatusMessage("P-LAB TMS9918 plugged", 2.0f);
            } else {
                showTMS9918 = !showTMS9918;
                if (!showTMS9918) {
                    tms9918Enabled = false;
                    emulation->setTMS9918Enabled(false);
                    setStatusMessage("P-LAB TMS9918 unplugged", 2.0f);
                }
            }
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(tms9918Enabled ? "P-LAB TMS9918 Output (click to unplug)" : "Plug P-LAB Graphic Card (TMS9918)");
        }

        // --- P-LAB I/O Board & RTC ---
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,
            a1ioRtcEnabled ? ImVec4(0.2f, 0.4f, 0.8f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        if (ImGui::Button(ICON_FA_CLOCK, btnSize)) {
            if (!a1ioRtcEnabled) {
                a1ioRtcEnabled = true;
                showA1IO_RTC = true;
                emulation->setA1IO_RTCEnabled(true);
                setStatusMessage("P-LAB I/O Board & RTC plugged at $2000", 3.0f);
            } else {
                showA1IO_RTC = !showA1IO_RTC;
                if (!showA1IO_RTC) {
                    a1ioRtcEnabled = false;
                    emulation->setA1IO_RTCEnabled(false);
                    setStatusMessage("P-LAB I/O Board & RTC unplugged", 2.0f);
                }
            }
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(a1ioRtcEnabled ? "P-LAB I/O Board & RTC (click to unplug)"
                                              : "Plug P-LAB I/O Board & RTC");
        }

#if !POM1_IS_WASM
        // --- P-LAB Terminal Card ---
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,
            terminalCardEnabled ? ImVec4(0.2f, 0.6f, 0.4f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        if (ImGui::Button(ICON_FA_TERMINAL, btnSize)) {
            if (!terminalCardEnabled) {
                terminalCardEnabled = true;
                showTerminalCard = true;
                emulation->setTerminalCardEnabled(true);
                setStatusMessage("P-LAB Terminal Card plugged (telnet localhost 6502)", 3.0f);
            } else {
                showTerminalCard = !showTerminalCard;
                if (!showTerminalCard) {
                    terminalCardEnabled = false;
                    emulation->setTerminalCardEnabled(false);
                    setStatusMessage("P-LAB Terminal Card unplugged", 2.0f);
                }
            }
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(terminalCardEnabled ? "P-LAB Terminal Card (click to unplug)"
                                                  : "Plug P-LAB Terminal Card");
        }
#endif

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,
            wifiModemEnabled ? ImVec4(0.2f, 0.4f, 0.8f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        if (ImGui::Button("##bbsModemToolbar", btnSize)) {
            if (!wifiModemEnabled) {
                wifiModemEnabled = true;
                showWiFiModem = true;
                emulation->setWiFiModemEnabled(true);
                setStatusMessage("P-LAB Wi-Fi Modem plugged", 2.0f);
            } else {
                showWiFiModem = !showWiFiModem;
                if (!showWiFiModem) {
                    wifiModemEnabled = false;
                    emulation->setWiFiModemEnabled(false);
                    setStatusMessage("P-LAB Wi-Fi Modem unplugged", 2.0f);
                }
            }
        }
        drawToolbarTextLabel(ImGui::GetWindowDrawList(),
                             ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), "BBS");
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(wifiModemEnabled ? "P-LAB Wi-Fi Modem (click to unplug)"
                                               : "Plug P-LAB Wi-Fi Modem");
        }

        // --- Séparateur ---
        ImGui::SameLine(0, 12);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine(0, 12);

        // --- Resets groupés ---
        if (ImGui::Button(ICON_FA_ARROW_ROTATE_LEFT, btnSize)) reset();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Soft Reset (F5)");

        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_POWER_OFF, btnSize)) hardReset();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hard Reset (Ctrl+F5)");

        // --- Séparateur ---
        ImGui::SameLine(0, 12);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine(0, 12);

#if !POM1_IS_WASM
        // --- Vitesse CPU (x1 / x2 / Max) - masqué en WASM (rythme imposé par le navigateur)
        {
            bool is1M = (executionSpeed == POM1_CPU_CYCLES_PER_FRAME_1X_60HZ);
            if (is1M) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            if (ImGui::Button("x1", mhzBtnSize)) {
                executionSpeed = POM1_CPU_CYCLES_PER_FRAME_1X_60HZ;
                emulation->setExecutionSpeedCyclesPerFrame(executionSpeed);
            }
            if (is1M) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("x1 - 1.022727 MHz (~%d cycles/frame @ 60 Hz)", POM1_CPU_CYCLES_PER_FRAME_1X_60HZ);
            }
        }
        ImGui::SameLine();
        {
            bool is2M = (executionSpeed == POM1_CPU_CYCLES_PER_FRAME_2X_60HZ);
            if (is2M) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            if (ImGui::Button("x2", mhzBtnSize)) {
                executionSpeed = POM1_CPU_CYCLES_PER_FRAME_2X_60HZ;
                emulation->setExecutionSpeedCyclesPerFrame(executionSpeed);
            }
            if (is2M) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("x2 - ~2.045 MHz (~%d cycles/frame @ 60 Hz)", POM1_CPU_CYCLES_PER_FRAME_2X_60HZ);
            }
        }
        ImGui::SameLine();
        {
            bool isMax = (executionSpeed == 1000000);
            if (isMax) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            if (ImGui::Button("Max", btnSize)) {
                executionSpeed = 1000000;
                emulation->setExecutionSpeedCyclesPerFrame(executionSpeed);
            }
            if (isMax) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Max");
        }

        ImGui::SameLine(0, 12);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine(0, 12);
#endif

        // --- Fenêtres toggle ---
        {
            bool dbg = showDebugger;
            if (dbg) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            if (ImGui::Button(ICON_FA_BUG, btnSize)) showDebugger = !showDebugger;
            if (dbg) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Debug (F12)");
        }
        ImGui::SameLine();
        {
            bool mem = showMemoryViewer;
            if (mem) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            if (ImGui::Button(ICON_FA_MEMORY, btnSize)) showMemoryViewer = !showMemoryViewer;
            if (mem) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Memory");
        }
        ImGui::SameLine();
        {
            bool map = showMemoryMap;
            if (map) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            if (ImGui::Button(ICON_FA_MAP, btnSize)) showMemoryMap = !showMemoryMap;
            if (map) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Memory Map");
        }

        // --- Séparateur ---
        ImGui::SameLine(0, 12);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine(0, 12);

        // --- Monitor phosphor tint: one swatch, click cycles Green → Brown → Monochrome ---
        {
            const ImVec2 swatchSize(22.0f, 22.0f);
            monitorTintCycleButton("##phosphor_cycle", swatchSize, screen.get());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s phosphor - click to cycle tint", monitorTintLabel(screen->monitorMode));
            }
        }

        ImGui::SameLine(0, 6);

        // --- Character mode: Font Awesome apple (charmap) / font (host) ---
        {
            const bool charm = (screen->characterRenderMode == Screen_ImGui::CharacterRenderMode::Apple1Charmap);
            const char* charIcon = charm ? ICON_FA_APPLE_WHOLE : ICON_FA_FONT;
            if (charm) {
                ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            }
            if (ImGui::Button(charIcon, btnSize)) {
                screen->characterRenderMode = charm ? Screen_ImGui::CharacterRenderMode::HostAscii
                                                     : Screen_ImGui::CharacterRenderMode::Apple1Charmap;
            }
            if (charm) {
                ImGui::PopStyleColor();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    charm ? "Apple-1 charmap (bitmap ROM)\nClick - use host ASCII font"
                          : "Host ASCII font\nClick - use Apple-1 charmap");
            }
        }

        // --- About button aligned to the right ---
        float aboutBtnW = ImGui::CalcTextSize(ICON_FA_CIRCLE_INFO).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SameLine(io.DisplaySize.x - aboutBtnW - ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button(ICON_FA_CIRCLE_INFO, btnSize)) showAbout = !showAbout;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("About");
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void MainWindow_ImGui::renderStatusBar()
{
    // Barre de statut simple en bas de l'écran
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, io.DisplaySize.y - 25));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, 25));

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("##StatusBar", nullptr, window_flags)) {
        // Côté gauche: message de statut
        ImGui::Text("%s", statusMessage.c_str());

        std::string cpuText = cpuRunning ? "RUNNING" : "STOPPED";
        std::string speedText;
        if (executionSpeed >= 1000000) {
            speedText = "| Max";
        } else {
            std::ostringstream oss;
            oss << "| " << std::fixed << std::setprecision(3)
                << (executionSpeed * 60.0 / 1000000.0) << " MHz";
            speedText = oss.str();
        }
        std::ostringstream ramOss;
        ramOss << "| RAM: " << presetRamKB << " KB";
        const int oorCount = emulation->getOutOfRangeAccessCount();
        if (oorCount > 0) {
            ramOss << " (OOR:" << oorCount;
            if (uiSnapshot.oorStrictMode) ramOss << "!";
            ramOss << ")";
        } else if (uiSnapshot.oorStrictMode && presetRamKB < 64) {
            ramOss << " [strict]";
        }
        std::string ramText = ramOss.str();

        std::string tapeText;
        if (uiSnapshot.cassetteLoadedTape) {
            std::ostringstream oss;
            oss << "| TAPE: " << (uiSnapshot.cassettePlaybackActive ? "READ" : "READY")
                << " (" << uiSnapshot.cassetteLoadedTransitionCount << " tr)";
            tapeText = oss.str();
        } else if (uiSnapshot.cassetteRecordedTape) {
            std::ostringstream oss;
            oss << "| TAPE OUT: " << uiSnapshot.cassetteRecordedTransitionCount << " tr";
            tapeText = oss.str();
        } else {
            tapeText = "| TAPE: empty";
        }

        std::string audioText = !uiSnapshot.cassetteAudioAvailable ? "| AUDIO OFF" : "";
        std::string keyText;
        if (uiSnapshot.keyReady) {
            std::ostringstream oss;
            oss << "| KEY: '" << uiSnapshot.lastKey << "'";
            keyText = oss.str();
        }

        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        float rightWidth =
            ImGui::CalcTextSize(cpuText.c_str()).x +
            ImGui::CalcTextSize(speedText.c_str()).x +
            ImGui::CalcTextSize(ramText.c_str()).x +
            ImGui::CalcTextSize(tapeText.c_str()).x +
            (audioText.empty() ? 0.0f : ImGui::CalcTextSize(audioText.c_str()).x) +
            (keyText.empty() ? 0.0f : ImGui::CalcTextSize(keyText.c_str()).x) +
            spacing * 5.0f;

        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - rightWidth - 16.0f));

        if (cpuRunning) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", cpuText.c_str());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", cpuText.c_str());
        }

        ImGui::SameLine();
        ImGui::Text("%s", speedText.c_str());

        ImGui::SameLine();
        ImGui::Text("%s", ramText.c_str());

        ImGui::SameLine();
        if (uiSnapshot.cassetteLoadedTape) {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f),
                               "%s", tapeText.c_str());
        } else if (uiSnapshot.cassetteRecordedTape) {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f),
                               "%s", tapeText.c_str());
        } else {
            ImGui::Text("%s", tapeText.c_str());
        }

        if (!uiSnapshot.cassetteAudioAvailable) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", audioText.c_str());
        }

        // État du clavier
        if (uiSnapshot.keyReady) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                               "%s", keyText.c_str());
        }
    }
    ImGui::End();
}
