// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MainWindow_Menu.cpp — implementations of the three big top-of-window
// renderers: the menu bar, the toolbar, and the bottom status bar.
// Extracted from MainWindow_ImGui.cpp to keep the original .cpp manageable.

#include "MainWindow_ImGui.h"
#include "MainWindow_Internal.h"
#include "NativeFileDialog.h"
#include "POM1Build.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "IconsFontAwesome6.h"

#include <GLFW/glfw3.h>

#include <cstdio>
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
            // setJukeBoxEnabled(true) → setMicroSDEnabled(false) → setIECCardEnabled(false)
            // on the bus; mirror the IEC UI flag or its window keeps rendering an
            // unplugged card (renderIECCardWindow gates on iecCardEnabled).
            iecCardEnabled = false;
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
            // CodeTank is a daughterboard of the TMS9918 Graphic Card — auto-
            // plug the host so the UI flags stay in sync with what Memory's
            // setCodeTankEnabled would do anyway. Also evict the Juke-Box
            // (overlapping $4000-$7FFF window) and the microSD (its Applesoft
            // Lite EEPROM window $6000-$7FFF sits inside CodeTank's) — Memory
            // does the bus side, mirror the UI flags (incl. the IEC add-on
            // that cascades off microSD).
            jukeBoxEnabled = false;
            emulation->setJukeBoxEnabled(false);
            microSDEnabled = false;
            iecCardEnabled = false;
            if (!tms9918Enabled) {
                tms9918Enabled = true;
                showTMS9918 = true;
                emulation->setTMS9918Enabled(true);
                sidSpecialEditionEnabled = false; // TMS9918 evicts SE
            }
            emulation->setCodeTankJumper(codeTankJumper);
            emulation->setCodeTankEnabled(true);
            codeTankEnabled = true;
            showCodeTankLibrary = true;
            setStatusMessage("P-LAB CodeTank plugged on TMS9918 host: $4000-$7FFF", 3.0f);
        };
        auto unplugCodeTankFromUi = [&]() {
            emulation->setCodeTankEnabled(false);
            codeTankEnabled = false;
            showCodeTankLibrary = false;
            codeTankPendingWozRunAt = 0.0;
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
            if (ImGui::MenuItem("Load Snapshot...")) {
                loadSnapshot();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Restore a previously saved POM1 state from snapshots/.\n"
                                  "Captures RAM + card-enabled flags + per-peripheral payload.");
            if (ImGui::MenuItem("Save Snapshot...")) {
                saveSnapshot();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Save current POM1 state to snapshots/ as a versioned .snap file.\n"
                                  "Same format as the --snapshot-save CLI flag.");
            ImGui::Separator();
            ImGui::MenuItem("Cassette Deck", nullptr, &showCassetteDeck);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Realistic procedural cassette deck.\nPiano keys, mechanical counter, live transport.");
            if (ImGui::MenuItem("P-LAB CodeTank Library...", nullptr, &showCodeTankLibrary)) {
                if (showCodeTankLibrary) plugCodeTankFromUi();
            }
            showHardwareTooltip(
                "CodeTank Library — P-LAB game-console cartridge (ROM window\n"
                "$4000-$7FFF on the TMS9918 host). Opens the window and plugs TMS9918 +\n"
                "CodeTank if needed. 32 KB ROM in roms/codetank/ (two 16 KB banks).\n"
                "After a Run (lower or upper bank), 4000R is sent to the Woz monitor.");
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
#if !POM1_IS_WASM
            ImGui::MenuItem("State Rewind...", nullptr, &showRewindTimeline);  // desktop only
#endif
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Settings")) {
            if (ImGui::MenuItem("Display Options")) {
                configScreen();
            }
            if (ImGui::MenuItem("CRT Effects (sliders)...", nullptr,
                                &showCrtSettings)) {}
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Universal shader CRT look for the Apple-1 screen AND the\n"
                    "graphics cards (GEN2 HGR / TMS9918 / GT-6144): scanlines,\n"
                    "phosphor persistence + gamma, shadow mask, barrel, vignette\n"
                    "and brightness/contrast/saturation/hue. On by default;\n"
                    "all settings are remembered across sessions.\n"
                    "(OpenGL backend only; no effect on the macOS Metal backend.)");
            if (ImGui::MenuItem("Save UI Windows Layout")) {
                savePresetLayout(activePresetIndex);
                setStatusMessage("UI windows layout saved for this preset", 2.5f);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Snapshot the current desktop for THIS preset: every window's\n"
                    "position + size AND which windows are open (tools, peripheral\n"
                    "panels, tutorials, info/photo windows). The whole arrangement\n"
                    "reappears when you return to this preset. Writes\n"
                    "ini/imgui_preset_%02d.ini (geometry + open windows) + preset_%02d.size;\n"
                    "copy those into ini_defaults/ to ship them as the default baseline.",
                    activePresetIndex, activePresetIndex);
            if (ImGui::BeginMenu("Reset Windows Layout")) {
                if (ImGui::MenuItem("This preset")) {
                    resetActivePresetLayout();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Restore every window's size + position AND the main OS window\n"
                        "to this preset's factory layout. Deletes the saved\n"
                        "ini/imgui_preset_%02d.ini + preset_%02d.size for the active preset.",
                        activePresetIndex, activePresetIndex);
                if (ImGui::MenuItem("All presets")) {
                    resetAllPresetLayouts();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Wipe every preset's saved layout (all ini/imgui_preset_NN.ini\n"
                        "+ preset_NN.size), re-seed the factory defaults, and reset the\n"
                        "active preset's windows now. Other presets revert on next load.");
                ImGui::EndMenu();
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
            {
                bool navMode = uiNavMode_;
                if (ImGui::MenuItem("UI keyboard navigation", shortcutLabel(GLFW_KEY_F10), &navMode))
                    setUiNavMode(navMode);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Accessibility: Tab / arrows / Space / Enter drive the POM1 interface\n"
                        "instead of typing into the Apple-1. Toggle any time with F10.");
            }
            if (ImGui::BeginMenu("UI Theme")) {
                const char* names[] = { "Dark (default)", "Light", "High contrast" };
                for (int t = 0; t < 3; ++t) {
                    if (ImGui::MenuItem(names[t], nullptr, uiTheme_ == t) && uiTheme_ != t) {
                        applyUiTheme(t);
                        saveUiSettings();
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            {
                // Startup boot preference. Default (neither box) = boot POM1
                // Fantasy. "Boot straight into this profile" pins the current
                // machine; "Show profile chooser" opts back into the picker.
                // The two are mutually exclusive (same ini/startup file).
                int startupPreset = -1;
                bool autoStart = readStartupPreset(startupPreset);
                bool showChooser = startupShowsChooser();

                bool autoToggle = autoStart;
                if (ImGui::MenuItem("Boot straight into this profile", nullptr, &autoToggle)) {
                    writeStartupPreset(autoToggle ? activePresetIndex : -1);
                    setStatusMessage(autoToggle
                        ? "POM1 will boot straight into this profile"
                        : "POM1 will boot into POM1 Fantasy by default", 3.0f);
                }
                if (ImGui::IsItemHovered()) {
                    const char* n = getPresetName(startupPreset);
                    if (autoStart && n)
                        ImGui::SetTooltip("Currently booting straight into \"%s\".\n"
                                          "Uncheck to return to the default (POM1 Fantasy).", n);
                    else
                        ImGui::SetTooltip("Check to always boot into the CURRENT profile.\n"
                                          "Default (unchecked) boots POM1 Fantasy.");
                }

                bool chooserToggle = showChooser;
                if (ImGui::MenuItem("Show profile chooser at startup", nullptr, &chooserToggle)) {
                    writeStartupChooser(chooserToggle);
                    setStatusMessage(chooserToggle
                        ? "POM1 will show the profile chooser at startup"
                        : "POM1 will boot into POM1 Fantasy by default", 3.0f);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Show the boot profile screen every launch.\n"
                                      "Default (unchecked) boots POM1 Fantasy straight away.");

                if (ImGui::MenuItem("Profile chooser now...")) {
                    showProfileChooser = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Reopen the boot profile screen (picking a profile restarts the machine).");
            }
#if !POM1_IS_WASM
            {
                bool nativeDialogs = pom1::NativeFileDialog::isEnabled();
                if (ImGui::MenuItem("Native OS file dialogs", nullptr, &nativeDialogs)) {
                    pom1::NativeFileDialog::setEnabled(nativeDialogs);
                    setStatusMessage(nativeDialogs
                                         ? "File dialogs: native OS picker"
                                         : "File dialogs: fast in-process browser", 2.5f);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "On (default): Load/Save use the OS-native file picker (Finder /\n"
                        "Explorer / zenity) - integrated, but slower to appear (separate\n"
                        "sandboxed process, cold-start lag on first open).\n"
                        "Off: POM1's instant in-process browser. Applies to every Load/Save\n"
                        "(memory, tape, snapshot, DevBench, Paint editors).");
            }
#endif
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
            // P-LAB A1-SID card version + full register address map. The two card
            // versions are mutually exclusive (same SID chip, different I/O window):
            // standard A1-SID at $C800-$CFFF vs A1-AUDIO Special Edition at
            // $CC00-$CC1F. Selecting a version plugs it (the backend evicts the
            // other); the register list reflects the active version's base.
            if (ImGui::BeginMenu("A1-SID version & addresses")) {
                ImGui::TextDisabled("Card version (I/O window)");
                if (ImGui::MenuItem("A1-SID  -  $C800-$CFFF", nullptr, sidEnabled)) {
                    sidEnabled = true;
                    sidSpecialEditionEnabled = false;
                    emulation->setSIDEnabled(true);
                    setStatusMessage("P-LAB A1-SID selected: $C800-$CFFF", 2.5f);
                }
                if (ImGui::MenuItem("A1-AUDIO Special Edition  -  $CC00-$CC1F",
                                    nullptr, sidSpecialEditionEnabled)) {
                    sidSpecialEditionEnabled = true;
                    sidEnabled = false;
                    emulation->setSIDSpecialEditionEnabled(true);
                    setStatusMessage("P-LAB A1-AUDIO SE selected: $CC00-$CC1F", 2.5f);
                }
                ImGui::Separator();

                // 29 SID registers ($00-$1C); regs $19-$1C are read-only.
                static const char* const kSidRegNames[29] = {
                    "V1 FREQ LO",  "V1 FREQ HI",  "V1 PW LO",    "V1 PW HI",
                    "V1 CONTROL",  "V1 ATK/DEC",  "V1 SUS/REL",
                    "V2 FREQ LO",  "V2 FREQ HI",  "V2 PW LO",    "V2 PW HI",
                    "V2 CONTROL",  "V2 ATK/DEC",  "V2 SUS/REL",
                    "V3 FREQ LO",  "V3 FREQ HI",  "V3 PW LO",    "V3 PW HI",
                    "V3 CONTROL",  "V3 ATK/DEC",  "V3 SUS/REL",
                    "FILTER FC LO","FILTER FC HI","RES/FILT",    "MODE/VOL",
                    "POT X (ro)",  "POT Y (ro)",  "OSC3/RND (ro)","ENV3 (ro)",
                };
                const unsigned base = sidSpecialEditionEnabled ? 0xCC00u : 0xC800u;
                ImGui::TextDisabled(sidSpecialEditionEnabled
                    ? "A1-AUDIO SE registers (base $CC00, addr & $1F)"
                    : "A1-SID registers (base $C800, addr & $1F)");
                for (int i = 0; i < 29; ++i)
                    ImGui::Text("  $%04X  R%02d  %s", base + i, i, kSidRegNames[i]);
                ImGui::EndMenu();
            }

            // GEN2 HGR colour decode — the HIRES artifact-colour pipeline. Two
            // choices, also mirrored in the GEN2 window's right-click popup:
            // the calibrated MAME LUT (default, fast path) vs OpenEmulator's
            // composite NTSC demodulator run on the CPU (softer, hardware-
            // faithful). Applied immediately via setRenderMode; gen2RenderMode
            // keeps the popup + the per-frame sync in agreement.
            if (ImGui::BeginMenu("GEN2 HGR colour decode")) {
                if (ImGui::MenuItem("NTSC MAME LUT (fast, default)", nullptr,
                                    gen2RenderMode == 0)) {
                    gen2RenderMode = 0;
                    graphicsCard.setRenderMode(GraphicsCard::RenderMode::MameLut);
                    setStatusMessage("GEN2 HGR: NTSC MAME artifact-colour LUT", 2.5f);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Calibrated 128-entry artifact-colour lookup table\n"
                        "(MAME apple2video.cpp, medium-colour). The default\n"
                        "fast path.");
                if (ImGui::MenuItem("Composite OpenEmulator (CPU)", nullptr,
                                    gen2RenderMode == 1)) {
                    gen2RenderMode = 1;
                    graphicsCard.setRenderMode(GraphicsCard::RenderMode::CompositeOECpu);
                    setStatusMessage("GEN2 HGR: OpenEmulator composite NTSC (CPU)", 2.5f);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Builds the 14.318 MHz composite signal from the HGR\n"
                        "bitstream and runs OpenEmulator's 17-tap FIR NTSC\n"
                        "demodulator on the CPU (no GLSL) - softer, physically\n"
                        "faithful mid-tones. Same 280x192 output as the LUT.");
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("DevBench")) {
            // POM1 Bench. Desktop: full cc65 (asm/C) + Wozmon-hex authoring. Web
            // (WASM): no cc65 toolchain in the browser, so the Bench is restricted
            // to the Wozmon-hex target (editor + Upload + Serial Monitor) — see
            // Pom1BenchHost, whose targets() is Woz-hex-only under POM1_IS_WASM.
            ImGui::MenuItem("POM1 Bench (sketch editor)...", nullptr, &showBench);
            ImGui::MenuItem("Telemetry Side Channel...", nullptr, &showTelemetry);
            ImGui::Separator();
            // --- TMS9918 group: VDP inspector + paint + sprite editors ---------
            // Always available here (unlike the card windows, which only appear
            // when the TMS9918 is plugged) — a dev-side VDP inspector. Opening it
            // plugs the TMS9918 so there is a live VDP to inspect.
            if (ImGui::MenuItem("TMS9918 VDP Inspector...", nullptr, &showTMS9918Inspector)) {
                if (showTMS9918Inspector && !tms9918Enabled) {
                    tms9918Enabled = true;
                    showTMS9918 = true;
                    emulation->setTMS9918Enabled(true);
                    sidSpecialEditionEnabled = false;   // TMS9918 evicts A1-AUDIO SE
                    setStatusMessage("TMS9918 plugged for VDP Inspector", 2.0f);
                }
            }
            // TMS9918 paint editor — draws live into the P-LAB Graphic Card VRAM
            // (Graphics II bitmap / Multicolor). Opening it plugs the TMS9918 so
            // there is a live card to paint into (the render loop also guards this).
            if (ImGui::MenuItem("TMS9918 Paint Editor...", nullptr, &showTMSPaintEditor)) {
                if (showTMSPaintEditor && !tms9918Enabled) {
                    tms9918Enabled = true;
                    showTMS9918 = true;
                    emulation->setTMS9918Enabled(true);
                    sidSpecialEditionEnabled = false;   // TMS9918 evicts A1-AUDIO SE
                    setStatusMessage("TMS9918 card plugged for TMS9918 Paint Editor", 2.0f);
                }
            }
            // TMS9918 sprite editor — draws monochrome 8×8 / 16×16 sprite patterns
            // into the P-LAB Graphic Card VRAM and places the sprite live via the
            // SAT. Opening it plugs the TMS9918 (the render loop also guards this).
            if (ImGui::MenuItem("TMS9918 Sprite Editor...", nullptr, &showTMSSpriteEditor)) {
                if (showTMSSpriteEditor && !tms9918Enabled) {
                    tms9918Enabled = true;
                    showTMS9918 = true;
                    emulation->setTMS9918Enabled(true);
                    sidSpecialEditionEnabled = false;   // TMS9918 evicts A1-AUDIO SE
                    setStatusMessage("TMS9918 card plugged for TMS9918 Sprite Editor", 2.0f);
                }
            }
            ImGui::Separator();
            // --- HGR (GEN2) group: paint + sprite editors ----------------------
            // HGR paint editor — draws live into the GEN2 HGR framebuffer with
            // faithful NTSC artifact colours. Opening it plugs the GEN2 card so
            // there is a live framebuffer to paint into (the render loop also
            // guards this, but enabling here makes the menu action immediate).
            if (ImGui::MenuItem("HGR Paint Editor...", nullptr, &showHGRPaintEditor)) {
                if (showHGRPaintEditor) {
                    showBench = false;   // HGR Painter and POM1 Bench are mutually exclusive
                    if (!graphicsCardEnabled) {
                        graphicsCardEnabled = true;
                        showGraphicsCard = true;
                        emulation->setHgrFramebufferAttached(true);
                        setStatusMessage("GEN2 HGR card plugged for HGR Paint Editor", 2.0f);
                    }
                }
            }
            // HGR sprite/shape editor — edits a byte-aligned HIRES bitmap (Apple II
            // has no hardware sprites), previews it over the live page and stamps
            // the raw bytes. Opening it plugs the GEN2 card (render loop also guards).
            if (ImGui::MenuItem("HGR Sprite Editor...", nullptr, &showHGRSpriteEditor)) {
                if (showHGRSpriteEditor && !graphicsCardEnabled) {
                    graphicsCardEnabled = true;
                    showGraphicsCard = true;
                    emulation->setHgrFramebufferAttached(true);
                    setStatusMessage("GEN2 HGR card plugged for HGR Sprite Editor", 2.0f);
                }
            }
            ImGui::Separator();
            // --- Audio editors --------------------------------------------------
            // Beeper SFX editor — draws + auditions a 1-bit sound effect through
            // the ACI speaker ($C030). Opening it plugs the ACI (render loop guards).
            if (ImGui::MenuItem("Beeper SFX Editor...", nullptr, &showSfxEditor)) {
                if (showSfxEditor && !aciEnabled) {
                    aciEnabled = true;
                    emulation->setACIEnabled(true);
                    setStatusMessage("ACI plugged for Beeper SFX Editor", 2.0f);
                }
            }
            // SID tracker — pattern grid + ADSR/filter instrument, auditioned on
            // the live A1-SID chip. Opening it plugs the A1-SID (render loop guards).
            if (ImGui::MenuItem("SID Tracker...", nullptr, &showSidTracker)) {
                if (showSidTracker && !sidEnabled) {
                    sidEnabled = true;
                    sidSpecialEditionEnabled = false;
                    // setSIDEnabled also evicts the Juke-Box on the bus
                    // ($CA00 sits inside the SID window) — mirror the flag
                    // like the Hardware-menu A1-SID item does.
                    jukeBoxEnabled = false;
                    emulation->setSIDEnabled(true);
                    setStatusMessage("A1-SID plugged for SID Tracker", 2.0f);
                }
            }
            ImGui::Separator();
            // Strict-mode toggle, drop-diagnostics dump and counter reset all
            // moved into the Silicon Strict Inspector window — single home for
            // the whole silicon-fidelity surface.
            if (ImGui::MenuItem("Silicon Strict Inspector...",
                                nullptr, &showSiliconStrictWindow)) {
                // Window state is now bound to the menu's checkmark — no
                // extra action needed; the render loop picks it up.
            }
            showHardwareTooltip(
                "Silicon Strict Inspector\n\n"
                "Opens a window with the strict-mode toggle, silicon-fidelity\n"
                "configuration (VRAM and RAM cold-boot noise, Juke-Box EEPROM\n"
                "write-cycle timing) and live drop diagnostics (by port,\n"
                "display phase, slot table, top PC sites).\n\n"
                "Goal: an emulator faithful enough that silicon-side bugs\n"
                "(uninitialised VRAM ghosts, assume-zero-RAM crashes,\n"
                "timing-tight VDP loops, EEPROM write losses) surface in POM1\n"
                "under strict mode, so you can fix them before flashing the\n"
                "cartridge.");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Hardware")) {
            // The Apple-1 ASCII keyboard is real input hardware, not a photo:
            // clicking its keycaps drives the same key queue as the physical
            // keyboard (incl. CLEAR SCREEN and the red RESET). It heads the menu.
            ImGui::MenuItem("Apple-1 ASCII Keyboard", nullptr, &showKeyboardPhoto);
            ImGui::Separator();
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
                if (!gateStrictPlug("GEN2", graphicsCardEnabled)) {
                    if (graphicsCardEnabled) showGraphicsCard = true;
                    emulation->setHgrFramebufferAttached(graphicsCardEnabled);
                }
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
                    // setCFFA1Enabled → setMicroSDEnabled(false) → setIECCardEnabled(false)
                    // on the bus; mirror the IEC UI flag or its window keeps rendering
                    // an unplugged card (renderIECCardWindow gates on iecCardEnabled).
                    iecCardEnabled = false;
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
            if (ImGui::MenuItem("P-LAB microSD Storage Card", nullptr, &microSDEnabled)) {
                emulation->setMicroSDEnabled(microSDEnabled);
                if (microSDEnabled) {
                    cffa1Enabled = false; // sync UI
                    jukeBoxEnabled = false;
                    if (codeTankEnabled) {
                        // Memory evicts CodeTank ($6000-$7FFF Applesoft Lite
                        // overlap); mirror UI. The TMS9918 host stays plugged.
                        codeTankEnabled = false;
                        showCodeTankLibrary = false;
                        codeTankPendingWozRunAt = 0.0;
                    }
                } else {
                    iecCardEnabled = false; // cascade
                }
            }
            showHardwareTooltip(
                "P-LAB microSD Storage Card\n"
                "ROM/VIA window: $8000-$9FFF, $A000-$A00F, and the Applesoft\n"
                "Lite EEPROM at $6000-$7FFF.\n\n"
                "Plugging it unplugs CFFA1, Juke-Box, and the CodeTank\n"
                "daughterboard, because their ROM windows overlap.");
            {
                bool gateOk = microSDEnabled;
                ImGui::BeginDisabled(!gateOk);
                if (ImGui::MenuItem("P-LAB IEC Add-on (microSD daughterboard)",
                                    nullptr, &iecCardEnabled)) {
                    emulation->setIECCardEnabled(iecCardEnabled);
                    if (iecCardEnabled) microSDEnabled = true; // cascade-on
                }
                ImGui::EndDisabled();
                showHardwareTooltip(
                    "P-LAB IEC daughterboard for the microSD Storage Card.\n"
                    "Drives the Commodore IEC bus on unused 65C22 PORTB pins\n"
                    "(bits 2-6) via an SN7406 inverter. Backed by a virtual\n"
                    "1541 mounted from disks/iec/dev8.d64 (174 848 B).\n\n"
                    "Firmware: SD CARD OS 1.3 (nippur72/apple1-sdcard, CC BY 4.0).\n"
                    "Type @$ for catalogue, @L NAME to LOAD, @S NAME a b to SAVE,\n"
                    "@CMD/@ERR for DOS commands and the error channel.\n\n"
                    "Requires microSD plugged.");
            }
            if (ImGui::MenuItem("P-LAB A1-SID Sound Card (SID @ $C800)", nullptr, &sidEnabled)) {
                if (!gateStrictPlug("A1-SID", sidEnabled)) {
                    emulation->setSIDEnabled(sidEnabled);
                    // Prototype and SE share the same MOS chip — plugging one
                    // auto-unplugs the other on the backend; mirror that here.
                    if (sidEnabled) {
                        sidSpecialEditionEnabled = false;
                        jukeBoxEnabled = false;
                    }
                }
            }
            showHardwareTooltip(
                "P-LAB A1-SID Sound Card\n"
                "SID registers: $C800-$CFFF.\n\n"
                "Plugging it unplugs A1-AUDIO SE (same SID chip) and Juke-Box\n"
                "($CA00 bank latch sits inside the SID window).");
            if (ImGui::MenuItem("A1-AUDIO Special Edition (SID @ $CC00)", nullptr, &sidSpecialEditionEnabled)) {
                if (!gateStrictPlug("A1-AUDIO-SE", sidSpecialEditionEnabled)) {
                    emulation->setSIDSpecialEditionEnabled(sidSpecialEditionEnabled);
                    if (sidSpecialEditionEnabled) {
                        // Mutually exclusive with the prototype SID and with TMS9918.
                        sidEnabled = false;
                        tms9918Enabled = false;
                        if (codeTankEnabled) {
                            // setSIDSpecialEditionEnabled → setTMS9918Enabled(false)
                            // cascade-unplugs CodeTank (its daughterboard); mirror
                            // the UI flags like the TMS9918 toggle path does.
                            codeTankEnabled = false;
                            showCodeTankLibrary = false;
                            codeTankPendingWozRunAt = 0.0;
                        }
                    }
                }
            }
            showHardwareTooltip(
                "Claudio Parmigiani's 10-unit A1-AUDIO Special Edition\n"
                "SID registers: $CC00-$CC1F.\n\n"
                "Plugging it unplugs A1-SID (same SID chip) and TMS9918\n"
                "($CC00/$CC01 overlap). Juke-Box can coexist.");
            if (ImGui::MenuItem("P-LAB Graphic Card (TMS9918)", nullptr, &tms9918Enabled)) {
                if (!gateStrictPlug("TMS9918", tms9918Enabled)) {
                    emulation->setTMS9918Enabled(tms9918Enabled);
                    if (tms9918Enabled) {
                        showTMS9918 = true;
                        sidSpecialEditionEnabled = false; // mutually exclusive
                    } else if (codeTankEnabled) {
                        // CodeTank is a daughterboard of the TMS9918 — Memory's
                        // setTMS9918Enabled cascade-disabled it; mirror in UI flags.
                        codeTankEnabled = false;
                        showCodeTankLibrary = false;
                        codeTankPendingWozRunAt = 0.0;
                        setStatusMessage("P-LAB CodeTank unplugged with TMS9918 host", 2.0f);
                    }
                }
            }
            showHardwareTooltip(
                "P-LAB Graphic Card (TMS9918)\n"
                "VDP ports: $CC00/$CC01.\n\n"
                "Plugging it unplugs A1-AUDIO SE, because the two cards share\n"
                "the $CC00 control/data window. Unplugging it cascade-unplugs\n"
                "the CodeTank daughterboard.");
            if (ImGui::MenuItem("P-LAB CodeTank ROM (TMS9918 daughterboard)",
                                nullptr, codeTankEnabled)) {
                if (codeTankEnabled) unplugCodeTankFromUi();
                else plugCodeTankFromUi();
            }
            showHardwareTooltip(
                "P-LAB CodeTank ROM (TMS9918 daughterboard)\n"
                "28c256 EEPROM; fixed ROM window: $4000-$7FFF.\n"
                "The board jumper selects lower or upper 16 kB of the 32 kB device.\n\n"
                "Daughterboard of the TMS9918 Graphic Card — no edge connector.\n"
                "Plugging it auto-plugs the TMS9918 host (and evicts A1-AUDIO SE,\n"
                "the Juke-Box, and the microSD — its Applesoft Lite window at\n"
                "$6000-$7FFF sits inside the CodeTank ROM). Unplugging the\n"
                "TMS9918 cascade-unplugs CodeTank.");
            if (ImGui::MenuItem("P-LAB I/O Board & RTC", nullptr, &a1ioRtcEnabled)) {
                if (!gateStrictPlug("A1-IO-RTC", a1ioRtcEnabled)) {
                    emulation->setA1IO_RTCEnabled(a1ioRtcEnabled);
                    if (a1ioRtcEnabled) showA1IO_RTC = true;
                }
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
            if (ImGui::MenuItem("P-LAB MODEM BBS WIFI", nullptr, &wifiModemEnabled)) {
                emulation->setWiFiModemEnabled(wifiModemEnabled);
                if (wifiModemEnabled) {
                    showWiFiModem = true;
                    jukeBoxEnabled = false;
                }
            }
            showHardwareTooltip(
                "P-LAB MODEM BBS WIFI\n"
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
            // Re-open the full-screen boot profile chooser mid-session — a
            // one-click way back to "pick a machine / language / editor" without
            // restarting POM1. Any pick there dismisses it and applies the config.
            if (ImGui::MenuItem("Profile Chooser...")) {
                showProfileChooser = true;
            }
            ImGui::Separator();
            // Greyed, non-clickable section titles (DevBench profiles first,
            // then the real machine configurations).
            ImGui::TextDisabled("Apple-1 Development");
            presetItem(0);   // Apple-1 CC65 Development Bench
            presetItem(1);   // Apple-1 TMS9918 Development Bench
            presetItem(2);   // Apple-1 GEN2 HGR Development Bench
            ImGui::Separator();
            ImGui::TextDisabled("Apple-1 Configurations");
            presetItem(3);   // Bare Apple-1 (July 1976)
            presetItem(4);   // Apple-1 with ACI & Integer BASIC (Oct 1976)
            presetItem(5);   // Apple-1 + SWTPC GT-6144 Graphic Terminal (1976)
            ImGui::Separator();
            presetItem(6);   // Replica-1 with ACI, Krusader (Briel)
            presetItem(7);   // Replica-1 with CFFA1 & Applesoft Lite (Dreher)
            ImGui::Separator();
            // P-LAB presets grouped together (indices 8..10). The A1-SID,
            // I/O & RTC, Wi-Fi Modem and Juke-Box cards no longer have a
            // dedicated preset — plug them from the Hardware menu or via
            // --enable on any preset.
            presetItem(8);   // P-LAB microSD + Applesoft Lite
            presetItem(9);   // P-LAB TMS9918 + CodeTank
            presetItem(10);  // P-LAB Multiplexing Fantasy
            ImGui::Separator();
            presetItem(11);  // Uncle Bernie's GEN2 HGR Color
            ImGui::Separator();
            presetItem(12);  // POM1 Multiplexing Fantasy (last -> banner)
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
            ImGui::MenuItem("Keyboard Shortcuts", nullptr, &showShortcutsHelp);
            if (ImGui::BeginMenu("Photos")) {
                ImGui::MenuItem("Woz & Jobs (1976)", nullptr, &showWozJobsPhoto);
                ImGui::MenuItem("Apple-1 Demo Session (1976)", nullptr, &showWozJobsRectPhoto);
                ImGui::MenuItem("P-LAB TMS9918 Card (Photo)", nullptr, &showTmsBoardPhoto);
                ImGui::MenuItem("GEN2 Video Workbench (Photo)", nullptr, &showGen2WorkbenchPhoto);
                ImGui::MenuItem("Steve Wozniak (Photo)", nullptr, &showWozPhoto);
                ImGui::MenuItem("Apple-1 (Copson) Photo", nullptr, &showCopsonApple1Photo);
                ImGui::MenuItem("Apple-1 Happy Woz (Photo)", nullptr, &showHappyWozPhoto);
                ImGui::MenuItem("P-LAB TMS9918 Board (Photo)", nullptr, &showPlabTms9918Photo);
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
            // setJukeBoxEnabled(true) → setMicroSDEnabled(false) → setIECCardEnabled(false)
            // on the bus; mirror the IEC UI flag or its window keeps rendering an
            // unplugged card (renderIECCardWindow gates on iecCardEnabled).
            iecCardEnabled = false;
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
            // Memory's setCodeTankEnabled evicts the microSD ($6000-$7FFF
            // Applesoft Lite overlap) and cascade-drops the IEC add-on —
            // mirror the UI flags.
            microSDEnabled = false;
            iecCardEnabled = false;
            // CodeTank is a daughterboard of the TMS9918 Graphic Card — auto-
            // plug the host so UI flags match what Memory just did.
            if (!tms9918Enabled) {
                tms9918Enabled = true;
                showTMS9918 = true;
                emulation->setTMS9918Enabled(true);
                sidSpecialEditionEnabled = false;
            }
            emulation->setCodeTankJumper(codeTankJumper);
            emulation->setCodeTankEnabled(true);
            codeTankEnabled = true;
            showCodeTankLibrary = true;
            setStatusMessage("P-LAB CodeTank plugged on TMS9918 host: $4000-$7FFF", 3.0f);
        };
        auto unplugCodeTankFromToolbar = [&]() {
            emulation->setCodeTankEnabled(false);
            codeTankEnabled = false;
            showCodeTankLibrary = false;
            codeTankPendingWozRunAt = 0.0;
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
            codeTankEnabled ? ImVec4(0.2f, 0.4f, 0.8f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        if (ImGui::Button("##codeTankToolbar", btnSize)) {
            if (codeTankEnabled) unplugCodeTankFromToolbar();
            else plugCodeTankFromToolbar();
        }
        drawToolbarTankIcon(ImGui::GetWindowDrawList(),
                            ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::PopStyleColor();
        showHardwareTooltip(
            "P-LAB CodeTank 28c256 ROM (daughterboard)\n"
            "Fixed ROM window: $4000-$7FFF. Daughterboard of the TMS9918 Graphic\n"
            "Card - has no edge connector, cannot exist standalone on the bus.\n\n"
            "Click plugs CodeTank, auto-plugs the TMS9918 host (evicts A1-AUDIO SE +\n"
            "Juke-Box + microSD), and opens the CodeTank Library. Click again to unplug.\n"
            "Unplugging the TMS9918 toolbar/menu entry also cascade-unplugs CodeTank.");

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,
            microSDEnabled ? ImVec4(0.2f, 0.4f, 0.8f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        if (ImGui::Button(ICON_FA_SD_CARD, btnSize)) {
            microSDEnabled = !microSDEnabled;
            emulation->setMicroSDEnabled(microSDEnabled);
            if (microSDEnabled) {
                cffa1Enabled = false; // mutual exclusion
                jukeBoxEnabled = false;
                if (codeTankEnabled) {
                    // Memory evicts CodeTank ($6000-$7FFF Applesoft Lite
                    // overlap); mirror UI. The TMS9918 host stays plugged.
                    codeTankEnabled = false;
                    showCodeTankLibrary = false;
                    codeTankPendingWozRunAt = 0.0;
                }
            } else {
                // setMicroSDEnabled(false) → setIECCardEnabled(false) on the bus;
                // mirror the IEC UI flag or its window keeps rendering an unplugged
                // card (renderIECCardWindow gates on iecCardEnabled). Matches the
                // Hardware-menu microSD path.
                iecCardEnabled = false;
            }
            setStatusMessage(microSDEnabled ? "P-LAB microSD Card plugged - type 8000R"
                                            : "P-LAB microSD Card unplugged", 2.0f);
        }
        ImGui::PopStyleColor();
        showHardwareTooltip(
            "P-LAB microSD Storage Card\n"
            "ROM/VIA window: $8000-$9FFF, $A000-$A00F, and the Applesoft\n"
            "Lite EEPROM at $6000-$7FFF.\n\n"
            "Click toggles the card. Plugging it unplugs CFFA1, Juke-Box,\n"
            "and the CodeTank daughterboard.");

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,
            cffa1Enabled ? ImVec4(0.2f, 0.4f, 0.8f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        if (ImGui::Button(ICON_FA_HARD_DRIVE, btnSize)) {
            cffa1Enabled = !cffa1Enabled;
            emulation->setCFFA1Enabled(cffa1Enabled);
            if (cffa1Enabled) {
                microSDEnabled = false; // mutual exclusion
                jukeBoxEnabled = false;
                // setCFFA1Enabled → setMicroSDEnabled(false) → setIECCardEnabled(false)
                // on the bus; mirror the IEC UI flag or its window keeps rendering
                // an unplugged card. Matches the Hardware-menu CFFA1 path.
                iecCardEnabled = false;
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
        if (ImGui::Button(ICON_FA_VOLUME_HIGH, btnSize)) {
            sidEnabled = !sidEnabled;
            // Honour the silicon-strict Parmigiani gate like the Hardware menu —
            // gateStrictPlug reverts sidEnabled and refuses on a bus conflict.
            if (!gateStrictPlug("A1-SID", sidEnabled)) {
                emulation->setSIDEnabled(sidEnabled);
                if (sidEnabled) {
                    sidSpecialEditionEnabled = false;
                    jukeBoxEnabled = false;
                }
                setStatusMessage(sidEnabled ? "P-LAB A1-SID plugged" : "P-LAB A1-SID unplugged", 2.0f);
            }
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
                if (!gateStrictPlug("TMS9918", tms9918Enabled)) {
                    showTMS9918 = true;
                    emulation->setTMS9918Enabled(true);
                    sidSpecialEditionEnabled = false;
                    setStatusMessage("P-LAB TMS9918 plugged", 2.0f);
                }
            } else {
                showTMS9918 = !showTMS9918;
                if (!showTMS9918) {
                    tms9918Enabled = false;
                    emulation->setTMS9918Enabled(false);
                    if (codeTankEnabled) {
                        // CodeTank rides on the TMS9918 host — Memory just
                        // cascade-disabled it, mirror in UI flags.
                        codeTankEnabled = false;
                        showCodeTankLibrary = false;
                        codeTankPendingWozRunAt = 0.0;
                        setStatusMessage("P-LAB TMS9918 + CodeTank daughterboard unplugged", 2.0f);
                    } else {
                        setStatusMessage("P-LAB TMS9918 unplugged", 2.0f);
                    }
                }
            }
        }
        ImGui::PopStyleColor();
        showHardwareTooltip(
            "P-LAB Graphic Card (TMS9918)\n"
            "VDP ports: $CC00/$CC01.\n\n"
            "Click toggles the output window/card. Plugging it unplugs A1-AUDIO SE.\n"
            "Unplugging it also unplugs the CodeTank daughterboard.");

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
                if (!gateStrictPlug("GEN2", graphicsCardEnabled)) {
                    emulation->setHgrFramebufferAttached(true);
                    showGraphicsCard = true;
                    // Plug only — load a demo via File > Open from "software/Graphic HGR/"
                    // (the folder auto-plugs GEN2 too).
                    setStatusMessage("GEN2 plugged", 2.0f);
                }
            } else {
                showGraphicsCard = !showGraphicsCard;
                if (!showGraphicsCard) {
                    graphicsCardEnabled = false;
                    emulation->setHgrFramebufferAttached(false);
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
                if (!gateStrictPlug("A1-IO-RTC", a1ioRtcEnabled)) {
                    showA1IO_RTC = true;
                    emulation->setA1IO_RTCEnabled(true);
                    setStatusMessage("P-LAB I/O Board & RTC plugged at $2000", 3.0f);
                }
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
            "P-LAB MODEM BBS WIFI\n"
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
        // Clear Screen — physical button (CLS): wipes the 40x24 buffer and
        // parks the cursor at (0,0) without touching the CPU. The running
        // program keeps executing and will simply resume drawing on a
        // freshly-blanked screen.
        if (ImGui::Button("##clrToolbar", btnSize)) {
            if (screen) screen->clear();
            setStatusMessage("Screen cleared (CPU continues)", 2.0f);
        }
        drawToolbarTextLabel(ImGui::GetWindowDrawList(),
                             ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), "CLS");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Clear Screen — wipes the display, cursor @ top-left.\nDoes NOT reset the CPU (running software keeps executing).");

        ImGui::SameLine();
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
        // Render the x1/x2/Max labels a touch smaller than the icon buttons
        // (owner request). Only the three labels are scaled; the button boxes
        // (fixed mhzBtnSize/btnSize) and the tooltips (separate windows) keep
        // their size. Reset to 1.0 right after the Max button.
        ImGui::SetWindowFontScale(0.85f);
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
        ImGui::SetWindowFontScale(1.0f);

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
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Memory Map Bar (Vertical)");
        }
        ImGui::SameLine();
        {
            bool barH = showMemoryBarH;
            if (barH) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            if (ImGui::Button(ICON_FA_BARS_STAGGERED, btnSize)) showMemoryBarH = !showMemoryBarH;
            if (barH) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Memory Map Bar (Horizontal)");
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

        // --- Séparateur ---
        ImGui::SameLine(0, 12);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine(0, 12);

        // --- Silicon profile toggle: ruler (Strict) / horse (Fantasy) -------
        //
        // Single click = bascule Strict <-> Fantasy en armant/desarmant tous les
        // knobs silicon-fidelity (meme logique que le master button dans
        // Hardware -> Silicon Strict Inspector) ET ouvre la fenetre Inspector
        // pour donner le contexte (live drop diagnostics + fine tuning par
        // knob). Pas d'icone licorne dans Font Awesome 6, le cheval sert
        // d'analogue visuel.
        {
            const bool strict = siliconStrictModeEnabled;
            const char* icon  = strict ? ICON_FA_RULER_COMBINED : ICON_FA_HORSE;
            if (strict) {
                ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            }
            if (ImGui::Button(icon, btnSize)) {
                const bool turnOn = !strict;
                siliconStrictModeEnabled     = turnOn;
                vramNoiseOnResetEnabled      = turnOn;
                systemRamNoiseOnResetEnabled = turnOn;
                dramRefreshEnabled           = turnOn;
                oorStrictModeEnabled         = turnOn;
                emulation->setSiliconStrictMode(turnOn);
                emulation->setVramNoiseOnReset(turnOn);
                emulation->setSystemRamNoiseOnReset(turnOn);
                emulation->setDramRefreshEnabled(turnOn);
                emulation->setOutOfRangeStrictMode(turnOn);
                // GEN2 HGR silicon-fidelity knobs are part of the bundle too —
                // arm/disarm all four (latch / floating-bus / scanner-phase /
                // DRAM noise) with the master profile, and keep the Inspector
                // checkbox flags in sync. Mirrors the preset apply path.
                gen2RandomPowerOnEnabled      = turnOn;
                gen2RandomLatchEnabled        = turnOn;
                gen2RandomFloatingBusEnabled  = turnOn;
                gen2RandomScannerPhaseEnabled = turnOn;
                gen2RandomDramNoiseEnabled    = turnOn;
                emulation->setGen2RandomPowerOn(turnOn);
                showSiliconStrictWindow = true;
                setStatusMessage(turnOn
                    ? "SILICON STRICT ON — strict Apple-1 timing + VRAM/RAM noise + DRAM refresh + GEN2 power-on + OOR armed"
                    : "MULTIPLEXING FANTASY — every silicon-fidelity knob OFF (incl. GEN2)",
                    3.5f);
            }
            if (strict) {
                ImGui::PopStyleColor();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    strict ? "Silicon Strict (real Apple-1 timing + VRAM/RAM noise + DRAM refresh)\n"
                             "Click - switch to Multiplexing Fantasy + open Inspector"
                           : "Multiplexing Fantasy (every silicon-fidelity knob OFF)\n"
                             "Click - switch to Silicon Strict + open Inspector");
            }
        }

#if !POM1_IS_WASM
        // --- State-rewind timeline band (between the silicon badge and About) ---
        // Desktop only: the rewind ring is captured on the dedicated emulation
        // thread; in the single-threaded, memory-bounded WASM build it stutters
        // the main loop, so rewind is disabled there entirely.
        // A live scrubber over the in-memory snapshot ring: drag to go back
        // through recent history — the plugged displays preview that instant
        // (rewindSeekTo restores + republishes the state) — and releasing
        // resumes the machine at that frame (rewindResumeHere, discarding the
        // rewound-past future). Sizes itself to the gap up to the About button.
        {
            ImGui::SameLine();
            EmulationController::RewindStatus rw = emulation->getRewindStatus();
            // One-shot: make the band live out of the box. The user can still
            // turn recording off in CPU → State Rewind; we don't re-force it.
            if (!rewindAutoStarted) {
                rewindAutoStarted = true;
                if (!rw.enabled) { emulation->setRewindEnabled(true); rw = emulation->getRewindStatus(); }
            }
            ImGuiStyle& style  = ImGui::GetStyle();
            const float aboutW = ImGui::CalcTextSize(ICON_FA_CIRCLE_INFO).x + style.FramePadding.x * 2.0f;
            const float aboutX = io.DisplaySize.x - aboutW - style.WindowPadding.x;
            const float bandW  = aboutX - ImGui::GetCursorPosX() - style.ItemSpacing.x;
            if (bandW >= 40.0f) {
                ImGui::SetNextItemWidth(bandW);
                if (!rw.enabled || rw.frameCount <= 1) {
                    ImGui::BeginDisabled();
                    int z = 0;
                    ImGui::SliderInt("##toolbar_rewind", &z, 0, 0,
                                     rw.enabled ? "timeline (recording...)" : "timeline (off)");
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("State timeline. Toggle recording in CPU → State Rewind.");
                } else {
                    const int lastFrame = static_cast<int>(rw.frameCount) - 1;
                    int pos = static_cast<int>(rw.currentPos);
                    if (pos > lastFrame) pos = lastFrame;
                    char label[40];
                    if (rw.previewing)
                        std::snprintf(label, sizeof(label), "REWIND  -%.1fs",
                                      static_cast<double>(lastFrame - pos) * 0.25);  // ~0.25 s/frame
                    else
                        std::snprintf(label, sizeof(label), "LIVE");
                    if (ImGui::SliderInt("##toolbar_rewind", &pos, 0, lastFrame, label))
                        emulation->rewindSeekTo(static_cast<std::size_t>(pos));      // drag → preview
                    if (ImGui::IsItemDeactivatedAfterEdit())
                        emulation->rewindResumeHere(static_cast<std::size_t>(pos));  // release → resume here
                    if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
                        ImGui::SetTooltip("State timeline — drag to scrub back through recent\n"
                                          "history; release to resume the machine at that instant.");
                }
            }
        }
#endif // !POM1_IS_WASM — rewind disabled in the web build

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

        std::string audioText = !uiSnapshot.cassetteAudioAvailable ? "| AUDIO OFF" : "";

        std::string siliconText;
        if (siliconStrictModeEnabled) {
            std::ostringstream oss;
            oss << "| STRICT";
            if (uiSnapshot.vdpDroppedWrites > 0) {
                oss << " (drop:" << uiSnapshot.vdpDroppedWrites << ")";
            }
            siliconText = oss.str();
        } else {
            siliconText = "| FANTASY";
        }

        // Accessibility: while F10 UI-navigation mode is on, typed keys drive
        // the interface (not the Apple-1) — make that state impossible to miss.
        std::string navText = uiNavMode_ ? "| UI NAV (F10)" : "";

        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        float rightWidth =
            ImGui::CalcTextSize(cpuText.c_str()).x +
            ImGui::CalcTextSize(speedText.c_str()).x +
            ImGui::CalcTextSize(ramText.c_str()).x +
            ImGui::CalcTextSize(siliconText.c_str()).x +
            (navText.empty() ? 0.0f : ImGui::CalcTextSize(navText.c_str()).x) +
            (audioText.empty() ? 0.0f : ImGui::CalcTextSize(audioText.c_str()).x) +
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
        ImGui::TextColored(siliconStrictModeEnabled
            ? ImVec4(0.85f, 0.55f, 0.25f, 1.0f)   // ambre = silicium réel
            : ImVec4(0.55f, 0.55f, 0.95f, 1.0f),  // bleu  = fantasy permissif
            "%s", siliconText.c_str());

        if (!navText.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.20f, 1.0f), "%s", navText.c_str());
        }

        if (!uiSnapshot.cassetteAudioAvailable) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", audioText.c_str());
        }

    }
    ImGui::End();
}
