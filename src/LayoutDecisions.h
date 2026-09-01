// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// LayoutDecisions.h — the layout and fullscreen DECISIONS, pure.
//
// Third seam of the family that started with Apple1KeyMap / FullscreenExpand /
// WindowGeometry / StagedCardConfiguration: no ImGui, no GLFW, no MainWindow,
// so every rule below is reachable from a test binary that links nothing.
//
// WHY THIS EXISTS. The fullscreen and layout-persistence paths are where the
// six août-2026 defects lived, and not one of them was pinnable where it stood:
// the rules were interleaved with `glfwSetWindowMonitor`, `ImGui::Checkbox` and
// a live window. What was extracted first was the TIMING (FullscreenExpand) and
// the FORMAT (WindowGeometry). This header is the third of the four things the
// backlog names — the ARBITRATION: which rect gets persisted, which geometry
// applies on restore, and what a click on the Fullscreen checkbox means when
// AppKit already owns the window.
//
// Everything here is a decision returned as a value. The caller keeps the GLFW
// calls, the file paths and the ImGui submission — a plan is never carried out
// on this side of the seam, which is what makes each rule assertable.

#ifndef POM1_LAYOUT_DECISIONS_H
#define POM1_LAYOUT_DECISIONS_H

#include <algorithm>
#include <cmath>

#include "WindowGeometry.h"

