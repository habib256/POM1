// The layout and fullscreen DECISIONS — pom1::layout (src/LayoutDecisions.h).
//
// Third seam of the family after Apple1KeyMap / FullscreenExpand /
// WindowGeometry / StagedCardConfiguration, and the one that closes the
// fullscreen path: the TIMING was extracted (FullscreenExpandSettler) and so
// was the FORMAT (wingeom), but the ARBITRATION stayed interleaved with GLFW
// and ImGui — which is exactly where the six août-2026 defects lived, none of
// them pinnable where it stood.
//
// Covered:
//   §1  the windowed rect is only ever sampled from a windowed frame;
//   §2  what gets persisted: windowed rect always, the kiosk exemption, the
//       macOS space, and GLFW_MAXIMIZED being meaningless inside one;
//   §3  which geometry applies on restore, all five branches;
//   §4  the round trip through the real sidecar writer/parser;
//   §5  a layout reset, and the dropped "Apple 1 Screen" placement;
//   §6  how a pending placement applies — forced entries are kept, normal ones
//       consumed;
//   §7  the Fullscreen checkbox against AppKit's animated exit;
//   §8  the OS window size, incl. the Fantasy floor and its deliberate absence
//       on WASM.
//
// Links nothing but WindowGeometry.cpp (for §4's real round trip). That is the
// point: none of this needs a window manager to be true.

#include "LayoutDecisions.h"
#include "WindowGeometry.h"

#include <cassert>
#include <cstdio>
#include <sstream>
#include <vector>

using namespace pom1::layout;
using pom1::wingeom::OsWindowGeom;

namespace {

// A session the user has dragged to 1200x800 at (120, 90).
SaveInputs windowedSession()
{
    SaveInputs s;
    s.liveW = s.windowedW = 1200;
    s.liveH = s.windowedH = 800;
    s.liveX = s.windowedX = 120;
    s.liveY = s.windowedY = 90;
    return s;
}

// The same session after it went fullscreen: the LIVE rect is now the display,
// the tracked windowed rect still remembers where the window was.
SaveInputs fullscreenSession()
{
    SaveInputs s = windowedSession();
    s.liveW = 2560; s.liveH = 1440;
    s.liveX = 0;    s.liveY = 0;
    s.anyFullscreen = true;
    return s;
}

} // namespace

