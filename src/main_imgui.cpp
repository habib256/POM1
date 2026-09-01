#include <iostream>
// std::string / std::vector are used by the window-creation diagnostic below,
// which is compiled on every platform — the <string>/<vector> includes further
// down sit inside the non-WASM branch, so name them here rather than relying on
// a transitive include holding for the WASM build too.
#include <string>
#include <vector>
#include <mutex>
#include <GLFW/glfw3.h>
#include "POM1Build.h"
#include "PomVersion.h"   // POM1_VERSION_STRING (generated from VERSION)
#include "imgui.h"
#include "imgui_impl_glfw.h"
// imgui_impl_opengl3.h is no longer included here directly — the rendering
// backend is wired through PomRenderer (src/PomRenderer.h). PomRenderer_GL
// owns the ImGui_ImplOpenGL3_* lifecycle; PomRenderer_Metal (Phase 2) will
// pull in imgui_impl_metal instead.
#include "PomRenderer.h"
#include "CliDispatcher.h"
#include "X11ErrorGuard.h"
#include "NativeFileDialog.h"
#include "MainWindow_ImGui.h"
#include "ResourceLocator.h"
#include "MachinePresets.h"
#include "IconsFontAwesome6.h"
#include "Logger.h"
#include "third_party/stb/stb_image.h"

#if !POM1_IS_WASM
// Telnet-triggered screenshot path: ESC S in TerminalCard arms a flag, the
// render loop captures the back-buffer with glReadPixels and emits the PNG
// via stb_image_write so an LLM piloting POM1 over telnet can read all
// rendered screens (Apple 1 text, GraphicsCard, TMS9918, GT6144, dialogs).
#include "TerminalCard.h"
#include "GraphicsCard.h"
#endif

// stb_image_write implementation — compiled on ALL platforms (this TU is the
// single impl site). The desktop screenshot path above is one user; the HGR/TMS
// paint + sprite editors' savePng (Pom1HgrPaintHost / Pom1TmsPaintHost) call
// stbi_write_png unconditionally, so WASM needs the impl linked too — without
// this outside the !WASM guard the WASM link fails with undefined stbi_write_png.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
// stb initialise ses stbi__write_context par `= { 0 }` : 24 hits
// -Wmissing-field-initializers dans du code vendorisé, qu'un simple
// drop-in de la prochaine version réintroduirait.
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "third_party/stb/stb_image_write.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#if POM1_IS_WASM
#include <emscripten.h>
#include <emscripten/html5.h>

// WASM browser-paste bridge. Emscripten's GLFW clipboard can't read text copied
// from outside the tab, so shell.html installs a native 'paste' DOM listener
// that ccall's this with the pasted text; we feed it through the Apple-1 keyboard
// FIFO (same path as desktop Ctrl+V). Target is set in main() once the window
// exists. Mirrors g_wasmAudioDevice / pom1_fillAudioBuffer in AudioDevice.cpp.
static MainWindow_ImGui* g_wasmPasteTarget = nullptr;
extern "C" {
EMSCRIPTEN_KEEPALIVE
void pom1_paste_text(const char* s)
{
    if (g_wasmPasteTarget && s) g_wasmPasteTarget->pasteText(s);
}
// Called by shell.html on pagehide/visibilitychange:hidden — flush the active
// preset's layout + global UI settings to the IDBFS-backed ini/ before the
// tab goes away (the Emscripten main loop never returns, so the desktop
// shutdown save can't run here).
EMSCRIPTEN_KEEPALIVE
void pom1_save_layout_now()
{
    if (g_wasmPasteTarget) g_wasmPasteTarget->saveActivePresetLayoutNow();
}

// Read-only browser-smoke contract. Low bits describe independently useful
// machine invariants; the current PC occupies the high 16 bits. This proves
// that Chromium reached a configured, running emulator core rather than only
// a healthy WebGL/ImGui shell.
EMSCRIPTEN_KEEPALIVE
uint32_t pom1_wasm_machine_probe()
{
    if (!g_wasmPasteTarget) return 0;
    uint32_t flags = 0x01u; // MainWindow exists
    if (g_wasmPasteTarget->getActivePresetIndex() >= 0) flags |= 0x02u;
    EmulationController* emu = g_wasmPasteTarget->getEmulationController();
    if (!emu) return flags;
    EmulationSnapshot snapshot;
    emu->copySnapshot(snapshot);
    if (snapshot.cpuRunning) flags |= 0x04u;
    if (snapshot.ramSizeKB > 0) flags |= 0x08u;
    if (snapshot.memory.size() == 0x10000 && snapshot.memory[0xFF00] != 0)
        flags |= 0x10u;
    return (static_cast<uint32_t>(snapshot.programCounter) << 16) | flags;
}

EMSCRIPTEN_KEEPALIVE
double pom1_wasm_measured_cpu_hz()
{
    if (!g_wasmPasteTarget) return 0.0;
    EmulationController* emu = g_wasmPasteTarget->getEmulationController();
    return emu ? emu->getMeasuredCpuHz() : 0.0;
}
}
#else
#include <atomic>
#include <chrono>
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

#if !POM1_IS_WASM
// SIGINT/SIGTERM hand-off so --save-tape can fire on `kill <pid>` (the
// telnet test scripts need this — they have no way to close the GLFW
// window remotely). The handler asks the main loop to exit cleanly so
// ~MainWindow_ImGui runs and the saveTape() path is reached.
static std::atomic<GLFWwindow*> g_signalWindow{nullptr};
static void pom1_signal_handler(int)
{
    // Loaded atomically and cleared to nullptr before glfwDestroyWindow() so a
    // signal arriving during teardown can never touch a freed window pointer.
    if (GLFWwindow* w = g_signalWindow.load(std::memory_order_acquire))
        glfwSetWindowShouldClose(w, 1);
}
#endif

#if !POM1_IS_WASM
#if !defined(__APPLE__)
/// Probe for pic/icon.png under the usual cwd-relative + exe-relative spots
/// so the GLFW window icon works regardless of where the binary is launched
/// from (build/, repo root, packaged Windows release …). macOS takes its
/// icon from the .app bundle, not GLFW — the helper is compiled out there.
static std::string find_app_icon_path()
{
    // Working directory, its ancestors, then the executable and the packaged
    // layouts around it — ResourceLocator.h owns that order for every POM1
    // resource. This function used to carry its own cwd walk plus a Win32-only
    // GetModuleFileNameA copy of the exe-relative half.
    return pom1::ResourceLocator::defaultLocator().find("pic/icon.png").string();
}
#endif  // !defined(__APPLE__)

/// Cherche un fichier de police `kFile` (fa-solid-900.ttf, DejaVuSans.ttf, …)
/// dans l'ordre de recherche unique de POM1 (`ResourceLocator.h`) : répertoire
/// de travail et ancêtres, puis l'exécutable et les dispositions que les
/// empaqueteurs mettent autour. Renvoie {} si introuvable.
static std::string find_font_path(const char* kFile)
{
    // Same single search order as every other POM1 resource. The old list also
    // named "build/fonts" for a run from the repo root against a build tree;
    // the locator's executable-derived roots cover that case and the Win32
    // next-to-the-exe one it used to spell out by hand.
    return pom1::ResourceLocator::defaultLocator()
        .find(std::string("fonts/") + kFile).string();
}

/// sans fa-solid-900.ttf, ImGui affiche « ? » à la place des icônes Font Awesome.
static std::string find_fa_solid_font_path() { return find_font_path("fa-solid-900.ttf"); }

/// Read the entire back-buffer through the renderer and write a top-down PNG
/// at `screenshots/pom1_latest.png`. Called from the render loop *after*
/// renderer->renderDrawData() and *before* renderer->present() so the
/// framebuffer holds the fully-rendered frame (every visible window, not
/// just the active graphics card). Posts the absolute path back to
/// TerminalCard so the telnet client gets the resolved location.
///
/// The GL backend reads via glReadPixels + Y-flip (bottom-up source). The
/// Metal backend (Phase 2) routes through a staging MTLTexture blit and
/// returns the same top-down RGBA8 layout, so this function is renderer-
/// agnostic.
static void capture_screenshot_to_png(TerminalCard& card)
{
    namespace fs = std::filesystem;
    const char* relPath = "screenshots/pom1_latest.png";

    auto* r = pom1::renderer();
    if (!r) {
        card.setScreenshotResult("renderer unavailable", false);
        return;
    }

    int fbW = 0, fbH = 0;
    std::vector<uint8_t> buf;
    if (!r->readBackbufferRGBA(fbW, fbH, buf)) {
        card.setScreenshotResult("framebuffer read failed", false);
        return;
    }

    std::error_code ec;
    fs::create_directories("screenshots", ec);
    // Don't bail on EEXIST; only bail if the directory genuinely cannot be
    // ensured. create_directories returns false-without-error when the dir
    // already exists, which is fine.
    if (ec) {
        card.setScreenshotResult(std::string("mkdir failed: ") + ec.message(), false);
        return;
    }

    const size_t rowBytes = static_cast<size_t>(fbW) * 4;
    const int rc = stbi_write_png(relPath, fbW, fbH, 4, buf.data(),
                                  static_cast<int>(rowBytes));
    if (rc == 0) {
        card.setScreenshotResult("stbi_write_png failed (check cwd write permissions)", false);
        return;
    }

    fs::path absPath = fs::absolute(relPath, ec);
    card.setScreenshotResult(ec ? std::string(relPath) : absPath.string(), true);
}
#endif

