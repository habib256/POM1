// X11ErrorGuard.cpp -- see X11ErrorGuard.h. All Xlib inclusion is isolated here.
//
// POM1_HAS_X11 is defined by CMake only when find_package(X11) succeeded on a
// Linux (non-WASM) build, so this file compiles to a no-op stub everywhere else
// (macOS, Windows, WASM, or a Linux box without X11 dev headers).

#include "X11ErrorGuard.h"

#if defined(POM1_HAS_X11)

#include <X11/Xlib.h>
#include <cstdio>

// Native GLFW access (glfwGetX11Display / glfwGetX11Window). glfw3native.h pulls
// in Xlib itself under GLFW_EXPOSE_NATIVE_X11 — already included above.
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>

namespace {

// Non-fatal replacement for Xlib's default error handler (which exits). Logs the
// decoded error and returns 0 so the protocol error is swallowed. The classic
// offender is a clipboard SelectionRequest / X_ChangeProperty against a requestor
// window that vanished — harmless to ignore.
int pom1X11ErrorHandler(Display* dpy, XErrorEvent* err)
{
    char text[256] = {0};
    if (dpy) XGetErrorText(dpy, err->error_code, text, sizeof(text) - 1);
    std::fprintf(stderr,
        "[X11] non-fatal Xlib error swallowed: %s (error %u, request %u.%u, "
        "resource 0x%lx, serial %lu)\n",
        text, static_cast<unsigned>(err->error_code),
        static_cast<unsigned>(err->request_code),
        static_cast<unsigned>(err->minor_code),
        err->resourceid, err->serial);
    return 0;   // do NOT exit
}

} // namespace

void pom1InstallX11ErrorGuard()
{
    XSetErrorHandler(pom1X11ErrorHandler);
}

bool pom1ShowGlfwWindowX11(GLFWwindow* window)
{
    if (!window)
        return false;
    // glfwGetX11Window() returns None (0) when the window is not an X11 window
    // (e.g. a Wayland session on an X11-capable GLFW build) — signal the caller
    // to fall back to glfwShowWindow() in that case.
    Display* dpy = glfwGetX11Display();
    Window   xw  = glfwGetX11Window(window);
    if (!dpy || xw == 0)
        return false;
    XMapWindow(dpy, xw);
    XFlush(dpy);
    return true;
}

#else  // !POM1_HAS_X11

void pom1InstallX11ErrorGuard() {}
bool pom1ShowGlfwWindowX11(struct GLFWwindow*) { return false; }

#endif
