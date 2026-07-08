#include <iostream>
#include <GLFW/glfw3.h>
#include "POM1Build.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
// imgui_impl_opengl3.h is no longer included here directly — the rendering
// backend is wired through PomRenderer (src/PomRenderer.h). PomRenderer_GL
// owns the ImGui_ImplOpenGL3_* lifecycle; PomRenderer_Metal (Phase 2) will
// pull in imgui_impl_metal instead.
#include "PomRenderer.h"
#include "CliDispatcher.h"
#include "X11ErrorGuard.h"
#include "MainWindow_ImGui.h"
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
    namespace fs = std::filesystem;
    static const char kFile[] = "icon.png";

    auto try_path = [](const fs::path& p) -> std::string {
        std::error_code ec;
        if (fs::is_regular_file(p, ec))
            return p.string();
        return {};
    };

    static const char* const rel_candidates[] = {
        "pic/icon.png",
        "../pic/icon.png",
        "../../pic/icon.png",
        "../../../pic/icon.png",
    };
    for (const char* r : rel_candidates) {
        std::string s = try_path(fs::path(r));
        if (!s.empty())
            return s;
    }

#if defined(_WIN32)
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        fs::path exeDir = fs::path(buf).parent_path();
        const fs::path next_to_exe[] = {
            exeDir / "pic" / kFile,
            exeDir.parent_path() / "pic" / kFile,
            exeDir.parent_path().parent_path() / "pic" / kFile,
        };
        for (const auto& p : next_to_exe) {
            std::string s = try_path(p);
            if (!s.empty())
                return s;
        }
    }
#endif
    return {};
}
#endif  // !defined(__APPLE__)

/// Cherche un fichier de police `kFile` (fa-solid-900.ttf, DejaVuSans.ttf, …) dans
/// les emplacements habituels : d'abord relatifs au répertoire de travail, puis à
/// côté de l'exécutable (Windows). Renvoie {} si introuvable.
static std::string find_font_path(const char* kFile)
{
    namespace fs = std::filesystem;

    auto try_path = [](const fs::path& p) -> std::string {
        std::error_code ec;
        if (fs::is_regular_file(p, ec))
            return p.string();
        return {};
    };

    static const char* const rel_dirs[] = {
        "fonts", "../fonts", "../../fonts", "../../../fonts", "build/fonts",
    };
    for (const char* d : rel_dirs) {
        std::string s = try_path(fs::path(d) / kFile);
        if (!s.empty())
            return s;
    }

#if defined(_WIN32)
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        fs::path exeDir = fs::path(buf).parent_path();
        const fs::path next_to_exe[] = {
            exeDir / "fonts" / kFile,
            exeDir.parent_path() / "fonts" / kFile,
            exeDir.parent_path().parent_path() / "fonts" / kFile,
        };
        for (const auto& p : next_to_exe) {
            std::string s = try_path(p);
            if (!s.empty())
                return s;
        }
    }
#endif
    return {};
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

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

static void glfw_char_callback(GLFWwindow* window, unsigned int codepoint)
{
    ImGui_ImplGlfw_CharCallback(window, codepoint);

    auto* mw = static_cast<MainWindow_ImGui*>(glfwGetWindowUserPointer(window));
    if (mw) {
        mw->handleGlfwChar(codepoint);
    }
}

static void glfw_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);

    // PRESS + REPEAT : le handler n’exécute les raccourcis sur REPEAT que pour F7 (step).
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        auto* mw = static_cast<MainWindow_ImGui*>(glfwGetWindowUserPointer(window));
        if (mw) {
            mw->handleGlfwKey(key, scancode, action, mods);
        }
    }
}

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

