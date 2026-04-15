// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MainWindow_Dialogs.cpp — modal-style dialogs and reference windows that
// don't fit cleanly into Hardware/Debug/File buckets: About, Hardware
// Reference, Display Settings, Memory Settings.

#include "MainWindow_ImGui.h"
#include "MainWindow_Internal.h"
#include "POM1Build.h"

#include "imgui.h"

#if !POM1_IS_WASM
#include <GLFW/glfw3.h>
#endif

#if POM1_IS_WASM
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace pom1::mainwindow::detail;
}

void MainWindow_ImGui::renderAboutDialog()
{
    ImGui::SetNextWindowSizeConstraints(ImVec2(520, 0), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::Begin("About POM1", &showAbout, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("POM1 v1.8.1 - Apple 1 Emulator (Dear ImGui)");
        ImGui::TextWrapped("Celebrating 50 years of Apple (1976-2026)");
        ImGui::TextWrapped("Copyright (C) 2000-2026 - GPL-3.0");
        ImGui::Separator();

        ImGui::Spacing();
        ImGui::TextWrapped("Authors:");
        ImGui::BulletText("Arnaud VERHILLE - original POM1 (Java, 2000) & Dear ImGui port (2026)");
        ImGui::BulletText("Ken WESSEN - upgrades, 65C02 support (2006)");
        ImGui::BulletText("Joe CROBAK - macOS Cocoa port");
        ImGui::BulletText("John D. CORRADO - C/SDL port (2006-2014)");

        ImGui::Spacing();
        ImGui::TextWrapped("Hardware emulated:");
        ImGui::BulletText("MOS 6502 CPU + PIA 6821 ($D0Fx aliasing)");
        ImGui::BulletText("Apple Cassette Interface (ACI) - live audio + .aci/.wav I/O");
        ImGui::BulletText("Uncle Bernie's GEN2 Color Graphics Card (280x192 HIRES)");
        ImGui::BulletText("P-LAB Apple-1 Graphic Card (TMS9918 VDP, sprites)");
        ImGui::BulletText("P-LAB A1-SID Sound Card (MOS 6581/8580)");
        ImGui::BulletText("P-LAB microSD Storage Card (65C22 VIA + SD CARD OS)");
        ImGui::BulletText("P-LAB MODEM BBS (65C51 ACIA + TCP/TELNET)");
        ImGui::BulletText("P-LAB Terminal Card (TCP server on localhost:6502)");

        ImGui::Spacing();
        ImGui::TextWrapped("Thanks to:");
        ImGui::BulletText("Steve WOZNIAK & Steve JOBS - for the Apple 1");
        ImGui::BulletText("Claudio PARMIGIANI (P-LAB) - designer of the entire P-LAB Apple-1 expansion family");
        ImGui::BulletText("Antonino PORCINO (Nippur72) - apple1-videocard-lib & apple1-sdcard firmware");
        ImGui::BulletText("Uncle BERNIE - GEN2 Color Graphics Card");
        ImGui::BulletText("Tom OWAD - AppleFritter community");
        ImGui::BulletText("Vince BRIEL - Replica 1");
        ImGui::BulletText("Lee DAVISON - Enhanced BASIC");
        ImGui::BulletText("Achim BREIDENBACH - Sim6502");
        ImGui::BulletText("Fabrice FRANCES - Java Microtan Emulator");

        ImGui::Spacing();
        ImGui::TextWrapped("Resources:");
        ImGui::BulletText("apple1software.com - Apple 1 software archive");
        ImGui::BulletText("applefritter.com/apple1 - Apple 1 community hub");
        ImGui::BulletText("p-l4b.github.io - P-LAB hardware reference");
    }
    ImGui::End();
}

namespace {

static const char kHardwareReferenceText[] = R"hwref(
Apple-1 / POM1 — Hardware reference (summary)

CPU
  MOS 6502 @ ~1.022727 MHz (POM1_CPU_CLOCK_HZ, 14.31818 MHz ÷ 14). Use the CPU menu for Stop, Run, Step, and reset.

PIA 6821 (keyboard & display)
  $D010  KBD   — last key, bit 7 set; read clears strobe
  $D011  KBDCR — bit 7 = key ready
  $D012  DSP   — write sends a character to the emulated display; read reflects terminal-speed busy/ready
  Incomplete decode: any $D0xx uses address & 3 → $D010–$D012 (Pagetable / Briel BASIC compatible).
  Keyboard is uppercased by default; the Terminal Card can inject raw 8-bit keys.

Memory map (64 KB always present; machine presets only warn on out-of-range RAM access)
  $0000–$00FF  Zero page
  $0100–$01FF  Stack
  $0200–$1FFF  RAM (programs often load at $0280 or $0300)
  $2000–$200F  P-LAB I/O Board & RTC (when enabled; overlays this RAM slice)
  $2000–$3FFF  GEN2 HGR framebuffer (8 KB, when Uncle Bernie GEN2 card is enabled)
  $4000–$7FFF  RAM
  $8000–$9FFF  SD CARD OS ROM (8 KB, microSD card)
  $9000–$AFFF  CFFA1 ROM + registers (when CFFA1 enabled; do not enable together with microSD)
  $A000–$A00F  65C22 VIA (microSD card)
  $A010–$AFFF  RAM
  $B000–$B003  65C51 ACIA (MODEM BBS)
  $B004–$BFFF  RAM
  $C000–$C0FF  Apple Cassette Interface ($C081 input, $C000 output latch)
  $C100–$C1FF  Woz ACI ROM
  $C800–$CFFF  A1-SID (29 registers, address & $1F, when enabled)
  $CC00 / $CC01  TMS9918 data / control (when enabled; wins over SID at these two addresses)
  $D010–$D012  See PIA above ($D0xx aliases)
  $E000–$EFFF  Apple BASIC ROM (4 KB), or Applesoft Lite region when CFFA1 preset loads replacement
  $FF00–$FFFF  Woz Monitor (256 B) + vectors at $FFFA–$FFFF

Expansion cards (menu Hardware)
  Uncle Bernie GEN2 — passive 280×192 HIRES read of $2000–$3FFF, NTSC-style artifact colour
  P-LAB TMS9918 — 16 KB VRAM, $CC00/$CC01
  P-LAB A1-SID — MOS 6581/8580, $C800–$CFFF (& $1F)
  P-LAB microSD — VIA $A000–$A00F, firmware ROM $8000–$9FFF, host folder sdcard/
  CFFA1 — ROM/I/O $9000–$AFFF, disk image cfcard/cfcard.po when present
  P-LAB MODEM BBS — ACIA $B000–$B003, Hayes/TCP (desktop + WASM)
  P-LAB Terminal Card — listens on localhost:6502, bridges $D010/$D011/$D012 (desktop only)
  P-LAB I/O & RTC — registers $2000–$200F

Assembling with cc65 (examples)
  ca65 -o build/program.o software/program.asm
  ld65 -C software/apple1.cfg -o build/program.bin build/program.o
  GEN2 programs: software/hgr/apple1_gen2.cfg (reserves $2000–$3FFF)

Full feature list, ROM table, and shortcuts: README.md in the repository.
)hwref";

} // namespace