// Last error GLFW reported, latched. GLFW hands the *reason* for a failure to
// this callback and returns only NULL from the failing call, so without keeping
// it here the window-creation diagnostic below would have nothing to say.
static int         g_lastGlfwErrorCode = 0;
static std::string g_lastGlfwErrorDesc;

static void glfw_error_callback(int error, const char* description)
{
    g_lastGlfwErrorCode = error;
    g_lastGlfwErrorDesc = description ? description : "";
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// Report a failed glfwCreateWindow() in terms the user can act on.
//
// This is the most opaque failure POM1 has, and issue #34 is why it gets 40
// lines: the user saw only the callback's one-liner — "WGL: The driver does not
// appear to support OpenGL" — and that line names the WRONG cause. GLFW emits it
// from choosePixelFormat() whenever the enumeration yields no non-generic pixel
// format, which is exactly what a *failed ICD load* looks like from outside. The
// reporter reasonably concluded his graphics driver was at fault and spent a
// month reinstalling NVIDIA drivers that were never the problem; the actual
// cause was an app-local msvcp140.dll shipped next to POM1.exe that the ICD
// picked up instead of the system one. So: name the real causes, most likely
// first, and hand over the facts already in hand.
// stderr + (on Windows) a message box, so the report also reaches the user who
// double-clicked POM1.exe in Explorer and watched the console flash away. POM1
// is a console-subsystem exe, so the stderr half alone covers terminal launches.
static void pom1_emit_fatal_report(const std::string& msg, const wchar_t* caption)
{
    fputs("\n", stderr);
    fputs(msg.c_str(), stderr);
    fflush(stderr);

#if defined(_WIN32)
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, nullptr, 0);
    if (wlen > 0) {
        std::vector<wchar_t> wide(static_cast<size_t>(wlen));
        if (MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, wide.data(), wlen) > 0)
            MessageBoxW(nullptr, wide.data(), caption, MB_OK | MB_ICONERROR);
    }
#else
    (void)caption;
#endif
}

// glfwInit() failing gets the same treatment as window creation below: it used
// to be a bare `return -1`, and on Linux it is the failure a headless or
// SSH-without-X user actually hits first.
static void pom1_report_glfw_init_failure()
{
    std::string msg = "POM1 could not initialise GLFW (no window system).\n\n";
    if (g_lastGlfwErrorCode != 0) {
        msg += "Last GLFW error: " + std::to_string(g_lastGlfwErrorCode) + " - "
             + g_lastGlfwErrorDesc + "\n";
    }
    msg += "\nMost likely causes, in order:\n";
#if defined(_WIN32)
    msg +=
      "  1. Running in a session with no desktop (a service, or a container).\n"
      "  2. The window station / desktop is not reachable for this user.\n";
#elif defined(__APPLE__)
    msg +=
      "  1. Running with no window server access (ssh, or a launch daemon).\n"
      "     POM1 needs a normal user session.\n";
#else
    msg +=
      "  1. No display: DISPLAY / WAYLAND_DISPLAY unset, or ssh without `-X`.\n"
      "  2. A headless machine. For scripted runs use the flags that exit before\n"
      "     any window opens, e.g. --list-presets, or run under xvfb-run.\n";
#endif
    msg += "\nPOM1 needs no window for --list-presets and the other query flags.\n";
    pom1_emit_fatal_report(msg, L"POM1 - cannot start");
}

static void pom1_report_window_creation_failure(const char* requestDesc)
{
    std::string msg = "POM1 could not open its window.\n\n";

    msg += "What POM1 asked the driver for: ";
    msg += requestDesc;
    msg += "\n";
    if (g_lastGlfwErrorCode != 0) {
        msg += "Last GLFW error: " + std::to_string(g_lastGlfwErrorCode) + " - "
             + g_lastGlfwErrorDesc + "\n";
    } else {
        msg += "Last GLFW error: (none reported)\n";
    }
    if (const char* ver = glfwGetVersionString()) {
        msg += std::string("GLFW build: ") + ver + "\n";
    }

    msg += "\nMost likely causes, in order:\n";
#if defined(_WIN32)
    msg +=
      "  1. A DLL sitting next to POM1.exe is shadowing the C runtime that your\n"
      "     GPU's OpenGL driver needs. Windows searches the application folder\n"
      "     BEFORE System32, so a stray msvcp140.dll / vcruntime140.dll there is\n"
      "     handed to the driver and the version mix stops it from loading. POM1\n"
      "     ships as one self-contained exe with no DLL: if you see any .dll next\n"
      "     to POM1.exe, delete it. (This was issue #34 — and note the GLFW error\n"
      "     above blames the driver, which in this case is a red herring.)\n"
      "  2. No vendor OpenGL driver present: a fresh Windows install still on the\n"
      "     Microsoft Basic Display Adapter, or a Remote Desktop / VM session with\n"
      "     no GPU passthrough (RDP caps OpenGL at 1.1). Install the GPU vendor's\n"
      "     driver, or run on the physical console.\n"
      "  3. GPU or driver older than OpenGL 3.2 (2009).\n";
#elif defined(__APPLE__)
    msg +=
      "  1. The Mac reports no OpenGL 3.2 core profile - every Mac since OS X\n"
      "     10.7 does, so this usually means a virtualised or remote session.\n"
      "  2. Running over a remote session with no window server access.\n";
#else
    msg +=
      "  1. No usable OpenGL driver: missing Mesa, or a headless session. Check\n"
      "     with `glxinfo | grep \"OpenGL version\"`.\n"
      "  2. No display reachable: DISPLAY / WAYLAND_DISPLAY unset, or SSH without\n"
      "     X forwarding.\n"
      "  3. GPU exposes OpenGL ES but not desktop GL 3.2 - Raspberry Pi 4/5 above\n"
      "     all. Rebuild the GLES tier: cmake -DPOM1_GLES=ON ..\n";
#endif

    pom1_emit_fatal_report(msg, L"POM1 - cannot open a window");
}

// Adaptive-UI throttle (P2-D): timestamp of the last user-input / window
// event. The main loop renders at full (vsync) rate for a grace period after
// any activity, then drops to a low idle rate when MainWindow reports nothing
// on screen needs animating. Updated from GLFW callbacks (which run inside
// glfwPollEvents — polled every loop tick even while idle, so a keypress
// wakes the renderer within one ~10 ms tick, not at the idle rate).
static std::atomic<double> g_lastActivityTime{0.0};
static void pom1_note_activity() { g_lastActivityTime.store(glfwGetTime(), std::memory_order_relaxed); }

// True only while NativeFileDialog's wait pump (below) is inside
// glfwPollEvents. That pump exists so a forked zenity/kdialog picker doesn't
// leave the compositor unanswered, but it necessarily runs from INSIDE an
// ImGui frame — the callers are menu handlers — which breaks the "callbacks
// run outside the frame" assumption the POM1 half of the key/char/drop
// handlers below is written against (handleGlfwKey dispatches shortcuts, and a
// shortcut re-entering applyMachineConfig/hardReset underneath a live Load
// Memory is not a state anything here is prepared for). ImGui's own handlers
// still run: they only queue events, which the next NewFrame consumes as
// usual. Dropping POM1-side input for the duration is also simply correct —
// the picker owns the focus, those keystrokes are not for the Apple-1.
// Render-thread only, hence a plain bool.
static bool g_inNativeDialogPump = false;

static void glfw_char_callback(GLFWwindow* window, unsigned int codepoint)
{
    pom1_note_activity();
    ImGui_ImplGlfw_CharCallback(window, codepoint);

    auto* mw = static_cast<MainWindow_ImGui*>(glfwGetWindowUserPointer(window));
    if (mw && !g_inNativeDialogPump) {
        mw->handleGlfwChar(codepoint);
    }
}

static void glfw_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    pom1_note_activity();
    ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);

    // PRESS + REPEAT : le handler n’exécute les raccourcis sur REPEAT que pour F7 (step).
    if ((action == GLFW_PRESS || action == GLFW_REPEAT) && !g_inNativeDialogPump) {
        auto* mw = static_cast<MainWindow_ImGui*>(glfwGetWindowUserPointer(window));
        if (mw) {
            mw->handleGlfwKey(key, scancode, action, mods);
        }
    }
}

