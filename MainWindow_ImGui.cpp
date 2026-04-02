#include "MainWindow_ImGui.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "IconsFontAwesome6.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <GLFW/glfw3.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

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
    memory = std::make_unique<Memory>();
    cpu = std::make_unique<M6502>(memory.get());
    screen = std::make_unique<Screen_ImGui>();
    memoryViewer = std::make_unique<MemoryViewer_ImGui>(memory.get());
    
    // Connecter l'affichage
    memory->setDisplayCallback(Screen_ImGui::displayCallback);
    cpu->setDisplayCallback(Screen_ImGui::displayCallback);

    // Initialiser le CPU au WOZ Monitor
    cpu->hardReset();
    
    // Démarrer le CPU automatiquement pour que le Woz Monitor fonctionne
    cpuRunning = true;
    stepMode = false;
    cpu->start();
    
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

#ifdef __EMSCRIPTEN__
    // Sync fullscreen flag with browser state (user may exit via Escape)
    EmscriptenFullscreenChangeEvent fsStatus;
    if (emscripten_get_fullscreen_status(&fsStatus) == EMSCRIPTEN_RESULT_SUCCESS) {
        fullscreen = fsStatus.isFullscreen;
    }
#endif

    // Gérer les entrées clavier
    handleKeyboardInput();
    
    // Mettre à jour l'exécution du CPU
    updateCpuExecution();

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
    if (firstFrame) {
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
        ImVec2 charSize = ImGui::CalcTextSize("M");
        ImGui::PopFont();
        float cellW = charSize.x * 1.4f; // must match Screen_ImGui::render() cell spacing
        float cellH = charSize.y * 1.3f;
        float sw = cellW * 40 * screen->scale + 40; // marge fenêtre
        float sh = cellH * 24 * screen->scale + 60;
        float toolbarBottom = ImGui::GetFrameHeight() + 34.0f;
        ImGui::SetNextWindowPos(ImVec2(10, toolbarBottom + 5));
        ImGui::SetNextWindowSize(ImVec2(sw, sh));
        // Redimensionner la fenêtre GLFW
        if (window) {
            int winW = (int)sw + 20;
            int winH = (int)sh + (int)toolbarBottom + 40;
            glfwSetWindowSize(window, winW, winH);
        }
        firstFrame = false;
    }

    // Resize Apple 1 screen window on fullscreen transitions
    if (fullscreen != wasFullscreen) {
        ImGuiIO& fsio = ImGui::GetIO();
        float toolbarBottom = ImGui::GetFrameHeight() + 34.0f;
        if (fullscreen) {
            // Fill the entire display
            ImGui::SetNextWindowPos(ImVec2(0, toolbarBottom));
            ImGui::SetNextWindowSize(ImVec2(fsio.DisplaySize.x, fsio.DisplaySize.y - toolbarBottom));
        } else {
            // Restore to default Apple 1 size
            ImGui::PushFont(fsio.Fonts->Fonts[0]);
            ImVec2 charSize = ImGui::CalcTextSize("M");
            ImGui::PopFont();
            float cellW = charSize.x * 1.4f;
            float cellH = charSize.y * 1.3f;
            float sw = cellW * 40 * screen->scale + 40;
            float sh = cellH * 24 * screen->scale + 60;
            ImGui::SetNextWindowPos(ImVec2(10, toolbarBottom + 5));
            ImGui::SetNextWindowSize(ImVec2(sw, sh));
        }
        wasFullscreen = fullscreen;
    }

    ImGui::Begin("Apple 1 Screen");
    screen->render();
    ImGui::End();

    // Visualiseur de mémoire
    if (showMemoryViewer) {
        ImGuiIO& mio = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(mio.DisplaySize.x - 410, 30), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(400, mio.DisplaySize.y * 0.45f), ImGuiCond_FirstUseEver);
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
    if (showSaveDialog) renderSaveDialog();

    // Barre de statut
    renderStatusBar();
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
            ImGui::Text("CPU Speed:");
            if (ImGui::RadioButton("1 MHz", executionSpeed == 16667)) { executionSpeed = 16667; }
            ImGui::SameLine();
            if (ImGui::RadioButton("2 MHz", executionSpeed == 33333)) { executionSpeed = 33333; }
            ImGui::SameLine();
            if (ImGui::RadioButton("Max", executionSpeed == 1000000)) { executionSpeed = 1000000; }
            ImGui::Separator();
            ImGui::Text("Terminal Speed (chars/sec):");
            static int termSpeed = 60;
            ImGui::SetNextItemWidth(150);
            if (ImGui::SliderInt("##termspeed", &termSpeed, 0, 2000, termSpeed == 0 ? "Max" : "%d c/s")) {
                memory->setTerminalSpeed(termSpeed);
            }
            ImGui::Separator();
            ImGui::MenuItem("Memory Viewer", shortcutLabel(GLFW_KEY_F1), &showMemoryViewer);
            ImGui::MenuItem("Memory Map", shortcutLabel(GLFW_KEY_F2), &showMemoryMap);
            ImGui::MenuItem("Debug Console", shortcutLabel(GLFW_KEY_F3), &showDebugger);
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
    float toolbarHeight = 34.0f;

    ImGui::SetNextWindowPos(ImVec2(0, menuBarHeight));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, toolbarHeight));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
    if (ImGui::Begin("##Toolbar", nullptr, flags)) {

        ImVec4 activeColor(0.2f, 0.4f, 0.8f, 1.0f);
        ImVec2 btnSize(28, 24);

        // --- Chargement (premier) ---
        if (ImGui::Button(ICON_FA_FOLDER_OPEN, btnSize)) loadMemory();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Load (Ctrl+O)");

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

        // --- Vitesse CPU ---
        {
            bool is1M = (executionSpeed == 16667);
            if (is1M) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            if (ImGui::Button("1M", btnSize)) executionSpeed = 16667;
            if (is1M) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("1 MHz");
        }
        ImGui::SameLine();
        {
            bool is2M = (executionSpeed == 33333);
            if (is2M) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            if (ImGui::Button("2M", btnSize)) executionSpeed = 33333;
            if (is2M) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("2 MHz");
        }
        ImGui::SameLine();
        {
            bool isMax = (executionSpeed == 1000000);
            if (isMax) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            if (ImGui::Button("Max", btnSize)) executionSpeed = 1000000;
            if (isMax) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Max");
        }

        // --- Séparateur ---
        ImGui::SameLine(0, 12);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine(0, 12);

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

        // Côté droit
        ImGui::SameLine(io.DisplaySize.x - 350);

        if (cpuRunning) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "RUNNING");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "STOPPED");
        }

        ImGui::SameLine();
        float mhz = executionSpeed * 60.0f / 1000000.0f;
        if (executionSpeed >= 1000000)
            ImGui::Text("| Max");
        else
            ImGui::Text("| %.1f MHz", mhz);

        ImGui::SameLine();
        ImGui::Text("| RAM: %d KB", memory->getRamSizeKB());

        // État du clavier
        if (memory->isKeyReady()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                               "| KEY: '%c'", memory->getLastKey());
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::renderAboutDialog()
{
    ImGui::SetNextWindowSize(ImVec2(400, 360), ImGuiCond_FirstUseEver);
    if (        ImGui::Begin("About POM1", &showAbout)) {
        ImGui::TextWrapped("POM1 - Apple 1 Emulator (Dear ImGui Version)");
        ImGui::Separator();
        
        ImGui::TextWrapped("Copyright © 2000-2026 GPL3");
        ImGui::Spacing();
        
        ImGui::TextWrapped("Written by:");
        ImGui::BulletText("Arnaud VERHILLE (2000-2026)");
        ImGui::SameLine();
        if (ImGui::SmallButton("gist974@gmail.com")) {
            // Ouvrir email (nécessiterait une implémentation système)
        }
        
        ImGui::BulletText("Upgraded by Ken WESSEN (21/2/2006)");
        ImGui::BulletText("Ported to C/SDL by John D. CORRADO (2006-2014)");
        ImGui::BulletText("Dear ImGui version by Arnaud VERHILLE (2026)");
        
        ImGui::Spacing();
        ImGui::TextWrapped("Thanks to:");
        ImGui::BulletText("Steve WOZNIAK & Steve JOBS");
        ImGui::BulletText("Vince BRIEL (Replica 1)");
        ImGui::BulletText("Fabrice FRANCES (Java Microtan Emulator)");
        ImGui::BulletText("And All Apple 1 Community");
    }
    ImGui::End();
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
            ImGui::Text("0x%04X", cpu->getProgramCounter());
            ImGui::NextColumn();
            
            ImGui::Text("Accumulator (A):");
            ImGui::NextColumn();
            ImGui::Text("0x%02X (%d)", cpu->getAccumulator(), cpu->getAccumulator());
            ImGui::NextColumn();
            
            ImGui::Text("X Register:");
            ImGui::NextColumn();
            ImGui::Text("0x%02X (%d)", cpu->getXRegister(), cpu->getXRegister());
            ImGui::NextColumn();
            
            ImGui::Text("Y Register:");
            ImGui::NextColumn();
            ImGui::Text("0x%02X (%d)", cpu->getYRegister(), cpu->getYRegister());
            ImGui::NextColumn();
            
            ImGui::Text("Stack Pointer (SP):");
            ImGui::NextColumn();
            ImGui::Text("0x%02X", cpu->getStackPointer());
            ImGui::NextColumn();
            
            ImGui::Text("Status Register:");
            ImGui::NextColumn();
            quint8 status = cpu->getStatusRegister();
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
                cpu->hardReset();
                setStatusMessage("CPU reset", 2.0f);
            }
            
            ImGui::Spacing();
            ImGui::Text("Execution Speed:");
            ImGui::SliderInt("##Speed", &executionSpeed, 1, 10000, "%d cycles/frame");

            ImGui::Spacing();
            ImGui::Text("Status: %s", cpuRunning ? "RUNNING" : "STOPPED");
        }
        
        // Désassemblage de l'instruction courante
        if (ImGui::CollapsingHeader("Current Instruction", ImGuiTreeNodeFlags_DefaultOpen)) {
            quint16 pc = cpu->getProgramCounter();
            int instrLen = 1;
            std::string disasm = disassemble(pc, instrLen);

            ImGui::Text("PC: $%04X", pc);
            // Show raw bytes
            std::stringstream rawBytes;
            for (int i = 0; i < instrLen; i++) {
                if (i > 0) rawBytes << " ";
                rawBytes << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                         << (int)memory->memRead(pc + i);
            }
            ImGui::Text("Bytes: %s", rawBytes.str().c_str());
            ImGui::Text("  %s", disasm.c_str());
        }
        
        // Pile
        if (ImGui::CollapsingHeader("Stack", ImGuiTreeNodeFlags_DefaultOpen)) {
            quint8 sp = cpu->getStackPointer();
            ImGui::Text("Stack Pointer: 0x01%02X", sp);
            
            ImGui::Text("Top 8 stack bytes:");
            ImGui::Columns(2, "StackColumns");
            for (int i = 0; i < 8; i++) {
                quint16 addr = 0x0100 + ((sp + i + 1) & 0xFF);
                quint8 value = memory->memRead(addr);
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
            quint16 currentPC = cpu->getProgramCounter();
            if (currentPC != lastPC && cpuRunning) {
                std::stringstream ss;
                ss << "PC: 0x" << std::hex << std::uppercase << currentPC 
                   << " - Opcode: 0x" << std::setfill('0') << std::setw(2) 
                   << static_cast<int>(memory->memRead(currentPC));
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

        ImGui::Checkbox("Green Monitor (vintage style)", &screen->greenMonitor);
        ImGui::Checkbox("Cursor", &screen->showCursor);

        ImGui::Spacing();
        ImGui::Text("Display Scale:");
        ImGui::SliderFloat("##Scale", &screen->scale, 0.5f, 4.0f, "%.1fx");

        ImGui::Spacing();
        ImGui::Text("CRT Effect");
        ImGui::Separator();
        ImGui::Checkbox("Scanlines", &screen->crtEffect);
        if (screen->crtEffect) {
            ImGui::SliderFloat("Scanline Intensity", &screen->crtScanlineAlpha, 0.0f, 0.8f, "%.2f");
        }

        ImGui::Spacing();
        if (ImGui::Checkbox("Fullscreen", &fullscreen)) {
#ifdef __EMSCRIPTEN__
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
        static bool writeProtect = true;

        ImGui::Text("ROM Protection");
        ImGui::Separator();
        if (ImGui::Checkbox("Write-protect ROMs", &writeProtect)) {
            memory->setWriteInRom(!writeProtect);
        }

        ImGui::Spacing();
        ImGui::Text("ROM Loading");
        ImGui::Separator();

        if (ImGui::Button("Reload BASIC")) {
            memory->setWriteInRom(true);
            int result = memory->loadBasic();
            memory->setWriteInRom(!writeProtect);
            setStatusMessage(result == 0 ? "BASIC reloaded" : memory->getLastError(), 3.0f);
        }

        if (ImGui::Button("Reload WOZ Monitor")) {
            memory->setWriteInRom(true);
            int result = memory->loadWozMonitor();
            memory->setWriteInRom(!writeProtect);
            setStatusMessage(result == 0 ? "WOZ Monitor reloaded" : memory->getLastError(), 3.0f);
        }

        if (ImGui::Button("Reload Krusader")) {
            memory->setWriteInRom(true);
            int result = memory->loadKrusader();
            memory->setWriteInRom(!writeProtect);
            setStatusMessage(result == 0 ? "Krusader reloaded" : memory->getLastError(), 3.0f);
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
                memory->resetMemory();
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
                std::string dirs[] = {"soft-asm", "../soft-asm", "../../soft-asm"};
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
                        if (ext == ".txt" || ext == ".bin")
                            loadDlg.fileList.push_back(entry.path().filename().string());
                    }
                }
                std::sort(loadDlg.dirList.begin(), loadDlg.dirList.end());
                std::sort(loadDlg.fileList.begin(), loadDlg.fileList.end());
            }
            loadDlg.filesScanned = true;
        }

        {
            std::string displayPath = "soft-asm/";
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
            int result;
            quint16 addr = 0;
            if (loadDlg.fileType == 0) {
                addr = (quint16)strtol(loadDlg.addressStr, nullptr, 16);
                result = memory->loadBinary(loadDlg.filePath, addr);
            } else {
                result = memory->loadHexDump(loadDlg.filePath, addr);
                snprintf(loadDlg.addressStr, sizeof(loadDlg.addressStr), "%04X", addr);
            }
            if (result == 0) {
                screen->clear();
                bool prevWriteInRom = memory->getWriteInRom();
                memory->setWriteInRom(true);
                memory->memWrite(0xFFFC, addr & 0xFF);
                memory->memWrite(0xFFFD, (addr >> 8) & 0xFF);
                memory->setWriteInRom(prevWriteInRom);
                cpu->hardReset();
                cpu->start();
                cpuRunning = true;
                stepMode = false;
                std::stringstream ss;
                ss << "Program started at 0x" << std::hex << std::uppercase << addr;
                setStatusMessage(ss.str(), 3.0f);
                showLoadDialog = false;
                loadDlg.reset();
            } else {
                setStatusMessage("Error: unable to load file", 3.0f);
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
            // Build path in soft-asm directory
            std::string path = filename;

            std::ofstream file(path, saveFormat == 0 ? std::ios::binary : std::ios::out);
            if (file.is_open()) {
                if (saveFormat == 0) {
                    // Binary format
                    for (quint16 a = startAddr; a <= endAddr; ++a) {
                        quint8 b = memory->memRead(a);
                        file.write(reinterpret_cast<char*>(&b), 1);
                        if (a == 0xFFFF) break;
                    }
                } else {
                    // Woz Monitor hex dump format
                    for (quint16 a = startAddr; a <= endAddr; a += 16) {
                        file << std::hex << std::uppercase << std::setfill('0')
                             << std::setw(4) << a << ":";
                        int lineEnd = std::min((int)a + 16, (int)endAddr + 1);
                        for (int i = a; i < lineEnd; ++i) {
                            file << " " << std::setfill('0') << std::setw(2) << (int)memory->memRead((quint16)i);
                        }
                        file << "\n";
                        if (a + 16 < a) break; // overflow guard
                    }
                }
                file.close();
                std::stringstream ss;
                ss << "Saved $" << std::hex << std::uppercase << startAddr
                   << "-$" << endAddr << " to " << path;
                setStatusMessage(ss.str(), 3.0f);
                showSaveDialog = false;
            } else {
                setStatusMessage("Error: unable to write file", 3.0f);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            showSaveDialog = false;
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

    // Cycles to run per character: enough for the CPU to read the key and echo it.
    // At ~60 chars/sec on a 1 MHz CPU, one character takes ~16667 cycles to display.
    // We use 2x that to leave margin for processing (WOZ Monitor read + echo loop).
    const int CYCLES_PER_CHAR = memory->getTerminalSpeed() > 0
        ? (2 * 1000000 / memory->getTerminalSpeed())
        : 1000; // max speed: minimal delay

    const char* p = clipboard;
    int charCount = 0;
    const int MAX_PASTE_CHARS = 4096;
    while (*p && charCount < MAX_PASTE_CHARS) {
        char c = *p;
        if (c == '\n') c = '\r';
        if (c == '\r' || (c >= 32 && c <= 126)) {
            // setKeyPressed() already converts to uppercase
            memory->setKeyPressed(c);
            cpu->run(CYCLES_PER_CHAR);
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
    cpu->softReset();
    setStatusMessage("Soft reset done", 2.0f);
}

void MainWindow_ImGui::hardReset()
{
    cpu->hardReset();
    memory->resetMemory();
    memory->initMemory();
    screen->clear();
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
    ImGui::SetNextWindowSize(ImVec2(340, 560), ImGuiCond_FirstUseEver);
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

    MemRegion regions[] = {
        { 0x0000, 0x00FF, IM_COL32(100, 100, 255, 255), "Zero Page" },
        { 0x0100, 0x01FF, IM_COL32(255, 165,   0, 255), "Stack" },
        { 0x0200, 0x9FFF, IM_COL32( 80, 200,  80, 255), "User RAM" },
        { 0xA000, 0xBFFF, IM_COL32(200,  80, 200, 255), "Krusader ROM" },
        { 0xC000, 0xCFFF, IM_COL32( 60,  60,  60, 255), "Unused" },
        { 0xD000, 0xD0FF, IM_COL32(255,  80,  80, 255), "I/O (KBD/DSP)" },
        { 0xD100, 0xDFFF, IM_COL32( 60,  60,  60, 255), "Unused" },
        { 0xE000, 0xEFFF, IM_COL32(255, 255,  80, 255), "BASIC ROM" },
        { 0xF000, 0xFEFF, IM_COL32( 60,  60,  60, 255), "Unused" },
        { 0xFF00, 0xFFFF, IM_COL32(  0, 200, 255, 255), "Woz Monitor ROM" },
    };
    int numRegions = sizeof(regions) / sizeof(regions[0]);

    // --- Legend (Unused at the bottom) ---
    ImGui::Text("Legend:");
    ImGui::Separator();
    ImU32 unusedColor = IM_COL32(60, 60, 60, 255);
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
    // Unused entry last
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + 12, p.y + 12), unusedColor);
        dl->AddRect(p, ImVec2(p.x + 12, p.y + 12), IM_COL32(180, 180, 180, 255));
        ImGui::Dummy(ImVec2(16, 14));
        ImGui::SameLine();
        ImGui::Text("Unused");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Visual memory map ---
    // Each row = 256 bytes (one page), 256 rows total for 64KB
    // Display as a grid: each pixel = 1 page (256 bytes)
    ImGui::Text("Map (1 cell = 256 bytes):");
    ImGui::Spacing();

    const int COLS = 16;  // 16 columns x 16 rows = 256 pages = 64KB
    const int ROWS = 16;
    float cellSize = 16.0f;
    float spacing = 1.0f;

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const quint8* memPtr = memory->getMemoryPointer();

    // PC marker
    quint16 pc = cpu->getProgramCounter();
    int pcPage = pc >> 8;
    // SP marker
    quint8 sp = cpu->getStackPointer();
    int spPage = 1; // stack is always page 1

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
            if (addr >= 0x0200 && addr <= 0x9FFF && !hasData) {
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
    float rightMargin = origin.x + COLS * (cellSize + spacing) + 4;
    for (int row = 0; row < ROWS; ++row) {
        float y = origin.y + row * (cellSize + spacing) + 2;
        int kb = row * 4;
        char label[16];
        snprintf(label, sizeof(label), "%dK", kb);
        drawList->AddText(ImVec2(rightMargin, y), IM_COL32(150, 150, 150, 255), label);
    }

    // Reserve space for the grid + right labels
    ImGui::Dummy(ImVec2(COLS * (cellSize + spacing) + 40, ROWS * (cellSize + spacing)));

    ImGui::Spacing();
    ImGui::Text("PC = $%04X  SP = $01%02X", pc, sp);

    // Special addresses section
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("I/O Registers:");
    ImGui::BulletText("$D010  KBD   - Keyboard data");
    ImGui::BulletText("$D011  KBDCR - Keyboard control");
    ImGui::BulletText("$D012  DSP   - Display output");

    ImGui::Spacing();
    ImGui::Text("CPU Vectors:");
    ImGui::BulletText("$FFFA/B  NMI   -> $%04X",
        (int)memory->memRead(0xFFFA) | ((int)memory->memRead(0xFFFB) << 8));
    ImGui::BulletText("$FFFC/D  RESET -> $%04X",
        (int)memory->memRead(0xFFFC) | ((int)memory->memRead(0xFFFD) << 8));
    ImGui::BulletText("$FFFE/F  IRQ   -> $%04X",
        (int)memory->memRead(0xFFFE) | ((int)memory->memRead(0xFFFF) << 8));

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

void MainWindow_ImGui::updateCpuExecution()
{
    if (cpuRunning && !stepMode) {
        // Utiliser executionSpeed cycles par frame (configurable dans l'interface)
        cpu->run(executionSpeed);
    }
}

void MainWindow_ImGui::startCpu()
{
    cpuRunning = true;
    stepMode = false;
    cpu->start();
    setStatusMessage("CPU started - Running", 2.0f);
}

void MainWindow_ImGui::stopCpu()
{
    cpuRunning = false;
    cpu->stop();
    setStatusMessage("CPU stopped", 2.0f);
}

void MainWindow_ImGui::stepCpu()
{
    // Arrêter l'exécution automatique et activer le mode pas à pas
    cpuRunning = false;
    stepMode = true;
    cpu->stop();
    
    // Exécuter une seule instruction
    cpu->step();
    
    std::stringstream ss;
    ss << "Step - PC: 0x" << std::hex << std::uppercase << cpu->getProgramCounter();
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
            memory->setKeyPressed((char)c);
        } else if (c == '\r' || c == '\n') {
            memory->setKeyPressed('\r');
        } else if (c == '\b' || c == 127) {
            memory->setKeyPressed('\b');
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
        memory->setKeyPressed('\r');
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        memory->setKeyPressed('\b');
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        memory->setKeyPressed(27);
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
    if (action != GLFW_PRESS)
        return;

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
    quint8 opcode = memory->memRead(pc);
    const OpcodeInfo& info = opcodeInfo[opcode];
    quint8 lo = memory->memRead(pc + 1);
    quint8 hi = memory->memRead(pc + 2);
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