int main()
{
    // -----------------------------------------------------------------
    // §1 Only a windowed frame may update the tracked windowed rect.
    //
    // Keyed on the ANY-ROUTE answer: GLFW models only its own fullscreen, so
    // asking the `fullscreen` member here lets a macOS native space overwrite
    // the windowed rect with the screen size — and it is gone for good, since
    // that rect is the only thing leaving fullscreen has to land on.
    // -----------------------------------------------------------------
    {
        assert(shouldTrackWindowedRect(false, false, false));
        assert(!shouldTrackWindowedRect(true,  false, false));
        assert(!shouldTrackWindowedRect(false, true,  false));
        assert(!shouldTrackWindowedRect(false, false, true));
    }

    // -----------------------------------------------------------------
    // §2 What lands in ini/preset_NN.size.
    // -----------------------------------------------------------------
    {
        // Plain windowed session: the live rect IS the windowed rect.
        const OsWindowGeom g = decidePersistedGeometry(windowedSession());
        assert(g.w == 1200 && g.h == 800);
        assert(g.havePos && g.x == 120 && g.y == 90);
        assert(!g.maximized);
        assert(g.fullscreenMode == 0);
    }
    {
        // Fullscreen: persist the WINDOWED rect, never the screen-sized live
        // one — otherwise leaving fullscreen lands on a 2560x1440 "window".
        const OsWindowGeom g = decidePersistedGeometry(fullscreenSession());
        assert(g.w == 1200 && g.h == 800 && g.x == 120 && g.y == 90);
        assert(g.fullscreenMode == 1);
    }
    {
        // Maximized: same rule, the tracked rect wins over the live one.
        SaveInputs s = windowedSession();
        s.maximizedAttrib = true;
        s.liveW = 2560; s.liveH = 1400; s.liveX = 0; s.liveY = 0;
        const OsWindowGeom g = decidePersistedGeometry(s);
        assert(g.w == 1200 && g.h == 800);
        assert(g.maximized && g.fullscreenMode == 0);
    }
    {
        // A kiosk --fullscreen must NOT rewrite the profile as fullscreen: the
        // user's next plain launch would come up fullscreen for no reason.
        SaveInputs s = fullscreenSession();
        s.cliForcedFullscreen = true;
        const OsWindowGeom g = decidePersistedGeometry(s);
        assert(g.fullscreenMode == 0 &&
               "a CLI-forced fullscreen is not a user intent to persist");
        assert(g.w == 1200 && g.h == 800);
    }
    {
        // A macOS native space is ALWAYS something the user asked for with the
        // green button, so the kiosk flag cannot suppress mode 2 — it only ever
        // drives POM1's own fullscreen.
        SaveInputs s = fullscreenSession();
        s.nativeFullscreen    = true;
        s.cliForcedFullscreen = true;
        const OsWindowGeom g = decidePersistedGeometry(s);
        assert(g.fullscreenMode == 2);
    }
    {
        // GLFW_MAXIMIZED inside a native space reports the UNDERLYING window's
        // zoomed state. Persisting it would restore a maximized window nobody
        // asked for after leaving the space.
        SaveInputs s = fullscreenSession();
        s.nativeFullscreen = true;
        s.maximizedAttrib  = true;
        const OsWindowGeom g = decidePersistedGeometry(s);
        assert(!g.maximized && g.fullscreenMode == 2);
        assert(g.w == 1200 && g.h == 800);
    }

    // -----------------------------------------------------------------
    // §3 Which geometry applies on restore.
    // -----------------------------------------------------------------
    {
        // No sidecar, windowed: leave the OS alone.
        const RestorePlan p = planLayoutRestore(false, OsWindowGeom{}, false);
        assert(p.action == RestoreAction::Nothing);
        assert(!p.armExpand && !p.applySize && !p.adoptWindowedRect);
    }
    {
        // No sidecar, fullscreen. The .ini DID load and was authored for a
        // windowed frame, so the screen window still needs the expand — the
        // profile otherwise falls between two stools (applyMachineConfig skips
        // it because a layout was loaded; the sized path never runs).
        const RestorePlan p = planLayoutRestore(false, OsWindowGeom{}, true);
        assert(p.action == RestoreAction::ExpandOnly);
        assert(p.armExpand);
        assert(!p.applySize && "the OS frame is not ours to resize in fullscreen");
    }
    {
        // Sidecar authored WINDOWED, session fullscreen: fullscreen is a
        // SESSION property — keep the frame, adopt the rect for later, and
        // expand the screen window over the display.
        OsWindowGeom g; g.w = 1000; g.h = 700; g.havePos = true;
        g.fullscreenMode = 0;
        const RestorePlan p = planLayoutRestore(true, g, true);
        assert(p.action == RestoreAction::KeepFullscreenFrame);
        assert(p.adoptWindowedRect && p.armExpand);
        assert(!p.applySize && !p.applyPos);
    }
    {
        // Sidecar authored IN fullscreen, session fullscreen: its arrangement
        // is already fullscreen-sized, so do NOT expand over it.
        OsWindowGeom g; g.w = 1000; g.h = 700; g.fullscreenMode = 1;
        const RestorePlan p = planLayoutRestore(true, g, true);
        assert(p.action == RestoreAction::KeepFullscreenFrame);
        assert(p.adoptWindowedRect);
        assert(!p.armExpand);
        // Same for a profile saved in a macOS space.
        g.fullscreenMode = 2;
        assert(!planLayoutRestore(true, g, true).armExpand);
    }
    {
        // Mode 1 into a windowed session: restore straight into POM1's own
        // fullscreen, keeping the windowed rect for the way out.
        OsWindowGeom g; g.w = 1000; g.h = 700; g.havePos = true;
        g.fullscreenMode = 1;
        const RestorePlan p = planLayoutRestore(true, g, false);
        assert(p.action == RestoreAction::EnterOwnFullscreen);
        assert(p.adoptWindowedRect);
    }
    {
        // Mode 2 into a windowed session is NOT re-entered — that space belongs
        // to the green button. The recorded windowed rect comes back instead.
        OsWindowGeom g; g.w = 1000; g.h = 700; g.havePos = true;
        g.fullscreenMode = 2;
        const RestorePlan p = planLayoutRestore(true, g, false);
        assert(p.action == RestoreAction::ApplyWindowedRect);
        assert(p.applySize && p.applyPos);
        assert(!p.maximize && p.unmaximize);
    }
    {
        // Plain windowed restore, maximized, and a legacy two-field sidecar
        // (no position) — the OS keeps placing the window in that last case.
        OsWindowGeom g; g.w = 1000; g.h = 700; g.havePos = true; g.maximized = true;
        RestorePlan p = planLayoutRestore(true, g, false);
        assert(p.action == RestoreAction::ApplyWindowedRect);
        assert(p.maximize && !p.unmaximize);

        g.maximized = false; g.havePos = false;
        p = planLayoutRestore(true, g, false);
        assert(p.applySize && !p.applyPos);
    }

    // -----------------------------------------------------------------
    // §4 Save and restore agree, through the REAL sidecar.
    //
    // The two pure modules meet here: decidePersistedGeometry writes what
    // wingeom serialises, and planLayoutRestore reads back the same intent.
    // A round trip is the only way to catch a field that is decided correctly
    // and then dropped by the format (fullscreenMode is exactly that shape —
    // it arrived after the legacy two-field form).
    // -----------------------------------------------------------------
    {
        SaveInputs s = fullscreenSession();
        s.nativeFullscreen = true;

        std::ostringstream out;
        assert(pom1::wingeom::writeSizeSidecar(out, decidePersistedGeometry(s)));

        std::istringstream in(out.str());
        OsWindowGeom back;
        assert(pom1::wingeom::parseSizeSidecar(in, back));
        assert(back.w == 1200 && back.h == 800 && back.x == 120 && back.y == 90);
        assert(back.fullscreenMode == 2);

        // Restored into a plain windowed session: the space is not re-entered.
        const RestorePlan p = planLayoutRestore(true, back, false);
        assert(p.action == RestoreAction::ApplyWindowedRect);
        assert(p.applySize && p.applyPos);
    }

    // -----------------------------------------------------------------
    // §5 Reset window layout.
    // -----------------------------------------------------------------
    {
        // A curated ini_defaults/ seed was already force-applied by
        // LoadIniSettingsFromDisk — forcing it again would fight it.
        const ResetPlan p = planLayoutReset(true, false);
        assert(p.forceFrames == 0);
        assert(!p.resizeOsWindow && !p.armExpand && !p.dropScreenPlacement);
        assert(planLayoutReset(true, true).forceFrames == 0);
    }
    {
        // Windowed, no seed: rebuild from the table, force it, resize the frame.
        const ResetPlan p = planLayoutReset(false, false);
        assert(p.forceFrames == 2);
        assert(p.resizeOsWindow);
        assert(!p.armExpand && !p.dropScreenPlacement);
    }
    {
        // Fullscreen, no seed: the table's coordinates are windowed-frame ones,
        // so the screen window is expanded instead — and its placement MUST be
        // dropped, because the force runs after the expand's SetNextWindowSize
        // and would stamp the windowed rect back over it.
        const ResetPlan p = planLayoutReset(false, true);
        assert(p.forceFrames == 2);
        assert(p.armExpand && p.dropScreenPlacement);
        assert(!p.resizeOsWindow &&
               "the OS frame is not ours to resize inside a fullscreen space");
    }

    // -----------------------------------------------------------------
    // §6 Applying a pending placement.
    // -----------------------------------------------------------------
    {
        assert(!placementApply(false, false).apply);
        assert(!placementApply(false, true).apply);

        // Normal switch: apply once with FirstUseEver and consume, so the
        // user's own drags win for the rest of the session.
        const PlacementApply once = placementApply(true, false);
        assert(once.apply && !once.always && once.consume);

        // Reset: Always, and KEEP the entry so it re-applies until the force
        // drains. Consuming it here would apply the factory geometry to a
        // single frame of a window that has not been created yet.
        const PlacementApply forced = placementApply(true, true);
        assert(forced.apply && forced.always && !forced.consume);
    }

    // -----------------------------------------------------------------
    // §7 The Fullscreen checkbox, and AppKit's animated exit.
    // -----------------------------------------------------------------
    {
        // No latch: the box shows fullscreen by ANY route, so the green button
        // and the checkbox agree.
        CheckboxState s = fullscreenCheckboxState(false, false, 10.0, -1.0);
        assert(!s.showChecked && s.exitRequestedAt < 0.0);
        s = fullscreenCheckboxState(true, false, 10.0, -1.0);
        assert(s.showChecked);
        s = fullscreenCheckboxState(true, true, 10.0, -1.0);
        assert(s.showChecked && "a native space reads as fullscreen too");
    }
    {
        // Mid-exit: AppKit keeps the style mask set for the whole ~0.5 s
        // animation, so the raw answer is still "fullscreen". Showing that
        // snaps the box back to ticked and reads as "my click did nothing".
        const CheckboxState s = fullscreenCheckboxState(true, true, 10.2, 10.0);
        assert(!s.showChecked);
        assert(s.exitRequestedAt == 10.0 && "the latch is still in flight");
    }
    {
        // AppKit has left the space — retire the latch the frame we observe it.
        const CheckboxState s = fullscreenCheckboxState(false, false, 10.3, 10.0);
        assert(s.exitRequestedAt < 0.0);
        assert(!s.showChecked);
    }
    {
        // Never observed it leave: the timeout retires the latch anyway, or the
        // checkbox would be stuck unticked over a fullscreen window forever.
        const CheckboxState s =
            fullscreenCheckboxState(true, true, 10.0 + kMacNativeExitTimeoutSeconds + 0.01, 10.0);
        assert(s.exitRequestedAt < 0.0);
        assert(s.showChecked && "back to reporting the truth");
    }
    {
        // A click while a native transition runs is SWALLOWED: handing AppKit a
        // second toggleFullScreen: mid-animation puts the window right back in.
        const TogglePlan p = planFullscreenToggle(true, true, 10.2, 10.0);
        assert(p.action == ToggleAction::Ignore);
        assert(p.exitRequestedAt == 10.0 && "the in-flight latch survives");
        assert(!p.dropCliForced);
    }
    {
        // Inside a space: hand the exit to AppKit, latch it, and let go of a
        // kiosk force — render() would otherwise re-assert fullscreen the
        // moment the space finishes closing.
        const TogglePlan p = planFullscreenToggle(false, true, 42.0, -1.0);
        assert(p.action == ToggleAction::HandBackToAppKit);
        assert(!p.targetFullscreen);
        assert(p.dropCliForced);
        assert(p.exitRequestedAt == 42.0);
    }
    {
        // Entering POM1's own fullscreen. Nothing to release.
        const TogglePlan on = planFullscreenToggle(true, false, 5.0, -1.0);
        assert(on.action == ToggleAction::ApplyOwnFullscreen);
        assert(on.targetFullscreen && !on.dropCliForced);
        assert(on.exitRequestedAt < 0.0);

        // Unticking is the escape hatch out of a --fullscreen kiosk.
        const TogglePlan off = planFullscreenToggle(false, false, 5.0, -1.0);
        assert(off.action == ToggleAction::ApplyOwnFullscreen);
        assert(!off.targetFullscreen && off.dropCliForced);
    }

    // -----------------------------------------------------------------
    // §8 The OS window size a preset asks for.
    // -----------------------------------------------------------------
    {
        // No declared layout: the Apple 1 screen window plus chrome.
        OsWindowSizeInputs in;
        in.screenW = 600.0f; in.screenH = 400.0f;
        in.verticalChrome = 80.0f; in.extraW = 16;
        const OsWindowSize s = computeOsWindowSize(in);
        assert(s.w == 616 && s.h == 480);
    }
    {
        // A declared layout wider than the screen window grows the frame, pads
        // included; a smaller one never shrinks it.
        OsWindowSizeInputs in;
        in.screenW = 600.0f; in.screenH = 400.0f;
        in.verticalChrome = 80.0f; in.extraW = 16;
        in.rightPad = 10.0f; in.bottomPad = 20.0f;
        in.extentW = 900.0f; in.extentH = 300.0f;
        const OsWindowSize s = computeOsWindowSize(in);
        assert(s.w == 910);
        assert(s.h == 480 && "a short layout does not shrink the frame");
    }
    {
        // The POM1 Fantasy floor: switching to a bare profile must not shrink
        // the desktop window under the canonical frame.
        OsWindowSizeInputs in;
        in.screenW = 600.0f; in.screenH = 400.0f;
        in.verticalChrome = 80.0f;
        in.extentW = 700.0f; in.extentH = 300.0f;
        in.floorExtentW = 1400.0f; in.floorExtentH = 900.0f;
        const OsWindowSize s = computeOsWindowSize(in);
        assert(s.w == 1400 && s.h == 900);
    }
    {
        // WASM passes NO floor on purpose: there the canvas is the whole app
        // surface, so a bare Apple-1 profile must give a small canvas. The
        // clamps are what keep it usable and safe.
        OsWindowSizeInputs in;
        in.screenW = 600.0f; in.screenH = 400.0f;
        in.verticalChrome = 80.0f;
        in.extentW = 700.0f; in.extentH = 300.0f;
        in.minW = 320; in.minH = 240; in.maxW = 4096; in.maxH = 4096;
        const OsWindowSize s = computeOsWindowSize(in);
        assert(s.w == 700 && "no Fantasy floor in the browser");
        assert(s.h == 480);

        // Floor and ceiling both bite.
        OsWindowSizeInputs tiny;
        tiny.screenW = 10.0f; tiny.screenH = 10.0f;
        tiny.minW = 320; tiny.minH = 240; tiny.maxW = 4096; tiny.maxH = 4096;
        const OsWindowSize t = computeOsWindowSize(tiny);
        assert(t.w == 320 && t.h == 240);

        OsWindowSizeInputs huge;
        huge.screenW = 99999.0f; huge.screenH = 99999.0f;
        huge.minW = 320; huge.minH = 240; huge.maxW = 4096; huge.maxH = 4096;
        const OsWindowSize h = computeOsWindowSize(huge);
        assert(h.w == 4096 && h.h == 4096);
    }

    std::printf("layout_decisions_smoke: OK\n");
    return 0;
}
