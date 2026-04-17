#ifndef MAINWINDOW_IMGUI_H
#define MAINWINDOW_IMGUI_H

#include <string>
#include <vector>
#include <memory>
#include <cstring>
#include <GLFW/glfw3.h>
#include "CpuClock.h"
#include "POM1Build.h"
#include "EmulationController.h"
#include "MemoryViewer_ImGui.h"
#include "Screen_ImGui.h"
#include "GraphicsCard.h"
#include "TMS9918.h"

class MainWindow_ImGui
{
public:
    MainWindow_ImGui();
    ~MainWindow_ImGui();

    void render();
    void setWindow(GLFWwindow* win) { window = win; }
    static int getPresetCount();
    static const char* getPresetName(int index);
    void setDefaultPresetIndex(int index) { defaultPresetIndex = index; }
    void setTerminalCardOverride(bool enable) { terminalCardOverride = enable; }
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
    bool showHardwareReference = false;
    bool showScreenConfig = false;
    bool showMemoryConfig = false;
    bool showLoadDialog = false;
    bool showLoadTapeDialog = false;
    bool aciEnabled = true;   // Woz ACI cassette interface plugged (default on)
    bool showCassetteControl = false;
    bool showMemoryMap = false;
    bool showSaveDialog = false;
    bool showSaveTapeDialog = false;
    bool showGraphicsCard = false;
    bool graphicsCardEnabled = false;
    GLuint graphicsCardTexture = 0;
    bool showTMS9918 = false;
    bool tms9918Enabled = false;
    GLuint tms9918Texture = 0;
    std::array<uint32_t, TMS9918::kScreenWidth * TMS9918::kScreenHeight> tms9918PixelBuf{};
    bool sidEnabled = false;
    bool microSDEnabled = true;
    bool cffa1Enabled = false;
    bool wifiModemEnabled = false;
    bool showWiFiModem = false;
    bool terminalCardEnabled = !POM1_IS_WASM;
    bool showTerminalCard = !POM1_IS_WASM;
    bool a1ioRtcEnabled = false;
    bool showA1IO_RTC = false;
    bool fullscreen = false;

    // Keyboard input
    bool keyboardAutorepeat = false;  // default off: TTL keyboard has no repeat
    bool nextCharIsRepeat = false;    // set by handleGlfwKey, consumed by handleGlfwChar

    // Machine preset layout: pending window repositioning
    struct PendingWindowPlacement {
        std::string name;
        ImVec2 pos;
        ImVec2 size; // (0,0) = don't change size
    };
    std::vector<PendingWindowPlacement> pendingLayout;
    int windowedWidth = 1200;
    int windowedHeight = 800;
    int windowedPosX = 100;
    int windowedPosY = 100;
    
    // CPU execution state
    bool cpuRunning = false;
    bool stepMode = false;
    int executionSpeed = POM1_CPU_CYCLES_PER_FRAME_1X_60HZ; // ~1.022727 MHz @ 60 fps
    
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
    void renderHardwareReferenceWindow();
    void renderDebugDialog();
    void renderScreenConfigDialog();
    void renderMemoryConfigDialog();
    void renderLoadDialog();
    void renderLoadTapeDialog();
    void renderCassetteControlWindow();
    void renderMemoryMapWindow();
    void renderSaveDialog();
    void renderSaveTapeDialog();
    void renderGraphicsCardWindow();
    void renderTMS9918Window();
    void renderWiFiModemWindow();
    void renderTerminalCardWindow();
    void renderA1IO_RTCWindow();

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
    void applyMachineConfig(int presetIndex);
    void applyPendingLayout(const char* windowName);

    // CPU execution functions
    void startCpu();
    void stopCpu();
    void stepCpu();
    void updateCpuExecution(float deltaTime);
    
    // Utility functions
    void setStatusMessage(const std::string& message, float duration = 3.0f);
    void updateStatus(float deltaTime);
    std::string disassemble(quint16 pc, int& instrLen);

    // Load dialog state (non-static, reset on open)
    struct LoadDialogState {
        char filePath[512] = "";
        char addressStr[8] = "0280";
        int fileType = 1;
        std::vector<std::string> dirList;
        std::vector<std::string> fileList;
        bool filesScanned = false;
        std::string softAsmRoot;
        std::string currentDir;
        void reset() {
            filePath[0] = '\0';
            snprintf(addressStr, sizeof(addressStr), "0280");
            fileType = 1;
            dirList.clear();
            fileList.clear();
            filesScanned = false;
            softAsmRoot.clear();
            currentDir.clear();
        }
    };
    LoadDialogState loadDlg;

    // Loaded program/ROM regions (shown in Memory Map)
    struct LoadedRegion {
        std::string name;
        quint16 start;
        quint16 end;
    };
    std::vector<LoadedRegion> loadedPrograms;
    std::vector<LoadedRegion> loadedRoms; // ROMs loaded by presets (BASIC, Krusader, etc.)
    int presetRamKB = 32;                 // Usable RAM for current preset (display only)
    int defaultPresetIndex = -1;          // -1 = last preset (POM1)
    bool terminalCardOverride = false;    // --terminal: enable Terminal Card on top of any preset
    // applyMachineConfig() normally triggers emulation->hardReset() to wipe
    // RAM + reload default ROMs + reset every peripheral. But at boot the
    // first applyMachineConfig call runs RIGHT AFTER createPom1(), where
    // Memory::Memory()'s own initMemory() has already done all of that —
    // doing it again would load BASIC/WOZ/ACI/SD ROMs twice and start the
    // TerminalCard TCP server three times. Track whether we've already
    // applied a preset once so we can skip the redundant hardReset on
    // boot. Flipped to true by the first applyMachineConfig().
    bool presetAppliedOnce = false;

    // Deferred A1-SID plug: applyMachineConfig() unplugs the SID up front
    // (clean chip + ring reset) and stashes the preset's desired state
    // here; the actual re-plug fires a few frames later from render()
    // when pendingSidEnableFrames decrements to zero. Plugging the SID
    // in the same frame as the preset apply was producing silence on the
    // first tune loaded after boot — the chip was added to the audio
    // mixer before the CPU had time to settle, and would stay silent
    // until the user manually toggled the card. Deferring by a handful
    // of frames (~200 ms at 50 fps) lets the CPU run a few thousand
    // cycles first and fixes that.
    static constexpr int kSidEnableDeferFrames = 15;
    int pendingSidEnableFrames = 0;
    bool pendingSidEnable = false;

    struct TapeDialogState {
        char filePath[512] = "cassette.aci";
        void setDefaultPath(const char* path) {
            strncpy(filePath, path, sizeof(filePath) - 1);
            filePath[sizeof(filePath) - 1] = '\0';
        }
    };
    TapeDialogState loadTapeDlg;
    TapeDialogState saveTapeDlg;

    GraphicsCard graphicsCard;

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