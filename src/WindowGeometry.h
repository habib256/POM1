// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// WindowGeometry.h — the OS-window sidecar (ini/preset_NN.size) and the
// off-screen rescue rule.
//
// Extracted from MainWindow_Presets.cpp so the FORMAT and the CLAMP are
// testable without a window manager. The caller keeps the file paths and the
// GLFW calls; parsing, serialising and deciding where a window should land are
// pure and live here.

#ifndef POM1_WINDOW_GEOMETRY_H
#define POM1_WINDOW_GEOMETRY_H

#include <iosfwd>
#include <vector>

namespace pom1::wingeom {

/// A monitor work area (or any rectangle), in virtual-desktop coordinates.
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
};

/// Contents of ini/preset_NN.size: "W H [X Y M F]".
///
/// The sidecar grew from "W H" to the six-field form in juillet 2026; the extra
/// fields stay optional on read, so a legacy two-field file still loads with
/// the position untouched and the window windowed.
struct OsWindowGeom {
    /// Always the WINDOWED rect, even when saved from a fullscreen session —
    /// it is what leaving fullscreen has to land on.
    int  w = 0, h = 0;
    bool havePos = false;
    int  x = 0, y = 0;
    bool maximized = false;
    /// F field: 0 = windowed, 1 = POM1's own fullscreen (glfwSetWindowMonitor),
    /// 2 = macOS native fullscreen SPACE. 2 is distinct because it must never
    /// be restored programmatically — that space belongs to the green button,
    /// and forcing a borderless fullscreen onto a window AppKit already owns is
    /// a mess. Legacy files only ever carry 0 or 1, so they read unchanged.
    int  fullscreenMode = 0;
};

/// Largest dimension accepted from a sidecar. Comfortably exceeds any real
/// display while rejecting garbage like "2000000000" that would otherwise be
/// handed straight to glfwSetWindowSize.
inline constexpr int kMaxDimension = 16384;

/// Minimum overlap, in pixels on each axis, for a saved window to count as
/// "still reachable" on some monitor.
inline constexpr int kMinVisible = 64;

/// Parse a sidecar. False (and `g` left unusable) on a corrupt or absurd size,
/// which the caller must treat as "no saved geometry".
bool parseSizeSidecar(std::istream& in, OsWindowGeom& g);

/// Serialise in the format parseSizeSidecar reads back. A geometry with no
/// position is written in the legacy two-field form, so restoring it leaves
/// the OS free to place the window.
bool writeSizeSidecar(std::ostream& out, const OsWindowGeom& g);

/// Pull a saved window back onto a live monitor.
///
/// Keeps the geometry untouched when it still overlaps ANY work area by at
/// least kMinVisible on both axes — a window deliberately parked half off the
/// edge stays where the user left it. Otherwise it is shrunk to fit `primary`
/// and centred there. With no monitors at all the position is dropped
/// (havePos = false) and the OS gets to choose.
void clampToWorkAreas(OsWindowGeom& g,
                      const std::vector<Rect>& workAreas,
                      const Rect* primary);

} // namespace pom1::wingeom

#endif // POM1_WINDOW_GEOMETRY_H
