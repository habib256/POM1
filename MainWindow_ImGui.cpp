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

MainWindow_ImGui::MainWindow_ImGui()
{
    createPom1();
    setStatusMessage("Prêt", 0.0f);
}

MainWindow_ImGui::~MainWindow_ImGui()
{
    destroyPom1();
}

void MainWindow_ImGui::createPom1()
{
    std::cout << "Bienvenue dans POM1 - Émulateur Apple I" << std::endl;
    memory = std::make_unique<Memory>();
    cpu = std::make_unique<M6502>(memory.get());
    screen = std::make_unique<Screen_ImGui>();
    memoryViewer = std::make_unique<MemoryViewer_ImGui>(memory.get());
    
    // Connecter l'affichage (display goes through Memory's I/O at 0xD012)
    memory->setDisplayCallback(Screen_ImGui::displayCallback);

    // Initialiser le CPU au WOZ Monitor
    cpu->hardReset();
    
    // Démarrer le CPU automatiquement pour que le Woz Monitor fonctionne
    cpuRunning = true;
    stepMode = false;
    cpu->start();
    
    setStatusMessage("Système initialisé - WOZ Monitor chargé à 0xFF00 - CPU démarré", 3.0f);
}

void MainWindow_ImGui::destroyPom1()
{
    // Les unique_ptr se détruisent automatiquement
}