// Mouse / window callbacks: same chain-to-ImGui pattern as key/char above.
// They exist solely to feed the activity timestamp; ImGui's backend handlers
// do the actual input plumbing. Desktop only — WASM keeps the browser's
// requestAnimationFrame pacing and never throttles.
#if !POM1_IS_WASM
static void glfw_mouse_button_callback(GLFWwindow* w, int button, int action, int mods)
{
    pom1_note_activity();
    ImGui_ImplGlfw_MouseButtonCallback(w, button, action, mods);
}
static void glfw_cursor_pos_callback(GLFWwindow* w, double x, double y)
{
    pom1_note_activity();
    ImGui_ImplGlfw_CursorPosCallback(w, x, y);
}
static void glfw_scroll_callback(GLFWwindow* w, double dx, double dy)
{
    pom1_note_activity();
    ImGui_ImplGlfw_ScrollCallback(w, dx, dy);
}
static void glfw_window_focus_callback(GLFWwindow* w, int focused)
{
    pom1_note_activity();
    ImGui_ImplGlfw_WindowFocusCallback(w, focused);
}
// Expose/resize damage: with the idle throttle skipping present(), the window
// must repaint promptly when the WM asks (uncomposited X11 shows stale pixels
// otherwise). No ImGui counterpart to chain for these two.
static void glfw_window_refresh_callback(GLFWwindow*) { pom1_note_activity(); }
static void glfw_window_size_callback(GLFWwindow*, int, int) { pom1_note_activity(); }
// Drag-and-drop. GLFW owns `paths` only for the duration of this call, and we
// are inside glfwPollEvents — outside the ImGui frame — so the strings are
// copied into the MainWindow queue and the actual load happens at the top of
// the next render(). ImGui's own drop handler is deliberately NOT chained: POM1
// has no ImGui drag-and-drop payload of its own, and the platform-IO one only
// matters for multi-viewport, which stays off (see CLAUDE.md).
static void glfw_drop_callback(GLFWwindow* w, int count, const char** paths)
{
    pom1_note_activity();
    auto* mw = static_cast<MainWindow_ImGui*>(glfwGetWindowUserPointer(w));
    if (mw && !g_inNativeDialogPump) mw->queueDroppedFiles(paths, count);
}
#endif // !POM1_IS_WASM

#if !POM1_IS_WASM && defined(__APPLE__)
/// Provision `~/Library/Application Support/POM1/` on first launch, refresh
/// it on every subsequent launch, and chdir there. This is the Apple-canonical
/// split for app data:
///
///   Bundle/Contents/Resources/{roms,fonts,software,sketchs,dev,pic,cassettes,sdcard,cfcard}
///       read-only bytes shipped with the app, signed + notarized-friendly.
///
///   ~/Library/Application Support/POM1/
///       {roms,fonts,software,pic,cassettes,sketchs}  → symlinks into the bundle
///       sdcard/, cfcard/, ini/               → real dirs, user-writable,
///                                              seeded from the bundle once
///
/// chdir'ing here lets every existing cwd-relative probe (Memory ROM loader,
/// font probe, preset ini save/load, File > Load Memory defaults, cassette
/// tape-info lookup, …) resolve correctly without any per-call-site changes.
///
/// Dev flow: `build/POM1.app` has no Contents/Resources/roms (data is in
/// `build/` thanks to run_emulator.sh). Falls back to the .app's parent dir,
/// same as the original helper.
static void pom1_macos_provision_user_data_dir()
{
    namespace fs = std::filesystem;
    std::error_code ec;

    // ---- Locate the bundle's Resources dir ---------------------------------
    char buf[PATH_MAX];
    uint32_t n = sizeof(buf);
    if (_NSGetExecutablePath(buf, &n) != 0) return;
    fs::path exe = fs::canonical(buf, ec);
    if (ec) return;
    // exe = <Bundle>/Contents/MacOS/POM1 → <Bundle>/Contents/Resources/
    fs::path resourcesDir = exe.parent_path().parent_path() / "Resources";

    // ---- Dev fallback: no Resources/roms → chdir to the .app's parent dir,
    //      where run_emulator.sh copies ROMs + fonts for `build/POM1` use.
    if (!fs::is_directory(resourcesDir / "roms", ec)) {
        fs::path bundleParent = exe.parent_path()  // Contents/MacOS
                                   .parent_path()  // Contents
                                   .parent_path()  // POM1.app
                                   .parent_path(); // ../
        if (fs::is_directory(bundleParent / "roms", ec) ||
            fs::is_directory(bundleParent / "fonts", ec)) {
            fs::current_path(bundleParent, ec);
        }
        return;
    }

    // ---- Compute ~/Library/Application Support/POM1/ -----------------------
    const char* home = std::getenv("HOME");
    if (!home || !*home) return;
    fs::path userDataDir = fs::path(home) / "Library"
                                          / "Application Support" / "POM1";
    fs::create_directories(userDataDir, ec);
    if (ec) return;

    // ---- Re-link every read-only dir to the current bundle's Resources -----
    // Translocation + /Applications moves give a fresh bundle path each run,
    // so the symlinks have to be refreshed on every launch. Detect staleness
    // by comparing read_symlink target to the expected one.
    static constexpr const char* kReadOnlyDirs[] = {
        "roms", "fonts", "software", "pic", "cassettes", "sketchs"
    };
    for (const char* name : kReadOnlyDirs) {
        fs::path link   = userDataDir / name;
        fs::path target = resourcesDir / name;
        if (!fs::is_directory(target, ec)) continue;  // missing in bundle; skip

        bool recreate = true;
        if (fs::is_symlink(link, ec)) {
            std::error_code e;
            if (fs::read_symlink(link, e) == target && !e) recreate = false;
        }
        if (recreate) {
            std::error_code e;
            fs::remove(link, e);
            fs::create_symlink(target, link, e);
        }
    }

    // ---- Seed writable dirs on first launch --------------------------------
    // Never overwrite existing user data — only copy when the destination dir
    // doesn't exist at all.
    static constexpr const char* kWritableDirs[] = { "sdcard", "cfcard", "disks" };
    for (const char* name : kWritableDirs) {
        fs::path dst = userDataDir / name;
        if (fs::exists(dst, ec)) continue;
        fs::path src = resourcesDir / name;
        if (!fs::is_directory(src, ec)) {
            fs::create_directories(dst, ec);
            continue;
        }
        std::error_code e;
        fs::copy(src, dst, fs::copy_options::recursive, e);
    }
    fs::create_directory(userDataDir / "ini", ec);

    // ---- Finally, chdir so every cwd-relative probe resolves here ----------
    fs::current_path(userDataDir, ec);
}
#endif

#if !POM1_IS_WASM
// Headless driver (--headless): run the emulator with no GLFW window / GL /
// ImGui — for CI and scripted runs (telemetry golden-trace, lock-step game
// tests) that drive POM1 over the telemetry socket with no display. Reuses
// EmulationController (its own emulation thread) + runDeferredActions; the
// machine is the default 64K Apple-1. Preset / card-layout flags are GUI-only
// (applyMachineConfig is ImGui-coupled) and are skipped here — full-preset
// headless is a follow-up. See doc/CLI.md and doc/TELEMETRY_SIDE_CHANNEL.md.
static std::atomic<bool> g_headlessStop{false};
static void pom1_headless_signal_handler(int) { g_headlessStop.store(true); }

class HeadlessDisplayCapture final : public DisplayDevice {
public:
    void onChar(char c) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (text_.size() < 65536) text_.push_back(c);
    }

    std::string escapedText() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string out;
        out.reserve(text_.size());
        for (unsigned char c : text_) {
            if (c == '\r') out += "\\r";
            else if (c == '\n') out += "\\n";
            else if (c >= 0x20 && c < 0x7f) out.push_back(static_cast<char>(c));
        }
        return out;
    }

private:
    mutable std::mutex mutex_;
    std::string text_;
};

// Map a --enable/--disable card to its EmulationController facade (immediate,
// no GUI deferred plug). Cascades + mutex evictions live inside the setters.
static bool applyHeadlessCardOverride(EmulationController& emu, pom1::CliCard card,
                                      bool on, pom1::TopologyMode mode)
{
    using CC = pom1::CliCard;
    pom1::CardId id = pom1::CardId::Invalid;
    switch (card) {
        case CC::Aci:          id = pom1::CardId::Aci; break;
        case CC::Sid:          id = pom1::CardId::Sid; break;
        case CC::SidSE:        id = pom1::CardId::SidSpecialEdition; break;
        case CC::MicroSD:      id = pom1::CardId::MicroSD; break;
        case CC::Tms9918:      id = pom1::CardId::Tms9918; break;
        case CC::A1IoRtc:      id = pom1::CardId::A1IoRtc; break;
        case CC::Hgr:          id = pom1::CardId::Gen2; break;
        case CC::Cffa1:        id = pom1::CardId::Cffa1; break;
        case CC::WifiModem:    id = pom1::CardId::WifiModem; break;
        case CC::TerminalCard: id = pom1::CardId::TerminalCard; break;
        case CC::JukeBox:      id = pom1::CardId::JukeBox; break;
        case CC::CodeTank:     id = pom1::CardId::CodeTank; break;
        case CC::Pr40:         id = pom1::CardId::Pr40; break;
        case CC::GT6144:       id = pom1::CardId::Gt6144; break;
        case CC::ExtendedAci:  id = pom1::CardId::ExtendedAci; break;
        case CC::IEC:          id = pom1::CardId::Iec; break;
        case CC::Krusader: {
            std::string error;
            return !on || emu.reloadKrusader(error);
        }
    }
    const pom1::CardSet current = emu.getEnabledCards();
    if (on && pom1::wouldCreateConflict(current, id, mode)) return false;
    emu.setCardEnabled(id, on);
    return true;
}

// ── Headless graphics-regression capture (--dump-gen2-frame / --dump-tms-frame) ──
// FNV-1a 64 over the RGBA bytes — a stable golden value logged with each PNG so
// CI can assert a frame hash without diffing image files.
static uint64_t fnv1a64(const void* data, size_t n)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

