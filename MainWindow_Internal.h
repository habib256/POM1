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

/// Centred text label for toolbar buttons (BBS, HGR, etc.).
void drawToolbarTextLabel(ImDrawList* dl, const ImVec2& rmin, const ImVec2& rmax, const char* text);

inline constexpr int kMonitorTintCount = 3;

Screen_ImGui::MonitorMode monitorTintAdvance(Screen_ImGui::MonitorMode m);
ImVec4                    monitorTintSwatchColor(Screen_ImGui::MonitorMode m);
const char*               monitorTintLabel(Screen_ImGui::MonitorMode m);

/// CRT phosphor cycle button. Background tinted with the current monitor
/// tint, click advances to the next mode.
bool monitorTintCycleButton(const char* id, const ImVec2& size, Screen_ImGui* screen);

// ---------------------------------------------------------------------------
// Machine presets — table of all machine configurations available via the
// Hardware → Preset menu and applyMachineConfig(). Defined in
// MainWindow_Presets.cpp; consumed by MainWindow_Menu.cpp (menu list) and
// MainWindow_Presets.cpp (applyMachineConfig + getPresetName).
// Future migration target: load from external presets.json.
// ---------------------------------------------------------------------------

struct MachineWindowPlacement {
    const char* name;
    ImVec2 pos;
    ImVec2 size;  // (0,0) = no size override
};

enum class BasicType { Integer, ApplesoftLite };

struct MachineConfig {
    const char* name;
    const char* description;
    bool graphicsCard, microSD, sid, tms9918, a1ioRtc, wifiModem, terminalCard;
    bool krusader;
    bool cffa1;
    bool aci;                   // Apple Cassette Interface (false = pre-ACI bare 4K)
    int  ramKB;                 // Usable RAM in kilobytes (8 = Woz original, 32 = Replica/P-LAB)
    BasicType basicType;
    MachineWindowPlacement layout[8];
    int layoutCount;
};

extern const MachineConfig kMachinePresets[];
extern const int kMachinePresetCount;

} // namespace pom1::mainwindow::detail

#endif // MAINWINDOW_INTERNAL_H
