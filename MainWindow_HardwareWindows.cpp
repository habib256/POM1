// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MainWindow_HardwareWindows.cpp — per-card visualisation windows:
// GEN2 HGR, P-LAB TMS9918, Wi-Fi Modem, Terminal Card, A1-IO & RTC.
// Each is opened from the toolbar/menu and reads its state from
// uiSnapshot (no direct emulation access except via the EmulationController
// public API for occasional control actions).

#include "MainWindow_ImGui.h"
#include "MainWindow_Internal.h"
#include "POM1Build.h"
#include "WiFiModem.h"
#include "TerminalCard.h"
#include "PR40Printer.h"

#include "imgui.h"
#include "IconsFontAwesome6.h"

// renderTMS9918Window uploads a 256×192 RGBA texture each frame via raw GL
// calls (glGenTextures / glBindTexture / glTexImage2D / glTexSubImage2D).
// Including GLFW pulls in the platform GL headers on both desktop and WASM.
#include <GLFW/glfw3.h>
// MSVC + stock Win32 GL headers often omit these (they live in extension headers).
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
using namespace pom1::mainwindow::detail;
}

void MainWindow_ImGui::renderGraphicsCardWindow()
{
    // Lazy texture creation — same nearest-neighbour treatment as TMS9918 so
    // arbitrary window sizes still produce crisp pixel art.
    if (graphicsCardTexture == 0) {
        glGenTextures(1, &graphicsCardTexture);
        glBindTexture(GL_TEXTURE_2D, graphicsCardTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     GraphicsCard::kHiresWidth, GraphicsCard::kHiresHeight,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    // Per-scanline dirty hashing inside rasterizeToBuffer() means an idle
    // framebuffer costs ~7.7 KB of memory hashing and zero pixel writes.
    // The GL upload is skipped when nothing changed.
    if (graphicsCard.rasterizeToBuffer(uiSnapshot.memory.data())) {
        glBindTexture(GL_TEXTURE_2D, graphicsCardTexture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        GraphicsCard::kHiresWidth, GraphicsCard::kHiresHeight,
                        GL_RGBA, GL_UNSIGNED_BYTE, graphicsCard.pixels());
    }

    const float defPs = kVideoCardDefaultPixelScale;
    const float winW = GraphicsCard::kHiresWidth * defPs + 16.0f;
    const float winH = GraphicsCard::kHiresHeight * defPs + 36.0f;
    ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_FirstUseEver);
    const float minWinW = GraphicsCard::kHiresWidth * kVideoCardMinPixelScale + 16.0f;
    const float minWinH = GraphicsCard::kHiresHeight * kVideoCardMinPixelScale + 36.0f;
    ImGui::SetNextWindowSizeConstraints(ImVec2(minWinW, minWinH), ImVec2(FLT_MAX, FLT_MAX));
    applyPendingLayout("Uncle Bernie's GEN2 HGR Graphic Card");
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 255));
    if (ImGui::Begin("Uncle Bernie's GEN2 HGR Graphic Card", &showGraphicsCard)) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float pixelScale = defPs;
        ImVec2 size = layoutFitVideoViewport(avail, static_cast<float>(GraphicsCard::kHiresWidth),
                                             static_cast<float>(GraphicsCard::kHiresHeight), pixelScale);
        ImVec2 cursorPos = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(
            cursorPos.x + std::max(0.0f, (avail.x - size.x) * 0.5f),
            cursorPos.y + std::max(0.0f, (avail.y - size.y) * 0.5f)));

        ImGui::Image((ImTextureID)(uintptr_t)graphicsCardTexture, size);
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void MainWindow_ImGui::renderTMS9918Window()
{
    // Lazy texture creation — nearest-neighbour GL_NEAREST so every window size
    // gives a clean pixel-art result without the integer-scale black borders.
    if (tms9918Texture == 0) {
        glGenTextures(1, &tms9918Texture);
        glBindTexture(GL_TEXTURE_2D, tms9918Texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     TMS9918::kScreenWidth, TMS9918::kScreenHeight,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    // Render into the CPU pixel buffer then upload to the GPU texture.
    // IM_COL32 byte order [R,G,B,A] on little-endian matches GL_RGBA/GL_UNSIGNED_BYTE.
    TMS9918::renderToBuffer(tms9918PixelBuf.data(), uiSnapshot.tms9918);
    glBindTexture(GL_TEXTURE_2D, tms9918Texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    TMS9918::kScreenWidth, TMS9918::kScreenHeight,
                    GL_RGBA, GL_UNSIGNED_BYTE, tms9918PixelBuf.data());

    const float defPs = kTMS9918DefaultPixelScale;
    const float winW = TMS9918::kScreenWidth * defPs + 16.0f;
    const float winH = TMS9918::kScreenHeight * defPs + 36.0f;
    ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_FirstUseEver);
    const float minWinW = TMS9918::kScreenWidth * kVideoCardMinPixelScale + 16.0f;
    const float minWinH = TMS9918::kScreenHeight * kVideoCardMinPixelScale + 36.0f;
    ImGui::SetNextWindowSizeConstraints(ImVec2(minWinW, minWinH), ImVec2(FLT_MAX, FLT_MAX));
    applyPendingLayout("P-LAB Graphic Card (TMS9918)");
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 255));
    if (ImGui::Begin("P-LAB Graphic Card (TMS9918)", &showTMS9918)) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float ps = std::min(avail.x / TMS9918::kScreenWidth,
                            avail.y / TMS9918::kScreenHeight);
        ps = std::max(ps, kVideoCardMinPixelScale);
        ImVec2 size(std::floor(TMS9918::kScreenWidth  * ps),
                    std::floor(TMS9918::kScreenHeight * ps));

        ImVec2 cursor = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(
            cursor.x + std::max(0.0f, (avail.x - size.x) * 0.5f),
            cursor.y + std::max(0.0f, (avail.y - size.y) * 0.5f)));

        ImGui::Image((ImTextureID)(uintptr_t)tms9918Texture, size);
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