// Write a top-down RGBA8 buffer (software-renderer order — no Y-flip, unlike
// glReadPixels) to a PNG; log its dimensions + hash. Returns false on failure.
static bool dumpRgbaPng(const char* tag, const std::string& path,
                        const uint32_t* rgba, int w, int h)
{
    if (stbi_write_png(path.c_str(), w, h, 4, rgba, w * 4) == 0) {
        pom1::log().error("GFX", "stbi_write_png failed for " + path +
                                 " (does the directory exist / is it writable?)");
        return false;
    }
    char msg[256];
    std::snprintf(msg, sizeof(msg), "%s frame %dx%d hash=0x%016llx -> %s",
                  tag, w, h,
                  static_cast<unsigned long long>(
                      fnv1a64(rgba, static_cast<size_t>(w) * h * 4)),
                  path.c_str());
    pom1::log().info("GFX", msg);
    return true;
}

// Run the CPU for `totalCycles`, segmenting at each scheduled `--paste-at-cycle`
// directive so the keys are injected at an exact cumulative cycle count. Pastes
// are sorted by cycle (order-independent on the command line); any scheduled
// past `totalCycles` are warned and skipped. With no pastes this is a single
// runCyclesSync — identical to the previous behaviour. The point is determinism:
// two headless runs (e.g. --vram-noise ON vs OFF) land on the SAME game frame
// regardless of host speed, which the title-screen-gated TMS9918 sprite tests
// could not reach with wall-clock --paste.
static void runCyclesWithTimedPastes(EmulationController& emu, uint64_t totalCycles,
                                     std::vector<pom1::CliTimedPaste> pastes)
{
    constexpr int kTimedPasteCap = 4096;   // mirrors --paste's kMaxPasteChars
    std::sort(pastes.begin(), pastes.end(),
              [](const pom1::CliTimedPaste& a, const pom1::CliTimedPaste& b) {
                  return a.cycle < b.cycle;
              });
    uint64_t done = 0;
    for (const auto& p : pastes) {
        if (p.cycle > totalCycles) {
            char m[160];
            std::snprintf(m, sizeof(m),
                          "--paste-at-cycle %llu is past the run budget (%llu cycles) — skipped",
                          (unsigned long long)p.cycle, (unsigned long long)totalCycles);
            pom1::log().warn("CLI", m);
            continue;
        }
        if (p.cycle > done) {
            emu.runCyclesSync(p.cycle - done);
            done = p.cycle;
        }
        int sent = pom1::queueKeystrokes(emu, p.keys, kTimedPasteCap);
        // Deliver to $D010 NOW: runCyclesSync (below) pauses the async thread, so
        // nothing would otherwise drain the queue into Memory and the CPU would
        // never see the key. drainTo writes each queued char in turn (last wins on
        // $D010) — inject one key per --paste-at-cycle when a program reads several
        // prompts in sequence (each read needs its own strobe at its own cycle).
        emu.deliverQueuedKeys();
        char m[128];
        std::snprintf(m, sizeof(m), "--paste-at-cycle %llu: injected %d keys",
                      (unsigned long long)p.cycle, sent);
        pom1::log().info("CLI", m);
    }
    if (done < totalCycles)
        emu.runCyclesSync(totalCycles - done);
}

