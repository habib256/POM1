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
#include "TelemetryPort.h"  // schema/data frame sentinels for the decoded-state table

#include "imgui.h"
#include "IconsFontAwesome6.h"
#include "Pom1BenchHost.h"  // POM1 host for the portable bench/CodeBench editor
#include "CodeBench.h"      // bench/ portable editor window

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
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// cc65 Verify (Phase C) shells out to ca65/ld65 — desktop only.
#if !POM1_IS_WASM && !defined(_WIN32)
  #include <sys/wait.h>
#endif

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

    // Sync cosmetic monitor knobs into the rasterizer each frame — the UI
    // owns the source-of-truth flags but GraphicsCard memoises its state
    // internally and bypasses the diff when colour mode + persistence both
    // sit at their defaults.
    graphicsCard.setMonitorMode(
        static_cast<GraphicsCard::MonitorMode>(gen2MonitorMode));
    graphicsCard.setPhosphorPersistence(gen2PhosphorPersistence);
    graphicsCard.setScanlineAlpha(gen2ScanlineAlpha);

    // Beam-raced render (Phase 3): the soft-switch journal of the last
    // completed video frame travels with the snapshot; render() replays it
    // (vertical bands + horizontal mid-scanline splits) or falls back to
    // the per-scanline-diffed HGR fast path when the latch sits at the
    // classic GRAPHICS+HIRES+PAGE1 state with no events — an idle legacy
    // framebuffer still costs ~7.7 KB of memcmp and zero pixel writes.
    if (graphicsCard.render(uiSnapshot.memory.data(),
                            uiSnapshot.gen2DisplayState,
                            uiSnapshot.gen2FrameStartState,
                            uiSnapshot.gen2VideoEvents,
                            uiSnapshot.gen2FiftyHz
                                ? Gen2VideoScanner::kLinesPerFrame50Hz
                                : Gen2VideoScanner::kLinesPerFrame)) {
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

        const ImVec2 imgScreenPos = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID)(uintptr_t)graphicsCardTexture, size);

        // Scanline overlay — drawn after the image so it sits on top of the
        // texture pixels. Reuses Screen_ImGui's "1-px dark row every 2 display
        // pixels" model with user-controllable alpha. Skipped at alpha=0.
        if (gen2ScanlineAlpha > 0.001f && size.y > 1.0f) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const int alpha8 = static_cast<int>(gen2ScanlineAlpha * 255.0f) & 0xFF;
            const ImU32 col = IM_COL32(0, 0, 0, alpha8);
            const float pixelH = size.y / static_cast<float>(GraphicsCard::kHiresHeight);
            // Step every 2 logical scanlines so we get the alternating dark
            // pattern. Use integer y to avoid sub-pixel AA halving.
            for (int line = 0; line < GraphicsCard::kHiresHeight; line += 2) {
                const float y = std::floor(imgScreenPos.y + line * pixelH);
                dl->AddRectFilled(ImVec2(imgScreenPos.x, y),
                                  ImVec2(imgScreenPos.x + size.x, y + 1.0f),
                                  col);
            }
        }

        // Image-only window: nothing but the beam-cathode picture is drawn in
        // the content area. Cosmetic monitor knobs + the live $C25x latch state
        // live in a right-click context menu so the window stays pure picture
        // (invisible until summoned — no function is lost).
        if (ImGui::BeginPopupContextWindow("##gen2ctx")) {
            // Live soft-switch latch ($C250-$C257, read-only — read toggles +
            // returns HST0 in D7). Mirrors the Apple II $C05x semantics.
            const auto& ds = uiSnapshot.gen2DisplayState;
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                "$C25x: %s %s %s %s%s",
                ds.textMode ? "TEXT" : (ds.hiRes ? "HIRES" : "LORES"),
                ds.mixedMode ? "MIXED" : "FULL",
                ds.page2 ? "PAGE2" : "PAGE1",
                uiSnapshot.gen2FiftyHz ? "50Hz" : "60Hz",
                uiSnapshot.gen2VideoEvents.empty() ? "" : " \xC2\xB7 beam-split");
            ImGui::Separator();

            const char* modes[] = { "Colour", "Green (P1)", "Amber (P3)", "Mono" };
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::Combo("Monitor##gen2mode", &gen2MonitorMode, modes, IM_ARRAYSIZE(modes))) {
                graphicsCard.setMonitorMode(
                    static_cast<GraphicsCard::MonitorMode>(gen2MonitorMode));
            }
            ImGui::SetNextItemWidth(200.0f);
            ImGui::SliderFloat("Phosphor persistence##gen2persist",
                               &gen2PhosphorPersistence, 0.0f, 0.95f, "%.2f");
            ImGui::SetNextItemWidth(200.0f);
            ImGui::SliderFloat("Scanline overlay##gen2scan",
                               &gen2ScanlineAlpha, 0.0f, 1.0f, "%.2f");

            // Vertical-rate jumper of the release card: 262 lines @ 60 Hz
            // or 312 @ 50 Hz (NTSC color either way — Bernie's spec asks
            // emulators to expose the option). Changes VBL length and the
            // HST0 cadence, not the visible 192 lines.
            bool fiftyHz = uiSnapshot.gen2FiftyHz;
            if (ImGui::Checkbox("50 Hz vertical (312 lines)##gen2fiftyhz", &fiftyHz)) {
                emulation->setGen2FiftyHz(fiftyHz);
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void MainWindow_ImGui::renderTMS9918Window()
{
    if (bringTms9918WindowToFront) {
        ImGui::SetNextWindowFocus();
        bringTms9918WindowToFront = false;
    }

    // Lazy texture creation — nearest-neighbour GL_NEAREST so every window size
    // gives a clean pixel-art result without the integer-scale black borders.
    // Texture spans the FULL 288×216 frame (active 256×192 + R7 border bands).
    if (tms9918Texture == 0) {
        glGenTextures(1, &tms9918Texture);
        glBindTexture(GL_TEXTURE_2D, tms9918Texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     TMS9918::kFullWidth, TMS9918::kFullHeight,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    // The TMS9918 emulation already rasterises line-by-line into
    // uiSnapshot.tms9918.framebuffer (silicon-progressive raster, R7 border
    // bands, mid-frame R7/R1/VRAM changes all reflected). Upload the
    // framebuffer directly to the GPU texture — no per-snapshot rendering
    // needed in the UI path. IM_COL32 byte order [R,G,B,A] on little-endian
    // matches GL_RGBA/GL_UNSIGNED_BYTE.
    glBindTexture(GL_TEXTURE_2D, tms9918Texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    TMS9918::kFullWidth, TMS9918::kFullHeight,
                    GL_RGBA, GL_UNSIGNED_BYTE,
                    uiSnapshot.tms9918.framebuffer.data());

    const float defPs = kTMS9918DefaultPixelScale;
    const float winW = TMS9918::kFullWidth  * defPs + 16.0f;
    const float winH = TMS9918::kFullHeight * defPs + 36.0f;
    ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_FirstUseEver);
    const float minWinW = TMS9918::kFullWidth  * kVideoCardMinPixelScale + 16.0f;
    const float minWinH = TMS9918::kFullHeight * kVideoCardMinPixelScale + 36.0f;
    ImGui::SetNextWindowSizeConstraints(ImVec2(minWinW, minWinH), ImVec2(FLT_MAX, FLT_MAX));
    applyPendingLayout("P-LAB Graphic Card (TMS9918)");
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 255));
    if (ImGui::Begin("P-LAB Graphic Card (TMS9918)", &showTMS9918)) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float ps = std::min(avail.x / TMS9918::kFullWidth,
                            avail.y / TMS9918::kFullHeight);
        ps = std::max(ps, kVideoCardMinPixelScale);
        ImVec2 size(std::floor(TMS9918::kFullWidth  * ps),
                    std::floor(TMS9918::kFullHeight * ps));

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
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Drop the active TCP connection (sends NO CARRIER)");
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::renderTerminalCardWindow()
{
    ImGui::SetNextWindowSize(ImVec2(360, 280), ImGuiCond_FirstUseEver);
    applyPendingLayout("P-LAB Terminal Card");
    if (ImGui::Begin("P-LAB Terminal Card", &showTerminalCard)) {
#if POM1_IS_WASM
        // Browsers cannot open a listening TCP socket, so the telnet server
        // never comes up in the web build. Make this visible up front so the
        // user doesn't think the card is broken (the native screen + keyboard
        // keep working; only the external-terminal bridge is unavailable).
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
        ImGui::TextWrapped("Web build: the telnet server is disabled.");
        ImGui::PopStyleColor();
        ImGui::TextWrapped("Browsers cannot open a listening TCP socket, so "
                           "'telnet localhost 6502' has nothing to connect to. "
                           "Use the desktop build to drive POM1 from an external "
                           "terminal.");
        ImGui::Separator();
#endif
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

// Parse whitespace/comma-separated hex byte tokens ("06 41 0D", "AA,BB") into
// bytes. Shared by the Serial Monitor send line and the Bench "Raw bytes" upload.
static void parseHexTokens(const char* s, std::vector<unsigned char>& out)
{
    const char* p = s;
    while (*p) {
        while (*p && !std::isxdigit((unsigned char)*p)) ++p;
        if (!*p) break;
        int v = 0, digits = 0;
        while (*p && std::isxdigit((unsigned char)*p) && digits < 2) {
            char c = *p++;
            int d = (c <= '9') ? c - '0' : (c | 0x20) - 'a' + 10;
            v = v * 16 + d; ++digits;
        }
        out.push_back(static_cast<unsigned char>(v & 0xFF));
    }
}


// Rebuild the cached hex-dump / raw-text rendering of the accumulated TX bytes.
// Hex view = `OFFS  HH HH …(16)…  ascii`; text view maps printable bytes through
// and shows the rest as '.'. Called only when the byte buffer or view toggles.
static void formatTelemetryMonitor(const std::vector<unsigned char>& bytes,
                                   bool hex, std::string& out)
{
    out.clear();
    if (hex) {
        char line[96];
        for (std::size_t i = 0; i < bytes.size(); i += 16) {
            int n = std::snprintf(line, sizeof(line), "%04zX  ", i);
            std::string ascii;
            for (std::size_t j = 0; j < 16; ++j) {
                if (i + j < bytes.size()) {
                    unsigned char b = bytes[i + j];
                    n += std::snprintf(line + n, sizeof(line) - n, "%02X ", b);
                    ascii += (b >= 0x20 && b < 0x7F) ? static_cast<char>(b) : '.';
                } else {
                    n += std::snprintf(line + n, sizeof(line) - n, "   ");
                }
            }
            out.append(line);
            out.append(" ");
            out.append(ascii);
            out.append("\n");
        }
    } else {
        out.reserve(bytes.size());
        for (unsigned char b : bytes)
            out += (b >= 0x20 && b < 0x7F) || b == '\n' ? static_cast<char>(b) : '.';
    }
}

// ─────────────────────────────────────────────────────────────
// Self-describing telemetry: schema-driven "Decoded state" table
//
// Generalisation, not game-specific. We parse two frame kinds out of the same
// outbound wire stream (telemetryMonitorBytes): a SCHEMA frame (sentinel 0xA5)
// declaring a list of {type, name} field descriptors, and DATA frames (0xAA)
// carrying the field VALUES in schema order. The decoded table is built purely
// from whatever schema the game last emitted — nothing here knows about any
// particular game. See doc/TELEMETRY_SIDE_CHANNEL.md.
// ─────────────────────────────────────────────────────────────

// Field type codes (shared wire contract). Sized payload bytes per the type.
enum class TeleFieldType : uint8_t {
    U8 = 1, S8 = 2, U16 = 3, S16 = 4, Bool = 5, Char = 6
};

struct TeleField {
    TeleFieldType type;
    std::string   name;
};

static const char* teleTypeName(TeleFieldType t)
{
    switch (t) {
    case TeleFieldType::U8:   return "U8";
    case TeleFieldType::S8:   return "S8";
    case TeleFieldType::U16:  return "U16";
    case TeleFieldType::S16:  return "S16";
    case TeleFieldType::Bool: return "BOOL";
    case TeleFieldType::Char: return "CHAR";
    }
    return "?";
}

// Bytes a field of this type consumes from a data-frame payload.
static std::size_t teleTypeSize(TeleFieldType t)
{
    switch (t) {
    case TeleFieldType::U16:
    case TeleFieldType::S16:  return 2;
    default:                  return 1;   // U8/S8/BOOL/CHAR
    }
}

// Walk the wire buffer as [sentinel][len_lo][len_hi][payload] frames, recording
// the byte ranges of the LAST schema (0xA5) and LAST data (0xAA) frame seen.
// Returns true if a well-formed frame of the given sentinel was found; the
// payload range is [outBegin, outBegin+outLen). A truncated trailing frame
// (header or payload running past the buffer) is ignored.
static bool teleFindLastFrame(const std::vector<unsigned char>& buf, uint8_t sentinel,
                              std::size_t& outBegin, std::size_t& outLen)
{
    bool found = false;
    std::size_t i = 0;
    while (i + 3 <= buf.size()) {
        const uint8_t sent = buf[i];
        const std::size_t len = buf[i + 1] | (static_cast<std::size_t>(buf[i + 2]) << 8);
        if (i + 3 + len > buf.size()) break;   // truncated tail — stop
        if (sent == sentinel) {
            outBegin = i + 3;
            outLen   = len;
            found    = true;
        }
        i += 3 + len;
    }
    return found;
}

// Parse a schema-frame payload ([type:1][name ASCII…][0x00] descriptors) into a
// field list. Stops cleanly on a malformed/truncated descriptor.
static void teleParseSchema(const unsigned char* p, std::size_t len,
                            std::vector<TeleField>& out)
{
    out.clear();
    std::size_t i = 0;
    while (i < len) {
        const uint8_t code = p[i++];
        if (code < 1 || code > 6) break;        // unknown type — give up
        std::string name;
        while (i < len && p[i] != 0x00) name += static_cast<char>(p[i++]);
        if (i >= len) break;                    // name not terminated — truncated
        ++i;                                    // skip the 0x00 terminator
        out.push_back({ static_cast<TeleFieldType>(code), std::move(name) });
    }
}

// Decode one field's value (at *p, span bytes) into a display string.
static std::string teleDecodeValue(TeleFieldType type, const unsigned char* p, std::size_t span)
{
    char tmp[32];
    switch (type) {
    case TeleFieldType::U8:
        std::snprintf(tmp, sizeof(tmp), "%u", static_cast<unsigned>(p[0]));
        return tmp;
    case TeleFieldType::S8:
        std::snprintf(tmp, sizeof(tmp), "%d", static_cast<int>(static_cast<int8_t>(p[0])));
        return tmp;
    case TeleFieldType::U16: {
        const unsigned v = static_cast<unsigned>(p[0]) | (static_cast<unsigned>(p[1]) << 8);
        std::snprintf(tmp, sizeof(tmp), "%u", v);
        return tmp;
    }
    case TeleFieldType::S16: {
        const int16_t v = static_cast<int16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
        std::snprintf(tmp, sizeof(tmp), "%d", static_cast<int>(v));
        return tmp;
    }
    case TeleFieldType::Bool:
        return p[0] ? "true" : "false";
    case TeleFieldType::Char: {
        const unsigned char c = p[0];
        if (c >= 0x20 && c < 0x7F) std::snprintf(tmp, sizeof(tmp), "'%c'  ($%02X)", c, c);
        else                       std::snprintf(tmp, sizeof(tmp), "$%02X", c);
        return tmp;
    }
    }
    (void)span;
    return "?";
}

// Render the schema-driven "Decoded state" table from the accumulated wire bytes.
// Game-agnostic: every row name/type/value comes from the game's own schema. If
// no schema frame has been seen yet, show a greyed hint (the raw Serial Monitor
// below still carries the bytes).
static void renderTelemetryDecodedState(const std::vector<unsigned char>& bytes)
{
    ImGui::SeparatorText("Decoded state");

    std::size_t schemaBegin = 0, schemaLen = 0;
    if (!teleFindLastFrame(bytes, TelemetryPort::kSchemaSentinel, schemaBegin, schemaLen)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::TextWrapped("No schema frame — showing raw bytes below. A game can emit a "
                           "schema frame (TELE_CTRL=$03) so its fields appear here by name. "
                           "See doc/TELEMETRY_SIDE_CHANNEL.md.");
        ImGui::PopStyleColor();
        return;
    }

    std::vector<TeleField> fields;
    teleParseSchema(bytes.data() + schemaBegin, schemaLen, fields);
    if (fields.empty()) {
        ImGui::TextDisabled("Schema frame seen but no valid field descriptors decoded.");
        return;
    }

    std::size_t dataBegin = 0, dataLen = 0;
    const bool haveData = teleFindLastFrame(bytes, TelemetryPort::kFrameSentinel, dataBegin, dataLen);

    if (ImGui::BeginTable("##telemetry_decoded", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Field");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Value");
        ImGui::TableHeadersRow();

        std::size_t off = 0;            // offset into the data payload
        for (const TeleField& f : fields) {
            const std::size_t span = teleTypeSize(f.type);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(f.name.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(teleTypeName(f.type));
            ImGui::TableSetColumnIndex(2);
            if (haveData && off + span <= dataLen) {
                const std::string v = teleDecodeValue(f.type, bytes.data() + dataBegin + off, span);
                ImGui::TextUnformatted(v.c_str());
            } else {
                ImGui::TextDisabled("--");   // no value for this field yet
            }
            off += span;
        }
        ImGui::EndTable();
    }

    // Length-mismatch note: the data frame is shorter or longer than the schema.
    if (haveData) {
        std::size_t expected = 0;
        for (const TeleField& f : fields) expected += teleTypeSize(f.type);
        if (dataLen != expected) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.3f, 1.0f));
            ImGui::TextWrapped("Note: data frame is %zu B but schema expects %zu B "
                               "(%s) — showing what fits.",
                               dataLen, expected,
                               dataLen < expected ? "fields truncated" : "extra trailing bytes");
            ImGui::PopStyleColor();
        }
    } else {
        ImGui::TextDisabled("No data frame yet — fields will fill in as the game emits state.");
    }
}

void MainWindow_ImGui::renderTelemetryWindow()
{
    ImGui::SetNextWindowSize(ImVec2(440, 520), ImGuiCond_FirstUseEver);
    applyPendingLayout("Telemetry Side Channel");
    if (ImGui::Begin("Telemetry Side Channel", &showTelemetry)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::TextWrapped("Dev-only test-harness port at $C440-$C443 (not real "
                           "hardware) — the SDK's \"serial\". A game writes state + "
                           "an end-frame marker; this Serial Monitor shows the "
                           "outbound stream and injects inbound bytes (TELE_IN), "
                           "exactly as a TCP harness would. If the game emits a schema "
                           "frame (TELE_CTRL=$03) the \"Decoded state\" table below "
                           "names its fields. doc/TELEMETRY_SIDE_CHANNEL.md.");
        ImGui::PopStyleColor();
        ImGui::Separator();

#if POM1_IS_WASM
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
        ImGui::TextWrapped("Web build: no listening TCP socket, so no external "
                           "harness — but the Serial Monitor below still taps the "
                           "game's output and can inject input.");
        ImGui::PopStyleColor();
        ImGui::Separator();
#endif

        const auto& snap = uiSnapshot.telemetry;

        bool enabled = uiSnapshot.telemetryEnabled;
        if (ImGui::Checkbox("Enabled", &enabled)) {
            emulation->setTelemetryEnabled(enabled);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(opens localhost:%d)", snap.listenPort);

        if (uiSnapshot.telemetryEnabled) {
            // ---- Connection / counters ----
            if (snap.serverListening)
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f),
                    ICON_FA_SERVER " Listening on port %d", snap.listenPort);
            else
                ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f),
                    ICON_FA_SERVER " Server not running");
            ImGui::SameLine();
            if (snap.clientConnected)
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f),
                    ICON_FA_PLUG " %s", snap.clientAddress.c_str());
            else
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                    ICON_FA_PLUG " no harness");

            ImGui::Text("Lock-step: %s   Frames: %u   TX: %u B   RX: %u B",
                        snap.lockstep ? (snap.awaitingAck ? "PARKED" : "ARMED") : "off",
                        snap.framesSent, snap.bytesSent, snap.bytesReceived);

            // ---- Flow control: pause / step / run the game at FRAME granularity ----
            // Works for any telemetry game that closes each frame with TELE_FRAME
            // (e.g. the GEN2 Snake). Pause arms lock-step so the game halts at its
            // next emitted frame; Step releases exactly one; Run frees it again.
            ImGui::TextUnformatted("Flow:");
            ImGui::SameLine();
            ImGui::BeginDisabled(snap.lockstep);
            if (ImGui::Button(ICON_FA_PAUSE " Pause")) emulation->setTelemetryLockstep(true);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Pause the game: it halts at its next emitted frame (arms lock-step).");
            ImGui::SameLine();
            ImGui::BeginDisabled(!snap.lockstep);
            if (ImGui::Button(ICON_FA_FORWARD_STEP " Step")) emulation->telemetryReleaseFrame();
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Advance exactly one game frame, then re-park.");
            ImGui::SameLine();
            ImGui::BeginDisabled(!snap.lockstep);
            if (ImGui::Button(ICON_FA_PLAY " Run")) emulation->setTelemetryLockstep(false);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Resume free-running (disarm lock-step, release any park).");
            ImGui::SameLine();
            ImGui::TextDisabled(snap.awaitingAck ? "(parked)" : snap.lockstep ? "(armed)" : "(free)");

            // ---- Consume the TX tap (delta vs last seen total) ----
            uint64_t total = snap.txTotal;
            if (total < telemetryLastTxTotal) telemetryLastTxTotal = 0; // port was reset
            if (total > telemetryLastTxTotal) {
                uint64_t newCount = total - telemetryLastTxTotal;
                if (newCount > snap.txTap.size()) newCount = snap.txTap.size(); // fell behind
                const std::size_t off = snap.txTap.size() - static_cast<std::size_t>(newCount);
                telemetryMonitorBytes.insert(telemetryMonitorBytes.end(),
                                             snap.txTap.begin() + static_cast<std::ptrdiff_t>(off),
                                             snap.txTap.end());
                telemetryLastTxTotal = total;
                telemetryMonitorDirty = true;
                constexpr std::size_t kCap = 64 * 1024;
                if (telemetryMonitorBytes.size() > kCap)
                    telemetryMonitorBytes.erase(telemetryMonitorBytes.begin(),
                        telemetryMonitorBytes.begin() +
                        static_cast<std::ptrdiff_t>(telemetryMonitorBytes.size() - kCap));
            }

            // ---- Decoded state (schema-driven, game-agnostic) ----
            // Built from telemetryMonitorBytes, which the TX-tap block above has
            // just refreshed, so it reflects the latest schema + data frames.
            renderTelemetryDecodedState(telemetryMonitorBytes);

            // ---- Serial Monitor ----
            ImGui::Separator();
            ImGui::TextUnformatted("Serial Monitor (TX from game)");
            ImGui::SameLine();
            if (ImGui::Checkbox("Hex", &telemetryMonitorHex)) telemetryMonitorDirty = true;
            ImGui::SameLine();
            ImGui::Checkbox("Auto-scroll", &telemetryMonitorAutoScroll);
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) {
                telemetryMonitorBytes.clear();
                telemetryMonitorText.clear();
                telemetryMonitorDirty = false;
            }

            if (telemetryMonitorDirty) {
                formatTelemetryMonitor(telemetryMonitorBytes, telemetryMonitorHex,
                                       telemetryMonitorText);
                telemetryMonitorDirty = false;
            }

            ImGui::BeginChild("##telemetry_monitor", ImVec2(0, 180), true,
                              ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.95f, 0.55f, 1.0f));
            ImGui::TextUnformatted(telemetryMonitorText.c_str(),
                                   telemetryMonitorText.c_str() + telemetryMonitorText.size());
            ImGui::PopStyleColor();
            if (telemetryMonitorAutoScroll &&
                ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
                ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();

            // ---- Inbound injection (Serial Monitor → game) ----
            auto sendInput = [&]() {
                std::vector<unsigned char> out;
                if (telemetrySendHex) {
                    parseHexTokens(telemetrySendBuf, out);
                } else {
                    for (const char* p = telemetrySendBuf; *p; ++p)
                        out.push_back(static_cast<unsigned char>(*p));
                }
                if (!out.empty())
                    emulation->telemetryInject(out.data(), out.size());
                telemetrySendBuf[0] = '\0';
            };

            ImGui::SetNextItemWidth(-160.0f);
            bool entered = ImGui::InputText("##telemetry_send", telemetrySendBuf,
                                            sizeof(telemetrySendBuf),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (ImGui::Button("Send") || entered) sendInput();
            ImGui::SameLine();
            ImGui::Checkbox("Hex##send", &telemetrySendHex);
            ImGui::TextDisabled("→ TELE_IN ($C442). %s",
                                telemetrySendHex ? "Bytes: e.g. \"06 41 0D\"."
                                                 : "ASCII text.");

            // ---- Golden-trace log ----
            ImGui::Separator();
            ImGui::SetNextItemWidth(-90.0f);
            ImGui::InputText("##telemetry_log", telemetryLogPathBuf, sizeof(telemetryLogPathBuf));
            ImGui::SameLine();
            if (ImGui::Button("Log to file"))
                emulation->setTelemetryLogFile(telemetryLogPathBuf);
            ImGui::TextDisabled("Tees every frame to disk (same as --telemetry-log).");
        } else {
            ImGui::TextDisabled("Disabled — tick Enabled, or pass --telemetry-port N.");
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Registers ($C440-$C443)")) {
            ImGui::BulletText("$C440 TELE_DATA  (W)  push a byte into the frame");
            ImGui::BulletText("$C441 TELE_CTRL  (W)  $01 end-frame / $02 arm lock-step / $00 disarm");
            ImGui::Indent();
            ImGui::BulletText("$03 schema-frame: same payload window, but field descriptors");
            ImGui::BulletText("([type][name][$00]) — never parks lock-step. Decoded above.");
            ImGui::Unindent();
            ImGui::BulletText("$C441 TELE_STAT  (R)  b7 harness connected, b0 inbound available");
            ImGui::BulletText("$C442 TELE_IN    (R)  pop one inbound byte (ACK $06 is consumed)");
            ImGui::BulletText("$C443 TELE_INLEN (R)  inbound bytes pending");
            ImGui::Separator();
            ImGui::TextDisabled("Schema + data frames ride the same outbound (read) wire "
                                "stream; the decoder keeps the last of each.");
        }
    }
    ImGui::End();
}

