#ifndef MAINWINDOW_IMGUI_H
#define MAINWINDOW_IMGUI_H

#include <string>
#include <vector>
#include <memory>
#include <GLFW/glfw3.h>
#include "Memory.h"
#include "MemoryViewer_ImGui.h"
#include "M6502.h"
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

private:
    // Pom1 Apple I Hardware
    std::unique_ptr<M6502> cpu;
    std::unique_ptr<Memory> memory;
    std::unique_ptr<Screen_ImGui> screen;
    std::unique_ptr<MemoryViewer_ImGui> memoryViewer;
    
    // Window reference for keyboard callbacks
    GLFWwindow* window = nullptr;

    // Interface state
    bool showMemoryViewer = false;
    bool showDebugger = false;
    bool showAbout = false;
    bool showScreenConfig = false;
    bool showMemoryConfig = false;
    bool showLoadDialog = false;
    bool showMemoryMap = false;
    bool showSaveDialog = false;
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
    void renderMemoryMapWindow();
    void renderSaveDialog();

    // Action functions
    void loadMemory();
    void saveMemory();
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
    void updateCpuExecution();
    
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