// X11ErrorGuard.h -- make Xlib errors non-fatal on Linux/X11.
//
// By default Xlib's error handler prints the error and calls exit(3) — so a
// single BadWindow (classically GLFW's X11 clipboard SelectionRequest racing a
// requestor whose window has already gone away, X_ChangeProperty on a stale
// window) takes the whole app down. GLFW's own error callback only sees GLFW
// errors, not raw Xlib protocol errors, so it can't catch these.
//
// pom1InstallX11ErrorGuard() installs a handler that logs and RETURNS (non-fatal),
// so the emulator survives clipboard/property races. It is a no-op on every
// non-X11 platform (macOS, Windows, Wayland-only builds, WASM) and when X11 dev
// headers/libs were unavailable at build time. Call it once, right after
// glfwInit(). All Xlib inclusion is confined to X11ErrorGuard.cpp so Xlib's macro
// soup (Bool / None / Status / …) never leaks into the rest of the codebase.

#ifndef POM1_X11_ERROR_GUARD_H
#define POM1_X11_ERROR_GUARD_H

// Install a non-fatal Xlib error handler (Linux/X11 only; no-op elsewhere).
void pom1InstallX11ErrorGuard();

// Map POM1's GLFW window onto the screen via raw Xlib (XMapWindow), bypassing
// glfwShowWindow(). GLFW 3.3's X11 backend blocks in waitForVisibilityNotify()
// whenever it maps a window (both inside glfwCreateWindow when GLFW_VISIBLE is
// set, and inside glfwShowWindow) — on some X servers / window managers the
// expected VisibilityNotify event never arrives and GLFW spins forever, so the
// window is created but never rendered. Upstream dropped that wait in 3.4.
//
// The workaround: create the window with GLFW_VISIBLE=FALSE (so GLFW never runs
// the wait), then map it here directly. Returns true if the window was mapped as
// an X11 window; false if it is not X11 (e.g. a Wayland GLFW build/session) so
// the caller can fall back to glfwShowWindow(). No-op stub returning false on
// every non-X11 platform. All GLFW-native/Xlib inclusion stays in the .cpp.
struct GLFWwindow;
bool pom1ShowGlfwWindowX11(struct GLFWwindow* window);

#endif // POM1_X11_ERROR_GUARD_H
