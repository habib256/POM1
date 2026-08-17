// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// FullscreenExpand.h — when to re-expand the Apple 1 Screen window over a
// display that has just become fullscreen.
//
// Extracted from MainWindow_ImGui::render() so the timing rule is testable
// without a window: feed it a sequence of display sizes, assert when it fires.
// No ImGui, no GLFW.
//
// THE RULE, and why it is not a plain delay. A fixed frame count works only for
// a synchronous transition (glfwSetWindowMonitor resizes before the next
// frame). It is wrong on macOS: AppKit sets NSWindowStyleMaskFullScreen at the
// START of its ~0.5 s animated transition, so "are we fullscreen?" flips some
// thirty frames before the framebuffer reaches its final size. Expanding two
// frames in sizes the screen window to the pre-animation frame, and it stays
// undersized for the rest of the session — the very bug the feature existed to
// fix. So the expand waits for the display size to STOP MOVING, which covers
// the animated and synchronous paths with one rule.

#ifndef POM1_FULLSCREEN_EXPAND_H
#define POM1_FULLSCREEN_EXPAND_H

namespace pom1 {

class FullscreenExpandSettler
{
public:
    /// Frames the display size must hold still before the expand fires.
    static constexpr int kSettleFrames = 2;

    /// Request an expand, seeding the settle baseline with the size visible
    /// right now. Safe to call while one is already pending (it restarts).
    void arm(float displayW, float displayH);

    /// Drop a pending expand — the window left fullscreen before it fired.
    void cancel();

    bool pending() const { return frames_ > 0; }

    /// Advance one frame. Returns true EXACTLY ONCE, on the frame the caller
    /// must apply the expand; the size to expand to is the one passed in. A
    /// no-op returning false when nothing is pending.
    bool step(float displayW, float displayH);

private:
    int   frames_ = 0;
    float lastW_  = 0.0f;
    float lastH_  = 0.0f;
};

} // namespace pom1

#endif // POM1_FULLSCREEN_EXPAND_H
