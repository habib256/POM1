#include "MainWindow_ImGui.h"
#include "POM1Build.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "IconsFontAwesome6.h"
#include <cmath>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <GLFW/glfw3.h>

#if POM1_IS_WASM
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

namespace {

// Taille fenêtre « Apple 1 Screen » ≈ raster + chrome ImGui (serré pour limiter l’espace vide)
constexpr float kApple1ImGuiWinPadW = 22.0f;
constexpr float kApple1ImGuiWinPadH = 46.0f;
/** Marge OS autour de la zone utile (barre menu, dock, autres panneaux ImGui). */
constexpr int kApple1GlfwExtraW = 22;
constexpr int kApple1GlfwExtraH = 42;

// Aligné sur renderToolbar / SetNextWindowPos « Apple 1 Screen » / renderStatusBar
constexpr float kToolbarBandHeight = 34.0f;
constexpr float kGapBelowToolbarBeforeApple1 = 5.0f;
constexpr float kStatusBarBandHeight = 25.0f;
/** Barre menu : GetFrameHeight() est parfois un peu bas (thème / police) ; évite de couper le bas sur WASM. */
constexpr float kMainMenuBarHeightExtra = 6.0f;
/** Bordures de fenêtre ImGui « Apple 1 Screen », arrondis, léger jeu. */
constexpr float kApple1WindowDecorationSlop = 14.0f;

/** Hauteur totale hors zone raster Apple 1 (menu + toolbar + trou + statut + marge). */
static float apple1LayoutVerticalChrome()
{
    return ImGui::GetFrameHeight() + kMainMenuBarHeightExtra + kToolbarBandHeight +
           kGapBelowToolbarBeforeApple1 + kStatusBarBandHeight + kApple1WindowDecorationSlop;
}

} // namespace

// Single source of truth: keyboard shortcuts with display labels
// Entries with action=nullptr are handled specially in handleGlfwKey
const MainWindow_ImGui::Shortcut MainWindow_ImGui::shortcuts[] = {
    { GLFW_KEY_O,  GLFW_MOD_CONTROL, "Ctrl+O",  &MainWindow_ImGui::loadMemory },
    { GLFW_KEY_S,  GLFW_MOD_CONTROL, "Ctrl+S",  &MainWindow_ImGui::saveMemory },
    { GLFW_KEY_V,  GLFW_MOD_CONTROL, "Ctrl+V",  &MainWindow_ImGui::pasteCode },
    { GLFW_KEY_Q,  GLFW_MOD_CONTROL, "Ctrl+Q",  &MainWindow_ImGui::quit },
    { GLFW_KEY_F5, GLFW_MOD_CONTROL, "Ctrl+F5", &MainWindow_ImGui::hardReset },
    { GLFW_KEY_F5, 0,                "F5",       &MainWindow_ImGui::reset },
    { GLFW_KEY_F6, 0,                "F6",       nullptr }, // toggle start/stop
    { GLFW_KEY_F7, 0,                "F7",       &MainWindow_ImGui::stepCpu },
    { GLFW_KEY_F1, 0,                "F1",       nullptr }, // toggle showMemoryViewer
    { GLFW_KEY_F2, 0,                "F2",       nullptr }, // toggle showMemoryMap
    { GLFW_KEY_F3, 0,                "F3",       nullptr }, // toggle showDebugger
};
const int MainWindow_ImGui::shortcutCount = sizeof(shortcuts) / sizeof(shortcuts[0]);

const char* MainWindow_ImGui::shortcutLabel(int key, int mods)
{
    for (int i = 0; i < shortcutCount; i++) {
        if (shortcuts[i].key == key && shortcuts[i].mods == mods)
            return shortcuts[i].label;
    }
    return nullptr;
}

MainWindow_ImGui::MainWindow_ImGui()
{
    createPom1();
    setStatusMessage("Ready", 0.0f);
}

MainWindow_ImGui::~MainWindow_ImGui()
{
    destroyPom1();
}

void MainWindow_ImGui::createPom1()
{
    std::cout << "Welcome to POM1 - Apple I Emulator" << std::endl;
    screen = std::make_unique<Screen_ImGui>();
    emulation = std::make_unique<EmulationController>(screen.get());
    memoryViewer = std::make_unique<MemoryViewer_ImGui>();
    memoryViewer->setWriteCallback([this](quint16 address, quint8 value) {
        emulation->writeMemory(address, value);
    });
    // Republie cpuRunning=true (le constructeur publie une fois avant runRequested.store(true)).
    emulation->startCpu();
    emulation->copySnapshot(uiSnapshot);

    // Démarrer le CPU automatiquement pour que le Woz Monitor fonctionne
    cpuRunning = true;
    stepMode = false;
    
    setStatusMessage("System initialized - WOZ Monitor loaded at 0xFF00 - CPU started", 3.0f);
}

void MainWindow_ImGui::destroyPom1()
{
    // Les unique_ptr se détruisent automatiquement
}

