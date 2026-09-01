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
#include "ResourceLocator.h"
#include "POM1Build.h"
#include "PomRenderer.h"
#include "WiFiModem.h"
#include "TerminalCard.h"
#include "PR40Printer.h"
#include "TelemetryPort.h"  // schema/data frame sentinels for the decoded-state table

#include "imgui.h"
#include "IconsFontAwesome6.h"
#if POM1_DEVTOOLS
#include "Pom1BenchHost.h"  // POM1 host for the portable bench/CodeBench editor
#include "CodeBench.h"      // bench/ portable editor window
#endif
// Hardware framebuffer textures (HGR / TMS9918 / GT6144) used to drive GL
// directly; they now go through PomRenderer so the same code path lights up
// either OpenGL or Metal (Phase 2). No direct GL headers needed here.

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
    auto* r = pom1::renderer();
    if (!r) return;
    if (!graphicsCardTexture) {
        graphicsCardTexture = r->createTexture(
            GraphicsCard::kHiresWidth, GraphicsCard::kHiresHeight,
            pom1::PomRenderer::Filter::Nearest);
    }

    // Sync cosmetic monitor knobs into the rasterizer each frame — the UI
    // owns the source-of-truth flags but GraphicsCard memoises its state
    // internally and bypasses the diff when colour mode + persistence both
    // sit at their defaults.
    graphicsCard.setMonitorMode(
        static_cast<GraphicsCard::MonitorMode>(gen2MonitorMode));
    graphicsCard.setRenderMode(
        static_cast<GraphicsCard::RenderMode>(gen2RenderMode));
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
        r->updateTexture(graphicsCardTexture,
                         reinterpret_cast<const uint32_t*>(graphicsCard.pixels()));
        lastCardFbChangeTime = ImGui::GetTime();   // adaptive-UI: card is animating
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
        ImGui::Image(crtEffects.apply(pom1::Pom1CrtEffects::Slot::Gen2Hgr,
                                       graphicsCardTexture,
                                       r->textureWidth(graphicsCardTexture),
                                       r->textureHeight(graphicsCardTexture),
                                       std::max(1, static_cast<int>(size.x)),
                                       std::max(1, static_cast<int>(size.y))),
                     size);

        // Scanline overlay — drawn after the image so it sits on top of the
        // texture pixels. Reuses Screen_ImGui's "1-px dark row every 2 display
        // pixels" model with user-controllable alpha. Skipped at alpha=0, and
        // when the shader CRT is active (it already supplies scanlines).
        if (gen2ScanlineAlpha > 0.001f && size.y > 1.0f && !crtEffects.active()) {
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

            // HIRES colour pipeline: the calibrated MAME artifact-colour LUT
            // (fast path v1) vs OpenEmulator's composite NTSC demodulator run
            // on the CPU (softer, hardware-faithful). Both feed the same
            // 280×192 buffer, so switching is free.
            const char* renderModes[] = { "NTSC MAME (actuel)",
                                          "Composite OpenEmulator CPU" };
            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::Combo("NTSC render##gen2render", &gen2RenderMode,
                             renderModes, IM_ARRAYSIZE(renderModes))) {
                graphicsCard.setRenderMode(
                    static_cast<GraphicsCard::RenderMode>(gen2RenderMode));
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "HIRES colour decode.\n"
                    "NTSC MAME: calibrated 128-entry artifact-colour LUT.\n"
                    "Composite OpenEmulator CPU: builds the 14.318 MHz composite\n"
                    "signal and runs the 17-tap FIR NTSC demodulator on the CPU\n"
                    "(no GLSL) — softer, physically faithful mid-tones.");
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

    // Lazy texture creation — nearest-neighbour so every window size gives a
    // clean pixel-art result without the integer-scale black borders. Texture
    // spans the FULL 288×216 frame (active 256×192 + R7 border bands).
    auto* r = pom1::renderer();
    if (!r) return;
    if (!tms9918Texture) {
        tms9918Texture = r->createTexture(
            TMS9918::kFullWidth, TMS9918::kFullHeight,
            pom1::PomRenderer::Filter::Nearest);
    }

    // The TMS9918 emulation already rasterises line-by-line into
    // uiSnapshot.tms9918.framebuffer (silicon-progressive raster, R7 border
    // bands, mid-frame R7/R1/VRAM changes all reflected). IM_COL32 byte order
    // [R,G,B,A] on little-endian matches the renderer's RGBA8 layout.
    // Upload dirty-gate: the chip re-rasterises every line every frame even
    // when nothing changes, so "did the picture change" can only be decided
    // here — memcmp against the last uploaded copy (~249 KB, µs, early-out)
    // and skip the GPU upload when identical.
    static_assert(sizeof(uiSnapshot.tms9918.framebuffer) ==
                  sizeof(tms9918PixelBuf), "TMS framebuffer size mismatch");
    if (!tms9918FbUploaded
        || std::memcmp(tms9918PixelBuf.data(),
                       uiSnapshot.tms9918.framebuffer.data(),
                       sizeof(tms9918PixelBuf)) != 0) {
        std::memcpy(tms9918PixelBuf.data(),
                    uiSnapshot.tms9918.framebuffer.data(),
                    sizeof(tms9918PixelBuf));
        r->updateTexture(tms9918Texture, tms9918PixelBuf.data());
        tms9918FbUploaded = true;
        lastCardFbChangeTime = ImGui::GetTime();   // adaptive-UI: card is animating
    }

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

        ImGui::Image(crtEffects.apply(pom1::Pom1CrtEffects::Slot::Tms9918,
                                       tms9918Texture,
                                       r->textureWidth(tms9918Texture),
                                       r->textureHeight(tms9918Texture),
                                       std::max(1, static_cast<int>(size.x)),
                                       std::max(1, static_cast<int>(size.y))),
                     size);
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
    // Lazy texture creation — 64×96 monochrome framebuffer rendered through
    // the same nearest-neighbour pipeline as GEN2 HGR / TMS9918 for crisp
    // pixel art at arbitrary window sizes.
    auto* r = pom1::renderer();
    if (!r) return;
    if (!gt6144Texture) {
        gt6144Texture = r->createTexture(
            GT6144::kWidth, GT6144::kHeight,
            pom1::PomRenderer::Filter::Nearest);
    }

    // Same upload dirty-gate as the TMS window (the 64×96 rasterise is cheap;
    // only the GPU upload is worth skipping).
    GT6144::renderToBuffer(gt6144PixelBuf.data(), uiSnapshot.gt6144);
    if (!gt6144FbUploaded
        || std::memcmp(gt6144UploadedBuf.data(), gt6144PixelBuf.data(),
                       sizeof(gt6144PixelBuf)) != 0) {
        gt6144UploadedBuf = gt6144PixelBuf;
        r->updateTexture(gt6144Texture, gt6144PixelBuf.data());
        gt6144FbUploaded = true;
        lastCardFbChangeTime = ImGui::GetTime();   // adaptive-UI: card is animating
    }

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

        ImGui::Image(crtEffects.apply(pom1::Pom1CrtEffects::Slot::Gt6144,
                                       gt6144Texture,
                                       r->textureWidth(gt6144Texture),
                                       r->textureHeight(gt6144Texture),
                                       std::max(1, static_cast<int>(size.x)),
                                       std::max(1, static_cast<int>(size.y))),
                     size);
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
        ImGui::BulletText("Injection   (Ctrl-K): %s",
                          snap.injectionSuspended ? "SUSPENDED (local kbd)" : "active");

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
            ImGui::BulletText("Ctrl-K  /  ESC K   Suspend/resume key injection");
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
// bytes for the Serial Monitor send line.
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
// Game-agnostic: every named row comes from the game's own schema. The table
// ADAPTS THE SCHEMA TO THE RECEIVED DATA so it always reflects the actual frame:
//  - declared fields are decoded by name/type (or "--" if the frame is short);
//  - any data byte the schema does NOT cover (a frame longer than the schema, or
//    a frame with no schema at all) is shown as an inferred raw U8 row, so no
//    received byte is ever silently hidden. The raw Serial Monitor still follows.
static void renderTelemetryDecodedState(const std::vector<unsigned char>& schemaFrame,
                                        const std::vector<unsigned char>& bytes)
{
    ImGui::SeparatorText("Decoded state");

    std::size_t dataBegin = 0, dataLen = 0;
    const bool haveData = teleFindLastFrame(bytes, TelemetryPort::kFrameSentinel, dataBegin, dataLen);

    std::vector<TeleField> fields;
    if (!schemaFrame.empty())
        teleParseSchema(schemaFrame.data(), schemaFrame.size(), fields);
    const bool haveSchema = !fields.empty();

    // Bytes the declared fields account for (the schema's expected frame length).
    std::size_t expected = 0;
    for (const TeleField& f : fields) expected += teleTypeSize(f.type);

    // Nothing to show only when there is neither a usable schema nor any data.
    if (!haveSchema && !(haveData && dataLen > 0)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        if (!schemaFrame.empty())
            ImGui::TextWrapped("Schema frame seen but no valid field descriptors decoded — "
                               "data bytes, if any, will appear here as raw rows.");
        else
            ImGui::TextWrapped("No schema frame yet — a game can emit one (TELE_CTRL=$03) to "
                               "name its fields. Until then, any data frame is shown here as "
                               "raw bytes. See doc/TELEMETRY_SIDE_CHANNEL.md.");
        ImGui::PopStyleColor();
        return;
    }

    if (ImGui::BeginTable("##telemetry_decoded", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Field");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Value");
        ImGui::TableHeadersRow();

        std::size_t off = 0;            // offset into the data payload
        // 1) Declared schema fields, decoded by their type.
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
                ImGui::TextDisabled("--");   // frame too short for this field
            }
            off += span;
        }
        // 2) Adapt to the data: surface every byte the schema did not cover (extra
        //    trailing bytes, or the whole frame when no schema) as inferred raw rows.
        for (std::size_t k = 0; haveData && off < dataLen; ++off, ++k) {
            char name[24], val[24];
            std::snprintf(name, sizeof(name), haveSchema ? "extra[%zu]" : "byte[%zu]", k);
            const unsigned char b = bytes[dataBegin + off];
            std::snprintf(val, sizeof(val), "$%02X (%u)", b, static_cast<unsigned>(b));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(name);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("raw");
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(val);
        }
        ImGui::EndTable();
    }

    // Status line: describe how the view adapted to the frame.
    if (!haveData) {
        ImGui::TextDisabled("No data frame yet — fields will fill in as the game emits state.");
    } else if (!haveSchema) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::TextWrapped("No schema — inferred %zu raw byte(s) from the data frame.", dataLen);
        ImGui::PopStyleColor();
    } else if (dataLen > expected) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.3f, 1.0f));
        ImGui::TextWrapped("Adapted: schema covers %zu B, frame is %zu B — %zu extra byte(s) "
                           "shown as raw.", expected, dataLen, dataLen - expected);
        ImGui::PopStyleColor();
    } else if (dataLen < expected) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.3f, 1.0f));
        ImGui::TextWrapped("Note: frame is %zu B but schema expects %zu B — trailing fields "
                           "shown as \"--\".", dataLen, expected);
        ImGui::PopStyleColor();
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

            // ---- Inbound injection (Serial Monitor → game) ----
            ImGui::SeparatorText("Send");
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

                // Latch the schema frame BEFORE trimming. A game emits its schema
                // ONCE at startup, so it is the oldest frame and the first to be
                // evicted by the cap — without latching it, the "Decoded state"
                // loses its schema and flickers between decoded and "no schema".
                {
                    std::size_t sBegin = 0, sLen = 0;
                    if (teleFindLastFrame(telemetryMonitorBytes,
                                          TelemetryPort::kSchemaSentinel, sBegin, sLen))
                        telemetrySchemaFrame.assign(
                            telemetryMonitorBytes.begin() + static_cast<std::ptrdiff_t>(sBegin),
                            telemetryMonitorBytes.begin() + static_cast<std::ptrdiff_t>(sBegin + sLen));
                }

                // Trim at FRAME boundaries (whole [sentinel][len][payload] frames)
                // so byte 0 always stays a frame start — a mid-frame trim desyncs
                // the decoded-state parser and is the other half of the flicker.
                constexpr std::size_t kCap = 64 * 1024;
                if (telemetryMonitorBytes.size() > kCap) {
                    const std::size_t want = telemetryMonitorBytes.size() - kCap;
                    std::size_t cut = 0;
                    while (cut < want && cut + 3 <= telemetryMonitorBytes.size()) {
                        const std::size_t len = telemetryMonitorBytes[cut + 1] |
                            (static_cast<std::size_t>(telemetryMonitorBytes[cut + 2]) << 8);
                        if (cut + 3 + len > telemetryMonitorBytes.size()) break;
                        cut += 3 + len;
                    }
                    telemetryMonitorBytes.erase(telemetryMonitorBytes.begin(),
                        telemetryMonitorBytes.begin() + static_cast<std::ptrdiff_t>(cut));
                }
            }

            // ---- Decoded state (schema-driven, game-agnostic) ----
            // Uses the LATCHED schema (stable across buffer trims) + the last data
            // frame from the frame-aligned monitor buffer.
            renderTelemetryDecodedState(telemetrySchemaFrame, telemetryMonitorBytes);

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