namespace pom1::layout {

// ---------------------------------------------------------------------------
// 1. Which frames may update the tracked WINDOWED rect
// ---------------------------------------------------------------------------

/// The windowed rect is what savePresetLayout persists and what leaving
/// fullscreen has to land on, so it must only ever be sampled from a frame that
/// IS the windowed one. `osFullscreen` is the any-route answer
/// (MainWindow_ImGui::osWindowIsFullscreen), never the `fullscreen` member:
/// GLFW models only its own fullscreen, so a macOS native space would otherwise
/// overwrite the windowed rect with the screen size and lose it for good.
inline bool shouldTrackWindowedRect(bool osFullscreen, bool maximized, bool iconified)
{
    return !osFullscreen && !maximized && !iconified;
}

// ---------------------------------------------------------------------------
// 2. What goes into ini/preset_NN.size
// ---------------------------------------------------------------------------

/// Everything savePresetLayout can observe about the OS window.
struct SaveInputs {
    /// The window's rect right now (glfwGetWindowSize / glfwGetWindowPos).
    int liveW = 0, liveH = 0, liveX = 0, liveY = 0;
    /// The rect tracked by shouldTrackWindowedRect() above.
    int windowedW = 0, windowedH = 0, windowedX = 0, windowedY = 0;
    /// GLFW_MAXIMIZED, as reported.
    bool maximizedAttrib = false;
    /// The window sits in a macOS native fullscreen SPACE (AppKit style mask).
    bool nativeFullscreen = false;
    /// Fullscreen by ANY route — POM1's own toggle or the native space.
    bool anyFullscreen = false;
    /// A CLI --fullscreen (kiosk) forced this session.
    bool cliForcedFullscreen = false;
};

/// Decide the sidecar to write.
///
/// Three rules, each of them a defect that got out once:
///
///  - The rect persisted is ALWAYS the windowed one. A maximized or fullscreen
///    session writes the tracked rect instead of its live (screen-sized) frame,
///    so restoring it and then leaving fullscreen lands somewhere sane.
///  - GLFW_MAXIMIZED is meaningless inside a macOS space (AppKit reports the
///    underlying window's zoomed state), so it is not persisted as an intent.
///  - A kiosk --fullscreen must NOT rewrite the profile as fullscreen: the next
///    plain launch would come up fullscreen for no visible reason. The flag only
///    ever drives POM1's own fullscreen, so it cannot suppress mode 2 — a native
///    space is always something the user asked for with the green button.
inline wingeom::OsWindowGeom decidePersistedGeometry(const SaveInputs& in)
{
    wingeom::OsWindowGeom g;
    const bool maxed = !in.nativeFullscreen && in.maximizedAttrib;
    if (!maxed && !in.anyFullscreen) {
        g.w = in.liveW;  g.h = in.liveH;
        g.x = in.liveX;  g.y = in.liveY;
    } else {
        g.w = in.windowedW;  g.h = in.windowedH;
        g.x = in.windowedX;  g.y = in.windowedY;
    }
    g.havePos   = true;
    g.maximized = maxed;
    g.fullscreenMode = in.nativeFullscreen
                           ? 2
                           : ((in.anyFullscreen && !in.cliForcedFullscreen) ? 1 : 0);
    return g;
}

// ---------------------------------------------------------------------------
// 3. Which geometry applies on restore
// ---------------------------------------------------------------------------

enum class RestoreAction {
    /// No saved geometry and a windowed session — the OS keeps what it has.
    Nothing,
    /// No saved geometry, but the session is fullscreen: the .ini that DID load
    /// was authored for a windowed frame, so the Apple 1 Screen still needs the
    /// expand. Without this the profile falls between two stools —
    /// applyMachineConfig skips it because a layout *was* loaded, and the sized
    /// path below never runs.
    ExpandOnly,
    /// Fullscreen is a SESSION property, never a per-profile one: switching
    /// profile must not yank the user out of it. Keep the fullscreen frame and
    /// only adopt the incoming profile's windowed rect for later. (On macOS the
    /// setFrame: behind glfwSetWindowSize is ignored inside a space anyway, so
    /// the frame would stay screen-sized while every layout decision assumed it
    /// had shrunk.)
    KeepFullscreenFrame,
    /// Saved under POM1's own fullscreen toggle, session is windowed — restore
    /// straight into it.
    EnterOwnFullscreen,
    /// Windowed (mode 0), or saved in a macOS native space (mode 2). Mode 2 is
    /// deliberately NOT re-entered: that space belongs to the green button, and
    /// forcing a borderless fullscreen onto a window AppKit owns is a mess — so
    /// the underlying windowed rect it recorded is what comes back.
    ApplyWindowedRect,
};

struct RestorePlan {
    RestoreAction action = RestoreAction::Nothing;
    /// Copy the sidecar's rect into the tracked windowed members, so leaving
    /// fullscreen later lands on the incoming profile's frame.
    bool adoptWindowedRect = false;
    /// Re-expand the Apple 1 Screen over the display (armFullscreenScreenExpand).
    bool armExpand = false;
    bool applyPos  = false;
    bool applySize = false;
    bool maximize  = false;
    /// Un-maximize if the live window happens to be maximized.
    bool unmaximize = false;
};

/// `g` is the sidecar AFTER wingeom::clampToWorkAreas. `sessionFullscreen` is
/// the any-route answer, as everywhere else here.
inline RestorePlan planLayoutRestore(bool haveSidecar,
                                     const wingeom::OsWindowGeom& g,
                                     bool sessionFullscreen)
{
    RestorePlan p;
    if (!haveSidecar) {
        if (sessionFullscreen) {
            p.action    = RestoreAction::ExpandOnly;
            p.armExpand = true;
        }
        return p;
    }
    if (sessionFullscreen) {
        p.action             = RestoreAction::KeepFullscreenFrame;
        p.adoptWindowedRect  = true;
        // A profile saved IN fullscreen already carries a fullscreen-sized
        // arrangement: leave it alone. One authored windowed would put the
        // Apple 1 Screen in a corner of the display, so that one is expanded.
        p.armExpand          = (g.fullscreenMode == 0);
        return p;
    }
    if (g.fullscreenMode == 1) {
        p.action            = RestoreAction::EnterOwnFullscreen;
        p.adoptWindowedRect = true;
        return p;
    }
    p.action     = RestoreAction::ApplyWindowedRect;
    p.applyPos   = g.havePos;
    p.applySize  = true;
    p.maximize   = g.maximized;
    p.unmaximize = !g.maximized;
    return p;
}

// ---------------------------------------------------------------------------
// 4. Resetting the active preset's layout
// ---------------------------------------------------------------------------

struct ResetPlan {
    /// Resize the OS frame to defaultOsWindowSize(). Never while fullscreen —
    /// the reset still rearranges the ImGui windows inside the screen-sized
    /// frame, but the frame itself is not ours to shrink.
    bool resizeOsWindow = false;
    bool armExpand      = false;
    /// Drop the "Apple 1 Screen" entry from pendingLayout before forcing the
    /// rest. A reset force-applies entries with ImGuiCond_Always, and
    /// applyPendingLayout runs AFTER the expand's SetNextWindowSize — keeping
    /// the entry stamps the windowed rect back over the expand and leaves the
    /// screen undersized for the session.
    bool dropScreenPlacement = false;
    /// Frames to force the factory geometry for. A curated ini_defaults/ seed
    /// was already force-applied by LoadIniSettingsFromDisk, so it needs none.
    int forceFrames = 0;
};

inline ResetPlan planLayoutReset(bool curatedSeedLoaded, bool sessionFullscreen)
{
    ResetPlan p;
    if (curatedSeedLoaded)
        return p;                       // ini already applied; nothing to force
    p.forceFrames = 2;
    if (sessionFullscreen) {
        p.armExpand           = true;
        p.dropScreenPlacement = true;
    } else {
        p.resizeOsWindow = true;
    }
    return p;
}

// ---------------------------------------------------------------------------
// 5. How a pending placement is applied
// ---------------------------------------------------------------------------

struct PlacementApply {
    bool apply   = false;
    /// ImGuiCond_Always instead of ImGuiCond_FirstUseEver.
    bool always  = false;
    /// Erase the entry now. A forced entry is KEPT so it re-applies every frame
    /// until the force drains; a normal one is consumed once, which is what
    /// lets the user's own drags win on every later frame of the session.
    bool consume = false;
};

inline PlacementApply placementApply(bool found, bool resetForcing)
{
    PlacementApply a;
    if (!found) return a;
    a.apply   = true;
    a.always  = resetForcing;
    a.consume = !resetForcing;
    return a;
}

// ---------------------------------------------------------------------------
// 6. The Fullscreen checkbox, and AppKit
// ---------------------------------------------------------------------------

/// How long an in-flight "leave the native space" request stays latched when we
/// never observe AppKit actually leaving. The transition is ~0.5 s animated.
inline constexpr double kMacNativeExitTimeoutSeconds = 2.0;

struct CheckboxState {
    /// What the checkbox must SHOW this frame.
    bool showChecked = false;
    /// The latch to store back (-1 = none in flight).
    double exitRequestedAt = -1.0;
};

/// The checkbox reflects fullscreen by ANY route, so a session entered through
/// macOS' green button doesn't read as "off" — ticking it would then stack a
/// glfwSetWindowMonitor fullscreen on top of AppKit's space.
///
/// The latch is the whole subtlety: AppKit keeps NSWindowStyleMaskFullScreen set
/// for the entire exit animation, so `osFullscreen` stays true for ~0.5 s after
/// the user asked to leave. Showing that raw makes the box snap back to ticked,
/// which reads as "my click did nothing" — and the second click it invites hands
/// AppKit another toggleFullScreen:, putting the window straight back in.
inline CheckboxState fullscreenCheckboxState(bool osFullscreen,
                                             bool nativeFullscreen,
                                             double now,
                                             double exitRequestedAt)
{
    CheckboxState s;
    s.exitRequestedAt = exitRequestedAt;
    // Retire the latch once AppKit has actually left the space, or after the
    // timeout if we somehow never observe that.
    if (s.exitRequestedAt >= 0.0
        && (!nativeFullscreen || now - s.exitRequestedAt > kMacNativeExitTimeoutSeconds))
        s.exitRequestedAt = -1.0;
    s.showChecked = (s.exitRequestedAt >= 0.0) ? false : osFullscreen;
    return s;
}

enum class ToggleAction {
    /// Swallow the click — a native transition is already running.
    Ignore,
    /// Inside a macOS space: hand the exit back to AppKit
    /// (macWindowToggleNativeFullscreen) and stop. glfwSetWindowMonitor must
    /// never fight a window AppKit owns.
    HandBackToAppKit,
    /// POM1's own fullscreen (setOsFullscreen).
    ApplyOwnFullscreen,
};

struct TogglePlan {
    ToggleAction action = ToggleAction::Ignore;
    /// Target state for ApplyOwnFullscreen, and the value of the `fullscreen`
    /// member in every case (leaving a native space always ends up false).
    bool targetFullscreen = false;
    /// Release a CLI --fullscreen. Unticking is the ONLY escape hatch out of a
    /// kiosk session: without it render() re-asserts fullscreen next frame —
    /// including on the AppKit path, the moment the space finishes closing.
    bool dropCliForced = false;
    /// The latch to store back.
    double exitRequestedAt = -1.0;
};

/// `requested` is the value the checkbox now holds, i.e. what the user asked
/// for. Call only on the frame the checkbox actually toggled.
inline TogglePlan planFullscreenToggle(bool requested,
                                       bool nativeFullscreen,
                                       double now,
                                       double exitRequestedAt)
{
    TogglePlan p;
    p.exitRequestedAt = exitRequestedAt;
    if (exitRequestedAt >= 0.0) {
        p.action           = ToggleAction::Ignore;
        p.targetFullscreen = requested;
        return p;
    }
    if (nativeFullscreen) {
        p.action           = ToggleAction::HandBackToAppKit;
        p.targetFullscreen = false;
        p.dropCliForced    = true;
        p.exitRequestedAt  = now;
        return p;
    }
    p.action           = ToggleAction::ApplyOwnFullscreen;
    p.targetFullscreen = requested;
    p.dropCliForced    = !requested;
    return p;
}

// ---------------------------------------------------------------------------
// 7. The OS window size a preset asks for
// ---------------------------------------------------------------------------

/// Everything defaultOsWindowSize / computeWasmCanvasSize need once the caller
/// has done the two things that genuinely require a live UI: measuring the font
/// and walking the preset's layout table for its bounding box.
struct OsWindowSizeInputs {
    /// The Apple 1 Screen window, already scaled.
    float screenW = 0.0f, screenH = 0.0f;
    /// Menu bar + toolbar + gap + status band + decoration slop.
    float verticalChrome = 0.0f;
    int   extraW = 0;
    /// Bounding box of the preset's declared layout (0 = the preset declares none).
    float extentW = 0.0f, extentH = 0.0f;
    /// A second extent to floor at — POM1 Fantasy's, so switching presets never
    /// shrinks the desktop window. Deliberately ZERO on WASM: there the canvas
    /// IS the whole app surface, so a bare Apple-1 profile must give a small
    /// canvas and a fully-loaded one a large canvas. That is what "adapt to each
    /// profile" means in the browser, and a floor would defeat it.
    float floorExtentW = 0.0f, floorExtentH = 0.0f;
    float rightPad = 0.0f, bottomPad = 0.0f;
    /// 0 = unclamped (desktop). WASM clamps for usability and safety.
    int minW = 0, minH = 0, maxW = 0, maxH = 0;
};

struct OsWindowSize { int w = 0, h = 0; };

inline OsWindowSize computeOsWindowSize(const OsWindowSizeInputs& in)
{
    OsWindowSize out;
    out.w = static_cast<int>(in.screenW) + in.extraW;
    out.h = static_cast<int>(std::ceil(in.screenH + in.verticalChrome));
    const auto floorAt = [&](float ew, float eh) {
        if (ew <= 0.0f || eh <= 0.0f) return;
        out.w = std::max(out.w, static_cast<int>(std::ceil(ew + in.rightPad)));
        out.h = std::max(out.h, static_cast<int>(std::ceil(eh + in.bottomPad)));
    };
    floorAt(in.extentW, in.extentH);
    floorAt(in.floorExtentW, in.floorExtentH);
    if (in.minW > 0) out.w = std::max(out.w, in.minW);
    if (in.minH > 0) out.h = std::max(out.h, in.minH);
    if (in.maxW > 0) out.w = std::min(out.w, in.maxW);
    if (in.maxH > 0) out.h = std::min(out.h, in.maxH);
    return out;
}

} // namespace pom1::layout

#endif // POM1_LAYOUT_DECISIONS_H