void MainWindow_ImGui::render()
{
    float deltaTime = ImGui::GetIO().DeltaTime;
    updateStatus(deltaTime);
    emulation->copySnapshot(uiSnapshot);
    cpuRunning = uiSnapshot.cpuRunning;
    memoryViewer->updateLiveMemory(uiSnapshot.memory);
    memoryViewer->setGraphicsCardEnabled(graphicsCardEnabled);

#if POM1_IS_WASM
    // Sync fullscreen flag with browser state (user may exit via Escape)
    EmscriptenFullscreenChangeEvent fsStatus;
    if (emscripten_get_fullscreen_status(&fsStatus) == EMSCRIPTEN_RESULT_SUCCESS) {
        fullscreen = fsStatus.isFullscreen;
    }
#endif

    // Gérer les entrées clavier
    handleKeyboardInput();

    // Fenêtre principale avec menu
    if (ImGui::BeginMainMenuBar()) {
        renderMenuBar();
        ImGui::EndMainMenuBar();
    }

    // Barre d'outils sous le menu
    renderToolbar();

    // Fenêtre de l'écran (centrale)
    // Au premier frame, dimensionner selon l'écran Apple 1 (40x24 * scale)
    static bool firstFrame = true;
    static bool wasFullscreen = false;
    static int fullscreenResizePending = 0;
    if (firstFrame) {
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
        ImVec2 charSize = ImGui::CalcTextSize("M");
        ImGui::PopFont();
        const ImVec2 cell = Screen_ImGui::computeApple1CellDimensions(charSize);
        float sw = cell.x * Screen_ImGui::kApple1Columns * screen->scale + kApple1ImGuiWinPadW;
        float sh = cell.y * Screen_ImGui::kApple1Rows * screen->scale + kApple1ImGuiWinPadH;
        const float toolbarBottom = ImGui::GetFrameHeight() + kToolbarBandHeight;
        ImGui::SetNextWindowPos(ImVec2(10, toolbarBottom + kGapBelowToolbarBeforeApple1));
        ImGui::SetNextWindowSize(ImVec2(sw, sh));
        // Redimensionner la fenêtre GLFW (WASM : la boucle main_imgui applique la taille au canvas)
#if !POM1_IS_WASM
        if (window) {
            int winW = (int)sw + kApple1GlfwExtraW;
            int winH = (int)std::ceil(sh + apple1LayoutVerticalChrome());
            glfwSetWindowSize(window, winW, winH);
        }
#endif
        firstFrame = false;
    }

    // Resize Apple 1 screen window on fullscreen transitions
    if (fullscreen != wasFullscreen) {
        ImGuiIO& fsio = ImGui::GetIO();
        const float toolbarBottom = ImGui::GetFrameHeight() + kToolbarBandHeight;
        if (fullscreen) {
            // Defer resize by 1 frame so GLFW has updated DisplaySize
            fullscreenResizePending = 2;
        } else {
            // Restore to default Apple 1 size
            ImGui::PushFont(fsio.Fonts->Fonts[0]);
            ImVec2 charSize = ImGui::CalcTextSize("M");
            ImGui::PopFont();
            const ImVec2 cell = Screen_ImGui::computeApple1CellDimensions(charSize);
            float sw = cell.x * Screen_ImGui::kApple1Columns * screen->scale + kApple1ImGuiWinPadW;
            float sh = cell.y * Screen_ImGui::kApple1Rows * screen->scale + kApple1ImGuiWinPadH;
            ImGui::SetNextWindowPos(ImVec2(10, toolbarBottom + kGapBelowToolbarBeforeApple1));
            ImGui::SetNextWindowSize(ImVec2(sw, sh));
        }
        wasFullscreen = fullscreen;
    }
    if (fullscreenResizePending > 0) {
        fullscreenResizePending--;
        if (fullscreenResizePending == 0) {
            ImGuiIO& fsio = ImGui::GetIO();
            const float toolbarBottom = ImGui::GetFrameHeight() + kToolbarBandHeight;
            ImGui::SetNextWindowPos(ImVec2(0, toolbarBottom));
            ImGui::SetNextWindowSize(ImVec2(fsio.DisplaySize.x, fsio.DisplaySize.y - toolbarBottom));
        }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 5.0f));
    ImGui::Begin("Apple 1 Screen");
    screen->render();
    ImGui::End();
    ImGui::PopStyleVar();

    // Visualiseur de mémoire
    if (showMemoryViewer) {
        ImGuiIO& mio = ImGui::GetIO();
        // Width: "0x0000 " (7ch) + 16*"00 " (48ch) + " " (1ch) + 16 ASCII (16ch) + margins ~= 82 chars
        float charW = ImGui::CalcTextSize("0").x;
        float memViewerW = charW * 82.0f + ImGui::GetStyle().WindowPadding.x * 2.0f;
        ImGui::SetNextWindowPos(ImVec2(mio.DisplaySize.x - memViewerW - 10, 30), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(memViewerW, mio.DisplaySize.y * 0.45f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Memory Viewer", &showMemoryViewer);
        memoryViewer->render();
        ImGui::End();
    }

    // Console de débogage
    if (showDebugger) {
        ImGuiIO& dio = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(dio.DisplaySize.x - 410, dio.DisplaySize.y * 0.48f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(400, dio.DisplaySize.y * 0.45f), ImGuiCond_FirstUseEver);
        renderDebugDialog();
    }

    // Carte mémoire
    if (showMemoryMap) renderMemoryMapWindow();

    // Dialogues
    if (showAbout) renderAboutDialog();
    if (showScreenConfig) renderScreenConfigDialog();
    if (showMemoryConfig) renderMemoryConfigDialog();
    if (showLoadDialog) renderLoadDialog();
    if (showLoadTapeDialog) renderLoadTapeDialog();
    if (showCassetteControl) renderCassetteControlWindow();
    if (showSaveDialog) renderSaveDialog();
    if (showSaveTapeDialog) renderSaveTapeDialog();
    if (graphicsCardEnabled && showGraphicsCard) renderGraphicsCardWindow();

    // Barre de statut
    renderStatusBar();

#if POM1_IS_WASM
    // Taille canvas Web : menu + toolbar + trou + fenêtre Apple 1 + barre de statut (+ marge).
    {
        ImGuiIO& wasmIo = ImGui::GetIO();
        if (fullscreen) {
            wasmCanvasPixelW = (int)wasmIo.DisplaySize.x;
            wasmCanvasPixelH = (int)wasmIo.DisplaySize.y;
        } else {
            ImGui::PushFont(wasmIo.Fonts->Fonts[0]);
            ImVec2 charSize = ImGui::CalcTextSize("M");
            ImGui::PopFont();
            const ImVec2 cell = Screen_ImGui::computeApple1CellDimensions(charSize);
            float sw = cell.x * Screen_ImGui::kApple1Columns * screen->scale + kApple1ImGuiWinPadW;
            float sh = cell.y * Screen_ImGui::kApple1Rows * screen->scale + kApple1ImGuiWinPadH;
            wasmCanvasPixelW = (int)sw + kApple1GlfwExtraW;
            wasmCanvasPixelH = (int)std::ceil(sh + apple1LayoutVerticalChrome());
        }
        if (wasmCanvasPixelW < 320) {
            wasmCanvasPixelW = 320;
        }
        if (wasmCanvasPixelH < 240) {
            wasmCanvasPixelH = 240;
        }
    }
#endif

    // Après tous les widgets (barre d’outils, menus, débogueur) pour que la vitesse
    // soit poussée vers l’émulation dès le clic, pas au frame suivant.
    updateCpuExecution(deltaTime);
}

void MainWindow_ImGui::renderMenuBar()
{
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Load Memory", shortcutLabel(GLFW_KEY_O, GLFW_MOD_CONTROL))) {
                loadMemory();
            }
            if (ImGui::MenuItem("Save Memory", shortcutLabel(GLFW_KEY_S, GLFW_MOD_CONTROL))) {
                saveMemory();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Load Tape")) {
                loadTape();
            }
            if (ImGui::MenuItem("Save Tape")) {
                saveTape();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Paste Code", shortcutLabel(GLFW_KEY_V, GLFW_MOD_CONTROL))) {
                pasteCode();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", shortcutLabel(GLFW_KEY_Q, GLFW_MOD_CONTROL))) {
                quit();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("CPU")) {
            if (cpuRunning) {
                if (ImGui::MenuItem("Stop", shortcutLabel(GLFW_KEY_F6))) {
                    stopCpu();
                }
            } else {
                if (ImGui::MenuItem("Start", shortcutLabel(GLFW_KEY_F6))) {
                    startCpu();
                }
            }
            if (ImGui::MenuItem("Step", shortcutLabel(GLFW_KEY_F7))) {
                stepCpu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Soft Reset", shortcutLabel(GLFW_KEY_F5))) {
                reset();
            }
            if (ImGui::MenuItem("Hard Reset", shortcutLabel(GLFW_KEY_F5, GLFW_MOD_CONTROL))) {
                hardReset();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Debug Console", shortcutLabel(GLFW_KEY_F3))) {
                debugCpu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Settings")) {
            if (ImGui::MenuItem("Display Options")) {
                configScreen();
            }
            if (ImGui::MenuItem("Memory Options")) {
                configMemory();
            }
            ImGui::Separator();
#if !POM1_IS_WASM
            ImGui::Text("CPU Speed:");
            if (ImGui::RadioButton("1MHz", executionSpeed == 16667)) {
                executionSpeed = 16667;
                emulation->setExecutionSpeedCyclesPerFrame(executionSpeed);
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("2MHz", executionSpeed == 33333)) {
                executionSpeed = 33333;
                emulation->setExecutionSpeedCyclesPerFrame(executionSpeed);
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Max", executionSpeed == 1000000)) {
                executionSpeed = 1000000;
                emulation->setExecutionSpeedCyclesPerFrame(executionSpeed);
            }
            ImGui::Separator();
#endif
            ImGui::Text("Terminal Speed (chars/sec):");
            static int termSpeed = 60;
            ImGui::SetNextItemWidth(150);
            if (ImGui::SliderInt("##termspeed", &termSpeed, 0, 2000, termSpeed == 0 ? "Max" : "%d c/s")) {
                emulation->setTerminalSpeed(termSpeed);
            }
            ImGui::Separator();
            ImGui::MenuItem("Memory Viewer", shortcutLabel(GLFW_KEY_F1), &showMemoryViewer);
            ImGui::MenuItem("Memory Map", shortcutLabel(GLFW_KEY_F2), &showMemoryMap);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Hardware")) {
            if (ImGui::MenuItem("Cassette Control")) {
                showCassetteControl = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("GEN2 Graphics Card", nullptr, &graphicsCardEnabled)) {
                if (graphicsCardEnabled) showGraphicsCard = true;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {
                about();
            }
            ImGui::EndMenu();
        }
}

void MainWindow_ImGui::renderToolbar()
{
    ImGuiIO& io = ImGui::GetIO();
    float menuBarHeight = ImGui::GetFrameHeight();
    float toolbarHeight = kToolbarBandHeight;

    ImGui::SetNextWindowPos(ImVec2(0, menuBarHeight));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, toolbarHeight));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
    if (ImGui::Begin("##Toolbar", nullptr, flags)) {

        ImVec4 activeColor(0.2f, 0.4f, 0.8f, 1.0f);
        ImVec2 btnSize(28, 24);
#if !POM1_IS_WASM
        const float mhzBtnPadX = ImGui::GetStyle().FramePadding.x * 2.0f;
        const float mhzBtnW =
            std::max(ImGui::CalcTextSize("1MHz").x, ImGui::CalcTextSize("2MHz").x) + mhzBtnPadX;
        const ImVec2 mhzBtnSize(mhzBtnW, 24.0f);
#endif

        // --- Chargement (premier) ---
        if (ImGui::Button(ICON_FA_FOLDER_OPEN, btnSize)) loadMemory();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Load (Ctrl+O)");

        ImGui::SameLine();
        if (showCassetteControl)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.8f, 1.0f));
        else
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_Button]);
        if (ImGui::Button(ICON_FA_TAPE, btnSize)) showCassetteControl = !showCassetteControl;
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cassette Control");

        ImGui::SameLine();
        if (graphicsCardEnabled && showGraphicsCard)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.8f, 1.0f));
        else if (!graphicsCardEnabled)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        else
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_Button]);
        if (ImGui::Button(ICON_FA_TV, btnSize)) {
            if (!graphicsCardEnabled) {
                graphicsCardEnabled = true;
                showGraphicsCard = true;
                // Load demo HGR image at $2000 if available
                std::string demoPath;
                for (const auto& dir : {"software/gen2", "../software/gen2", "../../software/gen2"}) {
                    std::string p = std::string(dir) + "/N001.HGR.BIN";
                    if (std::filesystem::exists(p)) { demoPath = p; break; }
                }
                if (!demoPath.empty()) {
                    std::string error;
                    emulation->loadBinaryToRam(demoPath, 0x2000, error);
                    setStatusMessage("GEN2 plugged - demo image loaded at $2000", 3.0f);
                } else {
                    setStatusMessage("GEN2 plugged", 2.0f);
                }
            } else {
                showGraphicsCard = !showGraphicsCard;
                if (!showGraphicsCard) {
                    graphicsCardEnabled = false;
                    setStatusMessage("GEN2 unplugged", 2.0f);
                }
            }
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(graphicsCardEnabled ? "GEN2 Color Output (click to unplug)" : "Plug GEN2 Graphics Card");
        }

        // --- Séparateur ---
        ImGui::SameLine(0, 12);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine(0, 12);

        // --- Contrôles CPU ---
        if (cpuRunning) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
            if (ImGui::Button(ICON_FA_STOP, btnSize)) stopCpu();
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop (F6)");
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.5f, 0.1f, 1.0f));
            if (ImGui::Button(ICON_FA_PLAY, btnSize)) startCpu();
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Run (F6)");
        }

        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FORWARD_STEP, btnSize)) stepCpu();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Step (F7)");

        // --- Séparateur ---
        ImGui::SameLine(0, 12);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine(0, 12);

        // --- Resets groupés ---
        if (ImGui::Button(ICON_FA_ARROW_ROTATE_LEFT, btnSize)) reset();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Soft Reset (F5)");

        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_POWER_OFF, btnSize)) hardReset();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hard Reset (Ctrl+F5)");

        // --- Séparateur ---
        ImGui::SameLine(0, 12);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine(0, 12);

#if !POM1_IS_WASM
        // --- Vitesse CPU (1MHz / 2MHz / Max) — masqué en WASM (rythme imposé par le navigateur)
        {
            bool is1M = (executionSpeed == 16667);
            if (is1M) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            if (ImGui::Button("1MHz", mhzBtnSize)) {
                executionSpeed = 16667;
                emulation->setExecutionSpeedCyclesPerFrame(executionSpeed);
            }
            if (is1M) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("1MHz (~16667 cycles/frame @ 60 Hz)");
        }
        ImGui::SameLine();
        {
            bool is2M = (executionSpeed == 33333);
            if (is2M) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            if (ImGui::Button("2MHz", mhzBtnSize)) {
                executionSpeed = 33333;
                emulation->setExecutionSpeedCyclesPerFrame(executionSpeed);
            }
            if (is2M) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("2MHz (~33333 cycles/frame @ 60 Hz)");
        }
        ImGui::SameLine();
        {
            bool isMax = (executionSpeed == 1000000);
            if (isMax) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            if (ImGui::Button("Max", btnSize)) {
                executionSpeed = 1000000;
                emulation->setExecutionSpeedCyclesPerFrame(executionSpeed);
            }
            if (isMax) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Max");
        }

        ImGui::SameLine(0, 12);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine(0, 12);
