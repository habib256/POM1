// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MainWindow_Internal.h — PRIVATE header shared between the MainWindow_*.cpp
// translation units that together implement the MainWindow_ImGui class.
// Holds layout constants, drawing helpers, and machine-preset structures
// that several TUs need to see. NOT for public consumption: do not include
// from main_imgui.cpp or any external header.

#ifndef MAINWINDOW_INTERNAL_H
#define MAINWINDOW_INTERNAL_H

#include "imgui.h"
#include "CodeTank.h"
#include "JukeBox.h"
#include "MachinePresets.h"
#include "Screen_ImGui.h"

namespace pom1::mainwindow::detail {

// ---------------------------------------------------------------------------
// Layout constants — sizes of the menu bar / toolbar / status bar bands and
// the chrome around the Apple-1 raster window. Used by render() to position
// the screen and by hardware windows for first-frame placement.
// ---------------------------------------------------------------------------

// Apple 1 Screen window padding ≈ raster + ImGui chrome.
inline constexpr float kApple1ImGuiWinPadW = 22.0f;
inline constexpr float kApple1ImGuiWinPadH = 46.0f;
// OS chrome margin around the usable area (menu bar, dock, side panels).
inline constexpr int   kApple1GlfwExtraW   = 22;
inline constexpr int   kApple1GlfwExtraH   = 42;

// Aligned with renderToolbar / renderStatusBar / "Apple 1 Screen" placement.
inline constexpr float kToolbarBandHeight             = 34.0f;
inline constexpr float kGapBelowToolbarBeforeApple1   = 5.0f;
inline constexpr float kStatusBarBandHeight           = 25.0f;
// GetFrameHeight() can come up short on some themes/fonts, so we add slack
// to avoid clipping the menu bar bottom on WASM.
inline constexpr float kMainMenuBarHeightExtra        = 6.0f;
// "Apple 1 Screen" window decoration padding (borders, rounding, breathing).
inline constexpr float kApple1WindowDecorationSlop    = 14.0f;

// Default pixel scales for HGR / TMS9918 windows on first display.
inline constexpr float kVideoCardDefaultPixelScale = 2.0f;
inline constexpr float kTMS9918DefaultPixelScale   = 3.0f;
// Floor: keep pixels visible; the window can scroll if it goes below.
inline constexpr float kVideoCardMinPixelScale     = 0.25f;

// ---------------------------------------------------------------------------
// Interface zoom — Settings ▸ Interface zoom (user) × monitor content scale.
//
// `MainWindow_ImGui::applyUiTheme()` hands the whole ImGuiStyle to
// `ImGuiStyle::ScaleAllSizes()`, which covers every ImGui-owned size (padding,
// rounding, scrollbars, item spacing) and the fonts via FontScaleMain /
// FontScaleDpi. It does NOT know about the band constants above — those are
// POM1's own chrome, authored at 100 %. Multiply them through `uiPx()` at the
// point of use or the toolbar/status bands stay 34/25 px tall while their
// contents grow, and the dockspace ends up overlapping them.
// ---------------------------------------------------------------------------

/// Zoom currently applied by applyUiTheme() (user zoom × monitor DPI).
/// 1.0 until the first apply, so pre-theme callers are safe.
float uiScaleTotal();
/// Published by applyUiTheme(); nothing else should call this.
void  setUiScaleTotal(float s);

/// Scale a pixel constant authored at 100 % by the live interface zoom.
inline float  uiPx(float px)  { return px * uiScaleTotal(); }
inline ImVec2 uiPx(ImVec2 v)  { const float s = uiScaleTotal();
                                return ImVec2(v.x * s, v.y * s); }

/// Clamp bounds for the user zoom control — shared by the Settings menu, the
/// Display Settings slider and the ui.settings loader, so a hand-edited
/// `ui_scale` can never produce an unusable UI.
inline constexpr float kUiScaleMin  = 0.75f;
inline constexpr float kUiScaleMax  = 2.50f;
inline constexpr float kUiScaleStep = 0.05f;

// ---------------------------------------------------------------------------
// Layout / drawing helpers — defined in MainWindow_Layout.cpp.
// ---------------------------------------------------------------------------

/// Total vertical chrome above and below the Apple-1 raster (menu bar,
/// toolbar band, gap, status bar, decoration slop). Used by render() and
/// fullscreen layout to size the screen window.
float apple1LayoutVerticalChrome();

/// Compute the centred display size for a video card window that respects
/// the native aspect ratio. Returns (nativeW × ps, nativeH × ps), and writes
/// the chosen pixel scale into `pixelScaleOut`.
ImVec2 layoutFitVideoViewport(ImVec2 avail, float nativeW, float nativeH, float& pixelScaleOut);

/// Minimalist toolbar cassette icon (rounded rect + 2 reel holes).
void drawToolbarCassetteIcon(ImDrawList* dl, const ImVec2& rmin, const ImVec2& rmax);

/// Toolbar DIP chip icon for the Juke-Box card: plain black rectangle with
/// white pin stubs above and below. Vertical orientation.
void drawToolbarDipChipIcon(ImDrawList* dl, const ImVec2& rmin, const ImVec2& rmax);

/// P-LAB CodeTank — military-tank silhouette painted from primitives.
/// FontAwesome 6 Free has no tank glyph, so the toolbar button uses an
/// empty `##codeTankToolbar` ID and lets this helper paint over it.
void drawToolbarTankIcon(ImDrawList* dl, const ImVec2& rmin, const ImVec2& rmax);

/// Centred text label for toolbar buttons (BBS, HGR, etc.).
void drawToolbarTextLabel(ImDrawList* dl, const ImVec2& rmin, const ImVec2& rmax, const char* text);

/// Bullet + wrapped body text, so a long line reflows at the window edge
/// instead of clipping. Used for every Help-window bullet (Notes sections,
/// quick-start, acknowledgements) in both MainWindow_Dialogs.cpp and
/// MainWindow_Tutorials.cpp — which is why it lives here rather than as a
/// file-local static in either of them.
inline void bulletWrapped(const char* text)
{
    ImGui::Bullet();
    ImGui::TextWrapped("%s", text);
}

inline constexpr int kMonitorTintCount = 3;

Screen_ImGui::MonitorMode monitorTintAdvance(Screen_ImGui::MonitorMode m);
ImVec4                    monitorTintSwatchColor(Screen_ImGui::MonitorMode m);
const char*               monitorTintLabel(Screen_ImGui::MonitorMode m);

/// CRT phosphor cycle button. Background tinted with the current monitor
/// tint, click advances to the next mode.
bool monitorTintCycleButton(const char* id, const ImVec2& size, Screen_ImGui* screen);

// ---------------------------------------------------------------------------
// Machine presets — the table itself (MachineConfig / kMachinePresets[] /
// kMachinePresetCount / the named kPreset* indices) lives in the UI-free
// MachinePresets.h, included above; only the ImGui-flavoured helper at the
// bottom of this block stays on this side of the seam.
//
// The using-declarations below re-export those names into this namespace. They
// are NOT cosmetic: most MainWindow_*.cpp code sits in the GLOBAL namespace
// (`MainWindow_ImGui::applyMachineConfig(...)`) and reaches `detail` through a
// `using namespace pom1::mainwindow::detail;` directive, which pulls in names
// declared IN detail only — enclosing-namespace lookup never runs from there.
// Without these lines every call site would need a `pom1::` qualifier, and
// Pom1BenchHost's `md::kPresetGen2Bench` (md = this namespace) would not
// compile at all.
// ---------------------------------------------------------------------------

using pom1::PresetVec2;
using pom1::MachineWindowPlacement;
using pom1::BasicType;
using pom1::MachineConfig;
using pom1::PresetId;
using pom1::CardId;
using pom1::CardSet;
using pom1::presetIdFromIndex;
using pom1::isFantasyPreset;
using pom1::kDefaultPresetId;
using pom1::kMachinePresets;
using pom1::kMachinePresetCount;
// The registry accessors. `kMachinePresets[i]` is only safe for an index
// that came from a named constant; anything holding a user-supplied one
// goes through machinePresetAt(), and anything asking "does this machine
// multiplex?" through machinePresetMode() — isFantasyPreset() answers
// from a PresetId, and an external preset has none.
using pom1::machinePresetAt;
using pom1::machinePresetCount;
using pom1::machinePresetMode;
using pom1::machinePresetIsExternal;
using pom1::registerExternalPreset;
using pom1::clearExternalPresets;
using pom1::kPresetCC65Bench;
using pom1::kPresetTMS9918Bench;
using pom1::kPresetGen2Bench;
using pom1::kPresetIntegerCassette;
using pom1::kPresetMicroSD;
using pom1::kPresetTMS9918Card;
using pom1::kPresetGen2Color;

/// PresetVec2 → ImVec2. The preset table is UI-free and carries its own POD
/// vector type, so the handful of UI sites that feed a placement into ImGui
/// geometry convert here rather than MachinePresets.h learning about ImGui.
inline ImVec2 toImVec2(PresetVec2 v) { return ImVec2(v.x, v.y); }

/// Compute the axis-aligned bounding box (in ImGui screen coordinates)
/// that encloses every sized placement in `cfg.layout`. Entries whose size
/// is (0, 0) — "no size override" — contribute only their position and a
/// fallback extent picked by the caller. Returns (0, 0) when the layout
/// has no sized entries. Used by the first-frame GLFW window sizer so the
/// OS window grows to contain the preset's panels.
ImVec2 computePresetLayoutExtent(const MachineConfig& cfg,
                                 ImVec2 appleScreenFallbackSize);

} // namespace pom1::mainwindow::detail

#endif // MAINWINDOW_INTERNAL_H