#if POM1_DEVTOOLS
// The POM1 Bench editor is the portable bench/CodeBench, driven by a
// Pom1BenchHost (cc65 toolchain, presets, CodeTank/loadBinary deploy, telemetry
// Serial Monitor). See bench/IBenchHost.h. Host + bench are created lazily —
// either on first render or when a DevBench preset is applied (see
// applyMachineConfig in MainWindow_Presets.cpp).
void MainWindow_ImGui::ensureBench()
{
    if (benchHost_) return;
    benchHost_ = std::make_unique<Pom1BenchHost>(this);
    codeBench_ = std::make_unique<bench::CodeBench>(benchHost_.get());
}

void MainWindow_ImGui::renderBenchWindow()
{
    ensureBench();
    codeBench_->render("POM1 Bench", &showBench);
}
#endif  // POM1_DEVTOOLS

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
        if (pr40MechPhotoTexture && pr40MechPhotoWidth > 0 && pr40MechPhotoHeight > 0
            && pom1::renderer()) {
            ImGui::Separator();
            const float availW = ImGui::GetContentRegionAvail().x;
            const float aspect = static_cast<float>(pr40MechPhotoHeight)
                               / static_cast<float>(pr40MechPhotoWidth);
            ImGui::Image(pom1::renderer()->asImTextureID(pr40MechPhotoTexture),
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
            if (cardPlugged(pom1::CardId::JukeBox))
                evictMemoryMapRegionsForJukeBox();
            emulation->setJukeBoxJumper(jukeBoxJumper);
            emulation->setPresetRamKB(32);
            presetRamKB = 32;
            setStatusMessage("Juke-Box jumper: 32 kB RAM / 16 kB ROM", 2.0f);
        }
        if (ImGui::RadioButton("16 kB RAM / 32 kB ROM  ($4000-$BFFF)",
                               &jumperInt, static_cast<int>(JukeBox::Jumper::RAM16_ROM32))) {
            jukeBoxJumper = JukeBox::Jumper::RAM16_ROM32;
            if (cardPlugged(pom1::CardId::JukeBox))
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

// The roms/codetank directory, through POM1's single search order
// (`ResourceLocator.h`). Returns {} if it exists under no root.
std::filesystem::path resolveCodeTankLibraryRoot()
{
    return pom1::ResourceLocator::defaultLocator().findDirectory("roms/codetank");
}

struct CodeTankLibraryEntry {
    std::filesystem::path path;
    std::string           filename;      // display name (no parent dirs)
    std::string           blurbLower;    // sidecar "Lower jumper:" payload (what Low boots)
    std::string           blurbUpper;    // sidecar "Upper jumper:" payload (what Up boots)
    bool                  upper = false; // per-row jumper switch: false=Lower, true=Upper
    // Short line to show for the current switch position (falls back to the
    // other bank if one side has no sidecar entry).
    const std::string& blurb() const {
        return upper ? (blurbUpper.empty() ? blurbLower : blurbUpper)
                     : (blurbLower.empty() ? blurbUpper : blurbLower);
    }
};

// Strip a leading "4000R → " / "4000R -> " boot prefix + surrounding whitespace.
std::string codeTankStripBootPrefix(std::string s)
{
    const auto ws = [&] { std::size_t i = s.find_first_not_of(" \t"); s.erase(0, i == std::string::npos ? s.size() : i); };
    ws();
    if (s.rfind("4000R", 0) == 0 || s.rfind("4000r", 0) == 0) {
        s.erase(0, 5); ws();
        if (s.rfind("\xE2\x86\x92", 0) == 0) s.erase(0, 3);   // → (U+2192)
        else if (s.rfind("->", 0) == 0)      s.erase(0, 2);
        ws();
    }
    return s;
}

// Read the "Lower jumper:" / "Upper jumper:" payloads from a cartridge's .txt
// sidecar into the entry (both empty when there is no sidecar).
void codeTankReadSidecar(const std::filesystem::path& rom, CodeTankLibraryEntry& e)
{
    namespace fs = std::filesystem;
    fs::path sidecar = rom; sidecar.replace_extension(".txt");
    std::error_code ec;
    if (!fs::is_regular_file(sidecar, ec)) return;
    std::ifstream f(sidecar);
    std::string line;
    while (std::getline(f, line)) {
        std::size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        std::string t = line.substr(b);
        std::string key = t.substr(0, 13);
        for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const std::size_t col = t.find(':');
        if (col == std::string::npos) continue;
        if (key.rfind("lower jumper:", 0) == 0)
            e.blurbLower = codeTankStripBootPrefix(t.substr(col + 1));
        else if (key.rfind("upper jumper:", 0) == 0)
            e.blurbUpper = codeTankStripBootPrefix(t.substr(col + 1));
    }
}

// Every 32 kB .rom/.bin under roms/codetank/, sorted by name.
std::vector<CodeTankLibraryEntry> scanCodeTankLibrary()
{
    namespace fs = std::filesystem;
    std::vector<CodeTankLibraryEntry> out;
    const fs::path root = resolveCodeTankLibraryRoot();
    if (root.empty()) return out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        const fs::path& p = entry.path();
        const std::string ext = p.extension().string();
        if (ext != ".rom" && ext != ".bin") continue;
        if (fs::file_size(p, ec) != 0x8000u || ec) continue;   // 32 kB carts only
        CodeTankLibraryEntry e;
        e.path  = p;
        e.filename = p.filename().string();
        codeTankReadSidecar(p, e);
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(),
              [](const CodeTankLibraryEntry& a, const CodeTankLibraryEntry& b) {
                  return a.filename < b.filename;
              });
    return out;
}

} // namespace