void MainWindow_ImGui::renderHardwareReferenceWindow()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.55f, io.DisplaySize.y * 0.72f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.22f, io.DisplaySize.y * 0.08f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Hardware Reference", &showHardwareReference)) {
        ImGui::TextWrapped(
            "Condensed map and I/O summary for POM1. See README.md and CLAUDE.md for build notes and architecture.");
        ImGui::Separator();
        const char* textEnd = kHardwareReferenceText + std::strlen(kHardwareReferenceText);
        ImGui::BeginChild("hwref_scroll", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
        ImGui::PushFont(io.Fonts->Fonts[0]);
        ImGui::TextUnformatted(kHardwareReferenceText, textEnd);
        ImGui::PopFont();
        ImGui::PopStyleVar();
        ImGui::EndChild();
    }
    ImGui::End();
}
void MainWindow_ImGui::renderScreenConfigDialog()
{
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Display Settings", &showScreenConfig)) {
        ImGui::Text("Display Options");
        ImGui::Separator();

        int renderMode = static_cast<int>(screen->characterRenderMode);
        ImGui::Text("Character Rendering:");
        ImGui::RadioButton("Apple-1 Charmap", &renderMode, static_cast<int>(Screen_ImGui::CharacterRenderMode::Apple1Charmap));
        ImGui::SameLine();
        ImGui::RadioButton("ASCII Host", &renderMode, static_cast<int>(Screen_ImGui::CharacterRenderMode::HostAscii));
        screen->characterRenderMode = static_cast<Screen_ImGui::CharacterRenderMode>(renderMode);
        if (screen->characterRenderMode == Screen_ImGui::CharacterRenderMode::HostAscii) {
            ImGui::Indent();
            ImGui::SliderFloat("Host ASCII character size", &screen->hostAsciiGlyphScale, 1.0f, 2.0f, "%.2f×");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Larger than 1.0 uses more of each cell; may touch neighbors slightly.");
            }
            ImGui::Unindent();
        }

        ImGui::Spacing();
        ImGui::Text("Monitor Tint:");
        ImGui::SameLine();
        {
            const ImVec2 pad = ImGui::GetStyle().FramePadding;
            float labelW = 0.0f;
            for (int i = 0; i < kMonitorTintCount; ++i) {
                const auto m = static_cast<Screen_ImGui::MonitorMode>(i);
                labelW = std::max(labelW, ImGui::CalcTextSize(monitorTintLabel(m)).x);
            }
            const ImVec2 btnSize(labelW + pad.x * 2.0f + 8.0f, ImGui::GetFrameHeight());
            monitorTintCycleButton("##display_phosphor_cycle", btnSize, screen.get());
            const char* label = monitorTintLabel(screen->monitorMode);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s — click for next tint (green, brown, monochrome)", label);
            }
            const ImVec2 ts = ImGui::CalcTextSize(label);
            const ImVec2 p0 = ImGui::GetItemRectMin();
            const ImVec2 p1 = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(p0.x + (p1.x - p0.x - ts.x) * 0.5f, p0.y + (p1.y - p0.y - ts.y) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_Text), label);
        }
        ImGui::Checkbox("Cursor", &screen->showCursor);

        ImGui::Spacing();
        ImGui::Text("Display Scale:");
        ImGui::SliderFloat("##Scale", &screen->scale, 0.5f, 4.0f, "%.1fx");

        ImGui::Spacing();
        ImGui::Text("Image Adjustments:");
        ImGui::SliderFloat("Brightness", &screen->brightness, 0.2f, 1.5f, "%.2f");
        ImGui::SliderFloat("Contrast", &screen->contrast, 0.5f, 2.0f, "%.2f");

        ImGui::Spacing();
        ImGui::Text("CRT Effect");
        ImGui::Separator();
        ImGui::Checkbox("Scanlines", &screen->crtEffect);
        if (screen->crtEffect) {
            ImGui::SliderFloat("Scanline Intensity", &screen->crtScanlineAlpha, 0.0f, 0.9f, "%.2f");
        }

        ImGui::Spacing();
        if (ImGui::Checkbox("Fullscreen", &fullscreen)) {
#if POM1_IS_WASM
            if (fullscreen) {
                EmscriptenFullscreenStrategy strategy{};
                strategy.scaleMode = EMSCRIPTEN_FULLSCREEN_SCALE_STRETCH;
                strategy.canvasResolutionScaleMode = EMSCRIPTEN_FULLSCREEN_CANVAS_SCALE_HIDEF;
                strategy.filteringMode = EMSCRIPTEN_FULLSCREEN_FILTERING_DEFAULT;
                emscripten_request_fullscreen_strategy("#canvas", true, &strategy);
            } else {
                emscripten_exit_fullscreen();
            }
#else
            if (window) {
                if (fullscreen) {
                    glfwGetWindowPos(window, &windowedPosX, &windowedPosY);
                    glfwGetWindowSize(window, &windowedWidth, &windowedHeight);
                    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
                    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                    glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
                } else {
                    glfwSetWindowMonitor(window, nullptr, windowedPosX, windowedPosY, windowedWidth, windowedHeight, 0);
                }
            }
#endif
        }

        ImGui::Spacing();
        if (ImGui::Button("Close")) {
            showScreenConfig = false;
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::renderMemoryConfigDialog()
{
    ImGui::SetNextWindowSize(ImVec2(450, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Memory Settings", &showMemoryConfig)) {
        bool writeProtect = !uiSnapshot.writeInRom;

        ImGui::Text("ROM Protection");
        ImGui::Separator();
        if (ImGui::Checkbox("Write-protect ROMs", &writeProtect)) {
            emulation->setWriteInRom(!writeProtect);
        }

        ImGui::Spacing();
        ImGui::Text("ROM Loading");
        ImGui::Separator();

        if (ImGui::Button("Reload BASIC")) {
            std::string error;
            bool ok = emulation->reloadBasic(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            if (ok) {
                loadedRoms.erase(std::remove_if(loadedRoms.begin(), loadedRoms.end(),
                    [](const LoadedRegion& r) { return r.start >= 0xE000 && r.end <= 0xFFFF; }), loadedRoms.end());
                loadedRoms.push_back({"Integer BASIC", 0xE000, 0xEFFF});
                loadedRoms.push_back({"Woz Monitor", 0xFF00, 0xFFFF});
            }
            setStatusMessage(ok ? "BASIC reloaded" : error, 3.0f);
        }

        if (ImGui::Button("Reload Applesoft Lite")) {
            std::string error;
            bool ok = emulation->reloadApplesoftLite(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            if (ok) {
                const bool plabSd = emulation->isMicroSDEnabled() && !emulation->isCFFA1Enabled();
                if (plabSd) {
                    loadedRoms.erase(std::remove_if(loadedRoms.begin(), loadedRoms.end(),
                        [](const LoadedRegion& r) {
                            if (r.name.find("Applesoft") != std::string::npos) return true;
                            return r.start == 0x6000 && r.end == 0x7FFF;
                        }), loadedRoms.end());
                    loadedRoms.push_back({"Applesoft Lite (P-LAB microSD)", 0x6000, 0x7FFF});
                    auto hasRange = [](const std::vector<LoadedRegion>& v, quint16 s, quint16 e) {
                        for (const auto& r : v)
                            if (r.start == s && r.end == e) return true;
                        return false;
                    };
                    if (!hasRange(loadedRoms, 0xE000, 0xEFFF))
                        loadedRoms.push_back({"Integer BASIC", 0xE000, 0xEFFF});
                    if (!hasRange(loadedRoms, 0xFF00, 0xFFFF))
                        loadedRoms.push_back({"Woz Monitor", 0xFF00, 0xFFFF});
                } else {
                    loadedRoms.erase(std::remove_if(loadedRoms.begin(), loadedRoms.end(),
                        [](const LoadedRegion& r) { return r.start >= 0xE000 && r.end <= 0xFFFF; }), loadedRoms.end());
                    loadedRoms.push_back({"Applesoft Lite (CFFA1)", 0xE000, 0xFFFF});
                }
            }
            setStatusMessage(ok ? "Applesoft Lite reloaded" : error, 3.0f);
        }

        if (ImGui::Button("Reload WOZ Monitor")) {
            std::string error;
            bool ok = emulation->reloadWozMonitor(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            setStatusMessage(ok ? "WOZ Monitor reloaded" : error, 3.0f);
        }

        if (ImGui::Button("Reload Krusader")) {
            std::string error;
            bool ok = emulation->reloadKrusader(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            if (ok) {
                loadedRoms.erase(std::remove_if(loadedRoms.begin(), loadedRoms.end(),
                    [](const LoadedRegion& r) { return r.start == 0xA000; }), loadedRoms.end());
                loadedRoms.push_back({"Krusader", 0xA000, 0xBFFF});
            }
            setStatusMessage(ok ? "Krusader reloaded" : error, 3.0f);
        }

        if (ImGui::Button("Reload ACI ROM")) {
            std::string error;
            bool ok = emulation->reloadAciRom(error);
            if (!writeProtect) {
                emulation->setWriteInRom(true);
            }
            setStatusMessage(ok ? "ACI ROM reloaded" : error, 3.0f);
        }

        ImGui::Spacing();
        ImGui::Text("Out-of-range enforcement");
        ImGui::Separator();
        ImGui::TextWrapped("When the preset's RAM is smaller than 64 KB (e.g. bare-4K), "
                           "accesses beyond that ceiling are tracked as OOR. Enable strict "
                           "enforcement for hardware-accurate behaviour: reads return $FF "
                           "and writes are dropped, exactly like a real Apple-1 with no "
                           "RAM board in that region.");
        bool strict = uiSnapshot.oorStrictMode;
        if (ImGui::Checkbox("Strict enforcement (reads -> $FF, writes dropped)", &strict)) {
            emulation->setOutOfRangeStrictMode(strict);
        }
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                           "Active range at %d KB preset: $%04X - $7FFF",
                           presetRamKB, presetRamKB * 1024);
        if (presetRamKB >= 64) {
            ImGui::TextColored(ImVec4(0.85f, 0.75f, 0.45f, 1.0f),
                               "(No effect at 64 KB preset - no OOR region.)");
        }

        ImGui::Spacing();
        ImGui::Text("Memory");
        ImGui::Separator();

        if (ImGui::Button("Clear All Memory")) {
            ImGui::OpenPopup("Confirm##ClearMemory");
        }

        if (ImGui::BeginPopupModal("Confirm##ClearMemory", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Are you sure you want to clear all memory?");
            ImGui::Separator();

            if (ImGui::Button("Yes", ImVec2(120, 0))) {
                emulation->clearMemory();
                setStatusMessage("Memory cleared", 2.0f);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("No", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (ImGui::Button("Refresh Viewer")) {
            setStatusMessage("Viewer refreshed", 2.0f);
        }

        ImGui::Spacing();
        if (ImGui::Button("Close")) {
            showMemoryConfig = false;
        }
    }
    ImGui::End();
}