namespace {
// Lock the GT-6144 window to a 4:3 *content* area when the user drags.
// ImGui's callback only knows the total window rect, so we subtract the chrome
// (title bar + padding) before enforcing the ratio and add it back. The axis
// with the larger drag delta wins — that way dragging width resizes height and
// vice-versa, instead of one axis being silently pinned.
void GT6144_WindowAspectLock(ImGuiSizeCallbackData* data)
{
    constexpr float kChromeW = 16.0f;
    constexpr float kChromeH = 36.0f;
    const float dW = std::fabs(data->DesiredSize.x - data->CurrentSize.x);
    const float dH = std::fabs(data->DesiredSize.y - data->CurrentSize.y);
    if (dW >= dH) {
        const float content = std::max(1.0f, data->DesiredSize.x - kChromeW);
        data->DesiredSize.y = content * 3.0f / 4.0f + kChromeH;
    } else {
        const float content = std::max(1.0f, data->DesiredSize.y - kChromeH);
        data->DesiredSize.x = content * 4.0f / 3.0f + kChromeW;
    }
}
} // namespace

void MainWindow_ImGui::renderGT6144Window()
{
    // Lazy texture creation — 64x96 monochrome framebuffer rendered through
    // the same GL_NEAREST pipeline as GEN2 HGR / TMS9918 for crisp pixel art
    // at arbitrary window sizes.
    if (gt6144Texture == 0) {
        glGenTextures(1, &gt6144Texture);
        glBindTexture(GL_TEXTURE_2D, gt6144Texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     GT6144::kWidth, GT6144::kHeight,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    GT6144::renderToBuffer(gt6144PixelBuf.data(), uiSnapshot.gt6144);
    glBindTexture(GL_TEXTURE_2D, gt6144Texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    GT6144::kWidth, GT6144::kHeight,
                    GL_RGBA, GL_UNSIGNED_BYTE, gt6144PixelBuf.data());

    // Aspect correction: the GT-6144 sent its 64x96 logical matrix to a
    // stock 4:3 CRT (TV or composite monitor), so the visible pixels were
    // "petits rectangles" horizontally stretched — SWTPC's own documentation
    // describes them that way. The horizontal stretch factor needed to map
    // a 2:3 matrix (64:96) onto a 4:3 raster is exactly (4/3)/(2/3) = 2,
    // which makes each logical pixel render as a 2:1 rectangle (twice as
    // wide as it is tall). The uploaded texture remains native 64x96;
    // GL_NEAREST stretches it horizontally at blit time.
    constexpr float kGT6144AspectStretchX = 2.0f;
    const float displayAspectW = GT6144::kWidth  * kGT6144AspectStretchX; // 128
    const float displayAspectH = static_cast<float>(GT6144::kHeight);     // 96
    // Default to pixel-height scale 5 → 640x480 raster area + ImGui chrome.
    constexpr float kGT6144DefaultPixelScale = 5.0f;
    const float defPs = kGT6144DefaultPixelScale;
    const float winW = displayAspectW * defPs + 16.0f;
    const float winH = displayAspectH * defPs + 36.0f;
    ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_FirstUseEver);
    const float minWinW = displayAspectW * kVideoCardMinPixelScale + 16.0f;
    const float minWinH = displayAspectH * kVideoCardMinPixelScale + 36.0f;
    // The custom callback locks the content area to 4:3 as the user drags;
    // the min/max rectangles stay permissive on both axes since the callback
    // picks which axis is authoritative per frame.
    ImGui::SetNextWindowSizeConstraints(ImVec2(minWinW, minWinH),
                                        ImVec2(FLT_MAX, FLT_MAX),
                                        GT6144_WindowAspectLock);
    applyPendingLayout("SWTPC GT-6144 Graphic Terminal");
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 255));
    if (ImGui::Begin("SWTPC GT-6144 Graphic Terminal", &showGT6144)) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float pixelScale = defPs;
        ImVec2 size = layoutFitVideoViewport(avail, displayAspectW,
                                             displayAspectH, pixelScale);
        ImVec2 cursorPos = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(
            cursorPos.x + std::max(0.0f, (avail.x - size.x) * 0.5f),
            cursorPos.y + std::max(0.0f, (avail.y - size.y) * 0.5f)));

        ImGui::Image((ImTextureID)(uintptr_t)gt6144Texture, size);
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void MainWindow_ImGui::renderWiFiModemWindow()
{
    ImGui::SetNextWindowSize(ImVec2(380, 320), ImGuiCond_FirstUseEver);
    applyPendingLayout("P-LAB Wi-Fi Modem");
    if (ImGui::Begin("P-LAB Wi-Fi Modem", &showWiFiModem)) {
#if POM1_IS_WASM
        // Browsers cannot open raw TCP sockets, so BBS dialing always returns
        // NO CARRIER in the web build. Make this visible up front so the user
        // doesn't think the modem is broken.
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
        ImGui::TextWrapped("Web build: BBS dialing is disabled.");
        ImGui::PopStyleColor();
        ImGui::TextWrapped("Browsers cannot open raw TCP sockets. To reach a BBS "
                           "from this build you need a local WebSocket-to-TCP "
                           "bridge (websockify). Use the desktop build for "
                           "direct dialing.");
        ImGui::Separator();
#endif
        const auto& snap = uiSnapshot.wifiModem;

        // Connection status
        const char* stateStr = "Idle";
        ImVec4 stateColor(0.5f, 0.5f, 0.5f, 1.0f);
        if (snap.connected) {
            stateStr = "Connected";
            stateColor = ImVec4(0.2f, 0.9f, 0.2f, 1.0f);
        } else if (snap.statusReg & 0x08) { // RDRF set but not connected = connecting
            stateStr = "Connecting...";
            stateColor = ImVec4(0.9f, 0.9f, 0.2f, 1.0f);
        }

        ImGui::TextColored(stateColor, "Status: %s", stateStr);

        if (!snap.remoteHost.empty()) {
            ImGui::Text("Remote: %s:%d", snap.remoteHost.c_str(), snap.remotePort);
        }

        ImGui::Separator();
        ImGui::Text("Baud Rate: %d", snap.baudRate);
        ImGui::Text("Echo: %s", snap.echoEnabled ? "ON" : "OFF");
        ImGui::Text("Bytes Sent: %u", snap.bytesSent);
        ImGui::Text("Bytes Received: %u", snap.bytesReceived);

        ImGui::Separator();
        ImGui::Text("ACIA Registers ($B000-$B003):");
        ImGui::Text("  Status:  $%02X  [%s%s%s%s]",
            snap.statusReg,
            (snap.statusReg & 0x10) ? "TDRE " : "",
            (snap.statusReg & 0x08) ? "RDRF " : "",
            (snap.statusReg & 0x20) ? "DCD " : "",
            (snap.statusReg & 0x40) ? "DSR " : "");
        ImGui::Text("  Command: $%02X", snap.commandReg);
        ImGui::Text("  Control: $%02X", snap.controlReg);

        ImGui::Separator();
        if (snap.connected) {
            if (ImGui::Button("Disconnect")) {
                emulation->wifiModemDisconnect();
            }
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::renderTerminalCardWindow()
{
    ImGui::SetNextWindowSize(ImVec2(360, 280), ImGuiCond_FirstUseEver);
    applyPendingLayout("P-LAB Terminal Card");
    if (ImGui::Begin("P-LAB Terminal Card", &showTerminalCard)) {
        const auto& snap = uiSnapshot.terminalCard;

        // Server status
        if (snap.serverListening) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f),
                ICON_FA_SERVER " Listening on port %d", snap.listenPort);
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f),
                ICON_FA_SERVER " Server not running");
        }

        // Client connection
        if (snap.clientConnected) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f),
                ICON_FA_PLUG " Connected: %s", snap.clientAddress.c_str());
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                ICON_FA_PLUG " No client connected");
            ImGui::TextWrapped("Connect with: telnet localhost %d", snap.listenPort);
        }

        ImGui::Separator();

        // Mode indicators
        ImGui::Text("Modes:");
        ImGui::BulletText("UC Outgoing (Ctrl-O): %s", snap.uppercaseOutgoing ? "ON" : "OFF");
        ImGui::BulletText("UC Incoming (Ctrl-I): %s", snap.uppercaseIncoming ? "ON" : "OFF");
        ImGui::BulletText("8-bit Mode  (Ctrl-T): %s", snap.eightBitMode ? "ON" : "OFF");

        ImGui::Separator();

        // Traffic stats
        ImGui::Text("Bytes Sent:     %u", snap.bytesSent);
        ImGui::Text("Bytes Received: %u", snap.bytesReceived);

        ImGui::Separator();

        // Control commands help
        if (ImGui::CollapsingHeader("Control Commands")) {
            ImGui::BulletText("Ctrl-L  /  ESC L   Clear screen");
            ImGui::BulletText("Ctrl-R  /  ESC R   Reset Apple 1");
            ImGui::BulletText("Ctrl-O  /  ESC O   Toggle outgoing uppercase");
            ImGui::BulletText("Ctrl-I  /  ESC I   Toggle incoming uppercase");
            ImGui::BulletText("Ctrl-T  /  ESC T   Toggle 8-bit mode");
            ImGui::Spacing();
            ImGui::TextWrapped(
                "macOS/BSD: the tty line discipline eats Ctrl-T (status), "
                "Ctrl-O (discard) and Ctrl-R (rprnt) before telnet/nc can send "
                "them. Use the ESC-prefixed alternates (ESC then the letter), "
                "or disable the intercepts with 'stty status undef discard "
                "undef rprnt undef' before connecting.");
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::renderA1IO_RTCWindow()
{
    ImGui::SetNextWindowSize(ImVec2(380, 420), ImGuiCond_FirstUseEver);
    applyPendingLayout("P-LAB I/O Board & RTC");
    if (ImGui::Begin("P-LAB I/O Board & RTC", &showA1IO_RTC)) {
        const auto& snap = uiSnapshot.a1ioRtc;

        // RTC Clock display
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f),
            ICON_FA_CLOCK " %02d:%02d:%02d", snap.hour, snap.minute, snap.second);
        ImGui::SameLine();
        ImGui::Text("  %02d/%02d/20%02d", snap.day, snap.month, snap.year);

        ImGui::Separator();

        // Temperature
        ImGui::Text("DS3231 Temp: %d C", snap.tempRTC);
        if (snap.tempDS18B20 > 0 || snap.tempDS18B20dec > 0) {
            ImGui::Text("DS18B20:     %d.%d C", snap.tempDS18B20, snap.tempDS18B20dec);
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "DS18B20:     (probe disabled)");
        }

        ImGui::Separator();

        // Digital Outputs (16 bits)
        if (ImGui::CollapsingHeader("Digital Outputs (16)", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (int i = 15; i >= 0; --i) {
                bool on = (snap.digitalOutputs >> i) & 1;
                ImGui::SameLine();
                if (i == 7) { ImGui::SameLine(0, 8); } // gap between high/low byte
                ImVec4 color = on ? ImVec4(0.2f, 0.9f, 0.2f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
                ImGui::TextColored(color, "%d", on ? 1 : 0);
            }
            ImGui::Text("  Output: $%04X", snap.digitalOutputs);
        }

        // Analog Inputs (8 channels)
        if (ImGui::CollapsingHeader("Analog Inputs (8)")) {
            for (int i = 0; i < 8; ++i) {
                ImGui::Text("  CH%d: %3d", i + 1, snap.analogInputs[i]);
                ImGui::SameLine();
                ImGui::PushID(i);
                float val = static_cast<float>(snap.analogInputs[i]);
                ImGui::ProgressBar(val / 255.0f, ImVec2(120, 14), "");
                ImGui::PopID();
            }
        }

        // Digital Inputs (4 channels)
        if (ImGui::CollapsingHeader("Digital Inputs (4)")) {
            for (int i = 0; i < 4; ++i) {
                bool high = snap.digitalInputs[i] != 0;
                ImGui::TextColored(
                    high ? ImVec4(0.2f, 0.9f, 0.2f, 1.0f) : ImVec4(0.9f, 0.2f, 0.2f, 1.0f),
                    "  D%d: %s", i + 1, high ? "HIGH" : "LOW");
            }
        }

        ImGui::Separator();

        // VIA info
        ImGui::Text("VIA 65C22 at $2000-$200F");
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
            "Broadcast reg: %d  Strobe: %s",
            snap.currentRegister, snap.strobeActive ? "HIGH" : "LOW");

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Register Map")) {
            ImGui::BulletText("$2000  PORTB - Data bus (ATMEGA)");
            ImGui::BulletText("$2001  PORTA - Addr/ctrl (strobe, RW)");
            ImGui::BulletText("$2002  DDRB  - Data Direction B");
            ImGui::BulletText("$2003  DDRA  - Data Direction A");
            ImGui::BulletText("$200A  SR    - Shift Reg (16 outputs)");
            ImGui::BulletText("$200B  ACR   - Aux Control Register");
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::renderPR40Window()
{
    ImGui::SetNextWindowSize(ImVec2(440, 780), ImGuiCond_FirstUseEver);
    applyPendingLayout("SWTPC PR-40 Printer");
    if (ImGui::Begin("SWTPC PR-40 Printer", &showPR40)) {
        ensurePR40MechPhotoTexture();
        const auto& snap = uiSnapshot.pr40;

        // Status
        if (snap.busy) {
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f),
                ICON_FA_PRINT " BUSY (printing ~0.8 s mechanical cycle)");
        } else {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f),
                ICON_FA_PRINT " IDLE - ready to receive");
        }
        ImGui::Text("FIFO: %d / %d", snap.fifoLevel, PR40Printer::kFifoCapacity);
        ImGui::SameLine();
        ImGui::ProgressBar(static_cast<float>(snap.fifoLevel) /
                           static_cast<float>(PR40Printer::kFifoCapacity),
                           ImVec2(140, 12), "");
        ImGui::Text("Characters: %d    Lines: %d    Pages torn: %d",
                    snap.charactersPrinted, snap.linesPrinted, snap.pagesTornOff);

        ImGui::Separator();

        // DPDT switch
        ImGui::Text("DPDT switch (Jobs 1976 / 3-position community mod):");
        int mode = static_cast<int>(snap.mode);
        if (ImGui::RadioButton("Off##pr40mode", mode == 0)) {
            emulation->setPR40SwitchMode(0);
            setStatusMessage("PR-40: switch OFF - printer disconnected from PIA", 2.0f);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Mixed##pr40mode", mode == 1)) {
            emulation->setPR40SwitchMode(1);
            setStatusMessage("PR-40: Mixed mode - video + printer busy OR-merged on PB7", 2.0f);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Print Only##pr40mode", mode == 2)) {
            emulation->setPR40SwitchMode(2);
            setStatusMessage("PR-40: Print Only - PB7 bypasses video /RDA", 2.0f);
        }
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "Mixed = Jobs' original 2-pos wiring.  Print Only = later 3-pos mod.");

        ImGui::Separator();

        // Paper roll — full session history. ImGuiListClipper keeps the
        // per-frame TextUnformatted call count bounded to what's visible even
        // when the roll has thousands of lines.
        ImGui::Text("Paper roll (3 7/8\" continuous, 40 col — %d line%s this session):",
                    static_cast<int>(snap.recentLines.size()),
                    snap.recentLines.size() == 1 ? "" : "s");
        // Paper-roll look: off-white cream paper + black ink, period-faithful
        // to the PR-40's continuous 3 7/8" thermal-style roll.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(245, 240, 225, 255));
        ImGui::PushStyleColor(ImGuiCol_Text,    IM_COL32(20, 20, 20, 255));
        ImGui::PushStyleColor(ImGuiCol_Border,  IM_COL32(150, 140, 120, 255));
        ImGui::BeginChild("##pr40paper", ImVec2(0, 340), true);
        {
            // Wrap at the child's right edge so narrow ribbon widths don't
            // truncate printed lines (replaces the prior horizontal scrollbar).
            // ListClipper can't be used here — wrapping makes per-line height
            // non-uniform. For a typical PR-40 session (a few hundred lines
            // of ≤40 chars) rendering everything is well under a millisecond.
            ImGui::PushTextWrapPos(0.0f);
            for (const auto& line : snap.recentLines) {
                ImGui::TextUnformatted(line.c_str());
            }
            ImGui::PopTextWrapPos();
            // Auto-scroll when the user is at (or near) the bottom, so new
            // lines paid-out by the printer stay visible without yanking
            // scrollback when the user is reading old lines.
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor(3);

        if (ImGui::Button("Tear off page")) {
            emulation->clearPR40Paper();
            setStatusMessage("PR-40: page torn off", 2.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy to clipboard")) {
            std::string all;
            // Rough reservation: 41 chars/line (40 + '\n'). Avoids reallocs
            // on long rolls.
            all.reserve(snap.recentLines.size() * 41);
            for (const auto& line : snap.recentLines) {
                all += line;
                all += '\n';
            }
            ImGui::SetClipboardText(all.c_str());
            char msg[96];
            std::snprintf(msg, sizeof(msg),
                          "PR-40: %d line%s copied to clipboard",
                          static_cast<int>(snap.recentLines.size()),
                          snap.recentLines.size() == 1 ? "" : "s");
            setStatusMessage(msg, 2.5f);
        }
        ImGui::SameLine();
        // Save path resolves to the absolute cwd-relative location so the
        // user can actually find the file (was a status-message usability
        // bug: "saved to pr40_paper.txt" with no hint that cwd = build/).
        if (ImGui::Button("Save to pr40_paper.txt")) {
            std::error_code ec;
            const std::filesystem::path rel("pr40_paper.txt");
            const std::filesystem::path abs = std::filesystem::absolute(rel, ec);
            const std::string path = ec ? rel.string() : abs.string();
            std::string err;
            if (emulation->savePR40PaperRoll(path, err)) {
                setStatusMessage("PR-40: paper roll saved to " + path, 4.0f);
            } else {
                setStatusMessage(std::string("PR-40 save failed: ") + err, 4.0f);
            }
        }

        // Footer photo of the real PR-40 mechanism (top-down view of the
        // Sanders 240/M-style print head + paper roll + ribbon spools).
        // Fit to the full content width, aspect-preserved.
        if (pr40MechPhotoTexture != 0 && pr40MechPhotoWidth > 0 && pr40MechPhotoHeight > 0) {
            ImGui::Separator();
            const float availW = ImGui::GetContentRegionAvail().x;
            const float aspect = static_cast<float>(pr40MechPhotoHeight)
                               / static_cast<float>(pr40MechPhotoWidth);
            ImGui::Image((ImTextureID)(uintptr_t)pr40MechPhotoTexture,
                         ImVec2(availW, availW * aspect));
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::renderJukeBoxWindow()
{
    ImGui::SetNextWindowSize(ImVec2(420, 360), ImGuiCond_FirstUseEver);
    const char* windowTitle = "P-LAB Juke-Box";
    applyPendingLayout(windowTitle);
    if (ImGui::Begin(windowTitle, &showJukeBox)) {
        const auto& snap = uiSnapshot.jukeBox;

        // Firmware signature row — the one check that tells the user whether
        // the loaded ROM will actually respond to BD00R.
        if (snap.firmwarePresent) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f),
                ICON_FA_CIRCLE_CHECK " Program Manager signature at $BD00: FOUND");
            ImGui::Text("Boot page: %u", static_cast<unsigned>(snap.bootPage));
        } else {
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f),
                ICON_FA_TRIANGLE_EXCLAMATION " Program Manager signature at $BD00: MISSING");
            ImGui::TextWrapped(
                "Load a Juke-Box ROM built with P-LAB's EPROM_CREATOR "
                "(2-packer.sh or build_jukebox_rom.py) as roms/jukebox.rom. "
                "Without it the card is installed but BD00R hangs.");
        }

        // Current bank-register state — live because the firmware drives it
        // through $CA00 writes on every Px / Sx command.
        if (snap.pageCount > 1) {
            ImGui::Text("Current page: %u / %u    Sub-page: %u ($CA00 = $%02X)",
                        static_cast<unsigned>(snap.currentPage),
                        static_cast<unsigned>(snap.pageCount) - 1,
                        static_cast<unsigned>(snap.currentSubPage),
                        static_cast<unsigned>(snap.bankRegister));

            // --- Page navigator: directly write the $CA00 bank latch ---
            // Equivalent to the Program Manager's `Px` / `Sx` commands but
            // available without entering the firmware. The Program Manager
            // can still rewrite $CA00 on its next command.
            int curPage = static_cast<int>(snap.currentPage);
            int subPage = static_cast<int>(snap.currentSubPage);

            ImGui::PushItemWidth(80);
            if (ImGui::ArrowButton("##jb_page_prev", ImGuiDir_Left) && curPage > 0) {
                uint8_t v = static_cast<uint8_t>((subPage << 4) | (curPage - 1));
                emulation->setJukeBoxBankRegister(v);
            }
            ImGui::SameLine();
            if (ImGui::SliderInt("##jb_page", &curPage, 0, snap.pageCount - 1, "Page %u")) {
                uint8_t v = static_cast<uint8_t>((subPage << 4) | (curPage & 0x0F));
                emulation->setJukeBoxBankRegister(v);
            }
            ImGui::SameLine();
            if (ImGui::ArrowButton("##jb_page_next", ImGuiDir_Right)
                && curPage < snap.pageCount - 1) {
                uint8_t v = static_cast<uint8_t>((subPage << 4) | (curPage + 1));
                emulation->setJukeBoxBankRegister(v);
            }
            ImGui::PopItemWidth();

            // Sub-page toggle is meaningful only with the 16 kB ROM window.
            if (snap.jumper == JukeBox::Jumper::RAM32_ROM16) {
                ImGui::SameLine();
                bool sx = (subPage == 1);
                if (ImGui::Checkbox("Sx (upper 16 kB)", &sx)) {
                    uint8_t v = static_cast<uint8_t>(((sx ? 1 : 0) << 4) | (curPage & 0x0F));
                    emulation->setJukeBoxBankRegister(v);
                }
            }

            // --- Page editor: copy one 32 kB page over another ---
            // Authoring helper. Real flash needs an external programmer;
            // POM1 mutates the in-memory ROM buffer. Use "Save ROM to file"
            // below to persist the edits to roms/jukebox.rom.
            static int s_copyFrom = 0;
            static int s_copyTo   = 0;
            if (s_copyFrom >= snap.pageCount) s_copyFrom = 0;
            if (s_copyTo   >= snap.pageCount) s_copyTo   = 0;

            ImGui::PushItemWidth(70);
            ImGui::SliderInt("##jb_copy_from", &s_copyFrom, 0, snap.pageCount - 1, "From P%u");
            ImGui::SameLine();
            ImGui::TextDisabled(ICON_FA_ARROW_RIGHT);
            ImGui::SameLine();
            ImGui::SliderInt("##jb_copy_to", &s_copyTo, 0, snap.pageCount - 1, "To P%u");
            ImGui::PopItemWidth();
            ImGui::SameLine();
            const bool copyAllowed = (s_copyFrom != s_copyTo);
            ImGui::BeginDisabled(!copyAllowed);
            if (ImGui::Button("Copy page")) {
                std::string error;
                if (emulation->copyJukeBoxPage(static_cast<uint8_t>(s_copyFrom),
                                               static_cast<uint8_t>(s_copyTo), error)) {
                    char msg[96];
                    snprintf(msg, sizeof(msg),
                             "Juke-Box: copied page %d -> %d (RAM only — Save ROM to persist)",
                             s_copyFrom, s_copyTo);
                    setStatusMessage(msg, 4.0f);
                } else {
                    setStatusMessage("Juke-Box page copy failed: " + error, 4.0f);
                }
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::BeginDisabled(snap.romPath.empty());
            if (ImGui::Button("Save ROM to file")) {
                std::string error;
                if (emulation->saveJukeBoxRom("", error)) {
                    setStatusMessage("Juke-Box ROM saved: " + snap.romPath, 3.0f);
                } else {
                    setStatusMessage("Juke-Box ROM save failed: " + error, 4.0f);
                }
            }
            ImGui::EndDisabled();
        }

        ImGui::Separator();

        // Chip mode — which physical chip is socketed.
        ImGui::Text("Physical chip:");
        int modeInt = static_cast<int>(snap.chipMode);
        if (ImGui::RadioButton("Flash (paged, 16 kB..512 kB, read-only)",
                               &modeInt, static_cast<int>(JukeBox::ChipMode::Flash))) {
            jukeBoxChipMode = JukeBox::ChipMode::Flash;
            emulation->setJukeBoxChipMode(JukeBox::ChipMode::Flash);
            setStatusMessage("Juke-Box chip: Flash (paged, read-only)", 2.0f);
        }
        if (ImGui::RadioButton("EEPROM 28c256 (32 kB, writable with RW jumper)",
                               &modeInt, static_cast<int>(JukeBox::ChipMode::EEPROM28C256))) {
            jukeBoxChipMode = JukeBox::ChipMode::EEPROM28C256;
            emulation->setJukeBoxChipMode(JukeBox::ChipMode::EEPROM28C256);
            setStatusMessage("Juke-Box chip: EEPROM 28c256 (writable)", 2.0f);
        }

        ImGui::Separator();

        // ROM file info
        ImGui::Text("ROM file:");
        ImGui::SameLine();
        if (snap.romPath.empty()) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(none)");
        } else {
            ImGui::TextWrapped("%s", snap.romPath.c_str());
        }
        ImGui::Text("Size: %zu bytes (%.1f kB, %u page%s of 32 kB)",
                    snap.romSize,
                    static_cast<double>(snap.romSize) / 1024.0,
                    static_cast<unsigned>(snap.pageCount),
                    snap.pageCount == 1 ? "" : "s");

        if (ImGui::Button("Reload ROM")) {
            std::string error;
            if (emulation->reloadJukeBoxRom(error)) {
                setStatusMessage("Juke-Box ROM reloaded", 2.0f);
            } else {
                setStatusMessage(error, 4.0f);
            }
        }

        ImGui::Separator();

        int jumperInt = static_cast<int>(snap.jumper);
        // Jumper toggle — changing this swaps the ROM window + RAM ceiling.
        ImGui::Text("RAM / ROM jumper:");
        if (ImGui::RadioButton("32 kB RAM / 16 kB ROM  ($8000-$BFFF)",
                               &jumperInt, static_cast<int>(JukeBox::Jumper::RAM32_ROM16))) {
            jukeBoxJumper = JukeBox::Jumper::RAM32_ROM16;
            if (jukeBoxEnabled)
                evictMemoryMapRegionsForJukeBox();
            emulation->setJukeBoxJumper(jukeBoxJumper);
            emulation->setPresetRamKB(32);
            presetRamKB = 32;
            setStatusMessage("Juke-Box jumper: 32 kB RAM / 16 kB ROM", 2.0f);
        }
        if (ImGui::RadioButton("16 kB RAM / 32 kB ROM  ($4000-$BFFF)",
                               &jumperInt, static_cast<int>(JukeBox::Jumper::RAM16_ROM32))) {
            jukeBoxJumper = JukeBox::Jumper::RAM16_ROM32;
            if (jukeBoxEnabled)
                evictMemoryMapRegionsForJukeBox();
            emulation->setJukeBoxJumper(jukeBoxJumper);
            emulation->setPresetRamKB(16);
            presetRamKB = 16;
            setStatusMessage("Juke-Box jumper: 16 kB RAM / 32 kB ROM", 2.0f);
        }
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
            "Real hardware needs power-off to move the jumper - POM1 hot-swaps.");

        ImGui::Separator();

        // EEPROM RW jumper — only meaningful when the 28c256 is socketed.
        if (snap.chipMode == JukeBox::ChipMode::EEPROM28C256) {
            bool writable = snap.writable;
            if (ImGui::Checkbox("EEPROM write-enable (28xxx RW jumper)", &writable)) {
                emulation->setJukeBoxWritable(writable);
                setStatusMessage(writable
                    ? "Juke-Box EEPROM: write-enabled (writes persist to jukebox.rom)"
                    : "Juke-Box EEPROM: read-only", 3.0f);
            }
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                "When on, writes in the ROM window update the jukebox.rom file.");
        } else {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                "Flash is read-only - switch to EEPROM 28c256 to use the RW jumper.");
        }

        ImGui::Separator();

        if (ImGui::CollapsingHeader("Usage")) {
            ImGui::BulletText("BD00R   Launch the Program Manager (& prompt)");
            ImGui::BulletText("H       Help - list all Program Manager commands");
            ImGui::BulletText("D       Directory of programs on the current page");
            ImGui::BulletText("L<X>    Load program tagged with letter X");
            ImGui::BulletText("P<0-F>  Switch flash bank (writes to $CA00)");
            ImGui::BulletText("S<0|1>  Pick lower/upper 16 kB sub-page (16 kB logical mapping)");
            ImGui::BulletText("B       Enter BASIC (non-destructive, via E2B3R)");
            ImGui::BulletText("X       Exit Program Manager back to Woz Monitor");
            ImGui::Spacing();
            ImGui::TextWrapped(
                "Save-Program (B800R, # prompt): W = write RAM range to EEPROM, "
                "S = save current BASIC program, L = back to Program Manager, "
                "X = exit to Woz Monitor. Requires EEPROM 28c256 chip mode "
                "with the RW jumper on.");
        }

        if (ImGui::CollapsingHeader("Memory Map")) {
            if (snap.jumper == JukeBox::Jumper::RAM16_ROM32) {
                ImGui::BulletText("$0000-$3FFF  RAM (16 kB contiguous)");
                ImGui::BulletText("$4000-$4FFF  BASIC blob (copied to $E000 by LC)");
                ImGui::BulletText("$5000-$AFFF  Programs (Blocks 0-6)");
                ImGui::BulletText("$B000-$B7FF  Block 0 / Reserved");
                ImGui::BulletText("$B800-$BFFF  Save Program ($B800), Program Manager ($BD00)");
                ImGui::BulletText("$E000-$EFFF  RAM (BASIC interpreter lands here)");
            } else {
                ImGui::BulletText("$0000-$7FFF  RAM (32 kB contiguous)");
                ImGui::BulletText("$8000-$BFFF  ROM window (upper 16 kB of file)");
                ImGui::BulletText("$BD00        Program Manager (firmware entry)");
                ImGui::BulletText("$E000-$EFFF  RAM (BASIC interpreter lands here)");
            }
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::renderCodeTankWindow()
{
    ImGui::SetNextWindowSize(ImVec2(380, 260), ImGuiCond_FirstUseEver);
    applyPendingLayout("P-LAB CodeTank");
    if (ImGui::Begin("P-LAB CodeTank", &showCodeTank)) {
        const auto& snap = uiSnapshot.codeTank;
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f),
            ICON_FA_CIRCLE_CHECK " Fixed ROM window: $4000-$7FFF (16 kB)");
        ImGui::Text("Selected half: %s 16 kB",
                    snap.jumper == CodeTank::Jumper::Upper16 ? "upper" : "lower");
        ImGui::TextWrapped(
            "Standalone P-LAB ROM card built around a single 32 kB 28c256. "
            "The board jumper picks which 16 kB half is wired into "
            "$4000-$7FFF; the other half stays available by flipping the "
            "jumper. No Program Manager, no bank latch.");

        ImGui::Separator();
        ImGui::Text("ROM file:");
        ImGui::SameLine();
        if (snap.romPath.empty()) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                "(blank 28c256 — open the CodeTank Library to load one)");
        } else {
            ImGui::TextWrapped("%s", snap.romPath.c_str());
        }
        ImGui::Text("Size: %zu bytes (two 16 kB banks)", snap.romSize);

        if (ImGui::Button("Open CodeTank Library...")) {
            showCodeTankLibrary = true;
        }

        ImGui::Separator();

        int jumperInt = static_cast<int>(snap.jumper);
        ImGui::Text("Board jumper:");
        if (ImGui::RadioButton("Lower 16 kB of 28c256  ($4000-$7FFF)",
                               &jumperInt, static_cast<int>(CodeTank::Jumper::Lower16))) {
            codeTankJumper = CodeTank::Jumper::Lower16;
            emulation->setCodeTankJumper(codeTankJumper);
            setStatusMessage("CodeTank jumper: lower 16 kB", 2.0f);
        }
        if (ImGui::RadioButton("Upper 16 kB of 28c256  ($4000-$7FFF)",
                               &jumperInt, static_cast<int>(CodeTank::Jumper::Upper16))) {
            codeTankJumper = CodeTank::Jumper::Upper16;
            emulation->setCodeTankJumper(codeTankJumper);
            setStatusMessage("CodeTank jumper: upper 16 kB", 2.0f);
        }
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
            "Real hardware needs power-off to move the jumper - POM1 hot-swaps.");

        if (ImGui::CollapsingHeader("Memory Map")) {
            ImGui::BulletText("$0000-$3FFF  RAM (16 kB contiguous)");
            ImGui::BulletText("$4000-$7FFF  CodeTank ROM window (selected 16 kB half)");
            ImGui::BulletText("$CC00/$CC01  TMS9918 ports (coexists with CodeTank)");
            ImGui::BulletText("$E000-$EFFF  Integer BASIC ROM");
            ImGui::BulletText("$FF00-$FFFF  Woz Monitor");
        }
    }
    ImGui::End();
}

