#include <iostream>
#include <GLFW/glfw3.h>
#include "POM1Build.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "CliDispatcher.h"
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
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb/stb_image_write.h"
#endif

#if POM1_IS_WASM
#include <emscripten.h>
#include <emscripten/html5.h>
#else
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
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
static GLFWwindow* g_signalWindow = nullptr;
static void pom1_signal_handler(int)
{
    if (g_signalWindow) glfwSetWindowShouldClose(g_signalWindow, 1);
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

/// Cherche fa-solid-900.ttf : le chemin relatif « ../fonts » dépend du répertoire de travail ;
/// sans ce fichier, ImGui affiche « ? » à la place des icônes Font Awesome.
static std::string find_fa_solid_font_path()
{
    namespace fs = std::filesystem;
    static const char kFile[] = "fa-solid-900.ttf";

    auto try_path = [](const fs::path& p) -> std::string {
        std::error_code ec;
        if (fs::is_regular_file(p, ec))
            return p.string();
        return {};
    };

    static const char* const rel_candidates[] = {
        "fonts/fa-solid-900.ttf",
        "../fonts/fa-solid-900.ttf",
        "../../fonts/fa-solid-900.ttf",
        "../../../fonts/fa-solid-900.ttf",
        "build/fonts/fa-solid-900.ttf",
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

/// Read the entire back-buffer with glReadPixels and write a top-down PNG
/// at `screenshots/pom1_latest.png`. Called from the render loop *after*
/// ImGui_ImplOpenGL3_RenderDrawData() and *before* glfwSwapBuffers() so the
/// framebuffer holds the fully-rendered frame (every visible window, not
/// just the active graphics card). Posts the absolute path back to
/// TerminalCard so the telnet client gets the resolved location.
static void capture_screenshot_to_png(int fbW, int fbH, TerminalCard& card)
{
    namespace fs = std::filesystem;
    const char* relPath = "screenshots/pom1_latest.png";

    if (fbW < 1 || fbH < 1) {
        card.setScreenshotResult("framebuffer size is zero", false);
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

    std::vector<uint8_t> buf(static_cast<size_t>(fbW) * fbH * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());

    // Y-flip in place: glReadPixels gives bottom-up, PNG wants top-down.
    const size_t rowBytes = static_cast<size_t>(fbW) * 4;
    std::vector<uint8_t> rowTmp(rowBytes);
    for (int y = 0; y < fbH / 2; ++y) {
        uint8_t* top = buf.data() + static_cast<size_t>(y) * rowBytes;
        uint8_t* bot = buf.data() + static_cast<size_t>(fbH - 1 - y) * rowBytes;
        std::memcpy(rowTmp.data(), top, rowBytes);
        std::memcpy(top, bot, rowBytes);
        std::memcpy(bot, rowTmp.data(), rowBytes);
    }

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
///   Bundle/Contents/Resources/{roms,fonts,software,pic,cassettes,sdcard,cfcard}
///       read-only bytes shipped with the app, signed + notarized-friendly.
///
///   ~/Library/Application Support/POM1/
///       {roms,fonts,software,pic,cassettes}  → symlinks into the bundle
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
        "roms", "fonts", "software", "pic", "cassettes"
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
    static constexpr const char* kWritableDirs[] = { "sdcard", "cfcard" };
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

int main(int argc, char* argv[])
{
    // Install the Tee(stream + ring) logger so every subsystem message lands
    // both in stdout/stderr and in the ring buffer the debug console reads.
    pom1::initDefaultTeeLogger();
    pom1::log().info("POM1", "v1.8.6 - Apple 1 Emulator (Dear ImGui)");

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

    // Default bundled cassette: preload cassettes/WOZ_talk.mp3 when --tape
    // was not supplied. Probes the same cwd-relative locations as the
    // in-app file browser (build/ vs repo-root launches). Not auto-played
    // — the user presses Play when they want Woz to speak.
    if (plan.initialTapePath.empty()) {
        const char* probes[] = {
            "cassettes/WOZ_talk.mp3",
            "../cassettes/WOZ_talk.mp3",
            "../../cassettes/WOZ_talk.mp3",
        };
        for (const char* p : probes) {
            if (std::filesystem::exists(p)) {
                plan.initialTapePath = p;
                break;
            }
        }
        // Default bundled cassette loads silently; only --tape auto-plays.
        plan.initialTapeAutoPlay = false;
    }

    // --save-tape-format: append .aci/.wav only if the path has no extension.
    if (!plan.saveTapePath.empty()) {
        plan.saveTapePath = pom1::resolveSaveTapePath(plan.saveTapePath, plan.saveTapeFormat);
    }

    // Setup GLFW
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return -1;

    // OpenGL / GLSL context hints
#if POM1_IS_WASM
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
    GLFWwindow* window = glfwCreateWindow(1274, 801, "POM1 v1.8.6 - Apple 1 Emulator", NULL, NULL);
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

    glfwMakeContextCurrent(window);
#if !POM1_IS_WASM
    glfwSwapInterval(1); // Vsync (desktop)
#else
    // Sur Emscripten, glfwSwapInterval avant emscripten_set_main_loop_* provoque :
    // « emscripten_set_main_loop_timing: ... main loop does not exist ».
    // On applique l’intervalle dans le premier tick de la boucle (ci-dessous).
#endif

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    // NE PAS activer NavEnableKeyboard - le clavier est pour l'Apple 1, pas pour ImGui!
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Disponible seulement dans la branche docking
    // Disable ImGui's automatic imgui.ini load/save. POM1 manages per-preset
    // ini files under ini/imgui_preset_NN.ini manually via
    // MainWindow_ImGui::savePresetLayout / loadPresetLayout — called on
    // every applyMachineConfig and on clean shutdown.
    io.IniFilename = nullptr;
#if !POM1_IS_WASM
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
#endif

    // Charger les polices
    ImFontConfig fontConfig;
    fontConfig.SizePixels = 15.0f;
    ImFont* defaultFont = io.Fonts->AddFontDefault(&fontConfig);
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

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Create main application
    MainWindow_ImGui mainWindow;
    if (plan.presetIndex >= 0)
        mainWindow.setDefaultPresetIndex(plan.presetIndex);
    if (plan.terminalOverride)
        mainWindow.setTerminalCardOverride(true);
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
    if (!plan.deferredActions.empty())
        mainWindow.setDeferredCliActions(std::move(plan.deferredActions));
    mainWindow.setWindow(window);

#if !POM1_IS_WASM
    // Route SIGINT/SIGTERM into a "please close the window" request so the
    // destructor path (→ --save-tape dump) runs instead of std::terminate'ing
    // the process mid-flight.
    g_signalWindow = window;
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

    emscripten_set_main_loop_arg([](void* arg) {
        LoopContext* c = static_cast<LoopContext*>(arg);
        // Vsync : une fois la boucle principale enregistrée (évite set_main_loop_timing trop tôt).
        if (static bool wasmVsync = false; !wasmVsync) {
            wasmVsync = true;
            glfwMakeContextCurrent(c->window);
            glfwSwapInterval(1);
        }
        // Efface le message Emscripten du type « Getting the system running » / spinner dès la 1ʳᵉ frame.
        if (static bool clearedWasmStatus = false; !clearedWasmStatus) {
            clearedWasmStatus = true;
            emscripten_run_script(
                "if(typeof Module!=='undefined'&&Module.setStatus){Module.setStatus('');}"
                "var sp=document.getElementById('spinner');if(sp)sp.style.display='none';"
                "var st=document.getElementById('status');if(st)st.textContent='Ready';");
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

        ImGui_ImplOpenGL3_NewFrame();
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
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(c->window);
    }, &ctx, 0, true);
    // emscripten_set_main_loop_arg never returns when simulate_infinite_loop=true
#else
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Render application
        mainWindow.render();

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (auto* card = mainWindow.getEmulationController()
                ? mainWindow.getEmulationController()->getTerminalCardIfEnabled()
                : nullptr) {
            if (card->consumeScreenshotPending()) {
                capture_screenshot_to_png(display_w, display_h, *card);
            }
        }

        glfwSwapBuffers(window);
    }

    // Cleanup — save the active preset's ini BEFORE DestroyContext() so
    // ImGui still has its window-position state available to write.
    if (mainWindow.getActivePresetIndex() >= 0) {
        mainWindow.savePresetLayout(mainWindow.getActivePresetIndex());
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
#endif

    return 0;
} 