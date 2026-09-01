// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MainWindow_Settings.cpp — the settings dialogs: Display Settings (charmap,
// monitor tint, scanlines, UI scale, idle throttle, native file dialogs), the
// CRT effect sliders, and Memory Settings (RAM profile, ROM loading, the
// out-of-range strict-mode controls).
//
// Split out of MainWindow_Dialogs.cpp for the same reason as
// MainWindow_Tutorials.cpp — see that file's header. Pure code motion.
//
// Reminder for anything added here: POM1's own chrome constants
// (kToolbarBandHeight & co.) are authored at 100 % and must go through
// detail::uiPx(); explicit widget sizes need a floor, not a raw literal, or
// they clip their own glyph once the interface zoom is raised.

#include "MainWindow_ImGui.h"
#include "MainWindow_Internal.h"
#include "POM1Build.h"
#include "MacNativeFullscreen.h"  // macOS fullscreen space is invisible to GLFW
#include "LayoutDecisions.h"  // fullscreen / layout arbitration, pure

// The WASM fullscreen branch below calls emscripten_request_fullscreen_strategy /
// emscripten_exit_fullscreen. These includes came along from MainWindow_Dialogs.cpp
// and are NOT optional: the web build is the one nobody compiles locally, so a
// missing declaration here surfaces only in CI.
#if POM1_IS_WASM
#include <emscripten.h>
#include <emscripten/html5.h>
#endif
#include "Logger.h"

#include "imgui.h"

// Dear ImGui default font atlas: avoid Unicode en/em dash (U+2013/U+2014) in on-screen
// strings here - they show as "?". Use ASCII '-' for dashes in dialog/window text.

#include <algorithm>
#include <cstring>
// renderMemoryConfigDialog() builds the EhBASIC status line with an
// ostringstream. The include stayed behind in MainWindow_Dialogs.cpp when this
// file was split out of it — that TU no longer uses one — and the split TU
// compiled anyway wherever <sstream> happened to arrive transitively. The CI
// runner's libstdc++ does not provide it, so `std::stringstream` was an
// incomplete type there and only the Linux job saw it.
#include <sstream>
#include <string>
#include <vector>

namespace {
using namespace pom1::mainwindow::detail;
} // namespace

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
        // The checkbox reflects fullscreen by ANY route, so a session entered
        // through macOS' green button doesn't show up here as "off" (ticking it
        // would then stack a glfwSetWindowMonitor fullscreen on top of AppKit's
        // space). Leaving a native space is handed back to AppKit's own toggle.
#if !POM1_IS_WASM
        const bool nativeFs = window && pom1::macWindowIsNativeFullscreen(window);
#else
        const bool nativeFs = false;   // no AppKit space in a browser
