// mainwindow_logic_smoke_test.cpp — the logic lifted out of MainWindow.
//
// MainWindow_ImGui is a ~400-declaration class spread over eleven translation
// units, and the test suite deliberately keeps UI sources out of its binaries.
// That left the largest layer of POM1 with no coverage at all — and it is
// exactly where the bugs fixed in august 2026 lived: a destructive backspace in
// the display, six defects in the fullscreen path. Neither could be pinned.
//
// Three pieces of that logic are now pure objects with no ImGui and no GLFW,
// and this is their test. It is not a UI test: it asserts the DECISIONS the UI
// used to make inline — which byte a key sends, when a fullscreen expand may
// fire, what a layout sidecar means.

#include "Apple1KeyMap.h"
#include "FullscreenExpand.h"
#include "WindowGeometry.h"

#include <cassert>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
    if (!ok) { std::fprintf(stderr, "  FAIL: %s\n", what); ++failures; }
}

// ---------------------------------------------------------------- keyboard --
void testKeyMap()
{
    using namespace pom1::keymap;

    // github #38. The Apple-1 cannot delete the character left of the cursor,
    // so the host Backspace key sends '_' ($DF on the bus) — the Woz Monitor
    // echoes it and steps the input index back. $08 would print nothing and
    // leave a junk byte in the line, NOTCR testing only $DF and $9B.
    check(mapKey(kKeyBackspace, 0) == '_', "Backspace must send '_', not $08");
    check(mapKey(kKeyBackspace, 0) != '\b', "Backspace must not send $08");

    check(mapKey(kKeyEnter, 0)    == '\r', "Enter sends CR");
    check(mapKey(kKeyKpEnter, 0)  == '\r', "keypad Enter sends CR");
    check(mapKey(kKeyEscape, 0)   == 27,   "Escape sends $1B");

    // CTRL+letter → $01-$1A. Unreachable before august 2026: the toolkit emits
    // no character event for a CTRL chord, so Ctrl-C (Integer BASIC break) and
    // Ctrl-H (Applesoft Lite's line editor) could only be typed from the
    // on-screen keyboard.
    check(mapKey('A', kModControl) == 0x01, "Ctrl-A is $01");
    check(mapKey('C', kModControl) == 0x03, "Ctrl-C is $03 (Integer BASIC break)");
    check(mapKey('H', kModControl) == 0x08, "Ctrl-H is $08 (Applesoft Lite backspace)");
    check(mapKey('Z', kModControl) == 0x1A, "Ctrl-Z is $1A");
    for (int k = kKeyA; k <= kKeyZ; ++k)
        check(mapKey(k, kModControl) == static_cast<char>(k - kKeyA + 1),
              "every CTRL+letter maps to its control code");

    // Ctrl+Shift+letter yields the same code on a real ASCII keyboard.
    check(mapKey('C', kModControl | kModShift) == 0x03, "Ctrl+Shift+C is still $03");

    // ALT/SUPER are excluded so the OS keeps its Cmd- combos.
    check(mapKey('C', kModControl | kModSuper) == kNoKey, "Ctrl+Cmd+C is not ours");
    check(mapKey('C', kModControl | kModAlt)   == kNoKey, "Ctrl+Alt+C is not ours");
    check(!isControlChord(kModSuper),                "Cmd alone is not a CTRL chord");
    check(!isControlChord(kModShift),                "Shift alone is not a CTRL chord");
    check(isControlChord(kModControl | kModShift),   "Ctrl+Shift is a CTRL chord");

    // Printable keys arrive through the character callback; this path must stay
    // out of their way or every letter would double up.
    check(mapKey('A', 0) == kNoKey, "unmodified letters come from the char event");
    check(mapKey(' ', 0) == kNoKey, "space comes from the char event");
}

