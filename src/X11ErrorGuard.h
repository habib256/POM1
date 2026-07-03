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

#endif // POM1_X11_ERROR_GUARD_H