// Map a --enable/--disable card to its EmulationController facade (immediate,
// no GUI deferred plug). Cascades + mutex evictions live inside the setters.
static void applyHeadlessCardOverride(EmulationController& emu, pom1::CliCard card, bool on)
{
    using CC = pom1::CliCard;
    switch (card) {
        case CC::Aci:          emu.setACIEnabled(on); break;
        case CC::Sid:          emu.setSIDEnabled(on); break;
        case CC::SidSE:        emu.setSIDSpecialEditionEnabled(on); break;
        case CC::MicroSD:      emu.setMicroSDEnabled(on); break;
        case CC::Tms9918:      emu.setTMS9918Enabled(on); break;
        case CC::A1IoRtc:      emu.setA1IO_RTCEnabled(on); break;
        case CC::Hgr:          emu.setHgrFramebufferAttached(on); break;
        case CC::Cffa1:        emu.setCFFA1Enabled(on); break;
        case CC::WifiModem:    emu.setWiFiModemEnabled(on); break;
        case CC::TerminalCard: emu.setTerminalCardEnabled(on); break;
        case CC::JukeBox:      emu.setJukeBoxEnabled(on); break;
        case CC::CodeTank:     emu.setCodeTankEnabled(on); break;
        case CC::Pr40:         emu.setPR40Enabled(on); break;
        case CC::GT6144:       emu.setGT6144Enabled(on); break;
        case CC::IEC:          emu.setIECCardEnabled(on); break;
        case CC::Krusader:     { std::string e; if (on) emu.reloadKrusader(e); } break;
    }
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

    EmulationController emu(nullptr);   // null screen: the $D012 display sink is a no-op

    // Machine config: apply the preset (RAM + cards + BASIC ROM) immediately —
    // no GUI deferred plug — then explicit --enable/--disable overrides, then
    // the --terminal override. So `--headless --preset 11` plugs GEN2 + 48K for
    // an HGR game test, with no display.
    if (plan.presetIndex >= 0)
        MainWindow_ImGui::applyHeadlessConfig(emu, plan.presetIndex);
    for (const auto& o : plan.cardOverrides)
        applyHeadlessCardOverride(emu, o.card, o.enable);
    if (plan.terminalOverride)
        emu.setTerminalCardEnabled(true);
    // DRAM refresh stall override (the preset path leaves it off so 1:1-timed
    // demos stay exact). --dram-refresh arms the 4/65 CPU steal — the beam keeps
    // running, so beam-race code drifts as on real DRAM silicon.
    if (plan.dramRefreshOverride)
        emu.setDramRefreshEnabled(*plan.dramRefreshOverride);

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
    // the last scheduled cycle before dropping into the idle wait.
    if (!plan.timedPastes.empty()) {
        uint64_t maxPaste = 0;
        for (const auto& p : plan.timedPastes) maxPaste = std::max(maxPaste, p.cycle);
        runCyclesWithTimedPastes(emu, maxPaste, plan.timedPastes);
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
    // Install the Tee(stream + ring) logger so every subsystem message lands
    // both in stdout/stderr and in the ring buffer the debug console reads.
    pom1::initDefaultTeeLogger();
    pom1::log().info("POM1", "v1.9.3 - Apple 1 Emulator (Dear ImGui)");

#if !POM1_IS_WASM && defined(__APPLE__)
    pom1_macos_provision_user_data_dir();
#endif

    // Parse command-line arguments via the CLI dispatcher. The dispatcher
    // owns every verb — boot-time (preset, card overrides, cassette paths,
    // CPU speed) AND post-deferred-plug verbs (program load, --paste,
    // --rec, --sd-*, --rtc-freeze, etc.). `listPresetsSeen` is true when the
    // dispatcher printed the preset table; in that case main exits 0.
    bool listPresetsSeen = false;
    auto parsedPlan = pom1::parseCli(argc, argv, listPresetsSeen);
    if (listPresetsSeen) return 0;
    if (!parsedPlan) return 1;
    pom1::CliPlan plan = std::move(*parsedPlan);

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
    if (!glfwInit())
        return -1;

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
#elif POM1_IS_WASM
    // WebGL 2.0 = OpenGL ES 3.0 — GLSL ES 300
    const char* glsl_version = "#version 300 es";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
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
    GLFWwindow* window = glfwCreateWindow(1274, 801, "POM1 v1.9.3 - Apple 1 Emulator", NULL, NULL);
    if (window == NULL)
        return -1;

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
    // io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Disponible seulement dans la branche docking
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
    const char* fontPath = fontPathStorage.empty() ? "../fonts/fa-solid-900.ttf" : fontPathStorage.c_str();
#endif
    if (!io.Fonts->AddFontFromFileTTF(fontPath, 14.0f, &iconsConfig, iconsRanges)) {
        fprintf(stderr,
                "Warning: Could not load icon font (tried '%s') - toolbar/menu icons show as '?'\n"
                "  Install fonts next to the .exe (fonts\\fa-solid-900.ttf) or run from the repo with fonts/ present.\n",
                fontPath);
    }

    // HiDPI: on Linux (X11) and Windows, GLFW does not auto-scale the framebuffer,
    // so on a high-DPI monitor the UI font renders tiny (users had to poke
    // io.FontGlobalScale by hand). Seed it from the monitor's content scale
    // (glfwGetWindowContentScale, GLFW 3.3+). Skipped on macOS — Retina is handled
    // by io.DisplayFramebufferScale, so scaling here would double the size — and on
    // WASM, where the browser owns devicePixelRatio. Overridable at runtime in
    // Display Settings (auto toggle + manual slider).
#if !defined(__APPLE__) && !POM1_IS_WASM
    {
        float xs = 1.0f, ys = 1.0f;
        glfwGetWindowContentScale(window, &xs, &ys);
        if (xs > 1.01f) {
            io.FontGlobalScale = xs > 3.0f ? 3.0f : xs;
            fprintf(stderr, "[POM1] HiDPI: monitor content scale %.2f -> UI font scale %.2f\n",
                    xs, io.FontGlobalScale);
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

    // Create main application
    MainWindow_ImGui mainWindow;
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

        // Taille canvas : hors plein écran = celle calculée dans MainWindow (Apple 1 + chrome) ;
        // en plein écran = taille CSS du canvas (viewport navigateur).
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
            c->mainWindow->getWasmCanvasPixelSize(targetW, targetH);
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
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

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