// -------------------------------------------------------------- fullscreen --
void testFullscreenSettle()
{
    using pom1::FullscreenExpandSettler;

    // Idle: never fires unarmed.
    {
        FullscreenExpandSettler s;
        check(!s.pending(), "starts idle");
        for (int i = 0; i < 10; ++i)
            check(!s.step(1920, 1080), "an unarmed settler never fires");
    }

    // Synchronous path (glfwSetWindowMonitor): the size is already final when
    // the transition is noticed, so it settles immediately.
    {
        FullscreenExpandSettler s;
        s.arm(1920, 1080);
        check(s.pending(), "armed");
        int fired = 0, frame = 0;
        for (; frame < 10; ++frame)
            if (s.step(1920, 1080)) { ++fired; break; }
        check(fired == 1, "synchronous transition fires once");
        check(frame < FullscreenExpandSettler::kSettleFrames + 1,
              "and does so promptly");
        check(!s.pending(), "and disarms itself");
        for (int i = 0; i < 5; ++i)
            check(!s.step(1920, 1080), "it does not fire twice");
    }

    // macOS animated path — the reason this is a settle counter and not a
    // delay. AppKit sets the fullscreen style mask at the START of a ~0.5 s
    // animation, so the transition is noticed while the framebuffer is still
    // the windowed size and keeps growing for ~30 frames. A fixed 2-frame
    // countdown would fire at 1000x600 and leave the screen window that size
    // for the whole session.
    {
        FullscreenExpandSettler s;
        s.arm(1000, 600);                       // pre-animation frame
        bool firedEarly = false;
        for (int i = 1; i <= 30; ++i) {         // animation, size moving
            if (s.step(1000.0f + i * 30.0f, 600.0f + i * 16.0f)) firedEarly = true;
        }
        check(!firedEarly, "must NOT fire while the display size is still moving");
        check(s.pending(), "and must stay armed across the whole animation");

        int fired = 0;
        for (int i = 0; i < 5; ++i)
            if (s.step(1920, 1080)) ++fired;    // animation over, size steady
        check(fired == 1, "fires exactly once after the size settles");
    }

    // Leaving fullscreen before the expand landed must drop it.
    {
        FullscreenExpandSettler s;
        s.arm(1920, 1080);
        s.cancel();
        check(!s.pending(), "cancel disarms");
        for (int i = 0; i < 5; ++i)
            check(!s.step(1920, 1080), "a cancelled expand never fires");
    }
}

// ----------------------------------------------------------- .size sidecar --
bool parse(const std::string& text, pom1::wingeom::OsWindowGeom& g)
{
    std::istringstream in(text);
    return pom1::wingeom::parseSizeSidecar(in, g);
}

void testSidecar()
{
    using namespace pom1::wingeom;

    // Legacy two-field files still load: position untouched, windowed.
    {
        OsWindowGeom g;
        check(parse("1200 800\n", g), "legacy 2-field file parses");
        check(g.w == 1200 && g.h == 800, "legacy dimensions");
        check(!g.havePos, "legacy file carries no position");
        check(g.fullscreenMode == 0, "legacy file is windowed");
    }

    // Full six-field form, including the mode that replaced the old bool.
    {
        OsWindowGeom g;
        check(parse("1200 800 100 50 0 2\n", g), "6-field file parses");
        check(g.havePos && g.x == 100 && g.y == 50, "position read");
        check(!g.maximized, "maximized flag read");
        check(g.fullscreenMode == 2, "macOS native-space mode read");
    }

    // A monitor left of or above the primary lives at negative coordinates.
    {
        OsWindowGeom g;
        check(parse("800 600 -1500 -200 0 0\n", g), "negative position parses");
        check(g.havePos && g.x == -1500, "negative x is legitimate, not garbage");
    }

    // Garbage that would otherwise reach glfwSetWindowSize.
    for (const char* bad : {"", "0 800", "1200 0", "-5 -5", "2000000000 800", "abc def"}) {
        OsWindowGeom g;
        check(!parse(bad, g), "a corrupt sidecar is rejected");
    }

    // Round trip.
    {
        OsWindowGeom in;
        in.w = 1440; in.h = 900; in.havePos = true; in.x = -20; in.y = 33;
        in.maximized = true; in.fullscreenMode = 1;
        std::ostringstream out;
        check(writeSizeSidecar(out, in), "serialises");
        OsWindowGeom back;
        check(parse(out.str(), back), "re-reads what it wrote");
        check(back.w == in.w && back.h == in.h && back.x == in.x && back.y == in.y &&
              back.maximized == in.maximized && back.fullscreenMode == in.fullscreenMode,
              "round trip preserves every field");
    }

    // No position → legacy short form, so the OS places the window.
    {
        OsWindowGeom g; g.w = 640; g.h = 480;
        std::ostringstream out;
        writeSizeSidecar(out, g);
        check(out.str() == "640 480\n", "position-less geometry writes the short form");
    }
}