#endif

        // --- Fenêtres toggle ---
        {
            bool dbg = showDebugger;
            if (dbg) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            if (ImGui::Button(ICON_FA_BUG, btnSize)) showDebugger = !showDebugger;
            if (dbg) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Debug (F12)");
        }
        ImGui::SameLine();
        {
            bool mem = showMemoryViewer;
            if (mem) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            if (ImGui::Button(ICON_FA_MEMORY, btnSize)) showMemoryViewer = !showMemoryViewer;
            if (mem) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Memory");
        }
        ImGui::SameLine();
        {
            bool map = showMemoryMap;
            if (map) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            if (ImGui::Button(ICON_FA_MAP, btnSize)) showMemoryMap = !showMemoryMap;
            if (map) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Memory Map");
        }

        // --- Séparateur ---
        ImGui::SameLine(0, 12);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine(0, 12);

        // --- Character mode: Font Awesome apple (charmap) / font (host) ---
        {
            const bool charm = (screen->characterRenderMode == Screen_ImGui::CharacterRenderMode::Apple1Charmap);
            const char* charIcon = charm ? ICON_FA_APPLE_WHOLE : ICON_FA_FONT;
            if (charm) {
                ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            }
            if (ImGui::Button(charIcon, btnSize)) {
                screen->characterRenderMode = charm ? Screen_ImGui::CharacterRenderMode::HostAscii
                                                     : Screen_ImGui::CharacterRenderMode::Apple1Charmap;
            }
            if (charm) {
                ImGui::PopStyleColor();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    charm ? "Apple-1 charmap (bitmap ROM)\nClick — use host ASCII font"
                          : "Host ASCII font\nClick — use Apple-1 charmap");
            }
        }

        ImGui::SameLine(0, 6);

        // --- Monitor phosphor tint (color swatches, no text) ---
        {
            const ImVec2 swatchSize(22.0f, 22.0f);
            const ImVec4 borderSel(0.35f, 0.55f, 1.0f, 1.0f);
            const ImVec4 borderIdle(0.0f, 0.0f, 0.0f, 0.5f);

            auto swatch = [&](const char* id, bool selected, ImVec4 base, Screen_ImGui::MonitorMode mode,
                              const char* tip) {
                const ImVec4 hov(std::min(1.0f, base.x * 1.14f + 0.03f), std::min(1.0f, base.y * 1.14f + 0.03f),
                                 std::min(1.0f, base.z * 1.14f + 0.03f), 1.0f);
                const ImVec4 act(base.x * 0.82f, base.y * 0.82f, base.z * 0.82f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, base);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hov);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, act);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, selected ? 2.5f : 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Border, selected ? borderSel : borderIdle);
                if (ImGui::Button(id, swatchSize)) {
                    screen->monitorMode = mode;
                }
                ImGui::PopStyleColor(5);
                ImGui::PopStyleVar();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", tip);
                }
            };

            swatch("##phos_green", screen->monitorMode == Screen_ImGui::MonitorMode::Green,
                   ImVec4(0.12f, 0.78f, 0.28f, 1.0f), Screen_ImGui::MonitorMode::Green, "Green phosphor");
            ImGui::SameLine(0, 6);
            swatch("##phos_amber", screen->monitorMode == Screen_ImGui::MonitorMode::Amber,
                   ImVec4(0.98f, 0.58f, 0.12f, 1.0f), Screen_ImGui::MonitorMode::Amber, "Amber / brown phosphor");
            ImGui::SameLine(0, 6);
            swatch("##phos_mono", screen->monitorMode == Screen_ImGui::MonitorMode::Monochrome,
                   ImVec4(0.9f, 0.9f, 0.92f, 1.0f), Screen_ImGui::MonitorMode::Monochrome, "Monochrome (white)");
        }

        // --- About button aligned to the right ---
        float aboutBtnW = ImGui::CalcTextSize(ICON_FA_CIRCLE_INFO).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SameLine(io.DisplaySize.x - aboutBtnW - ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button(ICON_FA_CIRCLE_INFO, btnSize)) showAbout = !showAbout;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("About");
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void MainWindow_ImGui::renderStatusBar()
{
    // Barre de statut simple en bas de l'écran
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, io.DisplaySize.y - 25));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, 25));
    
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | 
                                   ImGuiWindowFlags_NoSavedSettings;
    
    if (ImGui::Begin("##StatusBar", nullptr, window_flags)) {
        // Côté gauche: message de statut
        ImGui::Text("%s", statusMessage.c_str());

        std::string cpuText = cpuRunning ? "RUNNING" : "STOPPED";
        std::string speedText;
        if (executionSpeed >= 1000000) {
            speedText = "| Max";
        } else {
            std::ostringstream oss;
            oss << "| " << std::fixed << std::setprecision(1)
                << (executionSpeed * 60.0f / 1000000.0f) << " MHz";
            speedText = oss.str();
        }
        std::ostringstream ramOss;
        ramOss << "| RAM: " << uiSnapshot.ramSizeKB << " KB";
        std::string ramText = ramOss.str();

        std::string tapeText;
        if (uiSnapshot.cassetteLoadedTape) {
            std::ostringstream oss;
            oss << "| TAPE: " << (uiSnapshot.cassettePlaybackActive ? "READ" : "READY")
                << " (" << uiSnapshot.cassetteLoadedTransitionCount << " tr)";
            tapeText = oss.str();
        } else if (uiSnapshot.cassetteRecordedTape) {
            std::ostringstream oss;
            oss << "| TAPE OUT: " << uiSnapshot.cassetteRecordedTransitionCount << " tr";
            tapeText = oss.str();
        } else {
            tapeText = "| TAPE: empty";
        }

        std::string audioText = !uiSnapshot.cassetteAudioAvailable ? "| AUDIO OFF" : "";
        std::string keyText;
        if (uiSnapshot.keyReady) {
            std::ostringstream oss;
            oss << "| KEY: '" << uiSnapshot.lastKey << "'";
            keyText = oss.str();
        }

        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        float rightWidth =
            ImGui::CalcTextSize(cpuText.c_str()).x +
            ImGui::CalcTextSize(speedText.c_str()).x +
            ImGui::CalcTextSize(ramText.c_str()).x +
            ImGui::CalcTextSize(tapeText.c_str()).x +
            (audioText.empty() ? 0.0f : ImGui::CalcTextSize(audioText.c_str()).x) +
            (keyText.empty() ? 0.0f : ImGui::CalcTextSize(keyText.c_str()).x) +
            spacing * 5.0f;

        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - rightWidth - 16.0f));

        if (cpuRunning) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", cpuText.c_str());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", cpuText.c_str());
        }

        ImGui::SameLine();
        ImGui::Text("%s", speedText.c_str());

        ImGui::SameLine();
        ImGui::Text("%s", ramText.c_str());

        ImGui::SameLine();
        if (uiSnapshot.cassetteLoadedTape) {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f),
                               "%s", tapeText.c_str());
        } else if (uiSnapshot.cassetteRecordedTape) {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f),
                               "%s", tapeText.c_str());
        } else {
            ImGui::Text("%s", tapeText.c_str());
        }

        if (!uiSnapshot.cassetteAudioAvailable) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", audioText.c_str());
        }

        // État du clavier
        if (uiSnapshot.keyReady) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                               "%s", keyText.c_str());
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::renderAboutDialog()
{
    ImGui::SetNextWindowSizeConstraints(ImVec2(380, 0), ImVec2(500, FLT_MAX));
    ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
    if (ImGui::Begin("About POM1", &showAbout, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("POM1 v1.2 - Apple 1 Emulator (Dear ImGui)");
        ImGui::Separator();

        ImGui::TextWrapped("Copyright (C) 2000-2026 GPL3");
        ImGui::Spacing();

        ImGui::TextWrapped("Written by:");
        ImGui::BulletText("Arnaud VERHILLE (2000-2026)");
        ImGui::SameLine();
        if (ImGui::SmallButton("gist974@gmail.com")) {
            // Ouvrir email
        }
        ImGui::BulletText("Upgraded by Ken WESSEN (21/2/2006)");
        ImGui::BulletText("MacOS Cocoa port by Joe CROBAK");
        ImGui::BulletText("Ported to C/SDL by John D. CORRADO (2006-2014)");
        ImGui::BulletText("Dear ImGui version by Arnaud VERHILLE (2026)");

        ImGui::Spacing();
        ImGui::TextWrapped("Thanks to:");
        ImGui::BulletText("Steve WOZNIAK & Steve JOBS");
        ImGui::BulletText("Vince BRIEL (Replica 1)");
        ImGui::BulletText("Lee DAVISON (Enhanced BASIC)");
        ImGui::BulletText("Achim BREIDENBACH (Sim6502)");
        ImGui::BulletText("Fabrice FRANCES (Java Microtan Emulator)");
        ImGui::BulletText("Uncle BERNIE (GEN2 Color Graphics Card)");
        ImGui::BulletText("Tom OWAD (Applefritter)");
        ImGui::BulletText("And All Apple 1 Community");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextWrapped("Emulation features:");
        ImGui::BulletText("MOS 6502 CPU with cycle-accurate timing");
        ImGui::BulletText("PIA 6821 with address aliasing ($D0Fx)");
        ImGui::BulletText("Apple Cassette Interface (ACI) with live audio");
        ImGui::BulletText("ACI live audio (44.1 kHz)");
        ImGui::BulletText("Uncle Bernie's GEN2 Color Graphics Card");
        ImGui::BulletText("All known Apple BASIC versions supported");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextWrapped("Resources:");
        ImGui::BulletText("https://apple1software.com/");
        ImGui::TextWrapped(
            "  Comprehensive archive of Apple 1 software,\n"
            "  hardware docs, and history. An invaluable\n"
            "  resource for the Apple 1 community.");
        ImGui::BulletText("https://applefritter.com/apple1/");
        ImGui::TextWrapped(
            "  The heart of the Apple 1 community.\n"
            "  Forums, technical discussions, and\n"
            "  decades of shared knowledge.");
    }
    ImGui::End();
}

void MainWindow_ImGui::renderGraphicsCardWindow()
{
    const float pixelScale = 2.0f;
    const float winW = GraphicsCard::kHiresWidth * pixelScale + 16;
    const float winH = GraphicsCard::kHiresHeight * pixelScale + 36;
    ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_FirstUseEver);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 255));
    if (ImGui::Begin("GEN2 Apple1 HGR Color Screen by Uncle Bernie", &showGraphicsCard)) {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Fill background black
        ImVec2 size(GraphicsCard::kHiresWidth * pixelScale,
                    GraphicsCard::kHiresHeight * pixelScale);
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                                IM_COL32(0, 0, 0, 255));

        graphicsCard.render(drawList, pos, pixelScale, uiSnapshot.memory.data());

        ImGui::Dummy(size);
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void MainWindow_ImGui::renderDebugDialog()
{
    ImGui::SetNextWindowSize(ImVec2(520, 520), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("CPU Debug Console", &showDebugger)) {
        ImGui::Text("6502 CPU Debugger");
        ImGui::Separator();
        
        // Informations sur les registres
        if (ImGui::CollapsingHeader("Registers", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Columns(2, "RegisterColumns");
            
            ImGui::Text("Program Counter (PC):");
            ImGui::NextColumn();
            ImGui::Text("0x%04X", uiSnapshot.programCounter);
            ImGui::NextColumn();
            
            ImGui::Text("Accumulator (A):");
            ImGui::NextColumn();
            ImGui::Text("0x%02X (%d)", uiSnapshot.accumulator, uiSnapshot.accumulator);
            ImGui::NextColumn();
            
            ImGui::Text("X Register:");
            ImGui::NextColumn();
            ImGui::Text("0x%02X (%d)", uiSnapshot.xRegister, uiSnapshot.xRegister);
            ImGui::NextColumn();
            
            ImGui::Text("Y Register:");
            ImGui::NextColumn();
            ImGui::Text("0x%02X (%d)", uiSnapshot.yRegister, uiSnapshot.yRegister);
            ImGui::NextColumn();
            
            ImGui::Text("Stack Pointer (SP):");
            ImGui::NextColumn();
            ImGui::Text("0x%02X", uiSnapshot.stackPointer);
            ImGui::NextColumn();
            
            ImGui::Text("Status Register:");
            ImGui::NextColumn();
            quint8 status = uiSnapshot.statusRegister;
            ImGui::Text("0x%02X [%c%c%c%c%c%c%c%c]", status,
                       (status & 0x80) ? 'N' : 'n',  // Negative
                       (status & 0x40) ? 'V' : 'v',  // Overflow
                       (status & 0x20) ? '1' : '0',  // Unused
                       (status & 0x10) ? 'B' : 'b',  // Break
                       (status & 0x08) ? 'D' : 'd',  // Decimal
                       (status & 0x04) ? 'I' : 'i',  // Interrupt disable
                       (status & 0x02) ? 'Z' : 'z',  // Zero
                       (status & 0x01) ? 'C' : 'c'); // Carry
            
            ImGui::Columns(1);
        }
        
        // Contrôles de débogage
        if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Button("Step")) {
                stepCpu();
            }
            ImGui::SameLine();

            if (cpuRunning) {
                if (ImGui::Button("Stop")) {
                    stopCpu();
                }
            } else {
                if (ImGui::Button("Start")) {
                    startCpu();
                }
            }
            ImGui::SameLine();
            
            if (ImGui::Button("Reset")) {
                stopCpu();
                emulation->hardReset();
                setStatusMessage("CPU reset", 2.0f);
            }
            
#if !POM1_IS_WASM
            ImGui::Spacing();
            ImGui::Text("Execution Speed:");
            ImGui::SliderInt("##Speed", &executionSpeed, 1, 10000, "%d cycles/frame");
#endif

            ImGui::Spacing();
            ImGui::Text("Status: %s", cpuRunning ? "RUNNING" : "STOPPED");
        }
        
        // Désassemblage de l'instruction courante
        if (ImGui::CollapsingHeader("Current Instruction", ImGuiTreeNodeFlags_DefaultOpen)) {
            quint16 pc = uiSnapshot.programCounter;
            int instrLen = 1;
            std::string disasm = disassemble(pc, instrLen);

            ImGui::Text("PC: $%04X", pc);
            // Show raw bytes
            std::stringstream rawBytes;
            for (int i = 0; i < instrLen; i++) {
                if (i > 0) rawBytes << " ";
                rawBytes << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                         << (int)uiSnapshot.memory[(pc + i) & 0xFFFF];
            }
            ImGui::Text("Bytes: %s", rawBytes.str().c_str());
            ImGui::Text("  %s", disasm.c_str());
        }
        
        // Pile
        if (ImGui::CollapsingHeader("Stack", ImGuiTreeNodeFlags_DefaultOpen)) {
            quint8 sp = uiSnapshot.stackPointer;
            ImGui::Text("Stack Pointer: 0x01%02X", sp);
            
            ImGui::Text("Top 8 stack bytes:");
            ImGui::Columns(2, "StackColumns");
            for (int i = 0; i < 8; i++) {
                quint16 addr = 0x0100 + ((sp + i + 1) & 0xFF);
                quint8 value = uiSnapshot.memory[addr];
                ImGui::Text("0x01%02X:", (sp + i + 1) & 0xFF);
                ImGui::NextColumn();
                ImGui::Text("0x%02X", value);
                ImGui::NextColumn();
            }
            ImGui::Columns(1);
        }
        
        // Console de log
        if (ImGui::CollapsingHeader("Log Console")) {
            ImGui::BeginChild("DebugConsole", ImVec2(0, 200), true);
            
            // Afficher les dernières opérations
            static std::vector<std::string> debugLog;
            static int lastPC = -1;
            
            // Log des changements de PC
            quint16 currentPC = uiSnapshot.programCounter;
            if (currentPC != lastPC && cpuRunning) {
                std::stringstream ss;
                ss << "PC: 0x" << std::hex << std::uppercase << currentPC 
                   << " - Opcode: 0x" << std::setfill('0') << std::setw(2) 
                   << static_cast<int>(uiSnapshot.memory[currentPC]);
                debugLog.push_back(ss.str());
                
                // Garder seulement les 50 dernières entrées
                if (debugLog.size() > 50) {
                    debugLog.erase(debugLog.begin());
                }
                lastPC = currentPC;
            }
            
            // Afficher le log
            for (const auto& entry : debugLog) {
                ImGui::Text("%s", entry.c_str());
            }
            
            // Auto-scroll vers le bas
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
            
            ImGui::EndChild();
            
            if (ImGui::Button("Clear Log")) {
                debugLog.clear();
            }
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::renderScreenConfigDialog()
{
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Display Settings", &showScreenConfig)) {
        ImGui::Text("Display Options");
        ImGui::Separator();

        int renderMode = static_cast<int>(screen->characterRenderMode);
        ImGui::Text("Character Rendering:");
        ImGui::RadioButton("Apple-1 Charmap", &renderMode, static_cast<int>(Screen_ImGui::CharacterRenderMode::Apple1Charmap));
        ImGui::SameLine();
        ImGui::RadioButton("ASCII Host", &renderMode, static_cast<int>(Screen_ImGui::CharacterRenderMode::HostAscii));
        screen->characterRenderMode = static_cast<Screen_ImGui::CharacterRenderMode>(renderMode);
        if (screen->characterRenderMode == Screen_ImGui::CharacterRenderMode::HostAscii) {
            ImGui::Indent();
            ImGui::SliderFloat("Host ASCII character size", &screen->hostAsciiGlyphScale, 1.0f, 2.0f, "%.2f×");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Larger than 1.0 uses more of each cell; may touch neighbors slightly.");
            }
            ImGui::Unindent();
        }

        int monitorMode = static_cast<int>(screen->monitorMode);
        ImGui::Spacing();
        ImGui::Text("Monitor Tint:");
        ImGui::RadioButton("Green", &monitorMode, static_cast<int>(Screen_ImGui::MonitorMode::Green));
        ImGui::SameLine();
        ImGui::RadioButton("Brown", &monitorMode, static_cast<int>(Screen_ImGui::MonitorMode::Amber));
        ImGui::SameLine();
        ImGui::RadioButton("Monochrome", &monitorMode, static_cast<int>(Screen_ImGui::MonitorMode::Monochrome));
        screen->monitorMode = static_cast<Screen_ImGui::MonitorMode>(monitorMode);
        ImGui::Checkbox("Cursor", &screen->showCursor);

        ImGui::Spacing();
        ImGui::Text("Display Scale:");
        ImGui::SliderFloat("##Scale", &screen->scale, 0.5f, 4.0f, "%.1fx");

        ImGui::Spacing();
        ImGui::Text("Image Adjustments:");
        ImGui::SliderFloat("Brightness", &screen->brightness, 0.2f, 1.5f, "%.2f");
        ImGui::SliderFloat("Contrast", &screen->contrast, 0.5f, 2.0f, "%.2f");

        ImGui::Spacing();
        ImGui::Text("CRT Effect");
        ImGui::Separator();
        ImGui::Checkbox("Scanlines", &screen->crtEffect);
        if (screen->crtEffect) {
            ImGui::SliderFloat("Scanline Intensity", &screen->crtScanlineAlpha, 0.0f, 0.9f, "%.2f");
        }

        ImGui::Spacing();
        if (ImGui::Checkbox("Fullscreen", &fullscreen)) {
#if POM1_IS_WASM
            if (fullscreen) {
                EmscriptenFullscreenStrategy strategy{};
                strategy.scaleMode = EMSCRIPTEN_FULLSCREEN_SCALE_STRETCH;
                strategy.canvasResolutionScaleMode = EMSCRIPTEN_FULLSCREEN_CANVAS_SCALE_HIDEF;
                strategy.filteringMode = EMSCRIPTEN_FULLSCREEN_FILTERING_DEFAULT;
                emscripten_request_fullscreen_strategy("#canvas", true, &strategy);
            } else {
                emscripten_exit_fullscreen();
            }
#else
            if (window) {
                if (fullscreen) {
                    glfwGetWindowPos(window, &windowedPosX, &windowedPosY);
                    glfwGetWindowSize(window, &windowedWidth, &windowedHeight);
                    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
                    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                    glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
                } else {
                    glfwSetWindowMonitor(window, nullptr, windowedPosX, windowedPosY, windowedWidth, windowedHeight, 0);
                }
            }
#endif
        }

        ImGui::Spacing();
        if (ImGui::Button("Close")) {
            showScreenConfig = false;
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::renderMemoryConfigDialog()
{
    ImGui::SetNextWindowSize(ImVec2(450, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Memory Settings", &showMemoryConfig)) {
        bool writeProtect = !uiSnapshot.writeInRom;

        ImGui::Text("ROM Protection");
        ImGui::Separator();
        if (ImGui::Checkbox("Write-protect ROMs", &writeProtect)) {
            emulation->setWriteInRom(!writeProtect);
        }

        ImGui::Spacing();
        ImGui::Text("ROM Loading");
        ImGui::Separator();

        if (ImGui::Button("Reload BASIC")) {
            std::string error;
            bool ok = emulation->reloadBasic(error);
            if (!writeProtect) {
                emulation->setWriteInRom(true);
            }
            setStatusMessage(ok ? "BASIC reloaded" : error, 3.0f);
        }

        if (ImGui::Button("Reload WOZ Monitor")) {
            std::string error;
            bool ok = emulation->reloadWozMonitor(error);
            if (!writeProtect) {
                emulation->setWriteInRom(true);
            }
            setStatusMessage(ok ? "WOZ Monitor reloaded" : error, 3.0f);
        }

        if (ImGui::Button("Reload Krusader")) {
            std::string error;
            bool ok = emulation->reloadKrusader(error);
            if (!writeProtect) {
                emulation->setWriteInRom(true);
            }
            setStatusMessage(ok ? "Krusader reloaded" : error, 3.0f);
        }

        if (ImGui::Button("Reload ACI ROM")) {
            std::string error;
            bool ok = emulation->reloadAciRom(error);
            if (!writeProtect) {
                emulation->setWriteInRom(true);
            }
            setStatusMessage(ok ? "ACI ROM reloaded" : error, 3.0f);
        }

        ImGui::Spacing();
        ImGui::Text("Memory");
        ImGui::Separator();

        if (ImGui::Button("Clear All Memory")) {
            ImGui::OpenPopup("Confirm##ClearMemory");
        }

        if (ImGui::BeginPopupModal("Confirm##ClearMemory", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Are you sure you want to clear all memory?");
            ImGui::Separator();

            if (ImGui::Button("Yes", ImVec2(120, 0))) {
                emulation->clearMemory();
                setStatusMessage("Memory cleared", 2.0f);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("No", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (ImGui::Button("Refresh Viewer")) {
            setStatusMessage("Viewer refreshed", 2.0f);
        }

        ImGui::Spacing();
        if (ImGui::Button("Close")) {
            showMemoryConfig = false;
        }
    }
    ImGui::End();
}

// Implémentation des actions
void MainWindow_ImGui::loadMemory()
{
    loadDlg.reset();
    showLoadDialog = true;
}

void MainWindow_ImGui::renderLoadDialog()
{
    ImGui::SetNextWindowSize(ImVec2(550, 450), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Load Program", &showLoadDialog)) {

        if (!loadDlg.filesScanned) {
            if (loadDlg.softAsmRoot.empty()) {
                std::string dirs[] = {"software", "../software", "../../software"};
                for (const auto& d : dirs) {
                    if (std::filesystem::is_directory(d)) {
                        loadDlg.softAsmRoot = std::filesystem::canonical(d).string();
                        loadDlg.currentDir = loadDlg.softAsmRoot;
                        break;
                    }
                }
            }
            loadDlg.dirList.clear();
            loadDlg.fileList.clear();
            if (!loadDlg.currentDir.empty() && std::filesystem::is_directory(loadDlg.currentDir)) {
                for (const auto& entry : std::filesystem::directory_iterator(loadDlg.currentDir)) {
                    if (entry.is_directory()) {
                        std::string name = entry.path().filename().string();
                        if (name[0] != '.')
                            loadDlg.dirList.push_back(name);
                    } else if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        if (ext == ".txt")
                            loadDlg.fileList.push_back(entry.path().filename().string());
                    }
                }
                std::sort(loadDlg.dirList.begin(), loadDlg.dirList.end());
                std::sort(loadDlg.fileList.begin(), loadDlg.fileList.end());
            }
            loadDlg.filesScanned = true;
        }

        {
            std::string displayPath = "software/";
            if (loadDlg.currentDir.size() > loadDlg.softAsmRoot.size())
                displayPath += loadDlg.currentDir.substr(loadDlg.softAsmRoot.size() + 1) + "/";
            ImGui::Text("%s", displayPath.c_str());
        }

        ImGui::BeginChild("FileList", ImVec2(-1, 220), true);

        if (loadDlg.currentDir != loadDlg.softAsmRoot) {
            if (ImGui::Selectable(".. /", false)) {
                loadDlg.currentDir = std::filesystem::path(loadDlg.currentDir).parent_path().string();
                loadDlg.filesScanned = false;
            }
        }

        for (const auto& d : loadDlg.dirList) {
            std::string label = d + "/";
            if (ImGui::Selectable(label.c_str(), false)) {
                loadDlg.currentDir = (std::filesystem::path(loadDlg.currentDir) / d).string();
                loadDlg.filesScanned = false;
            }
        }

        for (const auto& f : loadDlg.fileList) {
            if (ImGui::Selectable(f.c_str())) {
                std::string fullPath = (std::filesystem::path(loadDlg.currentDir) / f).string();
                strncpy(loadDlg.filePath, fullPath.c_str(), sizeof(loadDlg.filePath) - 1);
                loadDlg.filePath[sizeof(loadDlg.filePath) - 1] = '\0';
                if (f.size() > 4 && f.substr(f.size() - 4) == ".bin")
                    loadDlg.fileType = 0;
                else
                    loadDlg.fileType = 1;
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::Text("Selected file:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##filepath", loadDlg.filePath, sizeof(loadDlg.filePath));

        ImGui::RadioButton("Binary (.bin)", &loadDlg.fileType, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Hex dump (.txt)", &loadDlg.fileType, 1);

        if (loadDlg.fileType == 0) {
            ImGui::Text("Address (hex):");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80);
            ImGui::InputText("##address", loadDlg.addressStr, sizeof(loadDlg.addressStr),
                             ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
        }

        ImGui::Spacing();
        if (ImGui::Button("Load", ImVec2(120, 0))) {
            quint16 addr = 0;
            std::string error;
            bool ok = false;
            if (loadDlg.fileType == 0) {
                addr = (quint16)strtol(loadDlg.addressStr, nullptr, 16);
                ok = emulation->loadBinary(loadDlg.filePath, addr, error);
            } else {
                ok = emulation->loadHexDump(loadDlg.filePath, addr, error);
                snprintf(loadDlg.addressStr, sizeof(loadDlg.addressStr), "%04X", addr);
            }
            if (ok) {
                emulation->copySnapshot(uiSnapshot);
                cpuRunning = true;
                stepMode = false;
                std::stringstream ss;
                ss << "Program started at 0x" << std::hex << std::uppercase << addr;
                setStatusMessage(ss.str(), 3.0f);
                showLoadDialog = false;
                loadDlg.reset();
            } else {
                setStatusMessage(error.empty() ? "Error: unable to load file" : error, 3.0f);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            showLoadDialog = false;
            loadDlg.reset();
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::loadTape()
{
    showLoadTapeDialog = true;
}

void MainWindow_ImGui::renderLoadTapeDialog()
{
    ImGui::SetNextWindowSize(ImVec2(520, 220), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Load Tape", &showLoadTapeDialog)) {
        ImGui::TextWrapped("Load an Apple-1 cassette image. Supported formats: .aci (exact pulse dump) and .wav.");
        ImGui::Spacing();
        ImGui::Text("Tape file:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##loadtapefile", loadTapeDlg.filePath, sizeof(loadTapeDlg.filePath));

        if (uiSnapshot.cassetteLoadedTape) {
            ImGui::Spacing();
            ImGui::Text("Inserted tape: %s", uiSnapshot.cassetteLoadedTapePath.c_str());
            ImGui::Text("Transitions: %zu", uiSnapshot.cassetteLoadedTransitionCount);
        }

        ImGui::Spacing();
        if (ImGui::Button("Load Tape", ImVec2(120, 0))) {
            std::string error;
            if (emulation->loadTape(loadTapeDlg.filePath, error)) {
                emulation->copySnapshot(uiSnapshot);
                std::stringstream ss;
                ss << "Tape loaded: " << uiSnapshot.cassetteLoadedTransitionCount << " transitions";
                setStatusMessage(ss.str(), 3.0f);
                showLoadTapeDialog = false;
            } else {
                setStatusMessage(error, 3.0f);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Rewind", ImVec2(120, 0))) {
            emulation->rewindTape();
            emulation->copySnapshot(uiSnapshot);
            setStatusMessage("Tape rewound", 2.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            showLoadTapeDialog = false;
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::renderCassetteControlWindow()
{
    ImGui::SetNextWindowSize(ImVec2(460, 280), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Cassette Control", &showCassetteControl)) {
        auto renderStateBadge = [](const char* label, const ImVec4& color) {
            ImGui::TextColored(color, "%s", label);
        };

        ImGui::Text("Reader");
        ImGui::Separator();

        if (uiSnapshot.cassetteLoadedTape) {
            ImGui::TextWrapped("Inserted tape: %s", uiSnapshot.cassetteLoadedTapePath.c_str());
            ImGui::Text("Transitions: %zu", uiSnapshot.cassetteLoadedTransitionCount);
            ImGui::Text("State:");
            ImGui::SameLine();
            renderStateBadge(
                uiSnapshot.cassettePlaybackActive ? "READING" : "READY",
                uiSnapshot.cassettePlaybackActive ? ImVec4(0.95f, 0.75f, 0.25f, 1.0f)
                                                  : ImVec4(0.35f, 0.85f, 0.35f, 1.0f));
        } else {
            ImGui::Text("Inserted tape: none");
            ImGui::Text("State:");
            ImGui::SameLine();
            renderStateBadge("EMPTY", ImVec4(0.70f, 0.70f, 0.70f, 1.0f));
        }

        ImGui::Spacing();
        if (ImGui::Button("Load Tape", ImVec2(130, 0))) {
            showLoadTapeDialog = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Rewind", ImVec2(130, 0))) {
            emulation->rewindTape();
            emulation->copySnapshot(uiSnapshot);
            setStatusMessage("Tape rewound", 2.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Eject", ImVec2(130, 0))) {
            emulation->ejectTape();
            emulation->copySnapshot(uiSnapshot);
            setStatusMessage("Tape ejected", 2.0f);
        }

        ImGui::Spacing();
        ImGui::Text("Recorder");
        ImGui::Separator();
        ImGui::Text("Live audio mode:");
        bool stabilizedAudio = !uiSnapshot.cassetteHardwareAccurateLiveAudio;
        if (ImGui::RadioButton("Real-time stabilized", stabilizedAudio)) {
            emulation->setHardwareAccurateLiveAudio(false);
            emulation->copySnapshot(uiSnapshot);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Stable GUI audio at a fixed sample rate.");
        }
        bool hardwareAccurateAudio = uiSnapshot.cassetteHardwareAccurateLiveAudio;
        if (ImGui::RadioButton("Hardware faithful", hardwareAccurateAudio)) {
            emulation->setHardwareAccurateLiveAudio(true);
            emulation->copySnapshot(uiSnapshot);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Sound speed follows emulation speed like real hardware. Default at startup.");
        }
        ImGui::Spacing();
        ImGui::Text("Recorder state:");
        ImGui::SameLine();
        if (uiSnapshot.cassetteRecordedTransitionCount > 0) {
            renderStateBadge("RECORDED", ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
        } else {
            renderStateBadge("IDLE", ImVec4(0.70f, 0.70f, 0.70f, 1.0f));
        }
        ImGui::Text("Captured transitions: %zu", uiSnapshot.cassetteRecordedTransitionCount);
        ImGui::Text("Audio backend:");
        ImGui::SameLine();
        renderStateBadge(uiSnapshot.cassetteAudioAvailable ? "ACTIVE" : "UNAVAILABLE",
                         uiSnapshot.cassetteAudioAvailable ? ImVec4(0.35f, 0.85f, 0.35f, 1.0f)
                                                           : ImVec4(0.95f, 0.45f, 0.45f, 1.0f));
        ImGui::Text("Live queue: %.1f ms", uiSnapshot.cassetteQueuedAudioSeconds * 1000.0);

        ImGui::Spacing();
        if (ImGui::Button("Save Tape", ImVec2(130, 0))) {
            showSaveTapeDialog = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Capture", ImVec2(130, 0))) {
            emulation->clearTapeCapture();
            emulation->copySnapshot(uiSnapshot);
            setStatusMessage("Cassette capture cleared", 2.0f);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextWrapped("This window controls the Apple-1 cassette reader/recorder without changing the current audio rendering.");
    }
    ImGui::End();
}

void MainWindow_ImGui::saveMemory()
{
    showSaveDialog = true;
}

void MainWindow_ImGui::renderSaveDialog()
{
    ImGui::SetNextWindowSize(ImVec2(500, 320), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Save Memory", &showSaveDialog)) {
        static char filename[256] = "dump.txt";
        static char startStr[8] = "0000";
        static char endStr[8] = "0FFF";
        static int saveFormat = 1; // 0=binary, 1=hex dump

        ImGui::Text("Format:");
        ImGui::RadioButton("Binary (.bin)", &saveFormat, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Hex dump (.txt)", &saveFormat, 1);

        ImGui::Spacing();
        ImGui::Text("Filename:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##savefile", filename, sizeof(filename));

        ImGui::Spacing();
        ImGui::Text("Address range (hex):");
        ImGui::SetNextItemWidth(80);
        ImGui::InputText("##startaddr", startStr, sizeof(startStr),
                         ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
        ImGui::SameLine();
        ImGui::Text("-");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::InputText("##endaddr", endStr, sizeof(endStr),
                         ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);

        quint16 startAddr = (quint16)strtol(startStr, nullptr, 16);
        quint16 endAddr = (quint16)strtol(endStr, nullptr, 16);
        int size = (endAddr >= startAddr) ? (endAddr - startAddr + 1) : 0;
        ImGui::Text("Size: %d bytes (%d pages)", size, (size + 255) / 256);

        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(120, 0)) && size > 0) {
            // Build path in software directory
            std::string path = filename;
            std::string error;
            if (emulation->saveMemoryRange(path, startAddr, endAddr, saveFormat == 0, error)) {
                std::stringstream ss;
                ss << "Saved $" << std::hex << std::uppercase << startAddr
                   << "-$" << endAddr << " to " << path;
                setStatusMessage(ss.str(), 3.0f);
                showSaveDialog = false;
            } else {
                setStatusMessage(error.empty() ? "Error: unable to write file" : error, 3.0f);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            showSaveDialog = false;
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::saveTape()
{
    showSaveTapeDialog = true;
}

void MainWindow_ImGui::renderSaveTapeDialog()
{
    ImGui::SetNextWindowSize(ImVec2(520, 240), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Save Tape", &showSaveTapeDialog)) {
        ImGui::TextWrapped("Save the cassette signal captured from accesses to the ACI output flip-flop.");
        ImGui::Spacing();
        ImGui::Text("Output file:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##savetapefile", saveTapeDlg.filePath, sizeof(saveTapeDlg.filePath));

        ImGui::Spacing();
        ImGui::Text("Captured transitions: %zu", uiSnapshot.cassetteRecordedTransitionCount);
        ImGui::Text("Audio backend: %s", uiSnapshot.cassetteAudioAvailable ? "active" : "unavailable");

        ImGui::Spacing();
        if (ImGui::Button("Save Tape", ImVec2(120, 0))) {
            std::string error;
            if (emulation->saveTape(saveTapeDlg.filePath, error)) {
                emulation->copySnapshot(uiSnapshot);
                std::stringstream ss;
                ss << "Tape saved to " << saveTapeDlg.filePath;
                setStatusMessage(ss.str(), 3.0f);
                showSaveTapeDialog = false;
            } else {
                setStatusMessage(error, 3.0f);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Capture", ImVec2(120, 0))) {
            emulation->clearTapeCapture();
            emulation->copySnapshot(uiSnapshot);
            setStatusMessage("Cassette capture cleared", 2.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            showSaveTapeDialog = false;
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::pasteCode()
{
    const char* clipboard = glfwGetClipboardString(window);
    if (!clipboard || strlen(clipboard) == 0) {
        setStatusMessage("Clipboard is empty", 2.0f);
        return;
    }

    const char* p = clipboard;
    int charCount = 0;
    const int MAX_PASTE_CHARS = 4096;
    while (*p && charCount < MAX_PASTE_CHARS) {
        char c = *p;
        if (c == '\n') c = '\r';
        if (c == '\r' || (c >= 32 && c <= 126)) {
            emulation->queueKey(c);
            charCount++;
        }
        ++p;
    }
    std::stringstream ss;
    ss << "Pasted " << charCount << " characters";
    if (*p) ss << " (truncated at " << MAX_PASTE_CHARS << ")";
    setStatusMessage(ss.str(), 2.0f);
}

void MainWindow_ImGui::quit()
{
    glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void MainWindow_ImGui::reset()
{
    emulation->softReset();
    setStatusMessage("Soft reset done", 2.0f);
}

void MainWindow_ImGui::hardReset()
{
    emulation->hardReset();
    setStatusMessage("Hard reset done - Memory cleared", 2.0f);
}

void MainWindow_ImGui::debugCpu()
{
    showDebugger = !showDebugger;
}

void MainWindow_ImGui::configScreen()
{
    showScreenConfig = true;
}

void MainWindow_ImGui::configMemory()
{
    showMemoryConfig = true;
}

void MainWindow_ImGui::about()
{
    showAbout = true;
}

void MainWindow_ImGui::renderMemoryMapWindow()
{
    ImGui::SetNextWindowSize(ImVec2(880, 580), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Memory Map", &showMemoryMap)) {
        ImGui::End();
        return;
    }

    // Memory regions with colors
    struct MemRegion {
        quint16 start;
        quint16 end; // inclusive
        ImU32 color;
        const char* label;
    };

    std::vector<MemRegion> regions = {
        { 0x0000, 0x00FF, IM_COL32(100, 100, 255, 255), "Zero Page" },
        { 0x0100, 0x01FF, IM_COL32(255, 165,   0, 255), "Stack" },
        { 0x0200, 0x027F, IM_COL32(  0, 200, 255, 255), "Keyboard Buffer" },
    };
    if (graphicsCardEnabled) {
        regions.push_back({ 0x0280, 0x1FFF, IM_COL32( 80, 200,  80, 255), "User RAM" });
        regions.push_back({ 0x2000, 0x3FFF, IM_COL32(  0, 255, 200, 255), "GEN2 HGR Framebuffer" });
        regions.push_back({ 0x4000, 0x9FFF, IM_COL32( 80, 200,  80, 255), "User RAM" });
    } else {
        regions.push_back({ 0x0280, 0x9FFF, IM_COL32( 80, 200,  80, 255), "User RAM" });
    }
    std::vector<MemRegion> tail = {
        { 0xA000, 0xBFFF, IM_COL32(200,  80, 200, 255), "Krusader ROM" },
        { 0xC000, 0xC0FF, IM_COL32(255, 140,  80, 255), "ACI I/O" },
        { 0xC100, 0xC1FF, IM_COL32(255, 190,  80, 255), "ACI ROM" },
        { 0xC200, 0xCFFF, IM_COL32( 60,  60,  60, 255), "Unused" },
        { 0xD000, 0xD0FF, IM_COL32(255,  80,  80, 255), "I/O (KBD/DSP)" },
        { 0xD100, 0xDFFF, IM_COL32( 60,  60,  60, 255), "Unused" },
        { 0xE000, 0xEFFF, IM_COL32(255, 255,  80, 255), "BASIC ROM" },
        { 0xF000, 0xFEFF, IM_COL32( 60,  60,  60, 255), "Unused" },
        { 0xFF00, 0xFFFF, IM_COL32(  0, 200, 255, 255), "Woz Monitor ROM" },
    };
    regions.insert(regions.end(), tail.begin(), tail.end());
    int numRegions = static_cast<int>(regions.size());

    // Grille 2×2 : ligne du haut = carte | légende ; ligne du bas = I/O | ACI + vecteurs
    // (ACI et CPU vectors sous la ligne horizontale médiane de la fenêtre)
    const int COLS = 16;  // 16 columns x 16 rows = 256 pages = 64KB
    const int ROWS = 16;
    const float cellSize = 16.0f;
    const float spacing = 1.0f;
    const float gridW = COLS * (cellSize + spacing);
    const float gridH = ROWS * (cellSize + spacing);
    const float mapColW = gridW + 40.0f;

    const quint8* memPtr = uiSnapshot.memory.data();
    const quint16 pc = uiSnapshot.programCounter;
    const int pcPage = pc >> 8;
    const quint8 sp = uiSnapshot.stackPointer;
    const int spPage = 1; // stack is always page 1

    ImU32 unusedColor = IM_COL32(60, 60, 60, 255);

    ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("MemoryMapGrid", 2, tableFlags)) {
        ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthFixed, mapColW);
        ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch);

        // --- Ligne 0 : carte | légende ---
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Map (1 cell = 256 bytes):");
        ImGui::Spacing();

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

    for (int row = 0; row < ROWS; ++row) {
        for (int col = 0; col < COLS; ++col) {
            int page = row * COLS + col;
            quint16 addr = (quint16)(page << 8);

            // Find region color
            ImU32 baseColor = IM_COL32(40, 40, 40, 255);
            for (int r = 0; r < numRegions; ++r) {
                if (addr >= regions[r].start && addr <= regions[r].end) {
                    baseColor = regions[r].color;
                    break;
                }
            }

            // Check if page has non-zero data (for RAM regions, show activity)
            bool hasData = false;
            if (addr < 0xA000) { // Only check RAM area
                for (int b = 0; b < 256; ++b) {
                    if (memPtr[addr + b] != 0) {
                        hasData = true;
                        break;
                    }
                }
            }

            // Dim empty RAM pages
            ImU32 cellColor = baseColor;
            if (addr >= 0x0300 && addr <= 0x9FFF && !hasData) {
                // Darken unused RAM
                cellColor = IM_COL32(30, 60, 30, 255);
            }

            float x = origin.x + col * (cellSize + spacing);
            float y = origin.y + row * (cellSize + spacing);
            ImVec2 p0(x, y);
            ImVec2 p1(x + cellSize, y + cellSize);

            drawList->AddRectFilled(p0, p1, cellColor);

            // PC indicator: white border
            if (page == pcPage) {
                drawList->AddRect(p0, p1, IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);
            }
            // SP indicator: orange border
            if (page == spPage) {
                ImVec2 inner0(p0.x + 1, p0.y + 1);
                ImVec2 inner1(p1.x - 1, p1.y - 1);
                drawList->AddRect(inner0, inner1, IM_COL32(255, 165, 0, 255), 0.0f, 0, 1.0f);
            }

            // Tooltip on hover (only when this window is hovered)
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
                ImVec2 mousePos = ImGui::GetMousePos();
                if (mousePos.x >= p0.x && mousePos.x < p1.x &&
                    mousePos.y >= p0.y && mousePos.y < p1.y) {
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        showMemoryViewer = true;
                        memoryViewer->navigateToAddress(addr);
                    }
                    ImGui::BeginTooltip();
                    ImGui::Text("Page $%02X : $%04X-$%04X", page, addr, addr + 0xFF);
                    for (int r = 0; r < numRegions; ++r) {
                        if (addr >= regions[r].start && addr <= regions[r].end) {
                            ImGui::Text("%s", regions[r].label);
                            break;
                        }
                    }
                    if (page == pcPage) ImGui::Text("PC = $%04X", pc);
                    ImGui::EndTooltip();
                }
            }
        }
    }

    // Address labels on the right: each row = 4KB
    const float rightMargin = origin.x + gridW + 4.0f;
    for (int row = 0; row < ROWS; ++row) {
        float y = origin.y + row * (cellSize + spacing) + 2;
        int kb = row * 4;
        char label[16];
        snprintf(label, sizeof(label), "%dK", kb);
        drawList->AddText(ImVec2(rightMargin, y), IM_COL32(150, 150, 150, 255), label);
    }

        ImGui::Dummy(ImVec2(mapColW, gridH));
        ImGui::Text("PC = $%04X  SP = $01%02X", pc, sp);

        ImGui::TableSetColumnIndex(1);
        ImGui::Text("Legend:");
        ImGui::Separator();
        for (int i = 0; i < numRegions; ++i) {
            if (regions[i].color == unusedColor) continue;
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + 12, p.y + 12), regions[i].color);
            dl->AddRect(p, ImVec2(p.x + 12, p.y + 12), IM_COL32(180, 180, 180, 255));
            ImGui::Dummy(ImVec2(16, 14));
            ImGui::SameLine();
            ImGui::Text("$%04X-$%04X %s", regions[i].start, regions[i].end, regions[i].label);
        }
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + 12, p.y + 12), unusedColor);
            dl->AddRect(p, ImVec2(p.x + 12, p.y + 12), IM_COL32(180, 180, 180, 255));
            ImGui::Dummy(ImVec2(16, 14));
            ImGui::SameLine();
            ImGui::Text("Unused");
        }

        // --- Ligne 1 : I/O sous la carte | ACI + vecteurs sous la légende ---
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("I/O registers (PIA 6821):");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "  Apple Cassette Interface");
        ImGui::BulletText("$C000  OUT  - Tape output toggle");
        ImGui::BulletText("$C081  IN   - Tape input read");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "  Keyboard & Display");
        ImGui::BulletText("$D010  KBD   - Keyboard data");
        ImGui::BulletText("$D011  KBDCR - Keyboard control");
        ImGui::BulletText("$D012  DSP   - Display output");
        ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f),
            "  Aliases: $D0Fx = $D01x");

        ImGui::TableSetColumnIndex(1);
        ImGui::Text("CPU vectors:");
        ImGui::BulletText("$FFFA/B  NMI   -> $%04X",
            (int)uiSnapshot.memory[0xFFFA] | ((int)uiSnapshot.memory[0xFFFB] << 8));
        ImGui::BulletText("$FFFC/D  RESET -> $%04X",
            (int)uiSnapshot.memory[0xFFFC] | ((int)uiSnapshot.memory[0xFFFD] << 8));
        ImGui::BulletText("$FFFE/F  IRQ   -> $%04X",
            (int)uiSnapshot.memory[0xFFFE] | ((int)uiSnapshot.memory[0xFFFF] << 8));

        ImGui::EndTable();
    }

    ImGui::End();
}

void MainWindow_ImGui::setStatusMessage(const std::string& message, float duration)
{
    statusMessage = message;
    statusTimer = duration;
}

void MainWindow_ImGui::updateStatus(float deltaTime)
{
    if (statusTimer > 0.0f) {
        statusTimer -= deltaTime;
        if (statusTimer <= 0.0f) {
            statusMessage = "Ready";
        }
    }
}

void MainWindow_ImGui::updateCpuExecution(float deltaTime)
{
    emulation->setExecutionSpeedCyclesPerFrame(executionSpeed);
#if POM1_IS_WASM
    emulation->pumpEmulationMainThread(static_cast<double>(deltaTime));
#endif
}

void MainWindow_ImGui::startCpu()
{
    cpuRunning = true;
    stepMode = false;
    emulation->startCpu();
    setStatusMessage("CPU started - Running", 2.0f);
}

void MainWindow_ImGui::stopCpu()
{
    cpuRunning = false;
    emulation->stopCpu();
    setStatusMessage("CPU stopped", 2.0f);
}

void MainWindow_ImGui::stepCpu()
{
    // Arrêter l'exécution automatique et activer le mode pas à pas
    cpuRunning = false;
    stepMode = true;
    emulation->stepCpu();
    emulation->copySnapshot(uiSnapshot);
    
    std::stringstream ss;
    ss << "Step - PC: 0x" << std::hex << std::uppercase << uiSnapshot.programCounter;
    setStatusMessage(ss.str(), 2.0f);
}

void MainWindow_ImGui::handleKeyboardInput()
{
    ImGuiIO& io = ImGui::GetIO();

    // Ne pas envoyer les touches à l'Apple 1 quand un widget ImGui a le focus
    if (io.WantTextInput) return;

    for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
        ImWchar c = io.InputQueueCharacters[i];
        if (c >= 32 && c <= 126) {
            emulation->queueKey((char)c);
        } else if (c == '\r' || c == '\n') {
            emulation->queueKey('\r');
        } else if (c == '\b' || c == 127) {
            emulation->queueKey('\b');
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
        emulation->queueKey('\r');
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        emulation->queueKey('\b');
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        emulation->queueKey(27);
    }
}

void MainWindow_ImGui::handleGlfwChar(unsigned int codepoint)
{
    // Les caractères sont traités par handleKeyboardInput() via InputQueueCharacters.
    // Ne pas envoyer ici pour éviter les doublons vers l'Apple 1.
    (void)codepoint;
}

void MainWindow_ImGui::handleGlfwKey(int key, int scancode, int action, int mods)
{
    (void)scancode;
    if (action == GLFW_RELEASE) {
        return;
    }
    // GLFW ne renvoie qu'un seul PRESS au début ; les pas suivants pendant un maintien sont des REPEAT.
    if (action == GLFW_REPEAT && key != GLFW_KEY_F7) {
        return;
    }

    int activeMods = mods & (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT | GLFW_MOD_ALT | GLFW_MOD_SUPER);

    for (int i = 0; i < shortcutCount; i++) {
        if (shortcuts[i].key != key || shortcuts[i].mods != activeMods)
            continue;

        if (shortcuts[i].action) {
            (this->*shortcuts[i].action)();
        } else if (key == GLFW_KEY_F6) {
            cpuRunning ? stopCpu() : startCpu();
        } else if (key == GLFW_KEY_F1) {
            showMemoryViewer = !showMemoryViewer;
        } else if (key == GLFW_KEY_F2) {
            showMemoryMap = !showMemoryMap;
        } else if (key == GLFW_KEY_F3) {
            showDebugger = !showDebugger;
        }
        return;
    }
}

// 6502 addressing modes for disassembly formatting
enum AddrMode {
    AM_IMP, AM_IMM, AM_ZP, AM_ZPX, AM_ZPY,
    AM_ABS, AM_ABX, AM_ABY, AM_IND,
    AM_IZX, AM_IZY, AM_REL
};

struct OpcodeInfo {
    const char* mnemonic;
    AddrMode mode;
};

// Complete 6502 opcode table (256 entries)
static const OpcodeInfo opcodeInfo[256] = {
    {"BRK",AM_IMP}, {"ORA",AM_IZX}, {"???",AM_IMP}, {"???",AM_IMP},  // 00-03
    {"???",AM_IMP}, {"ORA",AM_ZP},  {"ASL",AM_ZP},  {"???",AM_IMP},  // 04-07
    {"PHP",AM_IMP}, {"ORA",AM_IMM}, {"ASL",AM_IMP}, {"???",AM_IMP},  // 08-0B
    {"???",AM_IMP}, {"ORA",AM_ABS}, {"ASL",AM_ABS}, {"???",AM_IMP},  // 0C-0F
    {"BPL",AM_REL}, {"ORA",AM_IZY}, {"???",AM_IMP}, {"???",AM_IMP},  // 10-13
    {"???",AM_IMP}, {"ORA",AM_ZPX}, {"ASL",AM_ZPX}, {"???",AM_IMP},  // 14-17
    {"CLC",AM_IMP}, {"ORA",AM_ABY}, {"???",AM_IMP}, {"???",AM_IMP},  // 18-1B
    {"???",AM_IMP}, {"ORA",AM_ABX}, {"ASL",AM_ABX}, {"???",AM_IMP},  // 1C-1F
    {"JSR",AM_ABS}, {"AND",AM_IZX}, {"???",AM_IMP}, {"???",AM_IMP},  // 20-23
    {"BIT",AM_ZP},  {"AND",AM_ZP},  {"ROL",AM_ZP},  {"???",AM_IMP},  // 24-27
    {"PLP",AM_IMP}, {"AND",AM_IMM}, {"ROL",AM_IMP}, {"???",AM_IMP},  // 28-2B
    {"BIT",AM_ABS}, {"AND",AM_ABS}, {"ROL",AM_ABS}, {"???",AM_IMP},  // 2C-2F
    {"BMI",AM_REL}, {"AND",AM_IZY}, {"???",AM_IMP}, {"???",AM_IMP},  // 30-33
    {"???",AM_IMP}, {"AND",AM_ZPX}, {"ROL",AM_ZPX}, {"???",AM_IMP},  // 34-37
    {"SEC",AM_IMP}, {"AND",AM_ABY}, {"???",AM_IMP}, {"???",AM_IMP},  // 38-3B
    {"???",AM_IMP}, {"AND",AM_ABX}, {"ROL",AM_ABX}, {"???",AM_IMP},  // 3C-3F
    {"RTI",AM_IMP}, {"EOR",AM_IZX}, {"???",AM_IMP}, {"???",AM_IMP},  // 40-43
    {"???",AM_IMP}, {"EOR",AM_ZP},  {"LSR",AM_ZP},  {"???",AM_IMP},  // 44-47
    {"PHA",AM_IMP}, {"EOR",AM_IMM}, {"LSR",AM_IMP}, {"???",AM_IMP},  // 48-4B
    {"JMP",AM_ABS}, {"EOR",AM_ABS}, {"LSR",AM_ABS}, {"???",AM_IMP},  // 4C-4F
    {"BVC",AM_REL}, {"EOR",AM_IZY}, {"???",AM_IMP}, {"???",AM_IMP},  // 50-53
    {"???",AM_IMP}, {"EOR",AM_ZPX}, {"LSR",AM_ZPX}, {"???",AM_IMP},  // 54-57
    {"CLI",AM_IMP}, {"EOR",AM_ABY}, {"???",AM_IMP}, {"???",AM_IMP},  // 58-5B
    {"???",AM_IMP}, {"EOR",AM_ABX}, {"LSR",AM_ABX}, {"???",AM_IMP},  // 5C-5F
    {"RTS",AM_IMP}, {"ADC",AM_IZX}, {"???",AM_IMP}, {"???",AM_IMP},  // 60-63
    {"???",AM_IMP}, {"ADC",AM_ZP},  {"ROR",AM_ZP},  {"???",AM_IMP},  // 64-67
    {"PLA",AM_IMP}, {"ADC",AM_IMM}, {"ROR",AM_IMP}, {"???",AM_IMP},  // 68-6B
    {"JMP",AM_IND}, {"ADC",AM_ABS}, {"ROR",AM_ABS}, {"???",AM_IMP},  // 6C-6F
    {"BVS",AM_REL}, {"ADC",AM_IZY}, {"???",AM_IMP}, {"???",AM_IMP},  // 70-73
    {"???",AM_IMP}, {"ADC",AM_ZPX}, {"ROR",AM_ZPX}, {"???",AM_IMP},  // 74-77
    {"SEI",AM_IMP}, {"ADC",AM_ABY}, {"???",AM_IMP}, {"???",AM_IMP},  // 78-7B
    {"???",AM_IMP}, {"ADC",AM_ABX}, {"ROR",AM_ABX}, {"???",AM_IMP},  // 7C-7F
    {"???",AM_IMP}, {"STA",AM_IZX}, {"???",AM_IMP}, {"???",AM_IMP},  // 80-83
    {"STY",AM_ZP},  {"STA",AM_ZP},  {"STX",AM_ZP},  {"???",AM_IMP},  // 84-87
    {"DEY",AM_IMP}, {"???",AM_IMP}, {"TXA",AM_IMP}, {"???",AM_IMP},  // 88-8B
    {"STY",AM_ABS}, {"STA",AM_ABS}, {"STX",AM_ABS}, {"???",AM_IMP},  // 8C-8F
    {"BCC",AM_REL}, {"STA",AM_IZY}, {"???",AM_IMP}, {"???",AM_IMP},  // 90-93
    {"STY",AM_ZPX}, {"STA",AM_ZPX}, {"STX",AM_ZPY}, {"???",AM_IMP},  // 94-97
    {"TYA",AM_IMP}, {"STA",AM_ABY}, {"TXS",AM_IMP}, {"???",AM_IMP},  // 98-9B
    {"???",AM_IMP}, {"STA",AM_ABX}, {"???",AM_IMP}, {"???",AM_IMP},  // 9C-9F
    {"LDY",AM_IMM}, {"LDA",AM_IZX}, {"LDX",AM_IMM}, {"???",AM_IMP},  // A0-A3
    {"LDY",AM_ZP},  {"LDA",AM_ZP},  {"LDX",AM_ZP},  {"???",AM_IMP},  // A4-A7
    {"TAY",AM_IMP}, {"LDA",AM_IMM}, {"TAX",AM_IMP}, {"???",AM_IMP},  // A8-AB
    {"LDY",AM_ABS}, {"LDA",AM_ABS}, {"LDX",AM_ABS}, {"???",AM_IMP},  // AC-AF
    {"BCS",AM_REL}, {"LDA",AM_IZY}, {"???",AM_IMP}, {"???",AM_IMP},  // B0-B3
    {"LDY",AM_ZPX}, {"LDA",AM_ZPX}, {"LDX",AM_ZPY}, {"???",AM_IMP},  // B4-B7
    {"CLV",AM_IMP}, {"LDA",AM_ABY}, {"TSX",AM_IMP}, {"???",AM_IMP},  // B8-BB
    {"LDY",AM_ABX}, {"LDA",AM_ABX}, {"LDX",AM_ABY}, {"???",AM_IMP},  // BC-BF
    {"CPY",AM_IMM}, {"CMP",AM_IZX}, {"???",AM_IMP}, {"???",AM_IMP},  // C0-C3
    {"CPY",AM_ZP},  {"CMP",AM_ZP},  {"DEC",AM_ZP},  {"???",AM_IMP},  // C4-C7
    {"INY",AM_IMP}, {"CMP",AM_IMM}, {"DEX",AM_IMP}, {"???",AM_IMP},  // C8-CB
    {"CPY",AM_ABS}, {"CMP",AM_ABS}, {"DEC",AM_ABS}, {"???",AM_IMP},  // CC-CF
    {"BNE",AM_REL}, {"CMP",AM_IZY}, {"???",AM_IMP}, {"???",AM_IMP},  // D0-D3
    {"???",AM_IMP}, {"CMP",AM_ZPX}, {"DEC",AM_ZPX}, {"???",AM_IMP},  // D4-D7
    {"CLD",AM_IMP}, {"CMP",AM_ABY}, {"???",AM_IMP}, {"???",AM_IMP},  // D8-DB
    {"???",AM_IMP}, {"CMP",AM_ABX}, {"DEC",AM_ABX}, {"???",AM_IMP},  // DC-DF
    {"CPX",AM_IMM}, {"SBC",AM_IZX}, {"???",AM_IMP}, {"???",AM_IMP},  // E0-E3
    {"CPX",AM_ZP},  {"SBC",AM_ZP},  {"INC",AM_ZP},  {"???",AM_IMP},  // E4-E7
    {"INX",AM_IMP}, {"SBC",AM_IMM}, {"NOP",AM_IMP}, {"???",AM_IMP},  // E8-EB
    {"CPX",AM_ABS}, {"SBC",AM_ABS}, {"INC",AM_ABS}, {"???",AM_IMP},  // EC-EF
    {"BEQ",AM_REL}, {"SBC",AM_IZY}, {"???",AM_IMP}, {"???",AM_IMP},  // F0-F3
    {"???",AM_IMP}, {"SBC",AM_ZPX}, {"INC",AM_ZPX}, {"???",AM_IMP},  // F4-F7
    {"SED",AM_IMP}, {"SBC",AM_ABY}, {"???",AM_IMP}, {"???",AM_IMP},  // F8-FB
    {"???",AM_IMP}, {"SBC",AM_ABX}, {"INC",AM_ABX}, {"???",AM_IMP},  // FC-FF
};

std::string MainWindow_ImGui::disassemble(quint16 pc, int& instrLen)
{
    quint8 opcode = uiSnapshot.memory[pc];
    const OpcodeInfo& info = opcodeInfo[opcode];
    quint8 lo = uiSnapshot.memory[(pc + 1) & 0xFFFF];
    quint8 hi = uiSnapshot.memory[(pc + 2) & 0xFFFF];
    char buf[32];

    switch (info.mode) {
    case AM_IMP:
        instrLen = 1;
        snprintf(buf, sizeof(buf), "%s", info.mnemonic);
        break;
    case AM_IMM:
        instrLen = 2;
        snprintf(buf, sizeof(buf), "%s #$%02X", info.mnemonic, lo);
        break;
    case AM_ZP:
        instrLen = 2;
        snprintf(buf, sizeof(buf), "%s $%02X", info.mnemonic, lo);
        break;
    case AM_ZPX:
        instrLen = 2;
        snprintf(buf, sizeof(buf), "%s $%02X,X", info.mnemonic, lo);
        break;
    case AM_ZPY:
        instrLen = 2;
        snprintf(buf, sizeof(buf), "%s $%02X,Y", info.mnemonic, lo);
        break;
    case AM_ABS:
        instrLen = 3;
        snprintf(buf, sizeof(buf), "%s $%04X", info.mnemonic, lo | (hi << 8));
        break;
    case AM_ABX:
        instrLen = 3;
        snprintf(buf, sizeof(buf), "%s $%04X,X", info.mnemonic, lo | (hi << 8));
        break;
    case AM_ABY:
        instrLen = 3;
        snprintf(buf, sizeof(buf), "%s $%04X,Y", info.mnemonic, lo | (hi << 8));
        break;
    case AM_IND:
        instrLen = 3;
        snprintf(buf, sizeof(buf), "%s ($%04X)", info.mnemonic, lo | (hi << 8));
        break;
    case AM_IZX:
        instrLen = 2;
        snprintf(buf, sizeof(buf), "%s ($%02X,X)", info.mnemonic, lo);
        break;
    case AM_IZY:
        instrLen = 2;
        snprintf(buf, sizeof(buf), "%s ($%02X),Y", info.mnemonic, lo);
        break;
    case AM_REL:
        instrLen = 2;
        {
            quint16 target = pc + 2 + (int8_t)lo;
            snprintf(buf, sizeof(buf), "%s $%04X", info.mnemonic, target);
        }
        break;
    }
    return buf;
} 