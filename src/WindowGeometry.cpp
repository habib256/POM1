// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// WindowGeometry.cpp — see WindowGeometry.h.

#include "WindowGeometry.h"

#include <algorithm>
#include <istream>
#include <ostream>

namespace pom1::wingeom {

bool parseSizeSidecar(std::istream& in, OsWindowGeom& g)
{
    if (!(in >> g.w >> g.h) ||
        g.w <= 0 || g.h <= 0 || g.w > kMaxDimension || g.h > kMaxDimension)
        return false;

    int x = 0, y = 0, m = 0, fs = 0;
    if (in >> x >> y) {
        // Same sanity bound as the size, but positions may be negative — the
        // monitor left of or above the primary one lives at negative
        // coordinates on every platform POM1 runs on.
        if (x >= -kMaxDimension && x <= kMaxDimension &&
            y >= -kMaxDimension && y <= kMaxDimension) {
            g.havePos = true;
            g.x = x;
            g.y = y;
        }
        if (in >> m >> fs) {
            g.maximized = (m != 0);
            // Ignore an out-of-range mode rather than rejecting the file: a
            // sidecar written by a newer POM1 should still restore its size.
            if (fs >= 0 && fs <= 2) g.fullscreenMode = fs;
        }
    }
    return true;
}

bool writeSizeSidecar(std::ostream& out, const OsWindowGeom& g)
{
    out << g.w << ' ' << g.h;
    if (g.havePos) {
        out << ' ' << g.x << ' ' << g.y << ' '
            << (g.maximized ? 1 : 0) << ' ' << g.fullscreenMode;
    }
    out << '\n';
    return static_cast<bool>(out);
}

void clampToWorkAreas(OsWindowGeom& g,
                      const std::vector<Rect>& workAreas,
                      const Rect* primary)
{
    if (!g.havePos) return;

    for (const Rect& m : workAreas) {
        const int ovX = std::min(g.x + g.w, m.x + m.w) - std::max(g.x, m.x);
        const int ovY = std::min(g.y + g.h, m.y + m.h) - std::max(g.y, m.y);
        if (ovX >= kMinVisible && ovY >= kMinVisible)
            return;                          // visible enough — keep as saved
    }

    if (!primary) { g.havePos = false; return; }

    g.w = std::min(g.w, primary->w);
    g.h = std::min(g.h, primary->h);
    g.x = primary->x + std::max(0, (primary->w - g.w) / 2);
    g.y = primary->y + std::max(0, (primary->h - g.h) / 2);
}

} // namespace pom1::wingeom