static int runHeadless(pom1::CliPlan& plan)
{
    pom1::log().info("POM1", "headless mode — no window (Ctrl-C / SIGTERM to exit)");

    HeadlessDisplayCapture display;
    // The audio device is built HERE and handed down, so the core it drives
    // does not construct a host service of its own. Declared before `emu` so
    // it outlives the sources registered on it (Memory unregisters them in its
    // destructor, but the declaration order says the same thing statically).
    AudioDevice audio(/*initializeHardware=*/true,
                      plan.audioLatencyMs ? *plan.audioLatencyMs : 0);
    EmulationController emu(&display, /*initializeAudioHardware=*/true, &audio);

    // Machine config: apply the preset (RAM + cards + BASIC ROM) immediately —
    // no GUI deferred plug — then explicit --enable/--disable overrides, then
    // the --terminal override. So `--headless --preset 11` plugs GEN2 + 48K for
    // an HGR game test, with no display.
    if (plan.presetIndex >= 0)
        MainWindow_ImGui::applyHeadlessConfig(emu, plan.presetIndex);
    const pom1::TopologyMode overrideMode =
        plan.presetIndex >= 0 &&
        pom1::isFantasyPreset(pom1::presetIdFromIndex(plan.presetIndex))
            ? pom1::TopologyMode::Fantasy : pom1::TopologyMode::Strict;
    for (const auto& o : plan.cardOverrides) {
        if (!applyHeadlessCardOverride(emu, o.card, o.enable, overrideMode)) {
            pom1::log().error("CLI", "card override rejected by topology policy");
            return 2;
        }
    }
    if (plan.terminalOverride)
        emu.setCardEnabled(pom1::CardId::TerminalCard, true);
    // DRAM refresh stall override (the preset path leaves it off so 1:1-timed
    // demos stay exact). --dram-refresh arms the 4/65 CPU steal — the beam keeps
    // running, so beam-race code drifts as on real DRAM silicon.
    if (plan.dramRefreshOverride)
        emu.setDramRefreshEnabled(*plan.dramRefreshOverride);
    if (plan.displayFieldSyncOverride)
        emu.setDisplayFieldSync(*plan.displayFieldSyncOverride);

    if (plan.cpuMax)
        emu.setExecutionSpeedCyclesPerFrame(1000000);
    else if (plan.executionSpeed)
        emu.setExecutionSpeedCyclesPerFrame(*plan.executionSpeed);

    if (plan.telemetryPort)
        emu.setTelemetryListenPort(static_cast<uint16_t>(*plan.telemetryPort));
    if (!plan.telemetryLogPath.empty())
        emu.setTelemetryLogFile(plan.telemetryLogPath);
    if (plan.telemetryPort || !plan.telemetryLogPath.empty())
        emu.setTelemetryEnabled(true);

    // Silicon-faithful cold-boot overrides (VRAM noise + RAM poison + the
    // read-before-write trap). Arm the flags, then a single hardReset re-seeds
    // memory accordingly. Must precede the CodeTank ROM override below —
    // hardReset reloads the preset's default daughterboard ROM, so the override
    // is applied AFTER the reset to survive.
    bool needPowerOnReset = false;
    if (plan.vramNoiseOnReset) {
        emu.setVramNoiseOnReset(true);
        needPowerOnReset = true;
        pom1::log().info("TMS9918", "VRAM power-on noise ON (--vram-noise): "
                                    "silicon-faithful cold-boot VRAM");
    }
    if (plan.tmsFrameFlagHostile) {
        emu.setTmsFrameFlagHostile(true);
        pom1::log().info("TMS9918", "Hostile frame-flag ON (--tms-frameflag-hostile): "
                                    "F never registers — unbounded WAIT_VBLANK polls hang");
    }
    if (plan.ramPoisonByte) {
        emu.setRamPoison(true, *plan.ramPoisonByte);
        needPowerOnReset = true;
        char m[96];
        std::snprintf(m, sizeof(m), "system RAM poisoned with $%02X (--ram-poison)",
                      (unsigned)*plan.ramPoisonByte);
        pom1::log().info("RAMTRAP", m);
    }
    if (plan.ramWriteTrap) {
        emu.setRamWriteTrap(true);
        needPowerOnReset = true;
        pom1::log().info("RAMTRAP", "read-before-write trap ARMED (--ram-trap): "
                                    "logging uninitialised RAM reads in [0,$2000) + "
                                    "[$E000,$F000) (Parmigiani high RAM bank)");
    }
    if (needPowerOnReset)
        emu.hardReset(/*animateBoot=*/false);
    // CodeTank ROM / jumper override (headless): the GUI path honours these via
    // MainWindow; the headless path did not, so --codetank-rom/--codetank-jumper
    // were silently ignored. Apply after any noise hardReset.
    if (plan.codeTankJumperOverride)
        emu.setCodeTankJumper(*plan.codeTankJumperOverride);
    if (!plan.codeTankRomPath.empty()) {
        std::string err;
        if (!emu.loadCodeTankRom(plan.codeTankRomPath, err))
            pom1::log().error("CodeTank", "--codetank-rom failed: " + err);
        else
            pom1::log().info("CodeTank", "--codetank-rom loaded: " + plan.codeTankRomPath);
    }

    // Phase-C deferred verbs (load / run / paste / step / sd-* / rtc / snapshot / break).
    pom1::runDeferredActions(plan.deferredActions, emu);

    // Graphics-regression capture: let the loaded program render a settled
    // frame, snapshot it, render the card's framebuffer with no display, write a
    // PNG, and exit. The render path is the same CPU software renderer the UI
    // uses (GraphicsCard::render / the TMS9918 progressive raster), so a headless
    // capture is pixel-identical to the GUI — the basis for automated graphics
    // regression (golden-image diff). The logged FNV hash is a file-free golden.
    if (!plan.dumpGen2Path.empty() || !plan.dumpTmsPath.empty()) {
        // Settle the frame: deterministic (run exactly N emulated cycles —
        // host-independent) when --dump-after-cycles is given, else a wall-clock
        // sleep. The cycle path is the one to use for golden-image regression.
        // --paste-at-cycle forces the deterministic cycle path (wall-clock settle
        // can't schedule cycle-exact injections); the budget is --dump-after-cycles
        // if given, else the last scheduled injection cycle.
        if (!plan.timedPastes.empty()) {
            uint64_t maxPaste = 0;
            for (const auto& p : plan.timedPastes) maxPaste = std::max(maxPaste, p.cycle);
            uint64_t budget = plan.dumpAfterCycles > 0
                                  ? static_cast<uint64_t>(plan.dumpAfterCycles)
                                  : maxPaste;
            if (plan.dumpAfterCycles == 0)
                pom1::log().warn("CLI", "--paste-at-cycle without --dump-after-cycles: "
                                        "capturing right after the last injection "
                                        "(add --dump-after-cycles N for extra settle)");
            runCyclesWithTimedPastes(emu, budget, plan.timedPastes);
        }
        else if (plan.dumpAfterCycles > 0)
            emu.runCyclesSync(static_cast<uint64_t>(plan.dumpAfterCycles));
        else if (plan.dumpSettleMs > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(plan.dumpSettleMs));
        if (emu.isDramRefreshEnabled()) {
            char dbg[128];
            std::snprintf(dbg, sizeof(dbg),
                          "DRAM refresh ON — %llu CPU cycles stolen (beam keeps "
                          "running; beam-race code drifts)",
                          (unsigned long long)emu.getDramRefreshStallCount());
            pom1::log().info("GFX", dbg);
        }
        EmulationSnapshot snap;
        emu.copySnapshot(snap);
        bool ok = true;
        if (!plan.dumpGen2Path.empty()) {
            if (!snap.gen2Enabled)
                pom1::log().warn("GFX", "--dump-gen2-frame: GEN2 card not plugged "
                                        "(use --preset 11 or --enable hgr) — capturing anyway");
            GraphicsCard gc;
            gc.render(snap.memory.data(), snap.gen2DisplayState, snap.gen2FrameStartState,
                      snap.gen2VideoEvents,
                      snap.gen2FiftyHz ? Gen2VideoScanner::kLinesPerFrame50Hz
                                       : Gen2VideoScanner::kLinesPerFrame);
            ok &= dumpRgbaPng("GEN2", plan.dumpGen2Path, gc.pixels(),
                              GraphicsCard::kHiresWidth, GraphicsCard::kHiresHeight);
        }
        if (!plan.dumpTmsPath.empty()) {
            ok &= dumpRgbaPng("TMS9918", plan.dumpTmsPath, snap.tms9918.framebuffer.data(),
                              TMS9918::kFullWidth, TMS9918::kFullHeight);
        }
        return ok ? 0 : 1;
    }

    // No frame dump: if cycle-scheduled pastes were given (e.g. paired with
    // --telemetry-log to capture the resulting output stream), replay them up to
    // the last scheduled cycle before dropping into the idle wait. With
    // --exit-after-cycles the budget is that flag (pastes past it are skipped).
    if (!plan.timedPastes.empty()) {
        uint64_t maxPaste = 0;
        for (const auto& p : plan.timedPastes) maxPaste = std::max(maxPaste, p.cycle);
        const uint64_t budget = plan.exitAfterCycles > 0
                                    ? static_cast<uint64_t>(plan.exitAfterCycles)
                                    : maxPaste;
        runCyclesWithTimedPastes(emu, budget, plan.timedPastes);
    }
    else if (plan.exitAfterCycles > 0) {
        emu.runCyclesSync(static_cast<uint64_t>(plan.exitAfterCycles));
    }

    // Bounded run (--exit-after-cycles): the machine booted, every plugged card
    // survived N cycles of the Monitor / BASIC / card ROM, nothing crashed or
    // deadlocked. That is the whole assertion of the headless preset matrix —
    // exit 0 here, never fall into the signal wait.
    if (plan.exitAfterCycles > 0) {
        EmulationSnapshot snap;
        emu.copySnapshot(snap);
        char msg[96];
        std::snprintf(msg, sizeof(msg), "headless run complete — %d cycles, PC=$%04X",
                      plan.exitAfterCycles, (unsigned)snap.programCounter);
        pom1::log().info("POM1", msg);
        pom1::log().info("POM1", "headless display capture: " +
                                 display.escapedText());
        return 0;
    }

    std::signal(SIGINT,  pom1_headless_signal_handler);
    std::signal(SIGTERM, pom1_headless_signal_handler);
    while (!g_headlessStop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    pom1::log().info("POM1", "headless shutdown");
    return 0;
}
#endif // !POM1_IS_WASM

int main(int argc, char* argv[])
{
#if !POM1_IS_WASM && defined(__APPLE__)
    // MUST precede the logger: this chdirs into ~/Library/Application Support/
    // POM1/, and the logger's file sink resolves `logs/pom1.log` against the
    // working directory. Installed the other way round, a Finder launch wrote
    // its log next to wherever Finder happened to start us. It logs nothing
    // itself, so nothing is lost by running it first.
    pom1_macos_provision_user_data_dir();
#endif

    // Install the Tee(stream + ring + file) logger so every subsystem message
    // lands in stdout/stderr, in the ring buffer the debug console reads, and
    // in logs/pom1.log — the only one of the three that outlives the process.
    pom1::initDefaultTeeLogger();
    pom1::log().info("POM1", "v" POM1_VERSION_STRING " - Apple 1 Emulator (Dear ImGui)");
    if (pom1::sessionFileLog().isOpen())
        pom1::log().info("POM1", "session log: " + pom1::sessionFileLog().path());

    // Parse command-line arguments via the CLI dispatcher. The dispatcher
    // owns every verb — boot-time (preset, card overrides, cassette paths,
    // CPU speed) AND post-deferred-plug verbs (program load, --paste,
    // --rec, --sd-*, --rtc-freeze, etc.). `cliCleanExit` is true when the
    // dispatcher answered a print-and-leave flag (--help, --list-presets);
    // in that case main exits 0 without opening a window.
    bool cliCleanExit = false;
    auto parsedPlan = pom1::parseCli(argc, argv, cliCleanExit);
    if (cliCleanExit) return 0;
    if (!parsedPlan) return 1;
    pom1::CliPlan plan = std::move(*parsedPlan);

    // --audio-latency is applied where the device is constructed: runHeadless()
    // below, or main()'s own AudioDevice further down. It used to be a static
    // set here because Memory built the device deep inside the controller and
    // there was no other way to reach it.

#if !POM1_IS_WASM
    // Headless: no window, no GL — go straight to the emulator driver. Must run
    // before glfwInit so a display-less CI box never touches GLFW.
    if (plan.headless)
        return runHeadless(plan);
#endif

    // --tape: optional explicit cassette path (auto-play). Bundled WOZ_talk is
    // NOT loaded globally — only the POM1 Fantasy preset preloads it (see
    // applyMachineConfig in MainWindow_Presets.cpp).

    // --save-tape-format: append .aci/.wav only if the path has no extension.
    if (!plan.saveTapePath.empty()) {
        plan.saveTapePath = pom1::resolveSaveTapePath(plan.saveTapePath, plan.saveTapeFormat);
    }

    // Setup GLFW
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        pom1_report_glfw_init_failure();
        return -1;
    }

    // Make raw Xlib protocol errors non-fatal (Linux/X11 only; no-op elsewhere).
    // GLFW's error callback above never sees these — a stray clipboard BadWindow
    // would otherwise hit Xlib's default handler and exit() the whole emulator.
    pom1InstallX11ErrorGuard();

    // OpenGL / GLSL context hints
#if defined(POM1_HAS_METAL) && POM1_HAS_METAL
    // macOS + Metal: tell GLFW we don't want a GL context — PomRenderer_Metal
    // will own the back-buffer through a CAMetalLayer attached to the
    // NSWindow's contentView. glsl_version is unused by the Metal ImGui
    // backend (it ignores the parameter inside initImGuiBackend).
    const char* glsl_version = nullptr;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#elif POM1_GL_ES
    // OpenGL ES 3.0 — GLSL ES 300. Two targets share this branch: WASM (WebGL
    // 2.0 IS GLES 3.0) and the native GLES tier (-DPOM1_GLES=ON) for GPUs that
    // expose GLES 3.x but not desktop GL 3.2 — Raspberry Pi 4/5 above all,
    // where Mesa's V3D caps desktop GL at 3.1 and the core-profile request
    // below would simply fail.
    const char* glsl_version = "#version 300 es";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#if !POM1_IS_WASM && defined(GLFW_CONTEXT_CREATION_API)
    // Force EGL: GLFW's default on X11 is GLX, which can only produce a GLES
    // context when the server advertises GLX_EXT_create_context_es2_profile —
    // V3D does not. EGL is the path those drivers actually implement, and it
    // is also what a Wayland/KMS session uses anyway.
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
#endif
#else
    // GL 3.2 + GLSL 150 pour macOS / Linux / Windows
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
#endif

    // Create window
    // Stable WM_CLASS / app-id so the Linux .desktop entry (StartupWMClass=POM1)
    // binds to the window — correct taskbar icon + grouping. Without it GLFW
    // derives WM_CLASS from the (version-laden) title and the entry won't match.
    // Harmless on macOS/Windows (the X11/Wayland hints are ignored).
#if !POM1_IS_WASM
    glfwWindowHintString(GLFW_X11_CLASS_NAME,    "POM1");
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "POM1");
#ifdef GLFW_WAYLAND_APP_ID
    glfwWindowHintString(GLFW_WAYLAND_APP_ID,    "POM1");