#endif
        // What the box SHOWS, and when an in-flight "leave the space" latch
        // retires, are pure — pom1::layout::fullscreenCheckboxState, pinned by
        // layout_decisions_smoke §7 along with the timeout and the swallowed
        // second click. This side keeps only the AppKit and GLFW calls.
        const pom1::layout::CheckboxState cs =
            pom1::layout::fullscreenCheckboxState(osWindowIsFullscreen(), nativeFs,
                                                  ImGui::GetTime(),
                                                  macNativeExitRequestedAt_);
        macNativeExitRequestedAt_ = cs.exitRequestedAt;

        bool fullscreenUi = cs.showChecked;
        if (ImGui::Checkbox("Fullscreen", &fullscreenUi)) {
            const pom1::layout::TogglePlan plan =
                pom1::layout::planFullscreenToggle(fullscreenUi, nativeFs,
                                                   ImGui::GetTime(),
                                                   macNativeExitRequestedAt_);
            macNativeExitRequestedAt_ = plan.exitRequestedAt;
            // Unticking is the escape hatch out of a --fullscreen kiosk: drop
            // the CLI force, or render() puts the window straight back on the
            // monitor next frame — including on the AppKit path, the moment the
            // space finishes closing.
            if (plan.dropCliForced) cliForcedFullscreen_ = false;
            switch (plan.action) {
            case pom1::layout::ToggleAction::Ignore:
                break;
            case pom1::layout::ToggleAction::HandBackToAppKit:
#if !POM1_IS_WASM
                pom1::macWindowToggleNativeFullscreen(window);
#endif
                fullscreen = plan.targetFullscreen;
                break;
            case pom1::layout::ToggleAction::ApplyOwnFullscreen:
                fullscreen = plan.targetFullscreen;
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
                setOsFullscreen(fullscreen);   // re-asserts the flag it was given
#endif
                break;
            }
        }

        ImGui::Spacing();
        ImGui::Text("Interface zoom");
        ImGui::Separator();
        {
            // Zoom scales the WHOLE interface — fonts AND widget geometry
            // (padding, rounding, scrollbars) AND POM1's own toolbar/status
            // bands — not just the font as it did before 1.9.5. Everything goes
            // through applyUiTheme(); see MainWindow_Presets.cpp.
            // Percent rather than a raw multiplier: "125 %" is the unit every
            // other desktop app uses for this control.
            int pct = static_cast<int>(uiScale_ * 100.0f + 0.5f);
            ImGui::SetNextItemWidth(uiPx(220.0f));
            if (ImGui::SliderInt("##uiscale", &pct,
                                 static_cast<int>(kUiScaleMin * 100.0f),
                                 static_cast<int>(kUiScaleMax * 100.0f), "%d %%"))
                setUiScale(static_cast<float>(pct) / 100.0f);
            if (ImGui::IsItemDeactivatedAfterEdit())
                saveUiSettings();   // save once when the drag ends, not per pixel

            auto zoomStep = [&](const char* label, float delta) {
                if (ImGui::Button(label)) {
                    setUiScale(uiScale_ + delta);
                    saveUiSettings();
                }
                ImGui::SameLine();
            };
            zoomStep(" - ", -kUiScaleStep * 2.0f);
            zoomStep(" + ", +kUiScaleStep * 2.0f);
            if (ImGui::Button("Reset to 100 %")) {
                setUiScale(1.0f);
                saveUiSettings();
            }

            if (ImGui::Checkbox("Auto (follow monitor DPI)", &uiHiDpiAuto_)) {
                syncUiDpiScale();       // pick the live scale up when re-enabled
                applyUiTheme(uiTheme_); // …and drop it when disabled
                saveUiSettings();       // persists across sessions (ini/ui.settings)
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Multiply the zoom above by the monitor's content scale,\n"
                                  "so the UI keeps its physical size on a HiDPI display.\n"
                                  "Turn off to use the zoom alone. Remembered across sessions.");
            if (uiHiDpiAuto_ && uiDpiScale_ > 1.005f) {
                ImGui::TextDisabled("Monitor scale: %.0f %% (from the OS) - effective UI scale %.0f %%",
                                    uiDpiScale_ * 100.0f, uiScale_ * uiDpiScale_ * 100.0f);
            }
        }

#if !POM1_IS_WASM
        ImGui::Spacing();
        ImGui::Text("Performance");
        ImGui::Separator();
        if (ImGui::Checkbox("Adaptive UI refresh (save CPU/GPU when idle)",
                            &uiIdleThrottle_)) {
            saveUiSettings();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "When nothing on screen is changing, redraw the UI ~5x per\n"
                "second instead of 60 — a big saving on old machines. The\n"
                "emulation itself always runs at full speed on its own thread;\n"
                "any input instantly restores the full frame rate.");
#endif

        ImGui::Spacing();
        if (ImGui::Button("Close")) {
            showScreenConfig = false;
        }
    }
    ImGui::End();
}