namespace {

// Search a list of candidate parents for a `codetank/` library directory.
// Mirrors the multi-cwd probe used elsewhere (build/, repo root, packaged
// macOS bundle). Returns the first directory that exists, or an empty path.
std::filesystem::path resolveCodeTankLibraryRoot()
{
    namespace fs = std::filesystem;
    const char* parents[] = { "roms", "../roms", "../../roms" };
    for (const char* p : parents) {
        fs::path candidate = fs::path(p) / "codetank";
        std::error_code ec;
        if (fs::is_directory(candidate, ec)) return candidate;
    }
    // Fall back to the parent dir of the legacy single-file path so the
    // user always sees `codetank.rom` even before the directory exists.
    for (const char* p : parents) {
        std::error_code ec;
        if (std::filesystem::is_directory(p, ec)) return std::filesystem::path(p);
    }
    return {};
}

struct CodeTankLibraryEntry {
    std::filesystem::path path;
    std::string           filename;     // display name (no parent dirs)
    std::uintmax_t        size = 0;     // bytes; 32768 is the only valid size
    std::string           description;  // sidecar .txt contents (if any), trimmed
    bool                  mirrored = false; // lower 16 kB == upper 16 kB
};

std::vector<CodeTankLibraryEntry> scanCodeTankLibrary()
{
    namespace fs = std::filesystem;
    std::vector<CodeTankLibraryEntry> out;
    fs::path root = resolveCodeTankLibraryRoot();
    if (root.empty()) return out;
    std::error_code ec;
    // The "library" is every .rom / .bin under roms/codetank/ plus the
    // legacy roms/codetank.rom (single-file shipped with POM1). Both ends
    // up in the same `out` vector deduplicated by absolute path.
    auto pushCandidate = [&](const fs::path& p) {
        if (!fs::is_regular_file(p, ec)) return;
        const auto sz = fs::file_size(p, ec);
        if (ec || sz != 0x8000u) return;   // CodeTank ROMs are exactly 32 kB
        const fs::path canon = fs::weakly_canonical(p, ec);
        for (const auto& existing : out) {
            std::error_code ec2;
            if (fs::weakly_canonical(existing.path, ec2) == canon) return;
        }
        CodeTankLibraryEntry e;
        e.path     = p;
        e.filename = p.filename().string();
        e.size     = sz;
        // Mirror detection: a 32 kB ROM whose two 16 kB halves are byte-
        // identical carries the same content under either jumper position
        // (typical of `--layout=menu` ROMs). The library shows one button.
        std::ifstream rf(p, std::ios::binary);
        if (rf) {
            std::vector<char> data(0x8000);
            rf.read(data.data(), data.size());
            if (rf.gcount() == static_cast<std::streamsize>(data.size())) {
                e.mirrored = std::memcmp(data.data(),
                                         data.data() + 0x4000, 0x4000) == 0;
            }
        }
        // Optional sidecar description: same name, ".txt" extension.
        fs::path sidecar = p; sidecar.replace_extension(".txt");
        if (fs::is_regular_file(sidecar, ec)) {
            std::ifstream f(sidecar);
            std::string buf((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
            // Trim trailing whitespace / newlines so the UI wraps cleanly.
            while (!buf.empty() && (buf.back() == '\n' || buf.back() == '\r'
                                    || buf.back() == ' ' || buf.back() == '\t'))
                buf.pop_back();
            e.description = std::move(buf);
        }
        out.push_back(std::move(e));
    };

    // Walk roms/codetank/ if it exists.
    if (fs::is_directory(root, ec) && root.filename() == "codetank") {
        for (const auto& entry : fs::directory_iterator(root, ec)) {
            const auto& p = entry.path();
            const auto ext = p.extension().string();
            if (ext == ".rom" || ext == ".bin") pushCandidate(p);
        }
    }
    // Always offer the shipped roms/codetank.rom (one parent up when root
    // is roms/codetank, or `root/codetank.rom` if root is the roms/ dir
    // because no `codetank/` subdirectory exists).
    fs::path legacy = (root.filename() == "codetank")
                      ? root.parent_path() / "codetank.rom"
                      : root / "codetank.rom";
    pushCandidate(legacy);
    std::sort(out.begin(), out.end(),
              [](const CodeTankLibraryEntry& a, const CodeTankLibraryEntry& b) {
                  return a.filename < b.filename;
              });
    return out;
}

} // namespace

void MainWindow_ImGui::renderCodeTankLibraryWindow()
{
    ImGui::SetNextWindowSize(ImVec2(480, 360), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("P-LAB CodeTank Library", &showCodeTankLibrary)) {
        // Cache the scan across frames; refresh on the Refresh button. Static
        // is fine here: this window only ever runs on the UI thread.
        static std::vector<CodeTankLibraryEntry> entries;
        static bool firstScan = true;
        if (firstScan) {
            entries = scanCodeTankLibrary();
            firstScan = false;
        }
        if (ImGui::SmallButton("Refresh")) {
            entries = scanCodeTankLibrary();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("roms/codetank/  (drop 32 kB .rom files here)");

        ImGui::Separator();

        // Currently-loaded ROM (highlighted in green).
        const std::string& currentRom = uiSnapshot.codeTank.romPath;

        ImGui::BeginChild("##codetank_lib_scroll",
                          ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 8.0f));
        if (entries.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f),
                ICON_FA_TRIANGLE_EXCLAMATION " No 32 kB ROMs in roms/codetank/.");
        } else {
            // Reused by both Load buttons below — keeps state side-effects in
            // one place so adding a new ROM type doesn't drift.
            auto plug = [&](const CodeTankLibraryEntry& e, CodeTank::Jumper j,
                            const char* halfTag) {
                std::string err;
                if (!emulation->loadCodeTankRom(e.path.string(), err)) {
                    setStatusMessage("CodeTank load failed: " + err, 5.0f);
                    return;
                }
                codeTankJumper = j;
                emulation->setCodeTankJumper(codeTankJumper);
                if (jukeBoxEnabled) {
                    jukeBoxEnabled = false;
                    emulation->setJukeBoxEnabled(false);
                }
                if (!codeTankEnabled) {
                    codeTankEnabled = true;
                    emulation->setCodeTankEnabled(true);
                }
                showCodeTank = true;
                setStatusMessage(std::string("CodeTank: ") + e.filename + halfTag,
                                 3.0f);
            };

            for (size_t i = 0; i < entries.size(); ++i) {
                const auto& e = entries[i];
                const bool isActive = (currentRom == e.path.string());
                ImGui::PushID(static_cast<int>(i));
                if (isActive) {
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          ImVec4(0.4f, 0.95f, 0.4f, 1.0f));
                    ImGui::Text(ICON_FA_PLAY " %s", e.filename.c_str());
                    ImGui::PopStyleColor();
                } else {
                    ImGui::TextUnformatted(e.filename.c_str());
                }
                if (!e.description.empty()) {
                    ImGui::Indent();
                    ImGui::TextWrapped("%s", e.description.c_str());
                    ImGui::Unindent();
                }
                if (e.mirrored) {
                    // Both halves identical (typical of menu-layout ROMs):
                    // one Load button is enough.
                    if (ImGui::Button("Load")) plug(e, CodeTank::Jumper::Lower16, "");
                } else {
                    if (ImGui::Button("Load Lower")) plug(e, CodeTank::Jumper::Lower16, " (lower)");
                    ImGui::SameLine();
                    if (ImGui::Button("Load Upper")) plug(e, CodeTank::Jumper::Upper16, " (upper)");
                }
                ImGui::Separator();
                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (codeTankEnabled) {
            if (ImGui::Button("Unplug CodeTank")) {
                emulation->setCodeTankEnabled(false);
                codeTankEnabled = false;
                setStatusMessage("CodeTank unplugged", 2.0f);
            }
            ImGui::SameLine();
        }
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
            "CodeTank window: $4000-$7FFF. Coexists with TMS9918 ($CC00/$CC01).");
    }
    ImGui::End();
}
