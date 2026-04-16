#include "MainWindow_ImGui.h"
#include "MainWindow_Internal.h"
#include "WiFiModem.h"
#include "TerminalCard.h"
#include "POM1Build.h"
#include "Disassembler6502.h"
#include "Logger.h"
#include "imgui.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

#if POM1_IS_WASM
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

namespace {

// Layout constants and helpers (drawing toolbar, monitor tint, viewport
// fitting, vertical chrome) live in MainWindow_Layout.cpp under
// pom1::mainwindow::detail. The using-directive below makes them callable
// unqualified from the renderXxx methods further down in this TU.
using namespace pom1::mainwindow::detail;

// MachineConfig / kMachinePresets / kMachinePresetCount moved to
// MainWindow_Presets.cpp. They remain accessible via the using-directive
// above (pom1::mainwindow::detail) to keep the menu/toolbar code unchanged.

} // namespace

// Single source of truth: keyboard shortcuts with display labels
// Entries with action=nullptr are handled specially in handleGlfwKey

MainWindow_ImGui::MainWindow_ImGui()
{
    createPom1();
    // Default ROMs (overridden on first frame by the default preset)
    loadedRoms.push_back({"Integer BASIC", 0xE000, 0xEFFF});
    loadedRoms.push_back({"Woz Monitor", 0xFF00, 0xFFFF});
    setStatusMessage("Ready", 0.0f);
}

MainWindow_ImGui::~MainWindow_ImGui()
{
    destroyPom1();
}

void MainWindow_ImGui::createPom1()
{
    pom1::log().info("POM1", "Welcome to POM1 - Apple I Emulator");
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
    if (tms9918Texture) {
        glDeleteTextures(1, &tms9918Texture);
        tms9918Texture = 0;
    }
    if (graphicsCardTexture) {
        glDeleteTextures(1, &graphicsCardTexture);
        graphicsCardTexture = 0;
    }
}

void MainWindow_ImGui::render()
{
    float deltaTime = ImGui::GetIO().DeltaTime;
    updateStatus(deltaTime);
    emulation->copySnapshot(uiSnapshot);
    cpuRunning = uiSnapshot.cpuRunning;
    // MemoryViewer setters are only consumed by render(), so don't bother
    // wiring them when the window is closed. The pointer hand-off in
    // updateLiveMemory() is cheap, but skipping the whole block keeps the
    // hot frame path tighter (and matches the fast-path mindset of the
    // perf passes — no work for invisible widgets).
    if (showMemoryViewer) {
        memoryViewer->updateLiveMemory(uiSnapshot.memory);
        memoryViewer->setGraphicsCardEnabled(graphicsCardEnabled);
        memoryViewer->setTMS9918Enabled(tms9918Enabled);
        memoryViewer->setSIDEnabled(sidEnabled);
        memoryViewer->setMicroSDEnabled(microSDEnabled);
        memoryViewer->setWiFiModemEnabled(wifiModemEnabled);
        memoryViewer->setTerminalCardEnabled(terminalCardEnabled);
    }
    if (showMemoryViewer) {
        std::vector<MemoryViewer_ImGui::RomRegion> mvRoms;
        mvRoms.reserve(loadedRoms.size());
        for (const auto& r : loadedRoms)
            mvRoms.push_back({r.start, r.end});
        memoryViewer->setLoadedRoms(mvRoms);
    }

#if POM1_IS_WASM
    // Sync fullscreen flag with browser state (user may exit via Escape)
    EmscriptenFullscreenChangeEvent fsStatus;
    if (emscripten_get_fullscreen_status(&fsStatus) == EMSCRIPTEN_RESULT_SUCCESS) {
        fullscreen = fsStatus.isFullscreen;
    }
#endif

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
        // Resize GLFW window to fit the Apple 1 screen
#if !POM1_IS_WASM
        if (window) {
            glfwSetWindowSize(window, (int)sw + kApple1GlfwExtraW,
                              (int)std::ceil(sh + apple1LayoutVerticalChrome()));
        }
#endif
        firstFrame = false;
        // Apply default preset now that ImGui is ready
        int idx = (defaultPresetIndex >= 0 && defaultPresetIndex < kMachinePresetCount)
                  ? defaultPresetIndex : (kMachinePresetCount - 1);
        applyMachineConfig(idx);
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
    applyPendingLayout("Apple 1 Screen");
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
    if (showHardwareReference) renderHardwareReferenceWindow();
    if (showScreenConfig) renderScreenConfigDialog();
    if (showMemoryConfig) renderMemoryConfigDialog();
    if (showLoadDialog) renderLoadDialog();
    if (showLoadTapeDialog) renderLoadTapeDialog();
    if (showCassetteControl) renderCassetteControlWindow();
    if (showSaveDialog) renderSaveDialog();
    if (showSaveTapeDialog) renderSaveTapeDialog();
    if (graphicsCardEnabled && showGraphicsCard) renderGraphicsCardWindow();
    if (tms9918Enabled && showTMS9918) renderTMS9918Window();
    if (wifiModemEnabled && showWiFiModem) renderWiFiModemWindow();
    if (terminalCardEnabled && showTerminalCard) renderTerminalCardWindow();
    if (a1ioRtcEnabled && showA1IO_RTC) renderA1IO_RTCWindow();

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






// Implémentation des actions

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
    if (screen) screen->resetDisplay(); // replay shift-register power-on pattern
    loadedPrograms.clear();
    loadedRoms.clear();
    loadedRoms.push_back({"Integer BASIC", 0xE000, 0xEFFF});
    loadedRoms.push_back({"Woz Monitor", 0xFF00, 0xFFFF});
    microSDEnabled = true;
#if !POM1_IS_WASM
    terminalCardEnabled = true;
    emulation->setTerminalCardEnabled(true);
    showTerminalCard = true;
#endif
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

// applyPendingLayout, applyMachineConfig, getPresetCount, getPresetName
// moved to MainWindow_Presets.cpp.


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


// 6502 disassembly moved to Disassembler6502.{h,cpp}. This wrapper keeps
// the existing MainWindow_ImGui::disassemble() entry point used by the
// debug UI (and avoids touching MainWindow_ImGui.h's public surface).
std::string MainWindow_ImGui::disassemble(quint16 pc, int& instrLen)
{
    return pom1::disassemble6502(uiSnapshot.memory.data(), pc, instrLen);
}
