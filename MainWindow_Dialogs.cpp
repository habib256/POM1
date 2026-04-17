// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MainWindow_Dialogs.cpp — modal-style dialogs and reference windows that
// don't fit cleanly into Hardware/Debug/File buckets: About, Hardware
// Reference, Display Settings, Memory Settings.

#include "MainWindow_ImGui.h"
#include "MainWindow_Internal.h"
#include "POM1Build.h"
#include "Logger.h"

#include "imgui.h"

// OpenGL texture upload (desktop + Emscripten): GLFW pulls in the platform GL headers.
#include <GLFW/glfw3.h>
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#if POM1_IS_WASM
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#if !POM1_IS_WASM
#include <filesystem>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#endif

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb/stb_image.h"

namespace {
using namespace pom1::mainwindow::detail;

static const char kAboutPhotoFile[] = "schlumberger-2-apple-1.jpg";

/** Chemin vers la photo About (WASM : bundle pic/ via --preload-file). */
static std::string find_about_photo_jpeg_path()
{
#if POM1_IS_WASM
    (void)kAboutPhotoFile;
    return std::string("pic/schlumberger-2-apple-1.jpg");
#else
    namespace fs = std::filesystem;

    auto try_path = [](const fs::path& p) -> std::string {
        std::error_code ec;
        if (fs::is_regular_file(p, ec))
            return p.string();
        return {};
    };

    static const char* const rel_candidates[] = {
        "pic/schlumberger-2-apple-1.jpg",
        "../pic/schlumberger-2-apple-1.jpg",
        "../../pic/schlumberger-2-apple-1.jpg",
        "../../../pic/schlumberger-2-apple-1.jpg",
    };
    for (const char* r : rel_candidates) {
        std::string s = try_path(fs::path(r));
        if (!s.empty())
            return s;
    }

#if defined(_WIN32)
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        fs::path exeDir = fs::path(buf).parent_path();
        const fs::path next_to_exe[] = {
            exeDir / "pic" / kAboutPhotoFile,
            exeDir.parent_path() / "pic" / kAboutPhotoFile,
            exeDir.parent_path().parent_path() / "pic" / kAboutPhotoFile,
        };
        for (const auto& p : next_to_exe) {
            std::string s = try_path(p);
            if (!s.empty())
                return s;
        }
    }
#endif
    return {};
#endif
}

} // namespace

void MainWindow_ImGui::ensureAboutPhotoTexture()
{
    if (aboutPhotoTexture != 0 || aboutPhotoLoadTried)
        return;
    aboutPhotoLoadTried = true;

    const std::string path = find_about_photo_jpeg_path();
    if (path.empty()) {
        pom1::log().warn("About", "Apple-1 photo not found (expected pic/schlumberger-2-apple-1.jpg)");
        return;
    }

    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels)
            stbi_image_free(pixels);
        pom1::log().warn("About", "Could not decode About photo: " + path);
        return;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    stbi_image_free(pixels);

    aboutPhotoTexture = tex;
    aboutPhotoWidth = w;
    aboutPhotoHeight = h;
}

