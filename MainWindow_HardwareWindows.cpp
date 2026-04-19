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
#include <string>

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

void MainWindow_ImGui::renderJukeBoxWindow()
{
    ImGui::SetNextWindowSize(ImVec2(420, 360), ImGuiCond_FirstUseEver);
    applyPendingLayout("P-LAB Juke-Box");
    if (ImGui::Begin("P-LAB Juke-Box", &showJukeBox)) {
        const auto& snap = uiSnapshot.jukeBox;

        // Firmware signature row — the one check that tells the user whether
        // the loaded ROM will actually respond to BD00R.
        if (snap.firmwarePresent) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f),
                ICON_FA_CIRCLE_CHECK " Program Manager signature at $BD00: FOUND");
        } else {
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f),
                ICON_FA_TRIANGLE_EXCLAMATION " Program Manager signature at $BD00: MISSING");
            ImGui::TextWrapped(
                "Load a Juke-Box ROM built with P-LAB's EPROM_CREATOR "
                "(2-packer.sh) as roms/jukebox.rom. Without it the card "
                "is installed but BD00R hangs.");
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
        ImGui::Text("Size: %zu bytes (%.1f kB)",
                    snap.romSize, static_cast<double>(snap.romSize) / 1024.0);

        if (ImGui::Button("Reload ROM")) {
            std::string error;
            if (emulation->reloadJukeBoxRom(error)) {
                setStatusMessage("Juke-Box ROM reloaded", 2.0f);
            } else {
                setStatusMessage(error, 4.0f);
            }
        }

        ImGui::Separator();

        // Jumper toggle — changing this swaps the ROM window + RAM ceiling.
        ImGui::Text("RAM / ROM jumper:");
        int jumperInt = static_cast<int>(snap.jumper);
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

        // EEPROM RW jumper
        bool writable = snap.writable;
        if (ImGui::Checkbox("EEPROM write-enable (28xxx RW jumper)", &writable)) {
            emulation->setJukeBoxWritable(writable);
            setStatusMessage(writable
                ? "Juke-Box EEPROM: write-enabled (writes persist to jukebox.rom)"
                : "Juke-Box EEPROM: read-only", 3.0f);
        }
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
            "When on, writes in the ROM window update the jukebox.rom file.");

        ImGui::Separator();

        if (ImGui::CollapsingHeader("Usage")) {
            ImGui::BulletText("BD00R   Launch the Program Manager (& prompt)");
            ImGui::BulletText("H       Help - list all Program Manager commands");
            ImGui::BulletText("D       Directory of programs on the current page");
            ImGui::BulletText("L<X>    Load program tagged with letter X");
            ImGui::BulletText("B       Enter BASIC (non-destructive, via E2B3R)");
            ImGui::BulletText("X       Exit Program Manager back to Woz Monitor");
            ImGui::Spacing();
            ImGui::TextWrapped(
                "Save-Program (B800R, # prompt): W = write RAM range to EEPROM, "
                "S = save current BASIC program, L = back to Program Manager, "
                "X = exit to Woz Monitor. Requires EEPROM write-enable on.");
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