// ------------------------------------------------------- off-screen rescue --
void testClamp()
{
    using namespace pom1::wingeom;
    const std::vector<Rect> single{{0, 0, 1920, 1080}};
    const Rect primary{0, 0, 1920, 1080};

    // Fully visible — untouched.
    {
        OsWindowGeom g; g.w = 800; g.h = 600; g.havePos = true; g.x = 100; g.y = 100;
        clampToWorkAreas(g, single, &primary);
        check(g.x == 100 && g.y == 100 && g.w == 800, "a visible window is left alone");
    }

    // Deliberately parked half off the right edge: still reachable, so keep it.
    {
        OsWindowGeom g; g.w = 800; g.h = 600; g.havePos = true; g.x = 1800; g.y = 100;
        clampToWorkAreas(g, single, &primary);
        check(g.x == 1800, "a window overlapping by >= kMinVisible stays put");
    }

    // The monitor it was saved on is gone (laptop undocked).
    {
        OsWindowGeom g; g.w = 800; g.h = 600; g.havePos = true; g.x = 3000; g.y = 200;
        clampToWorkAreas(g, single, &primary);
        check(g.x == (1920 - 800) / 2 && g.y == (1080 - 600) / 2,
              "an unreachable window is centred on the primary monitor");
    }

    // Second monitor present: a window on it is reachable and must not move.
    {
        const std::vector<Rect> dual{{0, 0, 1920, 1080}, {1920, 0, 2560, 1440}};
        OsWindowGeom g; g.w = 800; g.h = 600; g.havePos = true; g.x = 2400; g.y = 100;
        clampToWorkAreas(g, dual, &primary);
        check(g.x == 2400, "a window on a second monitor is left alone");
    }

    // Saved larger than the only monitor — shrink to fit.
    {
        const std::vector<Rect> small{{0, 0, 1280, 720}};
        const Rect smallPrimary{0, 0, 1280, 720};
        OsWindowGeom g; g.w = 3000; g.h = 2000; g.havePos = true; g.x = 5000; g.y = 5000;
        clampToWorkAreas(g, small, &smallPrimary);
        check(g.w == 1280 && g.h == 720, "an oversized window is shrunk to the work area");
        check(g.x == 0 && g.y == 0, "and pinned to its origin");
    }

    // No monitors at all: drop the position, let the OS decide.
    {
        OsWindowGeom g; g.w = 800; g.h = 600; g.havePos = true; g.x = 9000; g.y = 9000;
        clampToWorkAreas(g, {}, nullptr);
        check(!g.havePos, "with no monitor the position is surrendered to the OS");
    }

    // A geometry with no position is never invented one.
    {
        OsWindowGeom g; g.w = 800; g.h = 600;
        clampToWorkAreas(g, single, &primary);
        check(!g.havePos, "a position-less geometry stays position-less");
    }
}

} // namespace

int main()
{
    testKeyMap();
    testFullscreenSettle();
    testSidecar();
    testClamp();

    if (failures) {
        std::fprintf(stderr, "mainwindow_logic_smoke: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("mainwindow_logic_smoke: OK — keymap, fullscreen settle, "
                ".size sidecar and off-screen rescue\n");
    return 0;
}