// The POM1 Bench editor is the portable bench/CodeBench, driven by a
// Pom1BenchHost (cc65 toolchain, presets, CodeTank/loadBinary deploy, telemetry
// Serial Monitor). See bench/IBenchHost.h. Host + bench are created lazily.
void MainWindow_ImGui::renderBenchWindow()
{
    if (!benchHost_) {
        benchHost_ = std::make_unique<Pom1BenchHost>(this);
        codeBench_ = std::make_unique<bench::CodeBench>(benchHost_.get());
    }
    codeBench_->render("POM1 Bench", &showBench);
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
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Clear the paper roll (discards printed lines)");
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
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Copy the entire paper roll to the system clipboard");
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
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Write the paper roll to pr40_paper.txt in the working directory");

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
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Previous page (writes $CA00)");
            ImGui::SameLine();
            if (ImGui::SliderInt("##jb_page", &curPage, 0, snap.pageCount - 1, "Page %u")) {
                uint8_t v = static_cast<uint8_t>((subPage << 4) | (curPage & 0x0F));
                emulation->setJukeBoxBankRegister(v);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Drag to select Px page (writes $CA00 bank latch)");
            ImGui::SameLine();
            if (ImGui::ArrowButton("##jb_page_next", ImGuiDir_Right)
                && curPage < snap.pageCount - 1) {
                uint8_t v = static_cast<uint8_t>((subPage << 4) | (curPage + 1));
                emulation->setJukeBoxBankRegister(v);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Next page (writes $CA00)");
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
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(copyAllowed
                    ? "Copy 32 KB page From -> To (RAM only — use Save ROM to persist)"
                    : "Source and destination pages must differ");
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
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(snap.romPath.empty()
                    ? "No ROM loaded yet"
                    : "Persist in-memory ROM edits to roms/jukebox.rom");
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
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Re-read roms/jukebox.rom from disk (discards unsaved page edits)");

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

static std::string codeTankTrim(std::string s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.erase(0, 1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.pop_back();
    return s;
}

static bool codeTankStartsWithCi(const std::string& s, size_t off, const char* lit)
{
    const size_t n = std::strlen(lit);
    if (s.size() < off + n) return false;
    for (size_t k = 0; k < n; k++) {
        if (std::tolower(static_cast<unsigned char>(s[off + k]))
            != std::tolower(static_cast<unsigned char>(lit[k])))
            return false;
    }
    return true;
}

// Sidecar lines use "4000R → …" or "4000R -> …"; strip repeated leading
// segments so the UI shows the human-facing blurb only.
static void codeTankStripLeading4000rArrow(std::string& s)
{
    for (;;) {
        size_t i = 0;
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
            ++i;
        if (!codeTankStartsWithCi(s, i, "4000r")) break;
        i += 5;
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
            ++i;
        if (i + 2 < s.size()
            && static_cast<unsigned char>(s[i]) == 0xe2
            && static_cast<unsigned char>(s[i + 1]) == 0x86
            && static_cast<unsigned char>(s[i + 2]) == 0x92) {
            i += 3;
        } else if (i + 1 < s.size() && s[i] == '-' && s[i + 1] == '>') {
            i += 2;
        } else {
            break;
        }
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
            ++i;
        s.erase(0, i);
    }
}

static std::string codeTankJumperBlurbFromPayload(std::string payload)
{
    std::string s = codeTankTrim(std::move(payload));
    codeTankStripLeading4000rArrow(s);
    return codeTankTrim(std::move(s));
}

static bool codeTankLineIsTitleBlurb(const std::string& t)
{
    std::string lower = t;
    for (char& c : lower) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return lower.find("codetank cartridge") != std::string::npos;
}

struct CodeTankLibraryEntry {
    std::filesystem::path path;
    std::string           filename; // display name (no parent dirs)
    std::uintmax_t        size = 0; // bytes; 32768 is the only valid size
    bool                  sidecarPresent = false;
    std::string           bankLowerBlurb; // from sidecar "Lower jumper:" line
    std::string           bankUpperBlurb; // from sidecar "Upper jumper:" line
    std::string           sidecarExtra;   // other non-title lines from .txt
    bool                  mirrored = false; // lower 16 kB == upper 16 kB
};

static void parseCodeTankSidecarText(std::string text, CodeTankLibraryEntry& e)
{
    e.bankLowerBlurb.clear();
    e.bankUpperBlurb.clear();
    e.sidecarExtra.clear();
    if (text.empty()) return;
    std::istringstream iss(std::move(text));
    std::string        line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = codeTankTrim(line);
        if (t.empty()) continue;
        std::string keyTest = t;
        for (char& c : keyTest) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        if (keyTest.rfind("lower jumper:", 0) == 0) {
            const size_t col = t.find(':');
            if (col != std::string::npos)
                e.bankLowerBlurb = codeTankJumperBlurbFromPayload(t.substr(col + 1));
            continue;
        }
        if (keyTest.rfind("upper jumper:", 0) == 0) {
            const size_t col = t.find(':');
            if (col != std::string::npos)
                e.bankUpperBlurb = codeTankJumperBlurbFromPayload(t.substr(col + 1));
            continue;
        }
        if (codeTankLineIsTitleBlurb(t)) continue;
        if (!e.sidecarExtra.empty()) e.sidecarExtra.push_back('\n');
        e.sidecarExtra += t;
    }
}

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
            while (!buf.empty() && (buf.back() == '\n' || buf.back() == '\r'
                                    || buf.back() == ' ' || buf.back() == '\t'))
                buf.pop_back();
            e.sidecarPresent = true;
            parseCodeTankSidecarText(std::move(buf), e);
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

void drawCodeTankStatusPill(const char* label, bool enabled, ImU32 onColor)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const ImVec2 size(textSize.x + 18.0f, ImGui::GetFrameHeight());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                      enabled ? onColor : IM_COL32(55, 55, 62, 255), 5.0f);
    dl->AddText(ImVec2(pos.x + 9.0f, pos.y + (size.y - textSize.y) * 0.5f),
                enabled ? IM_COL32(12, 16, 18, 255)
                        : IM_COL32(150, 150, 156, 255),
                label);
    ImGui::Dummy(size);
}

void drawCodeTankConsoleHeader(bool codeTankOnline,
                               bool tmsOnline,
                               size_t romCount)
{
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = std::max(360.0f, ImGui::GetContentRegionAvail().x);
    const ImVec2 size(width, 98.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const ImVec2 end(start.x + size.x, start.y + size.y);
    dl->AddRectFilledMultiColor(start, end,
                                IM_COL32(8, 14, 20, 255),
                                IM_COL32(20, 38, 48, 255),
                                IM_COL32(14, 18, 28, 255),
                                IM_COL32(5, 8, 14, 255));
    dl->AddRect(start, end, IM_COL32(85, 210, 190, 190), 8.0f, 0, 1.5f);
    for (float y = start.y + 8.0f; y < end.y - 6.0f; y += 6.0f) {
        dl->AddLine(ImVec2(start.x + 8.0f, y), ImVec2(end.x - 8.0f, y),
                    IM_COL32(255, 255, 255, 14));
    }

    dl->AddText(ImVec2(start.x + 18.0f, start.y + 16.0f),
                IM_COL32(92, 245, 205, 255), "P-LAB CODETANK");
    dl->AddText(ImVec2(start.x + 18.0f, start.y + 38.0f),
                IM_COL32(240, 245, 255, 255), "APPLE-1 GAME CONSOLE");

    char stats[128];
    std::snprintf(stats, sizeof(stats), "%zu ROMS  |  BOOT 4000R  |  WINDOW $4000-$7FFF",
                  romCount);
    dl->AddText(ImVec2(start.x + 18.0f, start.y + 66.0f),
                IM_COL32(255, 206, 94, 255), stats);

    const float panelW = 210.0f;
    const ImVec2 panelMin(end.x - panelW - 16.0f, start.y + 16.0f);
    const ImVec2 panelMax(end.x - 16.0f, end.y - 16.0f);
    dl->AddRectFilled(panelMin, panelMax, IM_COL32(4, 6, 10, 170), 6.0f);
    dl->AddRect(panelMin, panelMax, IM_COL32(255, 206, 94, 160), 6.0f);
    dl->AddText(ImVec2(panelMin.x + 12.0f, panelMin.y + 12.0f),
                codeTankOnline ? IM_COL32(80, 245, 120, 255)
                               : IM_COL32(170, 170, 176, 255),
                codeTankOnline ? "CODETANK ONLINE" : "CODETANK STANDBY");
    dl->AddText(ImVec2(panelMin.x + 12.0f, panelMin.y + 34.0f),
                tmsOnline ? IM_COL32(80, 245, 220, 255)
                          : IM_COL32(170, 170, 176, 255),
                tmsOnline ? "TMS9918 HOST READY" : "TMS9918 HOST OFFLINE");

    ImGui::Dummy(size);
}

} // namespace

void MainWindow_ImGui::renderCodeTankLibraryWindow()
{
    ImGui::SetNextWindowSize(ImVec2(640, 520), ImGuiCond_FirstUseEver);
    applyPendingLayout("P-LAB CodeTank Library");
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
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Re-scan roms/codetank/ for cartridges");
        ImGui::SameLine();
        ImGui::TextDisabled("roms/codetank/  (32 kB .rom/.bin cartridges)");

        ImGui::Separator();

        // Currently-loaded ROM (highlighted in green).
        const std::string& currentRom = uiSnapshot.codeTank.romPath;
        drawCodeTankConsoleHeader(codeTankEnabled, tms9918Enabled, entries.size());
        ImGui::Spacing();
        drawCodeTankStatusPill("28C256 ROM", true, IM_COL32(255, 206, 94, 255));
        ImGui::SameLine();
        drawCodeTankStatusPill("TMS9918", tms9918Enabled, IM_COL32(80, 245, 220, 255));
        ImGui::SameLine();
        drawCodeTankStatusPill("CODETANK", codeTankEnabled, IM_COL32(80, 245, 120, 255));
        ImGui::SameLine();
        ImGui::TextDisabled("Type 4000R to RUN");
        ImGui::Spacing();

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
                // CodeTank is a daughterboard of the TMS9918 — auto-plug the
                // host so the UI flags match what Memory's setCodeTankEnabled
                // is about to do.
                if (!tms9918Enabled) {
                    tms9918Enabled = true;
                    showTMS9918 = true;
                    emulation->setTMS9918Enabled(true);
                    sidSpecialEditionEnabled = false;
                }
                if (!codeTankEnabled) {
                    codeTankEnabled = true;
                    emulation->setCodeTankEnabled(true);
                }
                bringTms9918WindowToFront = true;
                ImGui::SetWindowFocus("P-LAB Graphic Card (TMS9918)");
                emulation->hardReset();
                // Cold boot to Wozmon ~3 s wall clock before auto-run (realistic panel startup).
                constexpr double kCodeTankColdBootSeconds = 3.0;
                codeTankPendingWozRunAt = ImGui::GetTime() + kCodeTankColdBootSeconds;
                setStatusMessage(std::string("CodeTank: ") + e.filename + halfTag
                                     + " — reset; 4000R in 3 s",
                                 4.0f);
            };

            for (size_t i = 0; i < entries.size(); ++i) {
                const auto& e = entries[i];
                const bool isActive = (currentRom == e.path.string());
                ImGui::PushID(static_cast<int>(i));
                ImGui::PushStyleColor(ImGuiCol_ChildBg,
                                      isActive ? IM_COL32(18, 42, 30, 255)
                                               : IM_COL32(20, 22, 28, 255));
                ImGui::PushStyleColor(ImGuiCol_Border,
                                      isActive ? IM_COL32(95, 240, 130, 210)
                                               : IM_COL32(75, 80, 92, 180));
                ImGui::BeginChild(
                    "cart", ImVec2(0, 0),
                    ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
                    ImGuiWindowFlags_NoScrollbar);
                if (isActive) {
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          ImVec4(0.4f, 0.95f, 0.4f, 1.0f));
                    ImGui::Text(ICON_FA_PLAY " INSERTED  %s", e.filename.c_str());
                    ImGui::PopStyleColor();
                } else {
                    ImGui::Text(ICON_FA_GAMEPAD " CARTRIDGE  %s", e.filename.c_str());
                }
                ImGui::SameLine();
                ImGui::TextDisabled("32 KB / 2 x 16 KB");

                ImGui::Separator();

                const float ctBtnW =
                    ImGui::CalcTextSize("Run Upper Bank").x
                    + ImGui::GetStyle().FramePadding.x * 2.0f;
                auto drawBankRow = [&](const char* label, const std::string& blurb,
                                       const char* emptyHint, CodeTank::Jumper jumper,
                                       const char* halfTag) {
                    if (ImGui::Button(label, ImVec2(ctBtnW, 0))) {
                        plug(e, jumper, halfTag);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Insert this cartridge with %s 16 KB jumper, plug TMS9918,\nhard-reset and auto-type 4000R after ~3 s",
                            jumper == CodeTank::Jumper::Lower16 ? "Lower" : "Upper");
                    }
                    ImGui::SameLine();
                    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x
                                           + ImGui::GetContentRegionAvail().x);
                    if (!blurb.empty())
                        ImGui::TextWrapped("%s", blurb.c_str());
                    else {
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("%s", emptyHint);
                    }
                    ImGui::PopTextWrapPos();
                };

                if (e.mirrored) {
                    const std::string& mirBlurb =
                        !e.bankLowerBlurb.empty() ? e.bankLowerBlurb
                        : !e.bankUpperBlurb.empty()   ? e.bankUpperBlurb
                                                      : std::string{};
                    if (ImGui::Button("Run Lower Bank##ct_mir", ImVec2(ctBtnW, 0))) {
                        plug(e, CodeTank::Jumper::Lower16, "");
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Insert this mirrored cartridge, plug TMS9918,\nhard-reset and auto-type 4000R after ~3 s");
                    ImGui::SameLine();
                    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x
                                           + ImGui::GetContentRegionAvail().x);
                    if (!mirBlurb.empty())
                        ImGui::TextWrapped("%s", mirBlurb.c_str());
                    else {
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextWrapped(
                            "Mirrored banks — same 16 kB in both halves; one entry point.");
                    }
                    ImGui::PopTextWrapPos();
                } else {
                    drawBankRow("Run Lower Bank##ct_lo", e.bankLowerBlurb,
                                "(no Lower jumper: line in sidecar)",
                                CodeTank::Jumper::Lower16, " (lower)");
                    drawBankRow("Run Upper Bank##ct_hi", e.bankUpperBlurb,
                                "(no Upper jumper: line in sidecar)",
                                CodeTank::Jumper::Upper16, " (upper)");
                }
                if (!e.sidecarExtra.empty()) {
                    ImGui::Spacing();
                    ImGui::TextWrapped("%s", e.sidecarExtra.c_str());
                }
                if (!e.sidecarPresent) {
                    ImGui::Spacing();
                    ImGui::TextDisabled(
                        "No label sidecar. Drop a matching .txt file next to the ROM for cabinet notes.");
                }
                ImGui::EndChild();
                ImGui::PopStyleColor(2);
                ImGui::Spacing();
                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (codeTankEnabled) {
            if (ImGui::Button("Unplug CodeTank")) {
                emulation->setCodeTankEnabled(false);
                codeTankEnabled = false;
                showCodeTankLibrary = false;
                codeTankPendingWozRunAt = 0.0;
                setStatusMessage("CodeTank unplugged", 2.0f);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Remove the daughterboard (TMS9918 host stays plugged)");
            ImGui::SameLine();
        }
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
            "CodeTank window: $4000-$7FFF. Coexists with TMS9918 ($CC00/$CC01).");
    }
    ImGui::End();
}