void MainWindow_ImGui::render()
{
    float deltaTime = ImGui::GetIO().DeltaTime;
    updateStatus(deltaTime);
    
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
    if (firstFrame) {
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
        ImVec2 charSize = ImGui::CalcTextSize("M");
        ImGui::PopFont();
        float sw = charSize.x * 40 * screen->scale + 40; // marge fenêtre
        float sh = charSize.y * 24 * screen->scale + 60;
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
    ImGui::Begin("Écran Apple 1");
    screen->render();
    ImGui::End();

    // Visualiseur de mémoire
    if (showMemoryViewer) {
        ImGuiIO& mio = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(mio.DisplaySize.x - 410, 30), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(400, mio.DisplaySize.y * 0.45f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Visualiseur de mémoire", &showMemoryViewer);
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

    // Dialogues
    if (showAbout) renderAboutDialog();
    if (showScreenConfig) renderScreenConfigDialog();
    if (showMemoryConfig) renderMemoryConfigDialog();
    if (showLoadDialog) renderLoadDialog();

    // Barre de statut
    renderStatusBar();
}

void MainWindow_ImGui::renderMenuBar()
{
        if (ImGui::BeginMenu("Fichier")) {
            if (ImGui::MenuItem("Charger mémoire", "Ctrl+O")) {
                loadMemory();
            }
            if (ImGui::MenuItem("Sauvegarder mémoire", "Ctrl+S")) {
                saveMemory();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Coller code", "Ctrl+V")) {
                pasteCode();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quitter", "Ctrl+Q")) {
                quit();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("CPU")) {
            if (cpuRunning) {
                if (ImGui::MenuItem("Arrêter", "F6")) {
                    stopCpu();
                }
            } else {
                if (ImGui::MenuItem("Démarrer", "F6")) {
                    startCpu();
                }
            }
            if (ImGui::MenuItem("Pas à pas", "F7")) {
                stepCpu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset logiciel", "F5")) {
                reset();
            }
            if (ImGui::MenuItem("Reset matériel", "Ctrl+F5")) {
                hardReset();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Console de débogage", "F12")) {
                debugCpu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Configuration")) {
            if (ImGui::MenuItem("Options d'écran")) {
                configScreen();
            }
            if (ImGui::MenuItem("Options de mémoire")) {
                configMemory();
            }
            ImGui::Separator();
            ImGui::Text("Vitesse CPU:");
            if (ImGui::RadioButton("1 MHz", executionSpeed == 16667)) { executionSpeed = 16667; }
            ImGui::SameLine();
            if (ImGui::RadioButton("2 MHz", executionSpeed == 33333)) { executionSpeed = 33333; }
            ImGui::SameLine();
            if (ImGui::RadioButton("Max", executionSpeed == 1000000)) { executionSpeed = 1000000; }
            ImGui::Separator();
            ImGui::Text("Vitesse terminal (chars/sec):");
            static int termSpeed = 60;
            ImGui::SetNextItemWidth(150);
            if (ImGui::SliderInt("##termspeed", &termSpeed, 0, 2000, termSpeed == 0 ? "Max" : "%d c/s")) {
                memory->setTerminalSpeed(termSpeed);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Affichage")) {
            ImGui::MenuItem("Visualiseur de mémoire", nullptr, &showMemoryViewer);
            ImGui::MenuItem("Console de débogage", "F12", &showDebugger);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Aide")) {
            if (ImGui::MenuItem("À propos")) {
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
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Charger (Ctrl+O)");

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
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset logiciel (F5)");

        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_POWER_OFF, btnSize)) hardReset();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset matériel (Ctrl+F5)");

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
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mémoire");
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
        ImGui::BulletText("Porté en C/SDL par John D. CORRADO (2006-2014)");
        ImGui::BulletText("Version Dear ImGui par Arnaud VERHILLE (2026)");
        
        ImGui::Spacing();
        ImGui::TextWrapped("Thanks to:");
        ImGui::BulletText("Steve WOZNIAK & Steve JOBS");
        ImGui::BulletText("Vince BRIEL (Replica 1)");
        ImGui::BulletText("Fabrice FRANCES (Émulateur Java Microtan)");
        ImGui::BulletText("And All Apple 1 Community");
    }
    ImGui::End();
}

void MainWindow_ImGui::renderDebugDialog()
{
    ImGui::SetNextWindowSize(ImVec2(520, 520), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Console de débogage CPU", &showDebugger)) {
        ImGui::Text("Débogueur CPU 6502");
        ImGui::Separator();
        
        // Informations sur les registres
        if (ImGui::CollapsingHeader("Registres", ImGuiTreeNodeFlags_DefaultOpen)) {
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
        if (ImGui::CollapsingHeader("Contrôles", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Button("Pas à pas")) {
                stepCpu();
            }
            ImGui::SameLine();
            
            if (cpuRunning) {
                if (ImGui::Button("Arrêter")) {
                    stopCpu();
                }
            } else {
                if (ImGui::Button("Démarrer")) {
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
            ImGui::Text("Vitesse d'exécution:");
            ImGui::SliderInt("##Speed", &executionSpeed, 1, 10000, "%d cycles/frame");
            
            ImGui::Spacing();
            ImGui::Text("État: %s", cpuRunning ? "EN COURS" : "ARRÊTÉ");
        }
        
        // Désassemblage de l'instruction courante
        if (ImGui::CollapsingHeader("Instruction courante", ImGuiTreeNodeFlags_DefaultOpen)) {
            quint16 pc = cpu->getProgramCounter();
            quint8 opcode = memory->memPeek(pc);
            quint8 operand1 = memory->memPeek(pc + 1);
            quint8 operand2 = memory->memPeek(pc + 2);
            
            ImGui::Text("PC: 0x%04X", pc);
            ImGui::Text("Opcode: 0x%02X", opcode);
            ImGui::Text("Operands: 0x%02X 0x%02X", operand1, operand2);
            
            // Affichage simple de l'instruction (peut être étendu)
            ImGui::Text("Instruction: %s", getInstructionName(opcode).c_str());
        }
        
        // Pile
        if (ImGui::CollapsingHeader("Pile (Stack)", ImGuiTreeNodeFlags_DefaultOpen)) {
            quint8 sp = cpu->getStackPointer();
            ImGui::Text("Stack Pointer: 0x01%02X", sp);
            
            ImGui::Text("Top 8 bytes de la pile:");
            ImGui::Columns(2, "StackColumns");
            for (int i = 0; i < 8; i++) {
                quint16 addr = 0x0100 + ((sp + i + 1) & 0xFF);
                quint8 value = memory->memPeek(addr);
                ImGui::Text("0x01%02X:", (sp + i + 1) & 0xFF);
                ImGui::NextColumn();
                ImGui::Text("0x%02X", value);
                ImGui::NextColumn();
            }
            ImGui::Columns(1);
        }
        
        // Console de log
        if (ImGui::CollapsingHeader("Console de log")) {
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
                   << static_cast<int>(memory->memPeek(currentPC));
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
            
            if (ImGui::Button("Effacer log")) {
                debugLog.clear();
            }
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::renderScreenConfigDialog()
{
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Configuration de l'écran", &showScreenConfig)) {
        ImGui::Text("Options d'affichage");
        ImGui::Separator();

        ImGui::Checkbox("Moniteur vert (style vintage)", &screen->greenMonitor);
        ImGui::Checkbox("Curseur", &screen->showCursor);

        ImGui::Spacing();
        ImGui::Text("Échelle d'affichage:");
        ImGui::SliderFloat("##Scale", &screen->scale, 0.5f, 4.0f, "%.1fx");

        ImGui::Spacing();
        ImGui::Text("Effet écran cathodique (CRT)");
        ImGui::Separator();
        ImGui::Checkbox("Scanlines", &screen->crtEffect);
        if (screen->crtEffect) {
            ImGui::SliderFloat("Intensité scanlines", &screen->crtScanlineAlpha, 0.0f, 0.8f, "%.2f");
        }

        ImGui::Spacing();
        if (ImGui::Checkbox("Plein écran", &fullscreen)) {
            if (window) {
                if (fullscreen) {
                    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
                    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                    glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
                } else {
                    glfwSetWindowMonitor(window, nullptr, 100, 100, 1280, 720, 0);
                }
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("Fermer")) {
            showScreenConfig = false;
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::renderMemoryConfigDialog()
{
    ImGui::SetNextWindowSize(ImVec2(450, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Configuration de la mémoire", &showMemoryConfig)) {
        static bool writeProtect = true;
        
        ImGui::Text("Protection ROM");
        ImGui::Separator();
        if (ImGui::Checkbox("Protéger les ROM en écriture", &writeProtect)) {
            memory->setWriteInRom(!writeProtect);
        }
        
        ImGui::Spacing();
        ImGui::Text("Chargement des ROM");
        ImGui::Separator();
        
        if (ImGui::Button("Recharger BASIC")) {
            memory->setWriteInRom(true);
            memory->loadBasic();
            memory->setWriteInRom(!writeProtect);
            setStatusMessage("BASIC rechargé", 2.0f);
        }
        
        if (ImGui::Button("Recharger WOZ Monitor")) {
            memory->setWriteInRom(true);
            memory->loadWozMonitor();
            memory->setWriteInRom(!writeProtect);
            setStatusMessage("WOZ Monitor rechargé", 2.0f);
        }
        
        if (ImGui::Button("Recharger Krusader")) {
            memory->setWriteInRom(true);
            memory->loadKrusader();
            memory->setWriteInRom(!writeProtect);
            setStatusMessage("Krusader rechargé", 2.0f);
        }
        
        ImGui::Spacing();
        ImGui::Text("Mémoire");
        ImGui::Separator();
        
        if (ImGui::Button("Effacer toute la mémoire")) {
            ImGui::OpenPopup("Confirmation##ClearMemory");
        }
        
        if (ImGui::BeginPopupModal("Confirmation##ClearMemory", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Êtes-vous sûr de vouloir effacer toute la mémoire ?");
            ImGui::Separator();
            
            if (ImGui::Button("Oui", ImVec2(120, 0))) {
                memory->resetMemory();
                setStatusMessage("Mémoire effacée", 2.0f);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Non", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        
        if (ImGui::Button("Actualiser le visualiseur")) {
            setStatusMessage("Visualiseur actualisé", 2.0f);
        }
        
        ImGui::Spacing();
        if (ImGui::Button("Fermer")) {
            showMemoryConfig = false;
        }
    }
    ImGui::End();
}

// Implémentation des actions
void MainWindow_ImGui::loadMemory()
{
    showLoadDialog = true;
}

void MainWindow_ImGui::renderLoadDialog()
{
    ImGui::SetNextWindowSize(ImVec2(550, 450), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Charger un programme", &showLoadDialog)) {
        static char filePath[512] = "";
        static char addressStr[8] = "0300";
        static int fileType = 1; // 0=binary, 1=hex dump
        static std::vector<std::string> fileList;
        static bool filesScanned = false;
        static std::string softAsmDir;

        // Scanner le dossier soft-asm au premier affichage
        if (!filesScanned) {
            fileList.clear();
            // Chercher le dossier soft-asm relatif à l'exécutable
            std::string dirs[] = {"soft-asm", "../soft-asm", "../../soft-asm"};
            for (const auto& d : dirs) {
                if (std::filesystem::is_directory(d)) {
                    softAsmDir = std::filesystem::canonical(d).string();
                    break;
                }
            }
            if (!softAsmDir.empty()) {
                for (const auto& entry : std::filesystem::directory_iterator(softAsmDir)) {
                    if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        if (ext == ".txt" || ext == ".bin")
                            fileList.push_back(entry.path().filename().string());
                    }
                }
                std::sort(fileList.begin(), fileList.end());
            }
            filesScanned = true;
        }

        // Liste des programmes disponibles
        if (!fileList.empty()) {
            ImGui::Text("Programmes disponibles :");
            ImGui::BeginChild("FileList", ImVec2(-1, 200), true);
            for (const auto& f : fileList) {
                if (ImGui::Selectable(f.c_str())) {
                    std::string fullPath = softAsmDir + "/" + f;
                    strncpy(filePath, fullPath.c_str(), sizeof(filePath) - 1);
                    filePath[sizeof(filePath) - 1] = '\0';
                    // Auto-detect type
                    if (f.size() > 4 && f.substr(f.size() - 4) == ".bin")
                        fileType = 0;
                    else
                        fileType = 1;
                }
            }
            ImGui::EndChild();
        }

        ImGui::Separator();
        ImGui::Text("Fichier sélectionné :");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##filepath", filePath, sizeof(filePath));

        ImGui::RadioButton("Binaire (.bin)", &fileType, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Hex dump (.txt)", &fileType, 1);

        if (fileType == 0) {
            ImGui::Text("Adresse (hex) :");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80);
            ImGui::InputText("##address", addressStr, sizeof(addressStr),
                             ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
        }

        ImGui::Spacing();
        if (ImGui::Button("Charger", ImVec2(120, 0))) {
            int result;
            quint16 addr = 0;
            if (fileType == 0) {
                addr = (quint16)strtol(addressStr, nullptr, 16);
                result = memory->loadBinary(filePath, addr);
            } else {
                result = memory->loadHexDump(filePath, addr);
                snprintf(addressStr, sizeof(addressStr), "%04X", addr);
            }
            if (result == 0) {
                // Lancer le programme automatiquement
                screen->clear();
                // Écrire le vecteur reset vers l'adresse du programme
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
                ss << "Programme lancé à 0x" << std::hex << std::uppercase << addr;
                setStatusMessage(ss.str(), 3.0f);
                showLoadDialog = false;
                filesScanned = false;
            } else {
                setStatusMessage("Erreur : impossible de charger le fichier", 3.0f);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler", ImVec2(120, 0))) {
            showLoadDialog = false;
            filesScanned = false;
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::saveMemory()
{
    setStatusMessage("Fonction de sauvegarde de mémoire (à implémenter avec nativefiledialog)", 3.0f);
    // Nécessiterait nativefiledialog ou une implémentation système
}

void MainWindow_ImGui::pasteCode()
{
    setStatusMessage("Fonction de collage de code (à implémenter avec clipboard)", 3.0f);
    // Nécessiterait l'accès au presse-papiers système
}

void MainWindow_ImGui::quit()
{
    glfwSetWindowShouldClose(glfwGetCurrentContext(), GLFW_TRUE);
}

void MainWindow_ImGui::reset()
{
    cpu->softReset();
    setStatusMessage("Reset logiciel effectué", 2.0f);
}

void MainWindow_ImGui::hardReset()
{
    memory->resetMemory();
    memory->initMemory();
    cpu->hardReset();  // Must be after initMemory so reset vector is read from fresh ROMs
    screen->clear();
    setStatusMessage("Reset matériel effectué - Mémoire réinitialisée", 2.0f);
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
            statusMessage = "Prêt";
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
    setStatusMessage("CPU démarré - Exécution en cours", 2.0f);
}

void MainWindow_ImGui::stopCpu()
{
    cpuRunning = false;
    cpu->stop();
    setStatusMessage("CPU arrêté", 2.0f);
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
    ss << "Pas à pas - PC: 0x" << std::hex << std::uppercase << cpu->getProgramCounter();
    setStatusMessage(ss.str(), 2.0f);
}

void MainWindow_ImGui::handleKeyboardInput()
{
    ImGuiIO& io = ImGui::GetIO();

    // Ne pas envoyer les touches à l'Apple 1 quand un widget ImGui a le focus
    if (io.WantTextInput) return;

    for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
        ImWchar c = io.InputQueueCharacters[i];
        // Only process printable ASCII chars here; control keys (Enter, Backspace, Escape)
        // are handled separately via IsKeyPressed below to avoid double input
        if (c >= 32 && c <= 126) {
            memory->setKeyPressed((char)c);
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
    if (action != GLFW_PRESS)
        return;

    const bool ctrl = (mods & GLFW_MOD_CONTROL) != 0;

    // Raccourcis globaux (libellés dans le menu — ImGui ne les lie pas automatiquement)
    if (ctrl) {
        switch (key) {
        case GLFW_KEY_O: loadMemory(); return;
        case GLFW_KEY_S: saveMemory(); return;
        case GLFW_KEY_V: pasteCode(); return;
        case GLFW_KEY_Q: quit(); return;
        case GLFW_KEY_F5: hardReset(); return;
        default: break;
        }
    }

    switch (key) {
    case GLFW_KEY_F5: reset(); return;
    case GLFW_KEY_F6:
        if (cpuRunning)
            stopCpu();
        else
            startCpu();
        return;
    case GLFW_KEY_F7:
        stepCpu();
        return;
    case GLFW_KEY_F12:
        debugCpu();
        return;
    default:
        break;
    }

}

std::string MainWindow_ImGui::getInstructionName(quint8 opcode)
{
    // Table simplifiée des instructions 6502 les plus courantes
    switch (opcode) {
        // ADC
        case 0x69: return "ADC #$";
        case 0x65: return "ADC $";
        case 0x75: return "ADC $,X";
        case 0x6D: return "ADC $";
        case 0x7D: return "ADC $,X";
        case 0x79: return "ADC $,Y";
        case 0x61: return "ADC ($,X)";
        case 0x71: return "ADC ($),Y";
        
        // AND
        case 0x29: return "AND #$";
        case 0x25: return "AND $";
        case 0x35: return "AND $,X";
        case 0x2D: return "AND $";
        case 0x3D: return "AND $,X";
        case 0x39: return "AND $,Y";
        case 0x21: return "AND ($,X)";
        case 0x31: return "AND ($),Y";
        
        // Branch instructions
        case 0x10: return "BPL";
        case 0x30: return "BMI";
        case 0x50: return "BVC";
        case 0x70: return "BVS";
        case 0x90: return "BCC";
        case 0xB0: return "BCS";
        case 0xD0: return "BNE";
        case 0xF0: return "BEQ";
        
        // Compare
        case 0xC9: return "CMP #$";
        case 0xC5: return "CMP $";
        case 0xD5: return "CMP $,X";
        case 0xCD: return "CMP $";
        case 0xDD: return "CMP $,X";
        case 0xD9: return "CMP $,Y";
        case 0xC1: return "CMP ($,X)";
        case 0xD1: return "CMP ($),Y";
        
        // Jump/Call
        case 0x4C: return "JMP $";
        case 0x6C: return "JMP ($)";
        case 0x20: return "JSR $";
        case 0x60: return "RTS";
        case 0x40: return "RTI";
        
        // Load/Store
        case 0xA9: return "LDA #$";
        case 0xA5: return "LDA $";
        case 0xB5: return "LDA $,X";
        case 0xAD: return "LDA $";
        case 0xBD: return "LDA $,X";
        case 0xB9: return "LDA $,Y";
        case 0xA1: return "LDA ($,X)";
        case 0xB1: return "LDA ($),Y";
        
        case 0xA2: return "LDX #$";
        case 0xA6: return "LDX $";
        case 0xB6: return "LDX $,Y";
        case 0xAE: return "LDX $";
        case 0xBE: return "LDX $,Y";
        
        case 0xA0: return "LDY #$";
        case 0xA4: return "LDY $";
        case 0xB4: return "LDY $,X";
        case 0xAC: return "LDY $";
        case 0xBC: return "LDY $,X";
        
        case 0x84: return "STY $";
        case 0x94: return "STY $,X";
        case 0x8C: return "STY $";
        
        case 0x85: return "STA $";
        case 0x95: return "STA $,X";
        case 0x8D: return "STA $";
        case 0x9D: return "STA $,X";
        case 0x99: return "STA $,Y";
        case 0x81: return "STA ($,X)";
        case 0x91: return "STA ($),Y";
        
        // Stack operations
        case 0x48: return "PHA";
        case 0x68: return "PLA";
        case 0x08: return "PHP";
        case 0x28: return "PLP";
        
        // Status flags
        case 0x18: return "CLC";
        case 0x38: return "SEC";
        case 0x58: return "CLI";
        case 0x78: return "SEI";
        case 0xB8: return "CLV";
        case 0xD8: return "CLD";
        case 0xF8: return "SED";
        
        // Transfers
        case 0xAA: return "TAX";
        case 0x8A: return "TXA";
        case 0xA8: return "TAY";
        case 0x98: return "TYA";
        case 0x9A: return "TXS";
        case 0xBA: return "TSX";
        
        // Increment/Decrement
        case 0xE8: return "INX";
        case 0xC8: return "INY";
        case 0xCA: return "DEX";
        case 0x88: return "DEY";
        case 0xE6: return "INC $";
        case 0xF6: return "INC $,X";
        case 0xEE: return "INC $";
        case 0xFE: return "INC $,X";
        case 0xC6: return "DEC $";
        case 0xD6: return "DEC $,X";
        case 0xCE: return "DEC $";
        case 0xDE: return "DEC $,X";
        
        // Misc
        case 0x00: return "BRK";
        case 0xEA: return "NOP";
        
        default:
            return "???";
    }
} 