#endif
#endif
#if defined(POM1_HAS_X11)
    // Create the window HIDDEN so GLFW's X11 backend does not run its
    // waitForVisibilityNotify() inside glfwCreateWindow(). That wait hangs
    // forever on some X servers / window managers (GLFW 3.3; fixed upstream in
    // 3.4) — the window is created (WM shows an icon) but glfwCreateWindow never
    // returns, so the render loop never starts. We map the window ourselves,
    // right after creation, via pom1ShowGlfwWindowX11().
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
#endif
    const char* const kWindowTitle = "POM1 v" POM1_VERSION_STRING " - Apple 1 Emulator";
    GLFWwindow* window = glfwCreateWindow(1274, 801, kWindowTitle, NULL, NULL);

#if !POM1_IS_WASM && !defined(__APPLE__) && !POM1_GL_ES
    // Retry chain, desktop-GL only. First ask for 3.2 without pinning the CORE
    // profile. Some old and most virtualised ICDs (VMware, VirtualBox, older
    // Intel) refuse a core-profile request outright yet happily hand out a 3.2+
    // compatibility context — where `#version 150` is still valid, so nothing
    // downstream changes. Excluded on macOS, where a non-core 3.2 request
    // yields a legacy 2.1 context that GLSL 150 could not run on, and on the
    // GLES tier, which never asks for a profile in the first place.
    //
    // Then, still nothing, walk DOWN to 3.1 and 3.0. That used to be refused on
    // the grounds that `#version 150` was hardcoded in two places — it no longer
    // is: the ImGui preamble is stepped down to what the driver reports
    // (PomRenderer_GL.cpp, imguiGlslVersion) and the CRT stack tries a 150 → 140
    // → 130 cascade (OpenGLShader.cpp). Both shader sets only need GLSL 1.30
    // constructs, so a 3.0 context runs POM1 as-is. What this buys concretely:
    // Mesa's V3D on a **Raspberry Pi** exposes desktop GL 3.1 / GLSL 1.40 and
    // nothing above, so before this chain POM1 simply failed to open a window
    // there unless the launcher lied to it (MESA_GL_VERSION_OVERRIDE=3.3).
    // The purpose-built path is still -DPOM1_GLES=ON (real GLES 3.0 via EGL);
    // this is the safety net for a plain `cmake` build on such a box.
    // In the failure mode of issue #34, note, no fallback could have helped at
    // all — the ICD never loaded, so every context request failed identically.
    if (window == NULL) {
        static const struct { int major, minor; const char* what; } kFallbacks[] = {
            { 3, 2, "OpenGL 3.2 (no core-profile constraint)" },
            { 3, 1, "OpenGL 3.1" },
            { 3, 0, "OpenGL 3.0" },
        };
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_FALSE);
        for (const auto& fb : kFallbacks) {
            fprintf(stderr, "POM1: retrying with %s...\n", fb.what);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, fb.major);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, fb.minor);
            window = glfwCreateWindow(1274, 801, kWindowTitle, NULL, NULL);
            if (window != NULL) {
                fprintf(stderr, "POM1: got a %s context.\n", fb.what);
                break;
            }
        }
    }
#endif

    if (window == NULL) {
#if defined(POM1_HAS_METAL) && POM1_HAS_METAL
        pom1_report_window_creation_failure("a Metal-backed window (no GL context)");
#elif POM1_GL_ES
        pom1_report_window_creation_failure("OpenGL ES 3.0 (GLSL ES 300)");
#else
        pom1_report_window_creation_failure("OpenGL 3.2 core profile (GLSL 150)");
#endif
        glfwTerminate();
        return -1;
    }

#if !POM1_IS_WASM && !defined(__APPLE__)
    // OS window icon from pic/icon.png — no-op silently if the asset can't
    // be found (developer build without the pic/ tree, e.g.). Skipped on
    // macOS: GLFW emits "Regular windows do not have icons on macOS" — the
    // OS pulls the icon from the .app bundle / Info.plist instead.
    {
        const std::string iconPath = find_app_icon_path();
        if (!iconPath.empty()) {
            int iw = 0, ih = 0, ic = 0;
            unsigned char* pixels = stbi_load(iconPath.c_str(), &iw, &ih, &ic, 4);
            if (pixels && iw > 0 && ih > 0) {
                GLFWimage img;
                img.width = iw;
                img.height = ih;
                img.pixels = pixels;
                glfwSetWindowIcon(window, 1, &img);
                stbi_image_free(pixels);
            } else if (pixels) {
                stbi_image_free(pixels);
            }
        }
    }
#endif

#if defined(POM1_HAS_X11)
    // Now that the icon / WM properties are set, show the window ourselves (it
    // was created hidden above to dodge GLFW 3.3's X11 visibility-wait hang). If
    // this isn't an X11 window (Wayland GLFW build/session), fall back to the
    // normal path — glfwShowWindow only runs the hanging wait on the X11 backend.
    if (!pom1ShowGlfwWindowX11(window))
        glfwShowWindow(window);
#endif

#if defined(POM1_HAS_METAL) && POM1_HAS_METAL
    // Metal owns the back-buffer through CAMetalLayer.displaySyncEnabled —
    // no GL context to make current, no glfwSwapInterval to set. GLFW still
    // delivers input events to the same window.
#else
    glfwMakeContextCurrent(window);
#if !POM1_IS_WASM
    glfwSwapInterval(1); // Vsync (desktop)
#else
    // Sur Emscripten, glfwSwapInterval avant emscripten_set_main_loop_* provoque :
    // « emscripten_set_main_loop_timing: ... main loop does not exist ».
    // On applique l’intervalle dans le premier tick de la boucle (ci-dessous).