void MainWindow_ImGui::renderAboutDialog()
{
    ensureAboutPhotoTexture();

    float minWinW = 520.0f;
    if (aboutPhotoTexture != 0 && aboutPhotoWidth > 0) {
        const ImGuiStyle& st = ImGui::GetStyle();
        const float horizontalChrome = st.WindowPadding.x * 2.0f + st.ScrollbarSize + 4.0f;
        minWinW = std::max(minWinW,
                           std::min(1400.0f, static_cast<float>(aboutPhotoWidth) + horizontalChrome));
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(minWinW, 0), ImVec2(FLT_MAX, FLT_MAX));

    if (ImGui::Begin("About POM1", &showAbout, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("POM1 v1.8.1 - Apple 1 Emulator (Dear ImGui)");
        ImGui::TextWrapped("Celebrating 50 years of Apple (1976-2026)");
        ImGui::TextWrapped("Copyright (C) 2000-2026 - GPL-3.0");
        ImGui::Separator();

        if (aboutPhotoTexture != 0 && aboutPhotoWidth > 0 && aboutPhotoHeight > 0) {
            const float availW = ImGui::GetContentRegionAvail().x;
            const float iw = static_cast<float>(aboutPhotoWidth);
            const float ih = static_cast<float>(aboutPhotoHeight);
            const float scale = std::min(1.0f, availW / iw);
            const ImVec2 imgSize(iw * scale, ih * scale);
            ImGui::Image((ImTextureID)(uintptr_t)aboutPhotoTexture, imgSize);
            ImGui::Spacing();
        }

        ImGui::Spacing();
        ImGui::TextWrapped("Authors:");
        ImGui::BulletText("Arnaud VERHILLE - original POM1 (Java, 2000) & Dear ImGui port (2026)");
        ImGui::BulletText("Ken WESSEN - upgrades, 65C02 support (2006)");
        ImGui::BulletText("Joe CROBAK - macOS Cocoa port");
        ImGui::BulletText("John D. CORRADO - C/SDL port (2006-2014)");

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
Apple-1 / POM1 - Hardware reference (summary)

CPU
  MOS 6502 @ ~1.022727 MHz (POM1_CPU_CLOCK_HZ, 14.31818 MHz ÷ 14). Use the CPU menu for Stop, Run, Step, and reset.

PIA 6821 (keyboard & display)
  $D010  KBD   - last key, bit 7 set; read clears strobe
  $D011  KBDCR - bit 7 = key ready
  $D012  DSP   - write sends a character to the emulated display; read reflects terminal-speed busy/ready
  Incomplete decode: any $D0xx uses address & 3 -> $D010-$D012 (Pagetable / Briel BASIC compatible).
  Keyboard is uppercased by default; the Terminal Card can inject raw 8-bit keys.

Memory map (64 KB always present; machine presets only warn on out-of-range RAM access)
  $0000-$00FF  Zero page
  $0100-$01FF  Stack
  $0200-$1FFF  RAM (programs often load at $0280 or $0300)
  $2000-$200F  P-LAB I/O Board & RTC (when enabled; overlays this RAM slice)
  $2000-$3FFF  GEN2 HGR framebuffer (8 KB, when Uncle Bernie GEN2 card is enabled)
  $4000-$7FFF  RAM
  $8000-$9FFF  SD CARD OS ROM (8 KB, microSD card)
  $9000-$AFFF  CFFA1 ROM + registers (when CFFA1 enabled; do not enable together with microSD)
  $A000-$A00F  65C22 VIA (microSD card)
  $A010-$AFFF  RAM
  $B000-$B003  65C51 ACIA (MODEM BBS)
  $B004-$BFFF  RAM
  $C000-$C0FF  Apple Cassette Interface ($C081 input, $C000 output latch)
  $C100-$C1FF  Woz ACI ROM
  $C800-$CFFF  A1-SID (29 registers, address & $1F, when enabled)
  $CC00 / $CC01  TMS9918 data / control (when enabled; wins over SID at these two addresses)
  $D010-$D012  See PIA above ($D0xx aliases)
  $E000-$EFFF  Apple BASIC ROM (4 KB), or Applesoft Lite region when CFFA1 preset loads replacement
  $FF00-$FFFF  Woz Monitor (256 B) + vectors at $FFFA-$FFFF

Expansion cards (menu Hardware)
  Uncle Bernie GEN2 - passive 280x192 HIRES read of $2000-$3FFF, NTSC-style artifact colour
  P-LAB TMS9918 - 16 KB VRAM, $CC00/$CC01
  P-LAB A1-SID - MOS 6581/8580, $C800-$CFFF (& $1F)
  P-LAB microSD - VIA $A000-$A00F, firmware ROM $8000-$9FFF, host folder sdcard/
  CFFA1 - ROM/I/O $9000-$AFFF, disk image cfcard/cfcard.po when present
  P-LAB MODEM BBS - ACIA $B000-$B003, Hayes/TCP (desktop + WASM)
  P-LAB Terminal Card - listens on localhost:6502, bridges $D010/$D011/$D012 (desktop only)
  P-LAB I/O & RTC - registers $2000-$200F

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
                ImGui::SetTooltip("%s - click for next tint (green, brown, monochrome)", label);
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

        auto hasRange = [](const std::vector<LoadedRegion>& v, quint16 s, quint16 e) {
            for (const auto& r : v)
                if (r.start == s && r.end == e) return true;
            return false;
        };

        if (ImGui::Button("Load BASIC  [$E000-$EFFF + Woz $FF00-$FFFF]")) {
            std::string error;
            bool ok = emulation->reloadBasic(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            if (ok) {
                loadedRoms.erase(std::remove_if(loadedRoms.begin(), loadedRoms.end(),
                    [](const LoadedRegion& r) { return r.start >= 0xE000 && r.end <= 0xFFFF; }), loadedRoms.end());
                loadedRoms.push_back({"Integer BASIC", 0xE000, 0xEFFF});
                loadedRoms.push_back({"Woz Monitor", 0xFF00, 0xFFFF});
            }
            setStatusMessage(ok ? "BASIC loaded" : error, 3.0f);
        }

        if (ImGui::Button("Load Applesoft Lite (CFFA1)  [$E000-$FFFF]")) {
            std::string error;
            bool ok = emulation->reloadApplesoftLiteCFFA1(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            if (ok) {
                loadedRoms.erase(std::remove_if(loadedRoms.begin(), loadedRoms.end(),
                    [](const LoadedRegion& r) { return r.start >= 0xE000 && r.end <= 0xFFFF; }), loadedRoms.end());
                loadedRoms.push_back({"Applesoft Lite (CFFA1)", 0xE000, 0xFFFF});
            }
            setStatusMessage(ok ? "Applesoft Lite (CFFA1) loaded" : error, 3.0f);
        }

        if (ImGui::Button("Load Applesoft Lite (microSD)  [$6000-$7FFF + Woz $FF00-$FFFF]")) {
            std::string error;
            bool ok = emulation->reloadApplesoftLiteSDCard(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            if (ok) {
                loadedRoms.erase(std::remove_if(loadedRoms.begin(), loadedRoms.end(),
                    [](const LoadedRegion& r) {
                        if (r.name.find("Applesoft") != std::string::npos) return true;
                        return r.start == 0x6000 && r.end == 0x7FFF;
                    }), loadedRoms.end());
                loadedRoms.push_back({"Applesoft Lite (P-LAB microSD)", 0x6000, 0x7FFF});
                if (!hasRange(loadedRoms, 0xE000, 0xEFFF))
                    loadedRoms.push_back({"Integer BASIC", 0xE000, 0xEFFF});
                if (!hasRange(loadedRoms, 0xFF00, 0xFFFF))
                    loadedRoms.push_back({"Woz Monitor", 0xFF00, 0xFFFF});
            }
            setStatusMessage(ok ? "Applesoft Lite (microSD) loaded" : error, 3.0f);
        }

        if (ImGui::Button("Load WOZ Monitor  [$FF00-$FFFF]")) {
            std::string error;
            bool ok = emulation->reloadWozMonitor(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            setStatusMessage(ok ? "WOZ Monitor loaded" : error, 3.0f);
        }

        if (ImGui::Button("Load Krusader  [$A000-$BFFF]")) {
            std::string error;
            bool ok = emulation->reloadKrusader(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            if (ok) {
                loadedRoms.erase(std::remove_if(loadedRoms.begin(), loadedRoms.end(),
                    [](const LoadedRegion& r) { return r.start == 0xA000; }), loadedRoms.end());
                loadedRoms.push_back({"Krusader", 0xA000, 0xBFFF});
            }
            setStatusMessage(ok ? "Krusader loaded" : error, 3.0f);
        }

        if (ImGui::Button("Load ACI ROM  [$C100-$C1FF]")) {
            std::string error;
            bool ok = emulation->reloadAciRom(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            setStatusMessage(ok ? "ACI ROM loaded" : error, 3.0f);
        }

        if (ImGui::Button("Load SD Card OS  [$8000-$9FFF]")) {
            std::string error;
            bool ok = emulation->reloadSDCardRom(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            if (ok && !hasRange(loadedRoms, 0x8000, 0x9FFF))
                loadedRoms.push_back({"SD Card OS", 0x8000, 0x9FFF});
            setStatusMessage(ok ? "SD Card OS loaded" : error, 3.0f);
        }

        if (ImGui::Button("Load CFFA1 Firmware  [$9000-$AFDF]")) {
            std::string error;
            bool ok = emulation->reloadCFFA1Rom(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            if (ok && !hasRange(loadedRoms, 0x9000, 0xAFDF))
                loadedRoms.push_back({"CFFA1 Firmware", 0x9000, 0xAFDF});
            setStatusMessage(ok ? "CFFA1 Firmware loaded" : error, 3.0f);
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