// IEC Disk window — virtual 1541 status, mounted .d64 path, label/id/blocks
// free, dir listing, mount/unmount controls. Reads current state from the
// EmulationController's IECCard. Mount/unmount mutates from the UI thread —
// safe because we hold the EmulationController's stateMutex inside its
// setIECCardEnabled / mountDisk wrappers.
void MainWindow_ImGui::renderIECCardWindow()
{
    ImGui::SetNextWindowSize(ImVec2(370, 400), ImGuiCond_FirstUseEver);
    applyPendingLayout("IEC Disk");
    if (ImGui::Begin("IEC Disk", &showIECCard)) {
        auto s = emulation->getIECCardUIState();

        ImGui::Text("Device 8 (P-LAB IEC daughterboard)");
        ImGui::Separator();

        if (!s.hasDisk) {
            ImGui::TextColored(ImVec4(0.85f, 0.55f, 0.40f, 1.0f),
                "No disk mounted.");
            ImGui::TextWrapped(
                "Drop a .d64 file at disks/iec/dev8.d64 (174 848 B standard "
                "35-track) and re-plug the IEC card to mount it.");
        } else {
            ImGui::TextWrapped("Disk: %s", s.diskPath.c_str());
            ImGui::Text("Label: %s    ID: %s    DOS: 2A",
                s.label.c_str(), s.id.c_str());
            ImGui::Text("Free: %d / %d blocks", s.blocksFree, s.totalBlocks);
            ImGui::Spacing();

            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Directory");
            ImGui::BeginChild("iec_dir", ImVec2(0, 0), true);
            for (const auto& e : s.directory) {
                const char* typ = "PRG";
                switch (e.type & 0x07) {
                    case 0x01: typ = "SEQ"; break;
                    case 0x02: typ = "PRG"; break;
                    case 0x03: typ = "USR"; break;
                    case 0x04: typ = "REL"; break;
                    default:   typ = "DEL"; break;
                }
                ImGui::Text("%4u  \"%-16s\" %s", e.blocks, e.name.c_str(), typ);
            }
            ImGui::EndChild();
        }
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Silicon Strict Inspector — single home for silicon-fidelity toggles + the
// live drop-diagnostics panel. Opened from `Hardware → Silicon Strict
// Inspector...`.
//
// Layout (top → bottom):
//   1. Goal banner
//   2. Master toggle: Silicon Strict ON/OFF (with live status pill)
//   3. CollapsingHeader "TMS9918 Graphic Card" — cold-boot noise + drop diag
//   4. CollapsingHeader "Apple-1 system RAM"  — cold-boot noise
//   5. CollapsingHeader "Juke-Box EEPROM"     — write-cycle timing + counters
//
// Cold-boot toggles apply on the NEXT hardReset / resetMemory — flipping
// them mid-frame would corrupt the running picture. The UI states this
// explicitly so users do `File → Hard Reset` after toggling.
// ---------------------------------------------------------------------------
// ---- Parmigiani's "one board at a time" rule -------------------------------
//
// Real Apple-1 bus has no arbitration: when two P-LAB cards decode the same
// address window, the bus pulls itself apart and the system hangs. POM1's
// Multiplexing Fantasy presets break this for fun (#12, #14), but silicon-
// strict mode must enforce it. The conflict table mirrors the real cards
// documented in CLAUDE.md "Parmigiani's golden rule" section.
//
// Auto-unplug priority: the secondary card in each pair gets unplugged so
// the user keeps the more "anchoring" card (TMS9918 wins over SID at $CC00,
// GEN2 keeps its 8 KB footprint over A1-IO RTC's 16 bytes, JukeBox loses to
// CodeTank/CFFA1/microSD which are more commonly the focus of a session).
namespace {
struct ConflictRule {
    const char* primary;     // kept plugged
    bool* primaryFlag;       // pointer member-of MainWindow_ImGui — set by caller
    const char* secondary;   // unplugged when both are on
    bool* secondaryFlag;
    const char* reason;      // overlap range / electrical clash
};
} // namespace

std::vector<std::string> MainWindow_ImGui::listParmigianiConflicts() const
{
    std::vector<std::string> out;
    // Pair: (active condition, description).
    struct C { bool both; const char* desc; };
    const C table[] = {
        { graphicsCardEnabled && a1ioRtcEnabled,
          "GEN2 HGR ↔ A1-IO & RTC — $2000-$200F overlap" },
        { sidEnabled && tms9918Enabled,
          "A1-SID ↔ TMS9918 — $CC00/$CC01 overlap" },
        { sidSpecialEditionEnabled && tms9918Enabled,
          "A1-AUDIO SE ↔ TMS9918 — $CC00-$CC1F overlap" },
        { sidSpecialEditionEnabled && sidEnabled,
          "A1-AUDIO SE ↔ A1-SID — same SID chip on two windows" },
        { jukeBoxEnabled && cffa1Enabled,
          "Juke-Box ↔ CFFA1 — $9000-$AFDF inside Juke-Box ROM window" },
        { jukeBoxEnabled && microSDEnabled,
          "Juke-Box ↔ microSD — $8000-$9FFF inside Juke-Box ROM window" },
        { jukeBoxEnabled && wifiModemEnabled,
          "Juke-Box ↔ Wi-Fi Modem — $B000-$B003 inside Juke-Box ROM window" },
        { jukeBoxEnabled && sidEnabled,
          "Juke-Box ↔ A1-SID — $CA00 bank latch vs SID register file" },
        { jukeBoxEnabled && codeTankEnabled,
          "Juke-Box ↔ CodeTank — share $4000-$7FFF ROM window" },
    };
    for (const auto& c : table) {
        if (c.both) out.emplace_back(c.desc);
    }
    return out;
}

std::string MainWindow_ImGui::resolveParmigianiConflicts()
{
    std::vector<std::string> evicted;
    auto unplug = [&](const char* name, auto applyOff) {
        evicted.emplace_back(name);
        applyOff();
    };
    // Order matters: handle the heaviest cards first so a chain of evictions
    // settles in one pass.
    if (jukeBoxEnabled && codeTankEnabled) {
        unplug("Juke-Box", [&] {
            jukeBoxEnabled = false;
            emulation->setJukeBoxEnabled(false);
        });
    }
    if (jukeBoxEnabled && (cffa1Enabled || microSDEnabled || wifiModemEnabled || sidEnabled)) {
        unplug("Juke-Box", [&] {
            jukeBoxEnabled = false;
            emulation->setJukeBoxEnabled(false);
        });
    }
    if (sidSpecialEditionEnabled && (tms9918Enabled || sidEnabled)) {
        unplug("A1-AUDIO SE", [&] {
            sidSpecialEditionEnabled = false;
            emulation->setSIDSpecialEditionEnabled(false);
        });
    }
    if (sidEnabled && tms9918Enabled) {
        unplug("A1-SID", [&] {
            sidEnabled = false;
            emulation->setSIDEnabled(false);
        });
    }
    if (graphicsCardEnabled && a1ioRtcEnabled) {
        unplug("A1-IO & RTC", [&] {
            a1ioRtcEnabled = false;
            emulation->setA1IO_RTCEnabled(false);
            showA1IO_RTC = false;
        });
    }
    if (evicted.empty()) return {};
    std::string msg = "[STRICT] Evicted: ";
    for (size_t i = 0; i < evicted.size(); ++i) {
        if (i) msg += ", ";
        msg += evicted[i];
    }
    return msg;
}

bool MainWindow_ImGui::gateStrictPlug(const char* cardName, bool& uiFlag)
{
    if (!siliconStrictModeEnabled) return false;
    if (!uiFlag) return false;            // user unplugged — always fine
    if (!wouldCreateConflict(cardName)) return false;
    uiFlag = false;                       // revert the UI flip
    std::string msg = "[STRICT] ";
    msg += cardName;
    msg += " refused — multiplexing forbidden. Unplug the conflicting card first.";
    setStatusMessage(msg, 4.0f);
    return true;
}

bool MainWindow_ImGui::wouldCreateConflict(const char* cardName) const
{
    if (!cardName) return false;
    auto eq = [&](const char* a) {
        return std::strcmp(cardName, a) == 0;
    };
    if (eq("GEN2"))       return a1ioRtcEnabled;
    if (eq("A1-IO-RTC"))  return graphicsCardEnabled;
    if (eq("A1-SID"))     return tms9918Enabled || sidSpecialEditionEnabled || jukeBoxEnabled;
    if (eq("TMS9918"))    return sidEnabled || sidSpecialEditionEnabled;
    if (eq("A1-AUDIO-SE")) return sidEnabled || tms9918Enabled;
    if (eq("JukeBox"))    return codeTankEnabled || cffa1Enabled || microSDEnabled
                               || wifiModemEnabled || sidEnabled;
    if (eq("CodeTank"))   return jukeBoxEnabled;
    if (eq("CFFA1"))      return jukeBoxEnabled;
    if (eq("microSD"))    return jukeBoxEnabled;
    if (eq("WiFiModem"))  return jukeBoxEnabled;
    return false;
}

void MainWindow_ImGui::renderSiliconStrictWindow()
{
    ImGui::SetNextWindowSize(ImVec2(580, 540), ImGuiCond_FirstUseEver);
    applyPendingLayout("Silicon Strict Inspector");
    if (!ImGui::Begin("Silicon Strict Inspector", &showSiliconStrictWindow)) {
        ImGui::End();
        return;
    }

    // -------- 1. Master mode-toggle button (very visible) -----------------
    //
    // Two mutually-exclusive emulation profiles:
    //   SILICON STRICT   = real Apple-1 silicon timing + drops (green pill)
    //   MULTIPLEXING FANTASY = permissive emulator path, every write lands
    //                          instantly (purple pill, matches the preset
    //                          name shipped with POM1).
    // The button background recolours by current mode so the user can read
    // it in a glance from anywhere on screen.
    {
        const bool strict = siliconStrictModeEnabled;
        const ImVec4 bg     = strict ? ImVec4(0.18f, 0.55f, 0.28f, 1.0f)
                                     : ImVec4(0.55f, 0.18f, 0.55f, 1.0f);
        const ImVec4 bgHov  = strict ? ImVec4(0.22f, 0.68f, 0.34f, 1.0f)
                                     : ImVec4(0.68f, 0.22f, 0.68f, 1.0f);
        const ImVec4 bgAct  = strict ? ImVec4(0.14f, 0.45f, 0.22f, 1.0f)
                                     : ImVec4(0.45f, 0.14f, 0.45f, 1.0f);
        // Short label so it stays readable when the window is narrow; the
        // "click to switch" hint moves to a wrapped line below the button.
        const char* label   = strict ? "SILICON STRICT" : "MULTIPLEXING FANTASY";
        ImGui::PushStyleColor(ImGuiCol_Button,        bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bgHov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  bgAct);
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1, 1, 1, 1));
        if (ImGui::Button(label, ImVec2(-FLT_MIN, 42.0f))) {
            const bool turnOn = !siliconStrictModeEnabled;
            siliconStrictModeEnabled       = turnOn;
            vramNoiseOnResetEnabled        = turnOn;
            systemRamNoiseOnResetEnabled   = turnOn;
            dramRefreshEnabled             = turnOn;
            oorStrictModeEnabled           = turnOn;
            emulation->setSiliconStrictMode(turnOn);
            emulation->setVramNoiseOnReset(turnOn);
            emulation->setSystemRamNoiseOnReset(turnOn);
            emulation->setDramRefreshEnabled(turnOn);
            emulation->setOutOfRangeStrictMode(turnOn);
            // Strict-mode RAM topology: real Apple-1 has 8 KB dual-bank RAM
            // ($0000-$0FFF + $E000-$EFFF) with $1000-$7FFF floating. Force
            // the preset RAM ceiling to 8 KB when strict is armed; restore
            // to 64 KB when fantasy is armed (anything-goes emulation map).
            presetRamKB = turnOn ? 8 : 64;
            emulation->setPresetRamKB(presetRamKB);
            // monochromeVariant stays as-is — it represents which physical card
            // Bernie shipped (colour vs B&W), independent of strict-vs-fantasy.
            std::string msg = turnOn
                ? std::string("SILICON STRICT ON — 8 KB dual-bank RAM + strict timing + noise + refresh + OOR armed")
                : std::string("MULTIPLEXING FANTASY — every silicon-fidelity knob OFF, 64 KB RAM");
            if (turnOn) {
                // Going strict: resolve Parmigiani conflicts now. Multiplexing
                // forbidden once the master switch is green.
                const std::string evicted = resolveParmigianiConflicts();
                if (!evicted.empty()) {
                    msg += " · ";
                    msg += evicted;
                }
            }
            setStatusMessage(msg, 4.5f);
        }
        ImGui::PopStyleColor(4);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "One-click profile switch. Clicking ARMS or DISARMS every\n"
                "silicon-fidelity knob at once:\n"
                "   - TMS9918 openMSX slot-table timing\n"
                "   - Juke-Box EEPROM 28c256 byte-write cycle\n"
                "   - VRAM noise on cold boot / hard reset\n"
                "   - Apple-1 RAM noise on cold boot / hard reset\n"
                "   - Apple-1 DRAM refresh stall (4/65 cycle steal)\n"
                "   - Out-of-range RAM strict (reads -> $FF, writes dropped)\n"
                "   - RAM ceiling: 8 KB dual-bank ($0000-$0FFF + $E000-$EFFF)\n"
                "   - Parmigiani's one-board-at-a-time rule (auto-evict)\n\n"
                "Silicon Strict  : every knob ON — POM1 behaves like real\n"
                "                  warm-NMOS Apple-1 silicon. Multiplexing\n"
                "                  is forbidden; conflicting cards get\n"
                "                  auto-unplugged when armed.\n"
                "Multiplexing\n"
                "Fantasy         : every knob OFF, 64 KB flat RAM, multiple\n"
                "                  cards may decode overlapping windows.\n\n"
                "You can still fine-tune individual knobs in the sections\n"
                "below after clicking the master switch.");
        }
        // Hint under the button — wraps if the window is narrow.
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::TextWrapped("%s", strict
            ? "Click the button to switch to Multiplexing Fantasy."
            : "Click the button to switch to Silicon Strict.");
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::TextWrapped(
        "Cold-boot toggles below take effect at the next Hard Reset.");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // -------- 3. Apple-1 System RAM ---------------------------------------
    if (ImGui::CollapsingHeader("Apple-1 system RAM",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SeparatorText("Cold-boot");
        bool ramFlag = systemRamNoiseOnResetEnabled;
        if (ImGui::Checkbox("RAM noise on cold boot / hard reset##ram",
                            &ramFlag)) {
            systemRamNoiseOnResetEnabled = ramFlag;
            emulation->setSystemRamNoiseOnReset(ramFlag);
            setStatusMessage(ramFlag
                ? "RAM noise ON — takes effect at next Hard Reset"
                : "RAM noise OFF — zero-init preserved", 3.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Real 6502 RAM contains bistable noise at power-on. POM1\n"
                "default is zero-init. Turn ON to seed RAM with mt19937 noise\n"
                "so programs that assume ZP/RAM = 0 fail here the same way\n"
                "they fail on cold Apple-1 silicon.");
        }

        ImGui::SeparatorText("DRAM refresh");
        bool refreshFlag = dramRefreshEnabled;
        if (ImGui::Checkbox("DRAM refresh stall (4/65 cycles stolen from CPU)",
                            &refreshFlag)) {
            dramRefreshEnabled = refreshFlag;
            emulation->setDramRefreshEnabled(refreshFlag);
            setStatusMessage(refreshFlag
                ? "DRAM refresh ON — CPU stalls 4/65 cycles per scanline"
                : "DRAM refresh OFF — CPU runs at full 1.022727 MHz", 3.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Apple-1's refresh controller halts the 6502 during 4 of every\n"
                "65 cycles (H10·H6 NAND slots at horizontal counter $C9, $D9,\n"
                "$E9, $F9 — every 10th char). Non-transparent on this design,\n"
                "so cycle-counted code (Wozmon ACI cassette, Disk II Woz\n"
                "Machine) runs SLOWER on silicon than on the emulator.\n\n"
                "Turn ON to reproduce that drift in POM1. Effective CPU rate\n"
                "drops from 1.022727 MHz to ~960 058 Hz (61/65 ratio).\n\n"
                "Reference: UncleBernie on applefritter, Jan 2022.");
        }

        const uint64_t stalls = emulation->getDramRefreshStallCount();
        ImGui::Text("Stall cycles since reset: %llu",
                    (unsigned long long)stalls);
        if (stalls > 0) {
            // Stall cycles vs total cycles is exactly 4/65 by Bresenham
            // construction; show the equivalent wallclock loss for intuition.
            constexpr double kHz = 1022727.0;
            const double stalledSeconds = stalls / kHz;
            ImGui::Text("Equivalent wallclock loss: %.3f s of CPU time",
                        stalledSeconds);
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                "(no stalls yet — flip toggle ON to start counting)");
        }
        if (ImGui::Button("Reset refresh counter")) {
            emulation->resetDramRefreshStallCount();
            setStatusMessage("DRAM refresh stall counter reset", 2.0f);
        }

        ImGui::SeparatorText("Out-of-range RAM");
        // Pull live from snapshot so flips done in Memory Settings are mirrored
        // here without delay; keep oorStrictModeEnabled in sync for the master
        // button's arm/disarm cycle.
        oorStrictModeEnabled = uiSnapshot.oorStrictMode;
        bool oorFlag = oorStrictModeEnabled;
        if (ImGui::Checkbox("Strict out-of-range RAM (reads -> $FF, writes dropped)##oor",
                            &oorFlag)) {
            oorStrictModeEnabled = oorFlag;
            emulation->setOutOfRangeStrictMode(oorFlag);
            setStatusMessage(oorFlag
                ? "OOR strict ON — accesses above preset RAM ceiling read $FF, writes dropped"
                : "OOR strict OFF — accesses above preset RAM ceiling tracked but not enforced",
                3.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Real Apple-1 with no expansion RAM in [ramKB*1024 .. $7FFF]\n"
                "reads $FF (bus floats high) and drops writes. POM1 default\n"
                "tracks accesses (status bar shows OOR:N) without enforcing.\n"
                "Turn ON for hardware-accurate behaviour at < 64 KB presets;\n"
                "programs that wrongly assume RAM is there will fail here\n"
                "exactly like on bare-4K silicon.");
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::TextWrapped(
            "Active range at %d KB preset: $%04X - $7FFF.",
            presetRamKB, presetRamKB * 1024);
        ImGui::PopStyleColor();
        if (presetRamKB >= 64) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.75f, 0.45f, 1.0f));
            ImGui::TextWrapped(
                "(No effect at 64 KB preset — no OOR region.)");
            ImGui::PopStyleColor();
        }
    }

    // -------- 4. TMS9918 Graphic Card -------------------------------------
    if (ImGui::CollapsingHeader("TMS9918 Graphic Card",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SeparatorText("Cold-boot");
        bool vramFlag = vramNoiseOnResetEnabled;
        if (ImGui::Checkbox("VRAM noise on cold boot / hard reset##vram",
                            &vramFlag)) {
            vramNoiseOnResetEnabled = vramFlag;
            emulation->setVramNoiseOnReset(vramFlag);
            setStatusMessage(vramFlag
                ? "VRAM noise ON — takes effect at next Hard Reset"
                : "VRAM noise OFF — MSX1 bistable preserved", 3.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Real P-LAB Graphic Card boots with random DRAM noise in its\n"
                "16 KB VRAM. POM1 default is the MSX1 bistable $FF/$00 pattern\n"
                "(per meisei). Turn ON to seed VRAM with true mt19937 noise so\n"
                "uninitialised-SAT bugs (doc/TMS9918-SPRITE_INIT.md §4.2) show\n"
                "up here — exactly as on real silicon.");
        }

        ImGui::SeparatorText("Live drop diagnostics");
        const auto diag = emulation->getTms9918DropDiagnostics();
        ImGui::Text("Total drops:      %llu", (unsigned long long)diag.total);
        ImGui::Text("By port:          $CC00 = %llu    $CC01 = %llu",
                    (unsigned long long)diag.writeData,
                    (unsigned long long)diag.writeCtrl);
        ImGui::Text("By display phase: Active = %llu    VBlank = %llu",
                    (unsigned long long)diag.inActive,
                    (unsigned long long)diag.inVBlank);
        ImGui::Text("By slot table:    ScreenOff=%llu  Gfx12=%llu  "
                    "Gfx3=%llu  Text=%llu",
                    (unsigned long long)diag.byTable[0],
                    (unsigned long long)diag.byTable[1],
                    (unsigned long long)diag.byTable[2],
                    (unsigned long long)diag.byTable[3]);

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                           "Top PC sites (instruction at PC-3 for STA abs)");
        std::vector<std::pair<uint16_t, uint64_t>> pcs;
        pcs.reserve(diag.byPc.size());
        for (const auto& kv : diag.byPc) pcs.emplace_back(kv.first, kv.second);
        std::sort(pcs.begin(), pcs.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        ImGui::BeginChild("silstrict_pc_hist", ImVec2(0, 140), true);
        if (pcs.empty()) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                               "(no drops since last reset)");
        } else {
            const int topN = std::min((int)pcs.size(), 16);
            for (int i = 0; i < topN; ++i) {
                ImGui::Text("  $%04X    %llu drops",
                            pcs[i].first,
                            (unsigned long long)pcs[i].second);
            }
            if ((int)pcs.size() > topN) {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                   "  ... and %d more PC sites not shown",
                                   (int)pcs.size() - topN);
            }
        }
        ImGui::EndChild();

        if (ImGui::Button("Reset TMS9918 diagnostics")) {
            emulation->resetTms9918DropCount();
            setStatusMessage("TMS9918 drop diagnostics reset", 2.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Dump to stderr (top-16)")) {
            emulation->dumpTms9918DropDiagnostics(stderr, 16);
            setStatusMessage("TMS9918 drop diagnostics written to stderr", 3.0f);
        }
    }

    // -------- 4b. Active Parmigiani conflicts -----------------------------
    if (ImGui::CollapsingHeader("Active conflicts (Parmigiani's golden rule)",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        const std::vector<std::string> conflicts = listParmigianiConflicts();
        if (conflicts.empty()) {
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f),
                "OK — every plugged card respects one-board-at-a-time.");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                "%zu conflict%s detected:", conflicts.size(),
                conflicts.size() == 1 ? "" : "s");
            for (const auto& c : conflicts) {
                ImGui::Bullet();
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.6f, 1.0f), "%s", c.c_str());
            }
            ImGui::Spacing();
            if (siliconStrictModeEnabled) {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
                    "Strict mode is ON but conflicts are active — toggling the\n"
                    "master switch will auto-evict the secondary card in each pair.");
                if (ImGui::Button("Evict conflicts now")) {
                    const std::string m = resolveParmigianiConflicts();
                    setStatusMessage(m.empty()
                        ? "No conflicts to evict"
                        : m, 4.0f);
                }
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                    "Multiplexing Fantasy mode tolerates these for emulator\n"
                    "convenience — real silicon would hang the bus.");
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "P-LAB designer Claudio PARMIGIANI's golden rule: on real\n"
                "Apple-1 hardware exactly ONE card may decode each address\n"
                "window at a time. POM1's Multiplexing Fantasy presets break\n"
                "this on purpose (#12, #14). Silicon Strict mode auto-evicts\n"
                "the secondary card in every conflict pair when armed.");
        }
    }

    // -------- 5. Juke-Box EEPROM 28c256 -----------------------------------
    if (ImGui::CollapsingHeader("Juke-Box EEPROM (28c256)",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!jukeBoxEnabled) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                "(Juke-Box card unplugged — plug it from Hardware menu)");
        } else {
            const bool isEeprom =
                (uiSnapshot.jukeBox.chipMode == JukeBox::ChipMode::EEPROM28C256);
            ImGui::Text("Chip mode: %s",
                        isEeprom ? "EEPROM 28c256 (writable)"
                                 : "Flash (read-only)");
            if (!isEeprom) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                ImGui::TextWrapped(
                    "(Flash mode has no per-byte write cycle to enforce — "
                    "switch to EEPROM via Hardware → Juke-Box.)");
                ImGui::PopStyleColor();
            } else {
                constexpr double kHz = 1022727.0;
                ImGui::SeparatorText("Write timing");
                int writeCycleCpu = emulation->getJukeBoxEepromWriteCycleCpu();
                float writeMs = static_cast<float>(writeCycleCpu * 1000.0 / kHz);
                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::SliderFloat("Write cycle (ms)##eepromtwc",
                                       &writeMs, 1.0f, 25.0f, "%.1f ms")) {
                    int newCycles = static_cast<int>(writeMs * kHz / 1000.0);
                    emulation->setJukeBoxEepromWriteCycleCpu(newCycles);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Byte-write cycle duration of the 28c256.\n"
                        "Datasheet typical = 10 ms, max = 20 ms depending\n"
                        "on fab lot. Default 10 ms = 10228 cycles @ 1.022727 MHz.");
                }

                ImGui::SeparatorText("Live counters");
                const uint64_t total   = emulation->getJukeBoxEepromWritesTotal();
                const uint64_t dropped = emulation->getJukeBoxEepromWritesDropped();
                const bool busy        = emulation->isJukeBoxEepromWriteBusy();
                const int busyCycles   = emulation->getJukeBoxEepromWriteBusyCycles();
                const double busyMs    = busyCycles * 1000.0 / kHz;
                ImGui::Text("Successful writes: %llu",
                            (unsigned long long)total);
                ImGui::TextColored(dropped > 0
                                       ? ImVec4(1.0f, 0.55f, 0.4f, 1.0f)
                                       : ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                                   "Dropped (busy):    %llu",
                                   (unsigned long long)dropped);
                if (busy) {
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
                                       "WRITE BUSY — %d cycles remaining (%.2f ms)",
                                       busyCycles, busyMs);
                } else {
                    ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.5f, 1.0f), "Ready");
                }
                if (!siliconStrictModeEnabled) {
                    ImGui::Spacing();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
                    ImGui::TextWrapped(
                        "Strict mode is OFF — every write lands instantly "
                        "(no drops, legacy POM1 behaviour). Enable the master "
                        "switch at the top to model 28c256 silicon properly.");
                    ImGui::PopStyleColor();
                }
                if (ImGui::Button("Reset EEPROM counters")) {
                    emulation->resetJukeBoxEepromCounters();
                    setStatusMessage("Juke-Box EEPROM counters reset", 2.0f);
                }
            }
        }
    }

    ImGui::End();
}