#endif
#endif

    // Construct the graphics backend now that the window + (optional) GL
    // context are ready. makeRenderer() dispatches to PomRenderer_GL or
    // PomRenderer_Metal at compile time depending on POM1_HAS_METAL — the
    // call site stays platform-agnostic.
    auto rendererOwned = pom1::makeRenderer(window);
    pom1::setRenderer(rendererOwned.get());

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    // NE PAS activer NavEnableKeyboard au boot - le clavier est pour l'Apple 1,
    // pas pour ImGui. L'utilisateur peut basculer en mode navigation UI avec
    // F10 (MainWindow_ImGui::setUiNavMode) — accessibilité clavier complète,
    // avec indicateur "UI NAV" dans la barre de statut.
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // La navigation manette, elle, ne rentre jamais en conflit avec le clavier
    // Apple 1 — toujours active (no-op sans manette branchée).
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    // Docking (branche `docking` de Dear ImGui, dont le tag est épinglé dans
    // le fichier IMGUI_VERSION). Toute l'UI POM1 vit dans un DockSpace plein
    // cadre construit par MainWindow_ImGui::renderDockSpace(), entre la barre
    // d'outils et la barre de statut. Le multi-viewport (fenêtres détachées
    // hors de la fenêtre OS) reste VOLONTAIREMENT désactivé : WASM ne le
    // supporte pas et le backend Metal maison (PomRenderer_Metal.mm + son
    // CAMetalLayer + le CRT stack par slot) devrait gérer des viewports
    // secondaires — deux chemins divergents pour un gain marginal.
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Ancrage sans maintenir SHIFT (le défaut ImGui) : POM1 réserve SHIFT au
    // clavier Apple 1 émulé, et l'ancrage doit rester une action souris pure.
    io.ConfigDockingWithShift = false;
    // Disable ImGui's automatic imgui.ini load/save. POM1 manages per-preset
    // ini files under ini/imgui_preset_NN.ini manually via
    // MainWindow_ImGui::savePresetLayout / loadPresetLayout — called on
    // every applyMachineConfig and on clean shutdown.
    io.IniFilename = nullptr;
    // Create the per-preset ini directory up front so the first preset
    // load/save doesn't race the lazy path and so users can see the folder
    // exists before they start dragging windows around.
    {
        std::error_code ec;
        std::filesystem::create_directories("ini", ec);
        if (ec) {
            pom1::log().warn("POM1",
                "could not create ini/: " + ec.message());
        }
    }
    // Pre-populate every preset's layout file with its curated defaults (from
    // the preloaded ini_defaults/, falling back to kMachinePresets[].layout).
    // Existing files are left alone, so user customisations persist. After this
    // call, ini/imgui_preset_NN.ini exists for every preset, so each profile's
    // window positions are applied on first visit — including under WASM, where
    // this used to be skipped (the layouts then defaulted to garbage). The WASM
    // ini/ lives in MEMFS, so it survives preset switches within a session but
    // not a page reload (an IDBFS mount + FS.syncfs would add cross-reload
    // persistence — separate follow-up).
    MainWindow_ImGui::pregenerateMissingPresetLayouts();

    // Charger les polices. Police d'UI : DejaVuSans.ttf (fonts/) — une vraie fonte
    // vectorielle à large couverture Unicode (accents latins de l'UI FR, puces,
    // flèches, ☐/☑…) qui reste nette à toute taille. ImGui 1.92 raster les glyphes
    // à la demande (backend à textures), donc pas besoin de glyph-ranges : toute la
    // fonte est disponible. Repli sur la fonte intégrée (ProggyClean) si absente —
    // elle scale mal, ce que l'aperçu Markdown (titres agrandis) rend visible.
    ImFontConfig fontConfig;
    fontConfig.SizePixels = 15.0f;
    // DejaVu carries legacy X11 PUA glyphs (U+F001/U+F002 are seven-segment
    // "88" ligature leftovers) that shadow same-codepoint FontAwesome icons in
    // the merge below (first font holding a glyph wins) — ICON_FA_MUSIC used
    // to render as "88". Exclude the whole icon window from the UI font so
    // FontAwesome always supplies it.
    static const ImWchar uiFontExclude[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
    fontConfig.GlyphExcludeRanges = uiFontExclude;
    ImFont* defaultFont = nullptr;
#if POM1_IS_WASM
    const std::string uiFontPath = "fonts/DejaVuSans.ttf";   // preloaded in MEMFS
#else
    const std::string uiFontPath = find_font_path("DejaVuSans.ttf");
#endif
    if (!uiFontPath.empty())
        defaultFont = io.Fonts->AddFontFromFileTTF(uiFontPath.c_str(), 15.0f, &fontConfig);
    if (!defaultFont) {
        if (!uiFontPath.empty())
            fprintf(stderr, "Warning: Could not load UI font '%s' - falling back to the built-in font\n",
                    uiFontPath.c_str());
        defaultFont = io.Fonts->AddFontDefault(&fontConfig);
    }
    if (defaultFont) defaultFont->FallbackChar = (ImWchar)' ';

    // Fusionner la police d'icônes FontAwesome
    ImFontConfig iconsConfig;
    iconsConfig.MergeMode = true;
    iconsConfig.PixelSnapH = true;
    iconsConfig.GlyphMinAdvanceX = 15.0f;
    static const ImWchar iconsRanges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
#if POM1_IS_WASM
    const char* fontPath = "fonts/fa-solid-900.ttf";
#else
    std::string fontPathStorage = find_fa_solid_font_path();
    // Nothing found: hand AddFontFromFileTTF the canonical name so the failure
    // message below names what was expected, not a ../ guess the locator
    // has already tried from every root.
    const char* fontPath = fontPathStorage.empty() ? "fonts/fa-solid-900.ttf"
                                                   : fontPathStorage.c_str();
#endif
    if (!io.Fonts->AddFontFromFileTTF(fontPath, 14.0f, &iconsConfig, iconsRanges)) {
        fprintf(stderr,
                "Warning: Could not load icon font (tried '%s') - toolbar/menu icons show as '?'\n"
                "  Install fonts next to the .exe (fonts\\fa-solid-900.ttf) or run from the repo with fonts/ present.\n",
                fontPath);
    }

    // HiDPI: on Linux (X11) and Windows, GLFW does not auto-scale the framebuffer,
    // so on a high-DPI monitor the UI renders tiny. Read the monitor's content
    // scale (glfwGetWindowContentScale, GLFW 3.3+) and hand it to MainWindow
    // below — it multiplies it by the user's Interface zoom and scales the whole
    // UI (geometry AND fonts) in applyUiTheme(). Skipped on macOS — Retina is
    // handled by io.DisplayFramebufferScale, so scaling here would double the
    // size — and on WASM, where the browser owns devicePixelRatio. Overridable
    // at runtime in Settings ▸ UI Theme ▸ Interface zoom.
    float bootContentScale = 1.0f;
#if !defined(__APPLE__) && !POM1_IS_WASM
    {
        float xs = 1.0f, ys = 1.0f;
        glfwGetWindowContentScale(window, &xs, &ys);
        if (xs > 1.01f) {
            bootContentScale = xs > 3.0f ? 3.0f : xs;
            fprintf(stderr, "[POM1] HiDPI: monitor content scale %.2f -> UI scale %.2f\n",
                    xs, bootContentScale);
        }
    }
#endif

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends. On Metal we skip InitForOpenGL —
    // ImGui_ImplGlfw_InitForOther wires up the same input plumbing without
    // requiring a live GL context (which we no longer create on macOS).
#if defined(POM1_HAS_METAL) && POM1_HAS_METAL
    ImGui_ImplGlfw_InitForOther(window, true);
#else
    ImGui_ImplGlfw_InitForOpenGL(window, true);
#endif
    rendererOwned->initImGuiBackend(glsl_version);

    // Create main application. The audio device is owned HERE, above the
    // window, for two reasons: the core no longer builds host services, and it
    // must outlive the machine whose cassette/SID are registered on it (~Memory
    // unregisters them, so this is belt and braces).
    AudioDevice audioDevice(/*initializeHardware=*/true,
                            plan.audioLatencyMs ? *plan.audioLatencyMs : 0);
    MainWindow_ImGui mainWindow(&audioDevice);
    // Before anything renders: the first render() frame loads ini/ui.settings
    // and applies the theme, which bakes zoom × DPI into the style.
    mainWindow.setUiDpiScale(bootContentScale);
    if (plan.presetIndex >= 0)
        mainWindow.setDefaultPresetIndex(plan.presetIndex);
    if (plan.terminalOverride)
        mainWindow.setTerminalCardOverride(true);
    if (plan.telemetryPort)
        mainWindow.setTelemetryPortOverride(*plan.telemetryPort);
    if (!plan.telemetryLogPath.empty())
        mainWindow.setTelemetryLogPath(plan.telemetryLogPath);
    if (!plan.initialTapePath.empty()) {
        mainWindow.setInitialTapePath(plan.initialTapePath);
        mainWindow.setInitialTapeAutoPlay(plan.initialTapeAutoPlay);
    }
    if (!plan.saveTapePath.empty())
        mainWindow.setSaveTapePath(plan.saveTapePath);
    if (plan.cpuMax)
        mainWindow.setCpuMaxSpeedOnBoot(true);
    if (plan.fullscreen)
        mainWindow.requestCliFullscreen();
    if (plan.executionSpeed)
        mainWindow.setInitialExecutionSpeed(*plan.executionSpeed);
    if (!plan.cardOverrides.empty())
        mainWindow.setCardOverrides(std::move(plan.cardOverrides));
    if (plan.sidChipOverride)
        mainWindow.setSidChipOverride(*plan.sidChipOverride);
    if (plan.jukeBoxJumperOverride)
        mainWindow.setJukeBoxJumperOverride(*plan.jukeBoxJumperOverride);
    if (plan.jukeBoxChipModeOverride)
        mainWindow.setJukeBoxChipModeOverride(*plan.jukeBoxChipModeOverride);
    if (plan.codeTankJumperOverride)
        mainWindow.setCodeTankJumperOverride(*plan.codeTankJumperOverride);
    if (!plan.codeTankRomPath.empty())
        mainWindow.setCodeTankRomPathOverride(plan.codeTankRomPath);
    if (!plan.iecDiskPath.empty()) {
        // Mount immediately on the IEC card's virtual 1541. Memory ctor's
        // probe (disks/iec/dev8.d64) ran before this; this override
        // replaces what the probe loaded.
        if (!mainWindow.getEmulationController()->mountIECDisk(plan.iecDiskPath)) {
            pom1::log().warn("IEC", std::string("--iec-disk: failed to mount ") + plan.iecDiskPath);
        } else {
            pom1::log().info("IEC", std::string("--iec-disk: mounted ") + plan.iecDiskPath);
        }
    }
    if (plan.siliconStrictModeOverride)
        mainWindow.setSiliconStrictModeOverride(*plan.siliconStrictModeOverride);
    if (plan.dramRefreshOverride)
        mainWindow.setDramRefreshOverride(*plan.dramRefreshOverride);
    if (plan.displayFieldSyncOverride)
        mainWindow.setDisplayFieldSyncOverride(*plan.displayFieldSyncOverride);
    if (!plan.deferredActions.empty())
        mainWindow.setDeferredCliActions(std::move(plan.deferredActions));
    mainWindow.setWindow(window);

#if !POM1_IS_WASM
    // Route SIGINT/SIGTERM into a "please close the window" request so the
    // destructor path (→ --save-tape dump) runs instead of std::terminate'ing
    // the process mid-flight.
    g_signalWindow.store(window, std::memory_order_release);
    std::signal(SIGINT,  pom1_signal_handler);
    std::signal(SIGTERM, pom1_signal_handler);
#endif
    glfwSetWindowUserPointer(window, &mainWindow);

    // Installer nos callbacks GLFW APRÈS ImGui pour les chaîner
    glfwSetCharCallback(window, glfw_char_callback);
    glfwSetKeyCallback(window, glfw_key_callback);
#if !POM1_IS_WASM
    // Adaptive-UI throttle activity sources (desktop only — WASM keeps rAF).
    glfwSetMouseButtonCallback(window, glfw_mouse_button_callback);
    glfwSetCursorPosCallback(window, glfw_cursor_pos_callback);
    glfwSetScrollCallback(window, glfw_scroll_callback);
    glfwSetWindowFocusCallback(window, glfw_window_focus_callback);
    glfwSetWindowRefreshCallback(window, glfw_window_refresh_callback);
    glfwSetWindowSizeCallback(window, glfw_window_size_callback);
    // Drag-and-drop (desktop only: under Emscripten the browser owns the drop
    // and hands over a File object, not a path the native loaders could open).
    glfwSetDropCallback(window, glfw_drop_callback);
#endif

    // Keep the window answering the compositor while a forked native file
    // picker is up. Without this the render thread sits in waitpid() for the
    // whole life of the zenity/kdialog child and GNOME flags POM1 as "not
    // responding" — see NativeFileDialog::setWaitPump.
    pom1::NativeFileDialog::setWaitPump([]() {
        g_inNativeDialogPump = true;
        glfwPollEvents();
        g_inNativeDialogPump = false;
    });

    // Main loop
#if POM1_IS_WASM
    // Emscripten: browser controls the loop - pass a callback
    struct LoopContext {
        GLFWwindow* window;
        MainWindow_ImGui* mainWindow;
    };
    static LoopContext ctx;
    ctx.window = window;
    ctx.mainWindow = &mainWindow;
    g_wasmPasteTarget = &mainWindow;   // wire the browser-paste bridge (pom1_paste_text)

    emscripten_set_main_loop_arg([](void* arg) {
        LoopContext* c = static_cast<LoopContext*>(arg);
        // Vsync : une fois la boucle principale enregistrée (évite set_main_loop_timing trop tôt).
        if (static bool wasmVsync = false; !wasmVsync) {
            wasmVsync = true;
            glfwMakeContextCurrent(c->window);
            glfwSwapInterval(1);
        }
        glfwPollEvents();

        // Taille canvas : en plein écran = taille CSS du canvas (viewport
        // navigateur) ; hors plein écran = viewport, relevé au besoin du profil
        // (voir le bloc du bas).
        int targetW = 0;
        int targetH = 0;
        if (c->mainWindow->isWasmFullscreen()) {
            double cssW = 0.0;
            double cssH = 0.0;
            emscripten_get_element_css_size("#canvas", &cssW, &cssH);
            targetW = (int)cssW;
            targetH = (int)cssH;
            if (targetW < 1) {
                targetW = 1200;
            }
            if (targetH < 1) {
                targetH = 800;
            }
        } else {
            // Hors plein écran : PRENDRE TOUT LE VIEWPORT, puis garantir au
            // minimum ce que le profil réclame.
            //
            // Le canvas partait autrefois à la taille calculée par le profil
            // (écran Apple-1 + enveloppe des fenêtres) : plus petit que l'onglet,
            // il laissait des bandes noires alors que la place était libre ;
            // plus grand, il était réduit par le CSS. On prend donc le max par
            // axe entre le viewport et le besoin du profil.
            //
            // Quand le résultat dépasse l'onglet, c'est le CSS de shell.html
            // (max-width/max-height: 100vw/100vh sur un élément remplacé) qui
            // réduit l'image en conservant le rapport — l'UI n'est pas
            // reflowée, elle est mise à l'échelle, ce qui est le comportement
            // voulu : un profil large reste lisible en miniature plutôt que de
            // voir ses fenêtres sortir du cadre.
            //
            // clientWidth/Height du documentElement = viewport hors barres de
            // défilement (innerWidth les inclut et ferait osciller la taille).
            const int vpW = MAIN_THREAD_EM_ASM_INT({
                return document.documentElement.clientWidth | 0;
            });
            const int vpH = MAIN_THREAD_EM_ASM_INT({
                return document.documentElement.clientHeight | 0;
            });
            int wantW = 0;
            int wantH = 0;
            c->mainWindow->getWasmCanvasPixelSize(wantW, wantH);
            targetW = (vpW > wantW) ? vpW : wantW;
            targetH = (vpH > wantH) ? vpH : wantH;
            if (targetW < 1) {
                targetW = 1200;
            }
            if (targetH < 1) {
                targetH = 800;
            }
        }

        int bufW = 0;
        int bufH = 0;
        emscripten_get_canvas_element_size("#canvas", &bufW, &bufH);
        if (bufW != targetW || bufH != targetH) {
            emscripten_set_canvas_element_size("#canvas", targetW, targetH);
            glfwSetWindowSize(c->window, targetW, targetH);
        }

        pom1::renderer()->beginFrame();
        ImGui_ImplGlfw_NewFrame();

        ImGuiIO& io = ImGui::GetIO();
        int fbW = 0;
        int fbH = 0;
        glfwGetFramebufferSize(c->window, &fbW, &fbH);
        if (fbW < 1 || fbH < 1) {
            fbW = targetW;
            fbH = targetH;
        }
        io.DisplaySize = ImVec2((float)fbW, (float)fbH);

        ImGui::NewFrame();

        c->mainWindow->render();

        ImGui::Render();
        pom1::renderer()->clear(fbW, fbH, 0.45f, 0.55f, 0.60f, 1.00f);
        pom1::renderer()->renderDrawData(ImGui::GetDrawData());
        pom1::renderer()->present();

        // Signale au shell HTML que la 1ʳᵉ frame est réellement peinte (après le
        // swap, contrairement au clear de statut en début de boucle). Le shell
        // masque alors le splash « PLEASE WAIT » : la transition n'apparaît qu'une
        // fois l'écran Apple 1 visible, pas dès que le préchargement MEMFS finit.
        if (static bool firstFrameReadySignaled = false; !firstFrameReadySignaled) {
            firstFrameReadySignaled = true;
            emscripten_run_script(
                "if(globalThis.pom1FirstFrameReady){globalThis.pom1FirstFrameReady();}");
        }
    }, &ctx, 0, true);
    // emscripten_set_main_loop_arg never returns when simulate_infinite_loop=true
#else
    double lastUiRenderTime = 0.0;
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // Adaptive-UI throttle (P2-D, doc/PERF_VIEILLES_MACHINES_FR.md §9):
        // the EMULATION runs untouched on its own thread; only the ImGui
        // re-render is skipped. Full (vsync) rate while the user interacts,
        // during the boot window, or while MainWindow reports on-screen
        // motion; otherwise ~5 Hz. The 5 Hz floor is the safety net: any
        // animation the heuristic misses degrades to 5 fps for at most the
        // idle period, never freezes. Events stay polled every ~10 ms tick,
        // so input latency is one tick, not the idle period.
        {
            constexpr double kActiveAfterInputSec = 2.0;   // grace after any event
            constexpr double kBootFullRateSec     = 10.0;  // deferred CLI/plug window
            constexpr double kIdleFramePeriodSec  = 0.20;  // idle floor ≈ 5 Hz
            const double now = glfwGetTime();
            const bool uiActive =
                now < kBootFullRateSec
                || (now - g_lastActivityTime.load(std::memory_order_relaxed)) < kActiveAfterInputSec
                || mainWindow.wantsContinuousRender();
            if (!uiActive && (now - lastUiRenderTime) < kIdleFramePeriodSec) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            lastUiRenderTime = now;
        }

        // Start the Dear ImGui frame
        pom1::renderer()->beginFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Render application
        mainWindow.render();

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        pom1::renderer()->clear(display_w, display_h, 0.45f, 0.55f, 0.60f, 1.00f);
        pom1::renderer()->renderDrawData(ImGui::GetDrawData());

        // TEMP DEBUG: one-shot backbuffer dump when POM1_DEBUG_DUMP is set.
        if (const char* dp = getenv("POM1_DEBUG_DUMP")) {
            static int dbgFrame = 0;
            ++dbgFrame;
            if (dbgFrame % 60 == 0) fprintf(stderr, "[DBG] frame %d\n", dbgFrame);
            if (dbgFrame == 40) {
                int fbW = 0, fbH = 0; std::vector<uint8_t> buf;
                bool ok = pom1::renderer()->readBackbufferRGBA(fbW, fbH, buf);
                fprintf(stderr, "[DBG] readback ok=%d %dx%d bytes=%zu\n", ok, fbW, fbH, buf.size());
                if (ok) {
                    int rc = stbi_write_png(dp, fbW, fbH, 4, buf.data(), fbW * 4);
                    fprintf(stderr, "[DBG] png rc=%d -> %s\n", rc, dp);
                }
            }
        }

        if (auto* card = mainWindow.getEmulationController()
                ? mainWindow.getEmulationController()->getTerminalCardIfEnabled()
                : nullptr) {
            if (card->consumeScreenshotPending()) {
                capture_screenshot_to_png(*card);
            }
        }

        pom1::renderer()->present();
    }

    // Cleanup — save the active preset's ini + global UI settings BEFORE
    // DestroyContext() so ImGui still has its window-position state available.
    mainWindow.saveActivePresetLayoutNow();
    mainWindow.releaseGLResources();   // delete editor / hardware textures while ctx is live
    rendererOwned->shutdownImGuiBackend();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    // Drop the global pointer before the unique_ptr destroys the backend so
    // any late teardown call (assertions, debug builds) doesn't dereference
    // a stale renderer.
    pom1::setRenderer(nullptr);
    rendererOwned.reset();

    // Stop the signal handler from dereferencing a window we're about to free.
    g_signalWindow.store(nullptr, std::memory_order_release);
    glfwDestroyWindow(window);
    glfwTerminate();
#endif

    return 0;
}