// ─── CRT Effects (universal shader post-process) ─────────────────────────
//
// Master ON/OFF + the OpenEmulator-style glass knobs that drive the per-
// framebuffer CrtEffectStack (the Apple-1 text screen AND the GEN2/TMS/GT
// graphics cards). Ported from POM2's "CRT Settings" panel, trimmed to the
// knobs POM1's effect-only shader consumes. All values persist to
// ini/ui.settings under the crt_* keys.
void MainWindow_ImGui::renderCrtSettingsWindow()
{
    ImGui::SetNextWindowSize(ImVec2(380, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("CRT Effects", &showCrtSettings)) {
        ImGui::End();
        return;
    }

    bool changed = false;

    // Master ON/OFF, full-width at the top; greys out the knobs when off.
    {
        const bool on = crtEffects.enabled;
        const ImVec4 col = on ? ImVec4(0.16f, 0.52f, 0.22f, 1.0f)
                              : ImVec4(0.55f, 0.18f, 0.18f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, col);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            ImVec4(col.x + 0.08f, col.y + 0.08f, col.z + 0.08f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, col);
        if (ImGui::Button(on ? "CRT Effects: ON  (click to disable)"
                             : "CRT Effects: OFF  (click to enable)",
                          ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
            crtEffects.enabled = !crtEffects.enabled;
            changed = true;
        }
        ImGui::PopStyleColor(3);
    }
    ImGui::Separator();

    // Shader availability note. There is no longer a backend that structurally
    // cannot run the effect — macOS/Metal got its own stack
    // (CrtEffectStackMetal) — so the only thing left to report is a shader
    // that failed to build on this machine.
    {
        if (crtEffects.enabled && !crtEffects.active()) {
            ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1),
                "CRT shader unavailable — presenting the raw framebuffer.");
            ImGui::Separator();
        }
    }

    ImGui::BeginDisabled(!crtEffects.enabled);

    pom1::CrtParams& p = crtEffects.params;
    changed |= ImGui::SliderFloat("Brightness",  &p.brightness,  -0.5f, 0.5f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    changed |= ImGui::SliderFloat("Contrast",    &p.contrast,     0.5f, 1.5f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    changed |= ImGui::SliderFloat("Saturation",  &p.saturation,   0.0f, 2.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    changed |= ImGui::SliderFloat("Hue",         &p.hue,         -0.5f, 0.5f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::Separator();
    changed |= ImGui::SliderFloat("Sharpness",   &p.sharpness,    0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    changed |= ImGui::SliderFloat("Persistence", &p.persistence,  0.0f, 0.95f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::Separator();
    changed |= ImGui::SliderFloat("Scanlines",   &p.scanlines,    0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    changed |= ImGui::SliderFloat("Barrel",      &p.barrel,       0.0f, 0.30f, "%.3f", ImGuiSliderFlags_AlwaysClamp);

    ImGui::Separator();
    static const char* kMaskNames[] = {
        "Off", "Triad (3-stripe)", "Aperture grille (Trinitron)",
        "Dot mask (offset triads)"
    };
    int maskIdx = static_cast<int>(p.shadowMask);
    if (ImGui::Combo("Shadow mask", &maskIdx, kMaskNames,
                     IM_ARRAYSIZE(kMaskNames))) {
        p.shadowMask = static_cast<pom1::CrtParams::ShadowMask>(maskIdx);
        changed = true;
    }
    ImGui::BeginDisabled(p.shadowMask == pom1::CrtParams::ShadowMask::Off);
    changed |= ImGui::SliderFloat("Mask strength",
                                  &p.shadowMaskStrength, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();
    changed |= ImGui::SliderFloat("Luminance gain", &p.luminanceGain, 1.0f, 2.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    changed |= ImGui::SliderFloat("Center lighting", &p.centerLighting, 0.5f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    changed |= ImGui::SliderFloat("Phosphor curve (gamma)",
                                  &p.phosphorGamma, 0.6f, 2.6f, "%.3f", ImGuiSliderFlags_AlwaysClamp);

    ImGui::Spacing();
    if (ImGui::Button("Reset to defaults")) {
        p = pom1::CrtParams{};
        changed = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Saved to ini/ui.settings");

    ImGui::EndDisabled();

    if (changed) saveUiSettings();

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

        auto hasRange = [](const std::vector<LoadedRegion>& v, uint16_t s, uint16_t e) {
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
                    [](const LoadedRegion& r) { return r.start >= 0xE000; }), loadedRoms.end());
                loadedRoms.push_back({"Integer BASIC", 0xE000, 0xEFFF});
                loadedRoms.push_back({"Woz Monitor", 0xFF00, 0xFFFF});
            }
            setStatusMessage(ok ? "BASIC loaded" : error, 3.0f);
        }

        // Microsoft BASIC 6502 — the OSI-derived 8 KB build with floating point,
        // ported to the Apple-1's PIA. It shares the $E000 window with Woz's
        // Integer BASIC, which IS the mutex: flashing one evicts the other, the
        // same way it would on a real machine with a single BASIC EPROM socket.
        // Nothing else is needed to enforce it — the loadedRoms purge below
        // drops every $E000-$FFFF entry before recording the new occupant.
        if (ImGui::Button("Load Microsoft BASIC  [$E000-$FEFF + Woz $FF00-$FFFF]")) {
            std::string error;
            bool ok = emulation->reloadMsBasic(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            if (ok) {
                loadedRoms.erase(std::remove_if(loadedRoms.begin(), loadedRoms.end(),
                    [](const LoadedRegion& r) { return r.start >= 0xE000; }), loadedRoms.end());
                loadedRoms.push_back({"Microsoft BASIC", 0xE000, 0xFEFF});
                loadedRoms.push_back({"Woz Monitor", 0xFF00, 0xFFFF});
            }
            setStatusMessage(ok ? "Microsoft BASIC loaded — cold start E000R, warm start E003R"
                                : error, 4.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Microsoft BASIC 6502 (OSI lineage, 8 KB, FLOATING POINT).\n"
                "Woz's Integer BASIC has no floats at all — that is the reason\n"
                "to use this one. Both live at $E000, so loading either evicts\n"
                "the other, exactly like swapping the BASIC EPROM.\n\n"
                "Cold start: E000R    Warm start: E003R\n"
                "The cold start asks MEMORY SIZE? and TERMINAL WIDTH? —\n"
                "press RETURN twice to take the defaults.\n\n"
                "Reproducible build from public sources: see dev/msbasic/.");
        }

        if (ImGui::Button("Load Applesoft Lite (CFFA1)  [$E000-$FFFF]")) {
            std::string error;
            bool ok = emulation->reloadApplesoftLiteCFFA1(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            if (ok) {
                loadedRoms.erase(std::remove_if(loadedRoms.begin(), loadedRoms.end(),
                    [](const LoadedRegion& r) { return r.start >= 0xE000; }), loadedRoms.end());
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
                loadedRoms.push_back({"Applesoft Lite (loaded in card RAM)", 0x6000, 0x7FFF});
                if (!hasRange(loadedRoms, 0xE000, 0xEFFF))
                    loadedRoms.push_back({"Integer BASIC", 0xE000, 0xEFFF});
                if (!hasRange(loadedRoms, 0xFF00, 0xFFFF))
                    loadedRoms.push_back({"Woz Monitor", 0xFF00, 0xFFFF});
            }
            setStatusMessage(ok ? "Applesoft Lite (microSD) loaded" : error, 3.0f);
        }

        // EhBASIC is flashed into plain RAM at $5000-$7FFF, not into a ROM
        // window — so unlike the buttons above it has no bus decoding of its
        // own to win with, and any card mapping inside that range simply
        // shadows it. Unplug exactly the three that do (Parmigiani's one-board
        // rule); CFFA1 ($9000) and A1-IO/RTC ($2000) are left alone because
        // they do not overlap, which is why this does not call the broader
        // evictStorageCards().
        if (ImGui::Button("Load EhBASIC 2.22  [$5000-$7FFF, in RAM]")) {
            std::vector<std::string> evicted;
            if (cardPlugged(pom1::CardId::MicroSD)) {
                // Unplugging microSD cascade-drops the IEC add-on riding on
                // its VIA; only that card's window is the UI's to close.
                setCardPlugged(pom1::CardId::MicroSD, false);
                showIECCard = false;
                evicted.push_back("microSD");
            }
            if (cardPlugged(pom1::CardId::CodeTank)) {
                showCodeTankLibrary = false;
                codeTankPendingWozRunAt = 0.0;
                emulation->setCardEnabled(pom1::CardId::CodeTank, false);
                evicted.push_back("CodeTank");
            }
            if (cardPlugged(pom1::CardId::JukeBox)) {
                showJukeBox = false;
                setCardPlugged(pom1::CardId::JukeBox, false);
                evicted.push_back("Juke-Box");
            }

            std::string error;
            bool ok = emulation->reloadEhBasic(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            if (ok) {
                loadedRoms.erase(std::remove_if(loadedRoms.begin(), loadedRoms.end(),
                    [](const LoadedRegion& r) {
                        return !(r.end < 0x5000 || r.start > 0x7FFF);
                    }), loadedRoms.end());
                loadedRoms.push_back({"EhBASIC 2.22 (loaded in RAM)", 0x5000, 0x7FFF});
                std::stringstream ss;
                ss << "EhBASIC 2.22 loaded — cold start 5000R, warm start 5003R";
                if (!evicted.empty()) {
                    ss << "  [unplugged ";
                    for (size_t i = 0; i < evicted.size(); ++i) {
                        if (i) ss << ", ";
                        ss << evicted[i];
                    }
                    ss << ": was shadowing $5000-$7FFF]";
                }
                setStatusMessage(ss.str(), evicted.empty() ? 4.0f : 6.0f);
            } else {
                setStatusMessage(error, 3.0f);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Lee Davison's Enhanced 6502 BASIC 2.22 — floating point,\n"
                "string handling, IF..THEN..ELSE, DO..UNTIL, hex/binary\n"
                "literals. Far beyond any BASIC the Apple-1 actually shipped\n"
                "with.\n\n"
                "No Apple-1 port existed: POM1's lives in dev/ehbasic/ (PIA\n"
                "I/O + entry stub; the interpreter itself is untouched).\n\n"
                "Cold start: 5000R    Warm start: 5003R\n"
                "Programs live in $0300-$4FFF (~19 KB free).\n\n"
                "It is loaded into RAM, not a ROM window, so it needs at least\n"
                "32 KB and unplugs any card decoding $5000-$7FFF (microSD,\n"
                "CodeTank, Juke-Box).");
        }

        if (ImGui::Button("Load WOZ Monitor  [$FF00-$FFFF]")) {
            std::string error;
            bool ok = emulation->reloadWozMonitor(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            setStatusMessage(ok ? "WOZ Monitor loaded" : error, 3.0f);
        }

        if (ImGui::Button("Load Krusader  [$E000-$FFFF]")) {
            std::string error;
            bool ok = emulation->reloadKrusader(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            if (ok) {
                loadedRoms.erase(std::remove_if(loadedRoms.begin(), loadedRoms.end(),
                    [](const LoadedRegion& r) { return r.start == 0xA000; }), loadedRoms.end());
                loadedRoms.push_back({"Krusader", 0xE000, 0xFFFF});
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

            if (ImGui::Button("Yes", uiPx(ImVec2(120, 0)))) {
                emulation->clearMemory();
                setStatusMessage("Memory cleared", 2.0f);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("No", uiPx(ImVec2(120, 0)))) {
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
