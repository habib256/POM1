#ifndef MAINWINDOW_IMGUI_H
#define MAINWINDOW_IMGUI_H

#include <string>
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
    std::string getInstructionName(quint8 opcode);
};

#endif // MAINWINDOW_IMGUI_H 