void MainWindow_ImGui::renderCodeTankLibraryWindow()
{
    ImGui::SetNextWindowSize(ImVec2(460, 400), ImGuiCond_FirstUseEver);
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
        if (ImGui::SmallButton("Refresh"))
            entries = scanCodeTankLibrary();
        ImGui::Separator();

        const std::string& currentRom = uiSnapshot.codeTank.romPath;

        // Insert a cartridge with the chosen jumper, cascade-plug the TMS9918
        // host + CodeTank, hard-reset and auto-type 4000R after ~3 s.
        auto plug = [&](const CodeTankLibraryEntry& e, CodeTank::Jumper j) {
            std::string err;
            if (!emulation->loadCodeTankRom(e.path.string(), err)) {
                setStatusMessage("CodeTank load failed: " + err, 5.0f);
                return;
            }
            codeTankJumper = j;
            emulation->setCodeTankJumper(codeTankJumper);
            if (cardPlugged(pom1::CardId::JukeBox))
                setCardPlugged(pom1::CardId::JukeBox, false);
            // CodeTank is a daughterboard of the TMS9918 — auto-plug the host
            // so the UI flags match what Memory's setCodeTankEnabled is about
            // to do.
            if (!cardPlugged(pom1::CardId::Tms9918)) {
                showTMS9918 = true;
                emulation->setCardEnabled(pom1::CardId::Tms9918, true);
            }
            if (!cardPlugged(pom1::CardId::CodeTank))
                setCardPlugged(pom1::CardId::CodeTank, true);
            bringTms9918WindowToFront = true;
            ImGui::SetWindowFocus("P-LAB Graphic Card (TMS9918)");
            emulation->hardReset();
            // Cold boot to Wozmon ~3 s wall clock before auto-run (realistic panel startup).
            constexpr double kCodeTankColdBootSeconds = 3.0;
            codeTankPendingWozRunAt = ImGui::GetTime() + kCodeTankColdBootSeconds;
            setStatusMessage(std::string("CodeTank: ") + e.filename
                                 + (j == CodeTank::Jumper::Upper16 ? " (upper)" : " (lower)")
                                 + " — reset; 4000R in 3 s",
                             4.0f);
        };

        if (entries.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f),
                ICON_FA_TRIANGLE_EXCLAMATION " No 32 kB ROMs in roms/codetank/.");
        } else {
            for (size_t i = 0; i < entries.size(); ++i) {
                auto& e = entries[i];
                const bool isActive = (currentRom == e.path.string());
                ImGui::PushID(static_cast<int>(i));
                // Jumper switch (Low / Up) before the Run button.
                if (ImGui::RadioButton("Low", !e.upper)) e.upper = false;
                ImGui::SameLine();
                if (ImGui::RadioButton("Up", e.upper)) e.upper = true;
                ImGui::SameLine();
                if (ImGui::Button("Run"))
                    plug(e, e.upper ? CodeTank::Jumper::Upper16 : CodeTank::Jumper::Lower16);
                ImGui::SameLine();
                if (isActive)
                    ImGui::TextColored(ImVec4(0.4f, 0.95f, 0.4f, 1.0f), "%s", e.filename.c_str());
                else
                    ImGui::TextUnformatted(e.filename.c_str());
                if (!e.blurb().empty())
                    ImGui::TextDisabled("%s", e.blurb().c_str());
                ImGui::PopID();
            }
        }

        if (cardPlugged(pom1::CardId::CodeTank)) {
            ImGui::Separator();
            if (ImGui::Button("Unplug CodeTank")) {
                setCardPlugged(pom1::CardId::CodeTank, false);
                showCodeTankLibrary = false;
                codeTankPendingWozRunAt = 0.0;
                setStatusMessage("CodeTank unplugged", 2.0f);
            }
        }
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
