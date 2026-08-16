// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MacNativeFullscreen.mm — Cocoa half of MacNativeFullscreen.h. Compiled as
// Objective-C++ on macOS only (see CMakeLists.txt); the header supplies inline
// no-op fallbacks everywhere else. No object is allocated here, so the TU is
// ARC-agnostic (it compiles under the project default, no -fobjc-arc needed).

#include "MacNativeFullscreen.h"
#include "POM1Build.h"

#if !POM1_IS_WASM && defined(__APPLE__)

#import <Cocoa/Cocoa.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace pom1 {

bool macWindowIsNativeFullscreen(GLFWwindow* window)
{
    if (!window) return false;
    NSWindow* ns = glfwGetCocoaWindow(window);
    if (!ns) return false;
    // NSWindowStyleMaskFullScreen is set by AppKit for the whole duration of
    // the fullscreen space (including the animation), which is exactly the
    // window we must not resize.
    return ([ns styleMask] & NSWindowStyleMaskFullScreen) != 0;
}

void macWindowToggleNativeFullscreen(GLFWwindow* window)
{
    if (!window) return;
    NSWindow* ns = glfwGetCocoaWindow(window);
    // Only a window whose collectionBehavior advertises fullscreen-primary can
    // enter a space; GLFW sets that for every resizable window it creates, so
    // this is a plain toggle in practice.
    if (ns) [ns toggleFullScreen:nil];
}

} // namespace pom1

#endif // !POM1_IS_WASM && __APPLE__
