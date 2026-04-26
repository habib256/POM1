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

#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>

namespace {
using namespace pom1::mainwindow::detail;

void showHardwareTooltip(const char* text)
{
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text);
    }
}
}

void MainWindow_ImGui::renderMenuBar()
{
        auto plugJukeBoxFromUi = [&]() {
            jukeBoxChipMode = JukeBox::ChipMode::Flash;
            cffa1Enabled = false;
            microSDEnabled = false;
            wifiModemEnabled = false;
            sidEnabled = false;
            codeTankEnabled = false;
            emulation->setCodeTankEnabled(false);
            evictMemoryMapRegionsForJukeBox();
            emulation->setJukeBoxChipMode(jukeBoxChipMode);
            emulation->setJukeBoxJumper(jukeBoxJumper);
            emulation->setJukeBoxEnabled(true);
            jukeBoxEnabled = true;
            showJukeBox = true;
            setStatusMessage("P-LAB Juke-Box plugged - type BD00R for Program Manager", 3.0f);
        };
        auto unplugJukeBoxFromUi = [&]() {
            emulation->setJukeBoxEnabled(false);
            jukeBoxEnabled = false;
            setStatusMessage("P-LAB Juke-Box unplugged", 2.0f);
        };
        auto plugCodeTankFromUi = [&]() {
            // CodeTank only owns $4000-$7FFF; the only required eviction is
            // the Juke-Box (which can claim the same window).
            jukeBoxEnabled = false;
            emulation->setJukeBoxEnabled(false);
            emulation->setCodeTankJumper(codeTankJumper);
            emulation->setCodeTankEnabled(true);
            codeTankEnabled = true;
            showCodeTank = true;
            setStatusMessage("P-LAB CodeTank plugged: 28c256 at $4000-$7FFF", 3.0f);
        };
        auto unplugCodeTankFromUi = [&]() {
            emulation->setCodeTankEnabled(false);
            codeTankEnabled = false;
            setStatusMessage("P-LAB CodeTank unplugged", 2.0f);
        };

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
            ImGui::MenuItem("Memory Map Grid", shortcutLabel(GLFW_KEY_F2), &showMemoryMapGrid);
            ImGui::MenuItem("Memory Map Bar", nullptr, &showMemoryBar);
            ImGui::MenuItem("Memory Map Bar (Horizontal)", nullptr, &showMemoryBarH);
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
            // --- 1976: original-era expansions ------------------------------
            if (ImGui::MenuItem("Woz ACI Cassette Interface (1976)", nullptr, &aciEnabled)) {
                emulation->setACIEnabled(aciEnabled);
                setStatusMessage(aciEnabled ? "Woz ACI plugged" : "Woz ACI unplugged", 2.0f);
            }
            if (ImGui::MenuItem("SWTPC GT-6144 Graphic Terminal (1976)", nullptr, &gt6144Enabled)) {
                emulation->setGT6144Enabled(gt6144Enabled);
                if (gt6144Enabled) showGT6144 = true;
                setStatusMessage(gt6144Enabled
                    ? "SWTPC GT-6144 plugged (64x96 framebuffer at $D00A)"
                    : "SWTPC GT-6144 unplugged", 3.0f);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Southwest Technical Products, 1976 ($98.50).\n"
                                  "First commercial Apple-1 graphics card: write-only 64x96\n"
                                  "monochrome framebuffer on 6x Intel 2102 SRAM, I/O at $D00A.\n"
                                  "Power-on contents are visible SRAM bistable noise.");
            if (ImGui::MenuItem("SWTPC PR-40 Printer (Jobs 1976)", nullptr, &pr40Enabled)) {
                emulation->setPR40Enabled(pr40Enabled);
                if (pr40Enabled) showPR40 = true;
                setStatusMessage(pr40Enabled
                    ? "SWTPC PR-40 plugged (Jobs' $D012 sniff, DPDT to PB7)"
                    : "SWTPC PR-40 unplugged", 3.0f);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Steve Jobs, Interface Age Oct. 1976.\n"
                                  "Passive sniff on $D012. DPDT switch (Off/Mixed/Print Only)\n"
                                  "in the card window feeds PB7 so the Woz Monitor's BMI loop\n"
                                  "stalls during the ~0.8 s mechanical print cycle.");

            ImGui::Separator();
            // --- Community hardware (non-P-LAB) ----------------------------
            if (ImGui::MenuItem("Uncle Bernie's GEN2 HGR Graphic Card", nullptr, &graphicsCardEnabled)) {
                if (graphicsCardEnabled) showGraphicsCard = true;
            }
            showHardwareTooltip(
                "Uncle Bernie's GEN2 HGR Graphic Card\n"
                "Framebuffer: $2000-$3FFF.\n\n"
                "Hardware conflict: P-LAB I/O Board & RTC also decodes $2000-$200F.\n"
                "A real Apple-1 bus has no arbitration, so use one of these cards at a time.");
            if (ImGui::MenuItem("CFFA1 CompactFlash Card", nullptr, &cffa1Enabled)) {
                emulation->setCFFA1Enabled(cffa1Enabled);
                if (cffa1Enabled) {
                    microSDEnabled = false; // sync UI
                    jukeBoxEnabled = false;
                }
            }
            showHardwareTooltip(
                "CFFA1 CompactFlash Card\n"
                "ROM/register window: $9000-$AFFF.\n\n"
                "Plugging it unplugs microSD and Juke-Box, because their ROM\n"
                "windows overlap the CFFA1 firmware/register area.");

            ImGui::Separator();
            // --- P-LAB family ----------------------------------------------
            if (ImGui::MenuItem("P-LAB Apple-1 Juke-Box", nullptr, jukeBoxEnabled)) {
                if (jukeBoxEnabled) unplugJukeBoxFromUi();
                else plugJukeBoxFromUi();
            }
            showHardwareTooltip(
                "P-LAB Apple-1 Juke-Box\n"
                "ROM window: $4000-$BFFF or $8000-$BFFF. Bank latch: $CA00.\n"
                "Program Manager at $BD00: type BD00R from the Woz Monitor.\n\n"
                "Plugging it unplugs CodeTank, CFFA1, microSD, Wi-Fi Modem, and A1-SID.\n"
                "A1-AUDIO SE can coexist: it lives at $CC00-$CC1F, outside $CA00.");
            if (ImGui::MenuItem("P-LAB CodeTank (28c256 ROM, pairs with TMS9918)",
                                nullptr, codeTankEnabled)) {
                if (codeTankEnabled) unplugCodeTankFromUi();
                else plugCodeTankFromUi();
            }
            showHardwareTooltip(
                "P-LAB CodeTank 28c256 ROM\n"
                "Fixed ROM window: $4000-$7FFF.\n"
                "The board jumper selects lower or upper 16 kB of the 32 kB EEPROM.\n\n"
                "Designed to ship TMS9918 games on real silicon - the CodeTank window\n"
                "($4000-$7FFF) does not collide with TMS9918 ($CC00/$CC01), so the two\n"
                "cards coexist freely. Plugging unplugs the Juke-Box (overlapping ROM\n"
                "window).");
            if (ImGui::MenuItem("P-LAB CodeTank Library...", nullptr, &showCodeTankLibrary)) {
                if (showCodeTankLibrary) setStatusMessage("CodeTank Library opened", 2.0f);
            }
            showHardwareTooltip(
                "Browse the available 32 kB CodeTank ROM images in roms/codetank/.\n"
                "Each ROM holds two 16 kB banks - pick which one to wire into\n"
                "$4000-$7FFF and the CodeTank card plugs itself.");
            if (ImGui::MenuItem("P-LAB microSD Storage Card", nullptr, &microSDEnabled)) {
                emulation->setMicroSDEnabled(microSDEnabled);
                if (microSDEnabled) {
                    cffa1Enabled = false; // sync UI
                    jukeBoxEnabled = false;
                }
            }
            showHardwareTooltip(
                "P-LAB microSD Storage Card\n"
                "ROM/VIA window: $8000-$9FFF and $A000-$A00F.\n\n"
                "Plugging it unplugs CFFA1 and Juke-Box, because their ROM\n"
                "windows overlap the microSD firmware area.");
            if (ImGui::MenuItem("P-LAB A1-SID Sound Card (SID @ $C800)", nullptr, &sidEnabled)) {
                emulation->setSIDEnabled(sidEnabled);
                // Prototype and SE share the same MOS chip — plugging one
                // auto-unplugs the other on the backend; mirror that here.
                if (sidEnabled) {
                    sidSpecialEditionEnabled = false;
                    jukeBoxEnabled = false;
                }
            }
            showHardwareTooltip(
                "P-LAB A1-SID Sound Card\n"
                "SID registers: $C800-$CFFF.\n\n"
                "Plugging it unplugs A1-AUDIO SE (same SID chip) and Juke-Box\n"
                "($CA00 bank latch sits inside the SID window).");
            if (ImGui::MenuItem("A1-AUDIO Special Edition (SID @ $CC00)", nullptr, &sidSpecialEditionEnabled)) {
                emulation->setSIDSpecialEditionEnabled(sidSpecialEditionEnabled);
                if (sidSpecialEditionEnabled) {
                    // Mutually exclusive with the prototype SID and with TMS9918.
                    sidEnabled = false;
                    tms9918Enabled = false;
                }
            }
            showHardwareTooltip(
                "Claudio Parmigiani's 10-unit A1-AUDIO Special Edition\n"
                "SID registers: $CC00-$CC1F.\n\n"
                "Plugging it unplugs A1-SID (same SID chip) and TMS9918\n"
                "($CC00/$CC01 overlap). Juke-Box can coexist.");
            if (ImGui::MenuItem("P-LAB Graphic Card (TMS9918)", nullptr, &tms9918Enabled)) {
                emulation->setTMS9918Enabled(tms9918Enabled);
                if (tms9918Enabled) {
                    showTMS9918 = true;
                    sidSpecialEditionEnabled = false; // mutually exclusive
                }
            }
            showHardwareTooltip(
                "P-LAB Graphic Card (TMS9918)\n"
                "VDP ports: $CC00/$CC01.\n\n"
                "Plugging it unplugs A1-AUDIO SE, because the two cards share\n"
                "the $CC00 control/data window.");
            if (ImGui::MenuItem("P-LAB I/O Board & RTC", nullptr, &a1ioRtcEnabled)) {
                emulation->setA1IO_RTCEnabled(a1ioRtcEnabled);
                if (a1ioRtcEnabled) showA1IO_RTC = true;
            }
            showHardwareTooltip(
                "P-LAB I/O Board & RTC\n"
                "VIA window: $2000-$200F.\n\n"
                "Hardware conflict: Uncle Bernie's GEN2 HGR uses $2000-$3FFF.\n"
                "A real Apple-1 bus has no arbitration, so use one of these cards at a time.");
#if !POM1_IS_WASM
            if (ImGui::MenuItem("P-LAB Terminal Card", nullptr, &terminalCardEnabled)) {
                emulation->setTerminalCardEnabled(terminalCardEnabled);
                if (terminalCardEnabled) showTerminalCard = true;
            }
#endif
            if (ImGui::MenuItem("P-LAB MODEM BBS", nullptr, &wifiModemEnabled)) {
                emulation->setWiFiModemEnabled(wifiModemEnabled);
                if (wifiModemEnabled) {
                    showWiFiModem = true;
                    jukeBoxEnabled = false;
                }
            }
            showHardwareTooltip(
                "P-LAB MODEM BBS\n"
                "ACIA window: $B000-$B003.\n\n"
                "Plugging it unplugs Juke-Box when the Juke-Box ROM covers\n"
                "$B000-$B003.");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Presets")) {
            auto presetItem = [&](int i) {
                char ramLabel[24];
                std::snprintf(ramLabel, sizeof(ramLabel), "%d KB RAM", kMachinePresets[i].ramKB);
                if (ImGui::MenuItem(kMachinePresets[i].name, ramLabel))
                    applyMachineConfig(i);
            };
            presetItem(0);   // Bare Apple-1 (July 1976)
            presetItem(1);   // Apple-1 with ACI & Integer BASIC (Oct 1976)
            presetItem(2);   // Apple-1 + SWTPC GT-6144 Graphic Terminal (1976)
            ImGui::Separator();
            presetItem(3);   // Replica-1 with ACI, Krusader (Briel)
            presetItem(4);   // Replica-1 with CFFA1 & Applesoft Lite (Dreher)
            ImGui::Separator();
            // All P-LAB presets grouped together (indices 5..12)
            presetItem(5);   // P-LAB microSD + Applesoft Lite
            presetItem(6);   // P-LAB A1-SID
            presetItem(7);   // P-LAB A1-AUDIO Special Edition
            presetItem(8);   // P-LAB TMS9918
            presetItem(9);   // P-LAB I/O Board & RTC
            presetItem(10);  // P-LAB Wi-Fi Modem BBS
            presetItem(11);  // P-LAB Juke-Box (16 kB RAM)
            presetItem(12);  // P-LAB Multiplexing Fantasy
            ImGui::Separator();
            presetItem(13);  // Uncle Bernie's GEN2 HGR Color
            ImGui::Separator();
            presetItem(14);  // POM1 Multiplexing Fantasy (last -> banner)
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            ImGui::MenuItem("Welcome", nullptr, &showWelcome);
            if (ImGui::BeginMenu("Tutorials")) {
                // Software / getting started
                ImGui::MenuItem("Integer BASIC: write your first program",
                                nullptr, &showTutorialIntegerBasic);
                ImGui::MenuItem("Applesoft Lite: write your first program",
                                nullptr, &showTutorialApplesoft);
                ImGui::MenuItem("Krusader: assemble + disassemble + step",
                                nullptr, &showTutorialKrusader);
                ImGui::Separator();
                // 1976-era hardware
                ImGui::MenuItem("Cassette (ACI): load a program from tape",
                                nullptr, &showTutorialCassette);
                ImGui::MenuItem("SWTPC GT-6144: first commercial graphics card",
                                nullptr, &showTutorialGT6144);
                ImGui::MenuItem("SWTPC PR-40: Jobs' 40-column printer",
                                nullptr, &showTutorialPR40);
                ImGui::Separator();
                // Community hardware
                ImGui::MenuItem("Uncle Bernie's GEN2 HGR: NTSC color graphics",
                                nullptr, &showTutorialGEN2HGR);
                ImGui::MenuItem("CFFA1: ProDOS CompactFlash storage",
                                nullptr, &showTutorialCFFA1);
                ImGui::Separator();
                // P-LAB family
                ImGui::MenuItem("microSD: load and save programs",
                                nullptr, &showTutorialMicroSD);
                ImGui::MenuItem("A1-SID / A1-AUDIO SE: MOS 6581 / 8580 sound",
                                nullptr, &showTutorialSID);
                ImGui::MenuItem("P-LAB TMS9918: VDP graphics + sprites",
                                nullptr, &showTutorialTMS9918);
                ImGui::MenuItem("P-LAB I/O Board & RTC: DS3231 / analog I/O",
                                nullptr, &showTutorialA1IORTC);
                ImGui::MenuItem("P-LAB Juke-Box: EEPROM program library",
                                nullptr, &showTutorialJukeBox);
                ImGui::MenuItem("Wi-Fi Modem: connect to a telnet BBS",
                                nullptr, &showTutorialModemBBS);
#if !POM1_IS_WASM
                ImGui::MenuItem("P-LAB Terminal Card: drive POM1 via telnet",
                                nullptr, &showTutorialTerminalCard);
#endif
                ImGui::EndMenu();
            }
            ImGui::MenuItem("Hardware Reference", nullptr, &showHardwareReference);
            ImGui::MenuItem("Software Reference", nullptr, &showSoftwareReference);
            if (ImGui::BeginMenu("Photos")) {
                ImGui::MenuItem("Woz & Jobs (1976)", nullptr, &showWozJobsPhoto);
                ImGui::MenuItem("Apple-1 Demo Session (1976)", nullptr, &showWozJobsRectPhoto);
                ImGui::MenuItem("P-LAB TMS9918 Card (Photo)", nullptr, &showTmsBoardPhoto);
                ImGui::EndMenu();
            }
            ImGui::MenuItem("Ports & acknowledgements", nullptr, &showSpecialThanks);
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
        auto plugJukeBoxFromToolbar = [&]() {
            jukeBoxChipMode = JukeBox::ChipMode::Flash;
            cffa1Enabled = false;
            microSDEnabled = false;
            wifiModemEnabled = false;
            sidEnabled = false;
            codeTankEnabled = false;
            emulation->setCodeTankEnabled(false);
            evictMemoryMapRegionsForJukeBox();
            emulation->setJukeBoxChipMode(jukeBoxChipMode);
            emulation->setJukeBoxJumper(jukeBoxJumper);
            emulation->setJukeBoxEnabled(true);
            jukeBoxEnabled = true;
            showJukeBox = true;
            setStatusMessage("P-LAB Juke-Box plugged - type BD00R for Program Manager", 3.0f);
        };
        auto unplugJukeBoxFromToolbar = [&]() {
            emulation->setJukeBoxEnabled(false);
            jukeBoxEnabled = false;
            setStatusMessage("P-LAB Juke-Box unplugged", 2.0f);
        };
        auto plugCodeTankFromToolbar = [&]() {
            jukeBoxEnabled = false;
            emulation->setJukeBoxEnabled(false);
            emulation->setCodeTankJumper(codeTankJumper);
            emulation->setCodeTankEnabled(true);
            codeTankEnabled = true;
            showCodeTank = true;
            setStatusMessage("P-LAB CodeTank plugged: 28c256 at $4000-$7FFF", 3.0f);
        };
        auto unplugCodeTankFromToolbar = [&]() {
            emulation->setCodeTankEnabled(false);
            codeTankEnabled = false;
            setStatusMessage("P-LAB CodeTank unplugged", 2.0f);
        };
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
            if (microSDEnabled) {
                cffa1Enabled = false; // mutual exclusion
                jukeBoxEnabled = false;
            }
            setStatusMessage(microSDEnabled ? "P-LAB microSD Card plugged - type 8000R"
                                            : "P-LAB microSD Card unplugged", 2.0f);
        }
        ImGui::PopStyleColor();
        showHardwareTooltip(
            "P-LAB microSD Storage Card\n"
            "ROM/VIA window: $8000-$9FFF and $A000-$A00F.\n\n"
            "Click toggles the card. Plugging it unplugs CFFA1 and Juke-Box.");

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,
            cffa1Enabled ? ImVec4(0.2f, 0.4f, 0.8f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        if (ImGui::Button(ICON_FA_HARD_DRIVE, btnSize)) {
            cffa1Enabled = !cffa1Enabled;
            emulation->setCFFA1Enabled(cffa1Enabled);
            if (cffa1Enabled) {
                microSDEnabled = false; // mutual exclusion
                jukeBoxEnabled = false;
            }
            setStatusMessage(cffa1Enabled ? "CFFA1 CompactFlash plugged - type 9006R"
                                          : "CFFA1 CompactFlash unplugged", 2.0f);
        }
        ImGui::PopStyleColor();
        showHardwareTooltip(
            "CFFA1 CompactFlash Card\n"
            "ROM/register window: $9000-$AFFF.\n\n"
            "Click toggles the card. Plugging it unplugs microSD and Juke-Box.");

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,
            jukeBoxEnabled ? ImVec4(0.2f, 0.4f, 0.8f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        if (ImGui::Button("##jukeBoxToolbar", btnSize)) {
            if (jukeBoxEnabled) unplugJukeBoxFromToolbar();
            else plugJukeBoxFromToolbar();
        }
        drawToolbarDipChipIcon(ImGui::GetWindowDrawList(),
                               ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::PopStyleColor();
        showHardwareTooltip(
            "P-LAB Apple-1 Juke-Box\n"
            "ROM window: $4000-$BFFF or $8000-$BFFF. Bank latch: $CA00.\n\n"
            "Click toggles the Juke-Box card. Plugging it unplugs CodeTank,\n"
            "CFFA1, microSD, Wi-Fi Modem, and A1-SID. A1-AUDIO SE can coexist.");

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,
            codeTankEnabled ? ImVec4(0.2f, 0.4f, 0.8f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        if (ImGui::Button("CT", btnSize)) {
            if (codeTankEnabled) unplugCodeTankFromToolbar();
            else plugCodeTankFromToolbar();
        }
        ImGui::PopStyleColor();
        showHardwareTooltip(
            "P-LAB CodeTank 28c256 ROM\n"
            "Fixed ROM window: $4000-$7FFF. Pairs with the TMS9918 graphic card -\n"
            "the two cards do not collide and a CodeTank ROM can ship a TMS9918\n"
            "game that runs unchanged on real Apple-1 silicon.\n\n"
            "Click toggles CodeTank. Plugging it unplugs the Juke-Box.\n"
            "Use Hardware -> P-LAB CodeTank Library to pick which ROM to load.");

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
            if (sidEnabled) {
                sidSpecialEditionEnabled = false;
                jukeBoxEnabled = false;
            }
            setStatusMessage(sidEnabled ? "P-LAB A1-SID plugged" : "P-LAB A1-SID unplugged", 2.0f);
        }
        ImGui::PopStyleColor();
        showHardwareTooltip(
            "P-LAB A1-SID Sound Card\n"
            "SID registers: $C800-$CFFF.\n\n"
            "Click toggles the card. Plugging it unplugs A1-AUDIO SE and Juke-Box.");

        ImGui::SameLine();
        if (tms9918Enabled && showTMS9918)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.8f, 1.0f));
        else if (!tms9918Enabled)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        else
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_Button]);
        if (ImGui::Button(ICON_FA_TV, btnSize)) {
            if (!tms9918Enabled) {
                tms9918Enabled = true;
                showTMS9918 = true;
                emulation->setTMS9918Enabled(true);
                sidSpecialEditionEnabled = false;
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
        showHardwareTooltip(
            "P-LAB Graphic Card (TMS9918)\n"
            "VDP ports: $CC00/$CC01.\n\n"
            "Click toggles the output window/card. Plugging it unplugs A1-AUDIO SE.");

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
        showHardwareTooltip(
            "Uncle Bernie's GEN2 HGR Graphic Card\n"
            "Framebuffer: $2000-$3FFF.\n\n"
            "Hardware conflict with P-LAB I/O Board & RTC ($2000-$200F).\n"
            "Use one of these cards at a time on real hardware.");

        // --- SWTPC GT-6144 Graphic Terminal (1976) ---
        ImGui::SameLine();
        if (gt6144Enabled && showGT6144)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.8f, 1.0f));
        else if (!gt6144Enabled)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        else
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_Button]);
        if (ImGui::Button(ICON_FA_TABLE_CELLS, btnSize)) {
            if (!gt6144Enabled) {
                gt6144Enabled = true;
                showGT6144 = true;
                emulation->setGT6144Enabled(true);
                setStatusMessage("SWTPC GT-6144 plugged (64x96 framebuffer at $D00A)", 3.0f);
            } else {
                showGT6144 = !showGT6144;
                if (!showGT6144) {
                    gt6144Enabled = false;
                    emulation->setGT6144Enabled(false);
                    setStatusMessage("SWTPC GT-6144 unplugged", 2.0f);
                }
            }
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(gt6144Enabled ? "SWTPC GT-6144 Graphic Terminal (click to unplug)"
                                            : "Plug SWTPC GT-6144 Graphic Terminal (1976)");
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
        showHardwareTooltip(
            "P-LAB I/O Board & RTC\n"
            "VIA window: $2000-$200F.\n\n"
            "Hardware conflict with GEN2 HGR ($2000-$3FFF).\n"
            "Use one of these cards at a time on real hardware.");

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
                jukeBoxEnabled = false;
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
        showHardwareTooltip(
            "P-LAB MODEM BBS\n"
            "ACIA window: $B000-$B003.\n\n"
            "Click toggles the card. Plugging it unplugs Juke-Box when its ROM\n"
            "covers $B000-$B003.");

        // --- SWTPC PR-40 Printer (Jobs 1976) ---
        ImGui::SameLine();
        if (pr40Enabled && showPR40)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.8f, 1.0f));
        else if (!pr40Enabled)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        else
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_Button]);
        if (ImGui::Button(ICON_FA_PRINT, btnSize)) {
            if (!pr40Enabled) {
                pr40Enabled = true;
                showPR40 = true;
                emulation->setPR40Enabled(true);
                setStatusMessage("SWTPC PR-40 plugged (Jobs' $D012 sniff, DPDT to PB7)", 3.0f);
            } else {
                showPR40 = !showPR40;
                if (!showPR40) {
                    pr40Enabled = false;
                    emulation->setPR40Enabled(false);
                    setStatusMessage("SWTPC PR-40 unplugged", 2.0f);
                }
            }
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(pr40Enabled ? "SWTPC PR-40 Printer (click to unplug)"
                                          : "Plug SWTPC PR-40 Printer (Jobs 1976)");
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
            bool map = showMemoryMapGrid;
            if (map) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            if (ImGui::Button(ICON_FA_MAP, btnSize)) showMemoryMapGrid = !showMemoryMapGrid;
            if (map) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Memory Map Grid");
        }
        ImGui::SameLine();
        {
            bool bar = showMemoryBar;
            if (bar) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            if (ImGui::Button(ICON_FA_CHART_BAR, btnSize)) showMemoryBar = !showMemoryBar;
            if (bar) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Memory Map Bar");
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
