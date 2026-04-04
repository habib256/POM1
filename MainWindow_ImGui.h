#ifndef MAINWINDOW_IMGUI_H
#define MAINWINDOW_IMGUI_H

#include <string>
#include <vector>
#include <memory>
#include <cstring>
#include <GLFW/glfw3.h>
#include "POM1Build.h"
#include "EmulationController.h"
#include "MemoryViewer_ImGui.h"
#include "Screen_ImGui.h"

class MainWindow_ImGui
{
public:
    MainWindow_ImGui();
    ~MainWindow_ImGui();

    void render();
    void setWindow(GLFWwindow* win) { window = win; }
    void handleGlfwChar(unsigned int codepoint);
    void handleGlfwKey(int key, int scancode, int action, int mods);

#if POM1_IS_WASM
    /** Taille framebuffer/canvas pour le prochain tour de boucle (hors plein écran navigateur). */
    void getWasmCanvasPixelSize(int& outW, int& outH) const
    {
        outW = wasmCanvasPixelW;
        outH = wasmCanvasPixelH;
    }
    bool isWasmFullscreen() const { return fullscreen; }
#endif

private:
    // Pom1 Apple I Hardware
    std::unique_ptr<EmulationController> emulation;
    std::unique_ptr<Screen_ImGui> screen;
    std::unique_ptr<MemoryViewer_ImGui> memoryViewer;
    EmulationSnapshot uiSnapshot;
    
    // Window reference for keyboard callbacks
    GLFWwindow* window = nullptr;

#if POM1_IS_WASM
    int wasmCanvasPixelW = 1200;
    int wasmCanvasPixelH = 800;
#endif

    // Interface state
    bool showMemoryViewer = false;
    bool showDebugger = false;
    bool showAbout = false;
    bool showScreenConfig = false;
    bool showMemoryConfig = false;
    bool showLoadDialog = false;
    bool showLoadTapeDialog = false;
    bool showCassetteControl = false;
    bool showMemoryMap = false;
    bool showSaveDialog = false;
    bool showSaveTapeDialog = false;
    bool fullscreen = false;
    int windowedWidth = 1200;
    int windowedHeight = 800;
    int windowedPosX = 100;
    int windowedPosY = 100;
    
    // CPU execution state
    bool cpuRunning = false;
    bool stepMode = false;
    int executionSpeed = 16667; // cycles par frame (~1MHz @ 60fps)
    
    // Status
    std::string statusMessage;
    float statusTimer = 0.0f;

    // Pom1 functions
    void createPom1();
    void destroyPom1();

    // Menu functions
    void renderMenuBar();
    void renderToolbar();
    void renderStatusBar();
    
    // Dialog functions
    void renderAboutDialog();
    void renderDebugDialog();
    void renderScreenConfigDialog();
    void renderMemoryConfigDialog();
    void renderLoadDialog();
    void renderLoadTapeDialog();
    void renderCassetteControlWindow();
    void renderMemoryMapWindow();
    void renderSaveDialog();
    void renderSaveTapeDialog();

    // Action functions
    void loadMemory();
    void saveMemory();
    void loadTape();
    void saveTape();
    void pasteCode();
    void quit();
    void reset();
    void hardReset();
    void debugCpu();
    void configScreen();
    void configMemory();
    void about();
    
    // CPU execution functions
    void startCpu();
    void stopCpu();
    void stepCpu();
    void updateCpuExecution(float deltaTime);
    
    // Utility functions
    void setStatusMessage(const std::string& message, float duration = 3.0f);
    void updateStatus(float deltaTime);
    void handleKeyboardInput();
    std::string disassemble(quint16 pc, int& instrLen);

    // Load dialog state (non-static, reset on open)
    struct LoadDialogState {
        char filePath[512] = "";
        char addressStr[8] = "0300";
        int fileType = 1;
        std::vector<std::string> dirList;
        std::vector<std::string> fileList;
        bool filesScanned = false;
        std::string softAsmRoot;
        std::string currentDir;
        void reset() {
            filePath[0] = '\0';
            snprintf(addressStr, sizeof(addressStr), "0300");
            fileType = 1;
            dirList.clear();
            fileList.clear();
            filesScanned = false;
            softAsmRoot.clear();
            currentDir.clear();
        }
    };
    LoadDialogState loadDlg;

    struct TapeDialogState {
        char filePath[512] = "cassette.aci";
        void setDefaultPath(const char* path) {
            strncpy(filePath, path, sizeof(filePath) - 1);
            filePath[sizeof(filePath) - 1] = '\0';
        }
    };
    TapeDialogState loadTapeDlg;
    TapeDialogState saveTapeDlg;

    // Keyboard shortcuts — single source of truth for label + binding
    struct Shortcut {
        int key;
        int mods;          // 0 = no modifier, GLFW_MOD_CONTROL, etc.
        const char* label; // display string for MenuItem
        void (MainWindow_ImGui::*action)();
    };
    static const Shortcut shortcuts[];
    static const int shortcutCount;
    static const char* shortcutLabel(int key, int mods = 0);
};

#endif // MAINWINDOW_IMGUI_H 