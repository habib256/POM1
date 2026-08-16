// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MacNativeFullscreen.h — query/toggle macOS' NATIVE fullscreen space (the
// green title-bar button, ⌃⌘F, or View → Enter Full Screen).
//
// GLFW models exactly one kind of fullscreen: its own glfwSetWindowMonitor()
// borderless mode, which POM1 drives from Display Settings → "Fullscreen".
// AppKit's fullscreen SPACE is invisible to it — glfwGetWindowMonitor() still
// returns nullptr and GLFW_MAXIMIZED stays false. Without this seam every
// geometry decision in MainWindow (windowed-rect tracking, the per-preset OS
// frame restore, the Apple 1 Screen auto-expand) mistakes a natively
// fullscreen window for a plain windowed one and tries to resize it — a
// setFrame: AppKit silently ignores while in a fullscreen space.
//
// Off macOS (and under Emscripten) the query is a compile-time `false` and the
// toggle a no-op, so callers need no #if of their own.

#pragma once

struct GLFWwindow;

namespace pom1 {

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)

/** True when `window`'s NSWindow currently occupies a native fullscreen space. */
bool macWindowIsNativeFullscreen(GLFWwindow* window);

/** Enter/leave the native fullscreen space (AppKit animates the transition). */
void macWindowToggleNativeFullscreen(GLFWwindow* window);

#else

inline bool macWindowIsNativeFullscreen(GLFWwindow*) { return false; }
inline void macWindowToggleNativeFullscreen(GLFWwindow*) {}

#endif

} // namespace pom1
