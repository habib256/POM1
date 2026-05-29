// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MainWindow_DebugWindows.cpp — the Debug Console (CPU registers, step/run,
// disassembly, log) and the 64 KB Memory Map visualisation. Both windows
// read uiSnapshot heavily and are the main "what is the emulation doing
// right now?" surfaces.

#include "MainWindow_ImGui.h"
#include "MainWindow_Internal.h"
#include "POM1Build.h"
#include "Logger.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace {
using namespace pom1::mainwindow::detail;
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
            uint8_t status = uiSnapshot.statusRegister;
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
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Execute one CPU instruction (F7)");
            ImGui::SameLine();

            if (cpuRunning) {
                if (ImGui::Button("Stop")) {
                    stopCpu();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Pause the emulation thread");
            } else {
                if (ImGui::Button("Start")) {
                    startCpu();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Resume the emulation thread");
            }
            ImGui::SameLine();

            if (ImGui::Button("Reset")) {
                hardReset();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Hard reset (clears RAM, reloads ROMs) — Ctrl+F5");
            
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
            uint16_t pc = uiSnapshot.programCounter;
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
            uint8_t sp = uiSnapshot.stackPointer;
            ImGui::Text("Stack Pointer: 0x01%02X", sp);
            
            ImGui::Text("Top 8 stack bytes:");
            ImGui::Columns(2, "StackColumns");
            for (int i = 0; i < 8; i++) {
                uint16_t addr = 0x0100 + ((sp + i + 1) & 0xFF);
                uint8_t value = uiSnapshot.memory[addr];
                ImGui::Text("0x01%02X:", (sp + i + 1) & 0xFF);
                ImGui::NextColumn();
                ImGui::Text("0x%02X", value);
                ImGui::NextColumn();
            }
            ImGui::Columns(1);
        }
        
        // System log: snapshot the process-wide RingBufferLogger captured by
        // pom1::initDefaultTeeLogger(). One pass of filtering by level.
        if (ImGui::CollapsingHeader("System Log")) {
            static int filterLevelIdx = static_cast<int>(pom1::LogLevel::Info);
            const char* kLevelLabels[] = { "DEBUG", "INFO", "WARN", "ERROR" };
            ImGui::SetNextItemWidth(120);
            ImGui::Combo("Min level", &filterLevelIdx, kLevelLabels, IM_ARRAYSIZE(kLevelLabels));
            ImGui::SameLine();
            if (ImGui::Button("Clear Log")) {
                pom1::uiRingBuffer().clear();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Empty the in-app log ring buffer");
            ImGui::SameLine();
            bool brkTrace = emulation->isCpuBrkTraceEnabled();
            if (ImGui::Checkbox("BRK trace", &brkTrace)) {
                emulation->setCpuBrkTraceEnabled(brkTrace);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "When on, every BRK opcode logs CPU state, stack, recent\n"
                    "control-flow transfers, and bus state at WARN level.\n"
                    "Helpful to diagnose unexpected resets (BRK → Woz Monitor).");
            }
            ImGui::SameLine();
            if (ImGui::Button("Dump PC trace")) {
                emulation->dumpCpuPcTrace("manual PC trace");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Dump the CPU's ring buffer of recent non-sequential PC\n"
                    "transitions (JMP/JSR/RTS/branch/IRQ) to the log right now.");
            }

            ImGui::BeginChild("SystemLog", ImVec2(0, 220), true,
                              ImGuiWindowFlags_HorizontalScrollbar);
            const auto entries = pom1::uiRingBuffer().snapshot(
                static_cast<pom1::LogLevel>(filterLevelIdx));
            for (const auto& e : entries) {
                ImVec4 col;
                switch (e.level) {
                    case pom1::LogLevel::Error: col = ImVec4(1.0f, 0.45f, 0.45f, 1.0f); break;
                    case pom1::LogLevel::Warn:  col = ImVec4(1.0f, 0.85f, 0.40f, 1.0f); break;
                    case pom1::LogLevel::Info:  col = ImVec4(0.85f, 0.95f, 1.00f, 1.0f); break;
                    case pom1::LogLevel::Debug: col = ImVec4(0.65f, 0.65f, 0.65f, 1.0f); break;
                }
                ImGui::TextColored(col, "[%s] [%-5s] %s",
                                   e.tag.c_str(),
                                   pom1::levelName(e.level),
                                   e.message.c_str());
            }
            // Auto-scroll to bottom when the user is already there.
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
            ImGui::TextDisabled("Capacity: %zu entries", pom1::uiRingBuffer().capacity());
        }
    }
    ImGui::End();
}
std::vector<MainWindow_ImGui::MemRegion> MainWindow_ImGui::buildMemoryRegions()
{
    const uint32_t ramCeiling32 = static_cast<uint32_t>(presetRamKB) * 1024;
    const bool fullRam = (ramCeiling32 >= 0x10000);
    const ImU32 ramColor   = IM_COL32( 80, 200,  80, 255);
    const ImU32 unmapColor = IM_COL32( 40,  40,  40, 255);

    std::vector<MemRegion> regions;

    // --- Layer 0: base (User RAM + Unmapped) ---
    // Apple-1 hardware reality: presetRamKB == 8 is Parmigiani's standard
    // dual-bank layout (4 KB at $0000-$0FFF + 4 KB at $E000-$EFFF — same as
    // 99 % of Originals + every Replica). NOT a contiguous 8 KB at
    // $0000-$1FFF. The high-bank User RAM stays visible as green here; when
    // the preset loads BASIC / Applesoft Lite the Layer-3 yellow ROM band
    // overlays it (last-match-wins in the grid render). Other ramKB values
    // (4 / 16 / 32 / 64) keep the contiguous-low semantics.
    if (fullRam) {
        regions.push_back({ 0x0000, 0xFFFF, ramColor, "User RAM" });
    } else if (presetRamKB == 8) {
        regions.push_back({ 0x0000, 0x0FFF, ramColor,   "User RAM" });
        regions.push_back({ 0x1000, 0xDFFF, unmapColor, "Unmapped" });
        regions.push_back({ 0xE000, 0xEFFF, ramColor,   "User RAM" });
        regions.push_back({ 0xF000, 0xFFFF, unmapColor, "Unmapped" });
    } else {
        uint16_t ramTop = static_cast<uint16_t>(ramCeiling32);
        if (ramTop > 0)
            regions.push_back({ 0x0000, (uint16_t)(ramTop - 1), ramColor, "User RAM" });
        regions.push_back({ ramTop, 0xFFFF, unmapColor, "Unmapped" });
    }

    // --- Layer 1: CPU-reserved areas ---
    regions.push_back({ 0x0000, 0x00FF, IM_COL32(100, 100, 255, 255), "Zero Page" });
    regions.push_back({ 0x0100, 0x01FF, IM_COL32(255, 165,   0, 255), "Stack" });
    regions.push_back({ 0x0200, 0x027F, IM_COL32(  0, 200, 255, 255), "Keyboard Buffer" });

    // --- Layer 2: hardware cards / I/O / ROMs ---
    if (graphicsCardEnabled)
        regions.push_back({ 0x2000, 0x3FFF, IM_COL32(0, 255, 200, 255), "GEN2 HGR Framebuffer" });
    if (a1ioRtcEnabled)
        regions.push_back({ 0x2000, 0x200F, IM_COL32(255, 150, 50, 255), "IO_RTC VIA I/O" });
    if (microSDEnabled)
        regions.push_back({ 0x8000, 0x9FFF, IM_COL32(255, 200, 80, 255), "SD CARD OS ROM" });
    if (microSDEnabled)
        regions.push_back({ 0xA000, 0xA00F, IM_COL32(255, 150, 50, 255), "VIA 65C22 I/O" });
    if (cffa1Enabled) {
        regions.push_back({ 0x9000, 0xAFDF, IM_COL32(255, 200, 80, 255), "CFFA1 ROM" });
        regions.push_back({ 0xAFE0, 0xAFFF, IM_COL32(255, 150, 50, 255), "CF Card I/O" });
    }
    if (wifiModemEnabled)
        regions.push_back({ 0xB000, 0xB003, IM_COL32(0, 200, 200, 255), "ACIA 65C51 I/O" });

    if (aciEnabled) {
        regions.push_back({ 0xC000, 0xC0FF, IM_COL32(255, 140, 80, 255), "ACI I/O" });
        regions.push_back({ 0xC100, 0xC1FF, IM_COL32(255, 190, 80, 255), "ACI ROM" });
    }
    if (sidEnabled)
        regions.push_back({ 0xC800, 0xCFFF, IM_COL32(200, 100, 255, 255), "A1-SID I/O" });
    if (sidSpecialEditionEnabled)
        regions.push_back({ 0xCC00, 0xCC1F, IM_COL32(200, 100, 255, 255), "A1-AUDIO Special Edition I/O" });
    if (tms9918Enabled)
        regions.push_back({ 0xCC00, 0xCC01, IM_COL32(100, 200, 255, 255), "TMS9918 I/O" });

    regions.push_back({ 0xD000, 0xD0FF, IM_COL32(255, 80, 80, 255), "I/O (KBD/DSP)" });

    // --- Layer 3: loaded ROMs (BASIC/Applesoft Lite, Woz Monitor, Krusader) ---
    static std::vector<std::array<char, 64>> romLabels;
    romLabels.resize(loadedRoms.size());
    for (size_t i = 0; i < loadedRoms.size(); ++i) {
        snprintf(romLabels[i].data(), 64, "%s", loadedRoms[i].name.c_str());
        regions.push_back({ loadedRoms[i].start, loadedRoms[i].end,
                            IM_COL32(255, 255, 80, 255), romLabels[i].data() });
    }

    // P-LAB Juke-Box: drawn after preset ROM labels so violet wins on overlap.
    static char jbProgramsLabel[80];
    static char jbPatLabel[64];
    static char jbPmLabel[64];
    if (uiSnapshot.codeTankEnabled) {
        const ImU32 ctRom = IM_COL32(120, 80, 180, 255);
        snprintf(jbProgramsLabel, sizeof(jbProgramsLabel),
                 "CodeTank ROM %s 16 kB",
                 uiSnapshot.codeTank.jumper == CodeTank::Jumper::Upper16
                 ? "upper" : "lower");
        regions.push_back({ 0x4000, 0x7FFF, ctRom, jbProgramsLabel });
    }
    if (jukeBoxEnabled) {
        const ImU32 jbRomPrograms = IM_COL32(120,  80, 180, 255);
        const ImU32 jbRomPat      = IM_COL32(180, 130, 220, 255);
        const ImU32 jbProgMgr     = IM_COL32(230, 180, 255, 255);
        const auto& jb = uiSnapshot.jukeBox;
        const uint16_t romStart = (jb.jumper == JukeBox::Jumper::RAM16_ROM32)
                                 ? 0x4000 : 0x8000;
        if (jb.jumper == JukeBox::Jumper::RAM16_ROM32) {
            snprintf(jbProgramsLabel, sizeof(jbProgramsLabel),
                     "Juke-Box ROM P%u (32 kB, %s)",
                     jb.currentPage,
                     jb.chipMode == JukeBox::ChipMode::Flash ? "Flash" : "EEPROM");
        } else {
            snprintf(jbProgramsLabel, sizeof(jbProgramsLabel),
                     "Juke-Box ROM P%u:S%u (16 kB, %s)",
                     jb.currentPage, jb.currentSubPage,
                     jb.chipMode == JukeBox::ChipMode::Flash ? "Flash" : "EEPROM");
        }
        snprintf(jbPatLabel, sizeof(jbPatLabel), "Juke-Box PAT P%u", jb.currentPage);
        snprintf(jbPmLabel, sizeof(jbPmLabel), "Juke-Box Program Manager P%u", jb.currentPage);
        regions.push_back({ romStart, 0xBBFF, jbRomPrograms, jbProgramsLabel });
        regions.push_back({ 0xBC00,   0xBCFF, jbRomPat,      jbPatLabel });
        regions.push_back({ 0xBD00,   0xBFFF, jbProgMgr,     jbPmLabel });
    }

    // --- Layer 4: loaded programs (top priority) ---
    static std::vector<std::array<char, 64>> progLabels;
    progLabels.resize(loadedPrograms.size());
    for (size_t i = 0; i < loadedPrograms.size(); ++i) {
        snprintf(progLabels[i].data(), 64, "%s", loadedPrograms[i].name.c_str());
        regions.push_back({ loadedPrograms[i].start, loadedPrograms[i].end,
                            IM_COL32(100, 230, 100, 255), progLabels[i].data() });
    }

    return regions;
}

void MainWindow_ImGui::renderMemoryMapGridWindow()
{
    ImGui::SetNextWindowSize(ImVec2(880, 580), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Memory Map Grid", &showMemoryMapGrid)) {
        ImGui::End();
        return;
    }

    auto regions = buildMemoryRegions();
    int numRegions = static_cast<int>(regions.size());

    const uint32_t ramCeiling32 = static_cast<uint32_t>(presetRamKB) * 1024;
    const bool fullRam = (ramCeiling32 >= 0x10000);
    const ImU32 ramColor   = IM_COL32( 80, 200,  80, 255);
    const ImU32 unmapColor = IM_COL32( 40,  40,  40, 255);

    // Grille 2×2 : ligne du haut = carte | légende ; ligne du bas = I/O | ACI + vecteurs
    // (ACI et CPU vectors sous la ligne horizontale médiane de la fenêtre)
    const int COLS = 16;  // 16 columns x 16 rows = 256 pages = 64KB
    const int ROWS = 16;
    const float cellSize = 16.0f;
    const float spacing = 1.0f;
    const float gridW = COLS * (cellSize + spacing);
    const float gridH = ROWS * (cellSize + spacing);
    const float mapColW = gridW + 40.0f;

    const uint8_t* memPtr = uiSnapshot.memory.data();
    const uint16_t pc = uiSnapshot.programCounter;
    const int pcPage = pc >> 8;
    const uint8_t sp = uiSnapshot.stackPointer;
    const int spPage = 1; // stack is always page 1

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
            uint16_t addr = (uint16_t)(page << 8);

            // Find region color (last match wins — layers override)
            ImU32 baseColor = IM_COL32(40, 40, 40, 255);
            for (int r = 0; r < numRegions; ++r) {
                if (addr >= regions[r].start && addr <= regions[r].end) {
                    baseColor = regions[r].color;
                }
            }

            // Check if page has non-zero data (for RAM regions, show activity)
            // A page is User RAM only if no higher layer overrode the base RAM color
            bool hasData = false;
            bool isUserRam = (baseColor == ramColor);
            if (isUserRam) {
                for (int b = 0; b < 256; ++b) {
                    if (memPtr[addr + b] != 0) {
                        hasData = true;
                        break;
                    }
                }
            }

            // Empty RAM pages: dark green (available but unused)
            // Used RAM pages: bright green (active data)
            ImU32 cellColor = baseColor;
            if (isUserRam && !hasData) {
                cellColor = IM_COL32(20, 60, 20, 255); // dark green — RAM available but empty
            } else if (isUserRam && hasData) {
                cellColor = IM_COL32(80, 220, 80, 255); // bright green — RAM in use
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
                    // Last match wins (same as grid rendering)
                    const char* tooltipLabel = nullptr;
                    for (int r = 0; r < numRegions; ++r) {
                        if (addr >= regions[r].start && addr <= regions[r].end)
                            tooltipLabel = regions[r].label;
                    }
                    if (tooltipLabel) ImGui::Text("%s", tooltipLabel);
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
        int kb = (row + 1) * 4;
        char label[16];
        snprintf(label, sizeof(label), "%dK", kb);
        drawList->AddText(ImVec2(rightMargin, y), IM_COL32(150, 150, 150, 255), label);
    }

        ImGui::Dummy(ImVec2(mapColW, gridH));
        ImGui::Text("PC = $%04X  SP = $01%02X", pc, sp);

        ImGui::TableSetColumnIndex(1);
        ImGui::Text("Legend:");
        ImGui::Separator();
        // Skip layer 0 (User RAM / Unmapped base) — show from layer 1 onward,
        // and deduplicate by label+color.
        std::vector<std::pair<ImU32, const char*>> seen;
        for (int i = 2; i < numRegions; ++i) {
            if (regions[i].color == unmapColor) continue;
            // Deduplicate
            bool dup = false;
            for (const auto& s : seen) {
                if (s.first == regions[i].color && strcmp(s.second, regions[i].label) == 0) { dup = true; break; }
            }
            if (dup) continue;
            seen.push_back({regions[i].color, regions[i].label});
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + 12, p.y + 12), regions[i].color);
            dl->AddRect(p, ImVec2(p.x + 12, p.y + 12), IM_COL32(180, 180, 180, 255));
            ImGui::Dummy(ImVec2(16, 14));
            ImGui::SameLine();
            ImGui::Text("$%04X-$%04X %s", regions[i].start, regions[i].end, regions[i].label);
        }
        // Always show User RAM and Unmapped at the end
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + 12, p.y + 12), ramColor);
            dl->AddRect(p, ImVec2(p.x + 12, p.y + 12), IM_COL32(180, 180, 180, 255));
            ImGui::Dummy(ImVec2(16, 14));
            ImGui::SameLine();
            if (fullRam)
                ImGui::Text("$0000-$FFFF User RAM (64 KB)");
            else
                ImGui::Text("$0000-$%04X User RAM (%d KB)", (uint32_t)(ramCeiling32 - 1), presetRamKB);
        }
        if (!fullRam) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + 12, p.y + 12), unmapColor);
            dl->AddRect(p, ImVec2(p.x + 12, p.y + 12), IM_COL32(180, 180, 180, 255));
            ImGui::Dummy(ImVec2(16, 14));
            ImGui::SameLine();
            ImGui::Text("Unmapped");
        }

        // --- Ligne 1 : I/O sous la carte | ACI + vecteurs sous la légende ---
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("I/O registers (PIA 6821):");
        if (aciEnabled) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "  Apple Cassette Interface");
            ImGui::BulletText("$C000  OUT  - Tape output toggle");
            ImGui::BulletText("$C081  IN   - Tape input read");
        }
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "  Keyboard & Display");
        ImGui::BulletText("$D010  KBD   - Keyboard data");
        ImGui::BulletText("$D011  KBDCR - Keyboard control");
        ImGui::BulletText("$D012  DSP   - Display output");
        ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f),
            "  Aliases: $D0Fx = $D01x");
        if (tms9918Enabled) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "  P-LAB TMS9918 VDP");
            ImGui::BulletText("$CC00  DATA - VRAM data port");
            ImGui::BulletText("$CC01  CTRL - Control/status");
        }
        if (microSDEnabled) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "  P-LAB microSD Storage Card (65C22 VIA)");
            ImGui::BulletText("$A000  PORTB - Control (bit0: CPU_STROBE, bit7: MCU_STROBE)");
            ImGui::BulletText("$A001  PORTA - Data bus (bidirectional)");
            ImGui::BulletText("$A003  DDRA  - Data Direction A");
            ImGui::BulletText("$A004-$A005  Timer 1 Counter");
            ImGui::BulletText("$A00D  IFR   - Interrupt Flags");
            ImGui::BulletText("$8000-$9FFF  SD CARD OS ROM (8KB EEPROM)");
        }
        if (sidEnabled) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "  P-LAB A1-SID Sound Card");
            ImGui::BulletText("$C800-$C806  Voice 1 (freq, PW, ctrl, ADSR)");
            ImGui::BulletText("$C807-$C80D  Voice 2");
            ImGui::BulletText("$C80E-$C814  Voice 3");
            ImGui::BulletText("$C815-$C818  Filter (cutoff, res, mode/vol)");
            ImGui::BulletText("$C819-$C81C  Read-only (POT, OSC3, ENV3)");
        }
        if (sidSpecialEditionEnabled) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "  P-LAB A1-AUDIO Special Edition");
            ImGui::BulletText("$CC00-$CC06  Voice 1 (freq, PW, ctrl, ADSR)");
            ImGui::BulletText("$CC07-$CC0D  Voice 2");
            ImGui::BulletText("$CC0E-$CC14  Voice 3");
            ImGui::BulletText("$CC15-$CC18  Filter (cutoff, res, mode/vol)");
            ImGui::BulletText("$CC19-$CC1C  Read-only (POT, OSC3, ENV3)");
        }
        if (wifiModemEnabled) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "  P-LAB Wi-Fi Modem (65C51 ACIA)");
            ImGui::BulletText("$B000  DATA   - Serial data I/O");
            ImGui::BulletText("$B001  STATUS - Flags (TDRE, RDRF, DCD)");
            ImGui::BulletText("$B002  CMD    - Command (DTR, echo, RTS)");
            ImGui::BulletText("$B003  CTRL   - Control (baud, word len)");
        }
        if (terminalCardEnabled) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "  P-LAB Terminal Card (passive)");
            ImGui::BulletText("Eavesdrops $D012 display writes");
            ImGui::BulletText("Injects keys via $D010/$D011");
            ImGui::BulletText("TCP server on localhost:6502");
        }
        if (a1ioRtcEnabled) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "  P-LAB I/O Board & RTC (65C22 VIA)");
            ImGui::BulletText("$2000  PORTB - Data bus (ATMEGA)");
            ImGui::BulletText("$2001  PORTA - Addr/ctrl/strobe");
            ImGui::BulletText("$200A  SR    - Shift Reg (16 outputs)");
            ImGui::BulletText("$200B  ACR   - Aux Control Register");
            ImGui::BulletText("Regs 0-5: RTC  6: Temp  10-17: ADC  20-23: DIN");
        }
        if (uiSnapshot.codeTankEnabled) {
            const auto& ct = uiSnapshot.codeTank;
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f),
                "  CodeTank 28c256");
            ImGui::BulletText("$4000-$7FFF  ROM window (%s 16 kB half)",
                ct.jumper == CodeTank::Jumper::Upper16 ? "upper" : "lower");
            ImGui::BulletText("       No $CA00 latch; selection is the board jumper");
        }
        if (jukeBoxEnabled) {
            const auto& jb = uiSnapshot.jukeBox;
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f),
                "  P-LAB Juke-Box (%s)",
                jb.chipMode == JukeBox::ChipMode::Flash ? "Flash" : "EEPROM 28c256");
            if (jb.jumper == JukeBox::Jumper::RAM16_ROM32) {
                ImGui::BulletText("$4000-$BFFF  ROM window (32 kB, RAM16/ROM32)");
            } else {
                ImGui::BulletText("$8000-$BFFF  ROM window (16 kB, RAM32/ROM16)");
            }
            ImGui::BulletText("$CA00  Bank-select latch = $%02X", jb.bankRegister);
            if (jb.jumper == JukeBox::Jumper::RAM16_ROM32) {
                ImGui::BulletText("       Page P%u of %u", jb.currentPage, jb.pageCount);
            } else {
                ImGui::BulletText("       Page P%u:S%u of %u",
                    jb.currentPage, jb.currentSubPage, jb.pageCount);
            }
            if (jb.firmwarePresent)
                ImGui::BulletText("       Boot page: P%u", jb.bootPage);
            ImGui::BulletText("$B800  Save Program  %s",
                (jb.chipMode == JukeBox::ChipMode::EEPROM28C256 && jb.writable)
                    ? "(RW enabled)" : "(read-only)");
            ImGui::BulletText("$BD00  Program Manager (signature $A5 / LDA zp)");
            ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f),
                "  BD00R from Woz -> &prompt. H, D, L<id>, P<0-F>, B, X");
        }

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

void MainWindow_ImGui::renderMemoryBarWindow()
{
    applyPendingLayout("Memory Map Bar");
    ImGui::SetNextWindowSize(ImVec2(420, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Memory Map Bar", &showMemoryBar)) {
        ImGui::End();
        return;
    }

    auto regions = buildMemoryRegions();
    int numRegions = static_cast<int>(regions.size());

    const ImU32 ramColor   = IM_COL32( 80, 200,  80, 255);
    const uint8_t* memPtr = uiSnapshot.memory.data();
    const uint16_t pc = uiSnapshot.programCounter;

    // --- Flatten regions at page granularity (256 pages, last-match-wins) ---
    struct FlatRegion {
        uint16_t start, end;  // inclusive
        ImU32 color;
        const char* label;
    };

    struct PageInfo {
        ImU32 color;
        const char* label;
    };
    PageInfo pageMap[256];

    for (int page = 0; page < 256; ++page) {
        uint16_t addr = static_cast<uint16_t>(page << 8);
        ImU32 color = IM_COL32(40, 40, 40, 255);
        const char* label = "Unmapped";
        for (int r = 0; r < numRegions; ++r) {
            if (addr >= regions[r].start && addr <= regions[r].end) {
                color = regions[r].color;
                label = regions[r].label;
            }
        }
        // User RAM heatmap: scan 256 bytes for non-zero data
        if (color == ramColor) {
            bool hasData = false;
            for (int b = 0; b < 256; ++b) {
                if (memPtr[addr + b] != 0) { hasData = true; break; }
            }
            color = hasData ? IM_COL32(80, 220, 80, 255) : IM_COL32(20, 60, 20, 255);
        }
        pageMap[page] = { color, label };
    }

    // Merge consecutive pages with same color+label into flat spans
    std::vector<FlatRegion> flat;
    flat.reserve(32);
    flat.push_back({ 0x0000, 0x00FF, pageMap[0].color, pageMap[0].label });
    for (int page = 1; page < 256; ++page) {
        auto& prev = flat.back();
        if (pageMap[page].color == prev.color && pageMap[page].label == prev.label) {
            prev.end = static_cast<uint16_t>((page << 8) | 0xFF);
        } else {
            flat.push_back({
                static_cast<uint16_t>(page << 8),
                static_cast<uint16_t>((page << 8) | 0xFF),
                pageMap[page].color,
                pageMap[page].label
            });
        }
    }

    // --- Layout constants ---
    const float gutterW = 42.0f;
    const float barW = 48.0f;
    // Bar 2 is a faithful clone of bar 1 — same 48 px width, same bevels,
    // same Y positions (the distorted scale that gives every region enough
    // room for its label). Connector lines drawn between the two bars
    // delimit each zone visually; per-zone labels live in the gap.
    const float bar2W = 48.0f;
    const float labelGap = 10.0f;
    const float textH = ImGui::GetTextLineHeight();
    const float minRegionH = textH + 4.0f;  // every region tall enough for its name
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float barX = origin.x + gutterW;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Max label width across all flat regions (label line + address sub-line)
    float maxLabelW = 0.0f;
    for (const auto& fr : flat) {
        uint32_t sizeBytes = static_cast<uint32_t>(fr.end) - fr.start + 1;
        char labelBuf[128];
        if (sizeBytes >= 1024)
            snprintf(labelBuf, sizeof(labelBuf), "%s (%u KB)", fr.label, (unsigned)(sizeBytes / 1024));
        else
            snprintf(labelBuf, sizeof(labelBuf), "%s (%u B)", fr.label, (unsigned)sizeBytes);
        float w = ImGui::CalcTextSize(labelBuf).x;
        char addrBuf[24];
        snprintf(addrBuf, sizeof(addrBuf), "$%04X-$%04X", fr.start, fr.end);
        w = std::max(w, ImGui::CalcTextSize(addrBuf).x);
        if (w > maxLabelW) maxLabelW = w;
    }
    const float bar2X = barX + barW + labelGap + maxLabelW + labelGap;

    // --- Compute Y positions with minimum height per region ---
    // First pass: compute natural (proportional) height, clamp to minRegionH
    const float availH = std::max(100.0f, ImGui::GetContentRegionAvail().y - 4.0f);
    std::vector<float> regionH(flat.size());
    float totalNatural = 0.0f;
    float totalClamped = 0.0f;
    for (size_t i = 0; i < flat.size(); ++i) {
        float natural = (static_cast<float>(flat[i].end - flat[i].start + 1) / 65536.0f) * availH;
        regionH[i] = std::max(natural, minRegionH);
        totalNatural += natural;
        totalClamped += regionH[i];
    }
    // Scale large regions down so everything fits within availH if clamping expanded the total
    const float barHeight = std::max(availH, totalClamped);
    if (totalClamped > availH) {
        float excess = totalClamped - availH;
        // Shrink only regions that are bigger than minRegionH, proportionally
        float shrinkable = 0.0f;
        for (size_t i = 0; i < flat.size(); ++i)
            if (regionH[i] > minRegionH) shrinkable += (regionH[i] - minRegionH);
        if (shrinkable > 0.0f) {
            float ratio = std::min(1.0f, excess / shrinkable);
            for (size_t i = 0; i < flat.size(); ++i) {
                if (regionH[i] > minRegionH) {
                    float give = (regionH[i] - minRegionH) * ratio;
                    regionH[i] -= give;
                }
            }
        }
    }
    // Second pass: accumulate Y positions
    std::vector<float> regionY0(flat.size()), regionY1(flat.size());
    float curY = origin.y;
    for (size_t i = 0; i < flat.size(); ++i) {
        regionY0[i] = curY;
        regionY1[i] = curY + regionH[i];
        curY += regionH[i];
    }
    const float totalBarH = curY - origin.y;

    // addrToY: interpolate within the distorted region layout — both bars
    // share this mapping (bar 2 is a clone of bar 1, not a proportional
    // sibling).
    auto addrToY = [&](uint32_t addr) -> float {
        for (size_t i = 0; i < flat.size(); ++i) {
            uint32_t rStart = flat[i].start;
            uint32_t rEnd   = static_cast<uint32_t>(flat[i].end) + 1;
            if (addr >= rStart && addr < rEnd) {
                float t = static_cast<float>(addr - rStart) / static_cast<float>(rEnd - rStart);
                return regionY0[i] + t * (regionY1[i] - regionY0[i]);
            }
        }
        return regionY1.back();
    };

    // Helper: lighten/darken a color for 3D bevel effect
    auto lighten = [](ImU32 c, int amount) -> ImU32 {
        int r = std::min(255, (int)((c >>  0) & 0xFF) + amount);
        int g = std::min(255, (int)((c >>  8) & 0xFF) + amount);
        int b = std::min(255, (int)((c >> 16) & 0xFF) + amount);
        int a = (c >> 24) & 0xFF;
        return IM_COL32(r, g, b, a);
    };
    auto darken = [](ImU32 c, int amount) -> ImU32 {
        int r = std::max(0, (int)((c >>  0) & 0xFF) - amount);
        int g = std::max(0, (int)((c >>  8) & 0xFF) - amount);
        int b = std::max(0, (int)((c >> 16) & 0xFF) - amount);
        int a = (c >> 24) & 0xFF;
        return IM_COL32(r, g, b, a);
    };

    // --- Outer frames ---
    dl->AddRect(ImVec2(barX - 1, origin.y - 1),
                ImVec2(barX + barW + 1, origin.y + totalBarH + 1),
                IM_COL32(80, 80, 80, 255), 2.0f, 0, 1.0f);
    dl->AddRect(ImVec2(bar2X - 1, origin.y - 1),
                ImVec2(bar2X + bar2W + 1, origin.y + totalBarH + 1),
                IM_COL32(80, 80, 80, 255), 2.0f, 0, 1.0f);

    // Three-pass rendering so labels stay on top of the connector lines:
    //   Pass 1 — both bars (fills + bevels + separators), identical look
    //   Pass 2 — connector lines from each bar-1 region edge to the same
    //            edge in bar-2, visualising the per-region scale distortion
    //   Pass 3 — region labels in the middle gap (drawn last so the
    //            connector lines never punch through the text)

    // --- Pass 1: draw both bars (mirror styling) ---
    for (size_t ri = 0; ri < flat.size(); ++ri) {
        const auto& fr = flat[ri];
        const float y0 = regionY0[ri];
        const float y1 = regionY1[ri];
        const float h  = y1 - y0;

        // Bar 1 — distorted scale (min height guarantees label space).
        dl->AddRectFilled(ImVec2(barX, y0), ImVec2(barX + barW, y1), fr.color);
        if (h > 4.0f) {
            dl->AddLine(ImVec2(barX, y0 + 0.5f), ImVec2(barX + barW, y0 + 0.5f),
                        lighten(fr.color, 40), 1.0f);
            dl->AddLine(ImVec2(barX, y1 - 0.5f), ImVec2(barX + barW, y1 - 0.5f),
                        darken(fr.color, 40), 1.0f);
        }
        dl->AddLine(ImVec2(barX, y1), ImVec2(barX + barW, y1),
                    IM_COL32(30, 30, 30, 200), 1.0f);

        // Bar 2 — identical clone of bar 1 (same Y positions, same bevels).
        dl->AddRectFilled(ImVec2(bar2X, y0), ImVec2(bar2X + bar2W, y1), fr.color);
        if (h > 4.0f) {
            dl->AddLine(ImVec2(bar2X, y0 + 0.5f), ImVec2(bar2X + bar2W, y0 + 0.5f),
                        lighten(fr.color, 40), 1.0f);
            dl->AddLine(ImVec2(bar2X, y1 - 0.5f), ImVec2(bar2X + bar2W, y1 - 0.5f),
                        darken(fr.color, 40), 1.0f);
        }
        dl->AddLine(ImVec2(bar2X, y1), ImVec2(bar2X + bar2W, y1),
                    IM_COL32(30, 30, 30, 200), 1.0f);
    }

    // --- Pass 2: connector lines between bars (zone delimiters) ---
    // Bars 1 and 2 share the same Y layout, so each region's boundaries
    // line up at identical heights on both sides. We paint the gap between
    // the bars with a faint zone-coloured tint (so the eye groups the two
    // bar slabs into a single zone), then draw a dark hairline at every
    // boundary so the transitions are unambiguous.
    {
        const float bar1Right = barX + barW;
        const float bar2Left  = bar2X;
        for (size_t ri = 0; ri < flat.size(); ++ri) {
            const ImU32 zoneTint = (flat[ri].color & 0x00FFFFFFu) | (90u << 24);
            dl->AddRectFilled(ImVec2(bar1Right, regionY0[ri]),
                              ImVec2(bar2Left,  regionY1[ri]),
                              zoneTint);
        }
        // Top edge of the very first region — every subsequent boundary is
        // covered by the bottom-edge loop below.
        dl->AddLine(ImVec2(bar1Right, regionY0[0]),
                    ImVec2(bar2Left,  regionY0[0]),
                    IM_COL32(60, 60, 60, 220), 1.5f);
        for (size_t ri = 0; ri < flat.size(); ++ri) {
            dl->AddLine(ImVec2(bar1Right, regionY1[ri]),
                        ImVec2(bar2Left,  regionY1[ri]),
                        IM_COL32(60, 60, 60, 220), 1.5f);
        }
    }

    // --- Pass 3: per-zone labels (drawn last so the connector quads stay
    // visible underneath but the text remains crisp). ---
    for (size_t ri = 0; ri < flat.size(); ++ri) {
        const auto& fr = flat[ri];
        const float y0 = regionY0[ri];
        const float y1 = regionY1[ri];
        const float h  = y1 - y0;

        uint32_t sizeBytes = static_cast<uint32_t>(fr.end) - fr.start + 1;
        char labelBuf[128];
        if (sizeBytes >= 1024)
            snprintf(labelBuf, sizeof(labelBuf), "%s (%u KB)", fr.label, (unsigned)(sizeBytes / 1024));
        else
            snprintf(labelBuf, sizeof(labelBuf), "%s (%u B)", fr.label, (unsigned)sizeBytes);

        // Centre the text in the gap between the two bars rather than
        // anchoring it to bar 1's right edge — looks more balanced now
        // that the bars are the same width on either side.
        const float gapMidX  = (barX + barW + bar2X) * 0.5f;
        const float labelW   = ImGui::CalcTextSize(labelBuf).x;
        const float labelX   = gapMidX - labelW * 0.5f;
        const float labelY   = (y0 + y1) * 0.5f - textH * 0.5f;

        // Tiny dark tablet behind the text so it pops above the colour
        // tint — same trick the PC indicator uses below.
        dl->AddRectFilled(
            ImVec2(labelX - 3, labelY - 1),
            ImVec2(labelX + labelW + 3, labelY + textH + 1),
            IM_COL32(28, 28, 36, 200), 2.0f);
        dl->AddText(ImVec2(labelX, labelY),
                    IM_COL32(232, 232, 232, 255), labelBuf);

        // Address range underneath when there's vertical room for two lines.
        if (h > textH * 2.0f + 6.0f) {
            char addrBuf[24];
            snprintf(addrBuf, sizeof(addrBuf), "$%04X-$%04X", fr.start, fr.end);
            const float addrW = ImGui::CalcTextSize(addrBuf).x;
            const float addrX = gapMidX - addrW * 0.5f;
            const float addrY = labelY + textH + 1;
            dl->AddRectFilled(
                ImVec2(addrX - 3, addrY - 1),
                ImVec2(addrX + addrW + 3, addrY + textH + 1),
                IM_COL32(28, 28, 36, 180), 2.0f);
            dl->AddText(ImVec2(addrX, addrY),
                        IM_COL32(150, 150, 150, 255), addrBuf);
        }
    }

    // --- Address labels in gutter ---
    // $0000 at top, $FFFF at bottom are always drawn. Intermediate 8 KB
    // marks are only drawn when they don't overlap with each other or
    // with the fixed $0000/$FFFF labels.
    {
        const float topLabelY  = origin.y - 1;
        const float botLabelY  = origin.y + totalBarH - textH + 1;
        dl->AddText(ImVec2(origin.x, topLabelY),
                    IM_COL32(180, 180, 180, 255), "$0000");
        dl->AddText(ImVec2(origin.x, botLabelY),
                    IM_COL32(180, 180, 180, 255), "$FFFF");

        const float minSpacing = textH + 2.0f;
        float lastLabelY = topLabelY;  // tracks the bottom of the last drawn label

        for (int kb = 8; kb < 64; kb += 8) {
            uint32_t addr = static_cast<uint32_t>(kb) * 1024;
            float y = addrToY(addr);
            // Tick line across bar (always drawn)
            dl->AddLine(ImVec2(barX, y), ImVec2(barX + barW, y),
                        IM_COL32(255, 255, 255, 20), 1.0f);
            // Label only if enough room above and below
            float labelY = y - textH * 0.5f;
            if (labelY - lastLabelY >= minSpacing && botLabelY - labelY >= minSpacing) {
                char tick[8];
                snprintf(tick, sizeof(tick), "$%04X", static_cast<uint16_t>(addr));
                float tw = ImGui::CalcTextSize(tick).x;
                dl->AddText(ImVec2(barX - tw - 4, labelY),
                            IM_COL32(100, 100, 100, 255), tick);
                lastLabelY = labelY;
            }
        }
    }

    // --- PC indicator on bar 2: white arrow + label ---
    // Both bars share `addrToY` now that bar 2 mirrors bar 1's distorted
    // Y layout — the arrow lands on the right-hand side of the same
    // region the PC actually lives in.
    {
        float pcY = addrToY(pc);
        float sz = 6.0f;
        // Arrow on the right side, pointing left into bar 2
        dl->AddTriangleFilled(
            ImVec2(bar2X + bar2W + 3 + sz, pcY - sz),
            ImVec2(bar2X + bar2W + 3,      pcY),
            ImVec2(bar2X + bar2W + 3 + sz, pcY + sz),
            IM_COL32(255, 255, 255, 255));
        // Marker line across bar 2
        dl->AddLine(ImVec2(bar2X, pcY), ImVec2(bar2X + bar2W, pcY),
                    IM_COL32(255, 255, 255, 120), 1.0f);
        // "PC $XXXX" label to the right of the arrow
        char pcLabel[16];
        snprintf(pcLabel, sizeof(pcLabel), "PC $%04X", pc);
        float labelW = ImGui::CalcTextSize(pcLabel).x;
        float lx = bar2X + bar2W + 5 + sz;
        dl->AddRectFilled(
            ImVec2(lx - 2, pcY - sz - 1),
            ImVec2(lx + labelW + 2, pcY - sz + ImGui::GetTextLineHeight()),
            IM_COL32(40, 40, 50, 220), 2.0f);
        dl->AddText(ImVec2(lx, pcY - sz),
                    IM_COL32(255, 255, 255, 255), pcLabel);
    }

    // --- Viewport sync overlay ---
    if (showMemoryViewer && memoryViewer) {
        auto vp = memoryViewer->getViewportRange();
        float vpY0 = addrToY(vp.startAddress);
        float vpY1 = addrToY(vp.endAddress);
        if (vpY1 - vpY0 < 4.0f) vpY1 = vpY0 + 4.0f;
        // Bracket lines on left and right
        dl->AddRectFilled(ImVec2(barX, vpY0), ImVec2(barX + barW, vpY1),
                          IM_COL32(255, 255, 255, 35));
        dl->AddRect(ImVec2(barX, vpY0), ImVec2(barX + barW, vpY1),
                    IM_COL32(255, 255, 255, 200), 0.0f, 0, 1.5f);
        // Small bracket wings
        float wingW = 4.0f;
        dl->AddLine(ImVec2(barX - wingW, vpY0), ImVec2(barX, vpY0),
                    IM_COL32(255, 255, 255, 200), 1.5f);
        dl->AddLine(ImVec2(barX - wingW, vpY1), ImVec2(barX, vpY1),
                    IM_COL32(255, 255, 255, 200), 1.5f);
        dl->AddLine(ImVec2(barX + barW, vpY0), ImVec2(barX + barW + wingW, vpY0),
                    IM_COL32(255, 255, 255, 200), 1.5f);
        dl->AddLine(ImVec2(barX + barW, vpY1), ImVec2(barX + barW + wingW, vpY1),
                    IM_COL32(255, 255, 255, 200), 1.5f);
    }

    // --- Tooltip + click-to-navigate ---
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
        ImVec2 mouse = ImGui::GetMousePos();
        if (mouse.x >= barX && mouse.x < barX + barW &&
            mouse.y >= origin.y && mouse.y < origin.y + totalBarH) {
            // Reverse-map Y → address through distorted region layout
            uint32_t hoverAddr = 0xFFFF;
            const char* regionLabel = "Unknown";
            uint16_t regionStart = 0, regionEnd = 0;
            for (size_t i = 0; i < flat.size(); ++i) {
                if (mouse.y >= regionY0[i] && mouse.y < regionY1[i]) {
                    float t = (mouse.y - regionY0[i]) / (regionY1[i] - regionY0[i]);
                    uint32_t span = static_cast<uint32_t>(flat[i].end) - flat[i].start + 1;
                    hoverAddr = flat[i].start + static_cast<uint32_t>(t * span);
                    if (hoverAddr > 0xFFFF) hoverAddr = 0xFFFF;
                    regionLabel = flat[i].label;
                    regionStart = flat[i].start;
                    regionEnd = flat[i].end;
                    break;
                }
            }

            // Hover highlight line across the bar
            dl->AddLine(ImVec2(barX, mouse.y), ImVec2(barX + barW, mouse.y),
                        IM_COL32(255, 255, 255, 100), 1.0f);

            ImGui::BeginTooltip();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.6f, 1.0f), "$%04X",
                               static_cast<uint16_t>(hoverAddr));
            ImGui::Text("%s", regionLabel);
            uint32_t sz = static_cast<uint32_t>(regionEnd) - regionStart + 1;
            if (sz >= 1024)
                ImGui::TextDisabled("$%04X-$%04X (%u KB)", regionStart, regionEnd, (unsigned)(sz / 1024));
            else
                ImGui::TextDisabled("$%04X-$%04X (%u B)", regionStart, regionEnd, (unsigned)sz);
            // CodeTank/JukeBox bank info
            if (uiSnapshot.codeTankEnabled
                && hoverAddr >= 0x4000 && hoverAddr <= 0x7FFF) {
                ImGui::Separator();
                ImGui::Text("CodeTank %s 16 kB half",
                            uiSnapshot.codeTank.jumper == CodeTank::Jumper::Upper16
                            ? "upper" : "lower");
            } else if (jukeBoxEnabled) {
                const auto& jb = uiSnapshot.jukeBox;
                int romStart = (jb.jumper == JukeBox::Jumper::RAM16_ROM32) ? 0x4000 : 0x8000;
                if (static_cast<int>(hoverAddr) >= romStart && hoverAddr <= 0xBFFF) {
                    ImGui::Separator();
                    if (jb.jumper == JukeBox::Jumper::RAM16_ROM32)
                        ImGui::Text("Page P%u of %u (%s)", jb.currentPage, jb.pageCount,
                            jb.chipMode == JukeBox::ChipMode::Flash ? "Flash" : "EEPROM");
                    else
                        ImGui::Text("Page P%u:S%u of %u (%s)", jb.currentPage, jb.currentSubPage,
                            jb.pageCount,
                            jb.chipMode == JukeBox::ChipMode::Flash ? "Flash" : "EEPROM");
                }
            }
            ImGui::EndTooltip();

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                showMemoryViewer = true;
                memoryViewer->navigateToAddress(static_cast<int>(hoverAddr));
            }
        }
    }

    // Right margin holds the PC arrow + "PC $XXXX" label past the bar 2 edge.
    const float pcLabelW = ImGui::CalcTextSize("PC $FFFF").x;
    const float rightMargin = 5.0f + 6.0f /*arrow sz*/ + pcLabelW + 6.0f;
    ImGui::Dummy(ImVec2(gutterW + barW + labelGap + maxLabelW + labelGap + bar2W + rightMargin, totalBarH));
    if (ImGuiWindow* w = ImGui::GetCurrentWindow()) {
        memoryBarLastPos = w->Pos;
        memoryBarLastSize = w->SizeFull;
        memoryBarLastGeomValid = true;
    }
    ImGui::End();
}

void MainWindow_ImGui::renderMemoryBarHorizontalWindow()
{
    applyPendingLayout("Memory Map Bar (Horizontal)");
    ImGui::SetNextWindowSize(ImVec2(720, 78), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Memory Map Bar (Horizontal)", &showMemoryBarH)) {
        ImGui::End();
        return;
    }

    auto regions = buildMemoryRegions();
    int numRegions = static_cast<int>(regions.size());

    const ImU32 ramColor   = IM_COL32( 80, 200,  80, 255);
    const uint8_t* memPtr = uiSnapshot.memory.data();
    const uint16_t pc = uiSnapshot.programCounter;

    struct FlatRegion {
        uint16_t start, end;
        ImU32 color;
        const char* label;
    };
    struct PageInfo { ImU32 color; const char* label; };
    PageInfo pageMap[256];

    for (int page = 0; page < 256; ++page) {
        uint16_t addr = static_cast<uint16_t>(page << 8);
        ImU32 color = IM_COL32(40, 40, 40, 255);
        const char* label = "Unmapped";
        for (int r = 0; r < numRegions; ++r) {
            if (addr >= regions[r].start && addr <= regions[r].end) {
                color = regions[r].color;
                label = regions[r].label;
            }
        }
        if (color == ramColor) {
            bool hasData = false;
            for (int b = 0; b < 256; ++b) {
                if (memPtr[addr + b] != 0) { hasData = true; break; }
            }
            color = hasData ? IM_COL32(80, 220, 80, 255) : IM_COL32(20, 60, 20, 255);
        }
        pageMap[page] = { color, label };
    }

    std::vector<FlatRegion> flat;
    flat.reserve(32);
    flat.push_back({ 0x0000, 0x00FF, pageMap[0].color, pageMap[0].label });
    for (int page = 1; page < 256; ++page) {
        auto& prev = flat.back();
        if (pageMap[page].color == prev.color && pageMap[page].label == prev.label) {
            prev.end = static_cast<uint16_t>((page << 8) | 0xFF);
        } else {
            flat.push_back({
                static_cast<uint16_t>(page << 8),
                static_cast<uint16_t>((page << 8) | 0xFF),
                pageMap[page].color,
                pageMap[page].label
            });
        }
    }

    // --- Layout: very wide, very short ---
    const float textH = ImGui::GetTextLineHeight();
    const float topAxisH = textH + 2.0f;       // "$0000"   "$FFFF" row
    const float bar1H = 26.0f;                 // distorted (with min-width per region)
    const float minRegionW = 3.0f;             // every region at least 3 px wide so it's visible
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float availW = std::max(120.0f, avail.x - 4.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float barY0 = origin.y + topAxisH;
    const float barY1 = barY0 + bar1H;

    // --- Distorted widths (min-width per region, rescale if overflow) ---
    std::vector<float> regionW(flat.size());
    float totalClamped = 0.0f;
    for (size_t i = 0; i < flat.size(); ++i) {
        float natural = (static_cast<float>(flat[i].end - flat[i].start + 1) / 65536.0f) * availW;
        regionW[i] = std::max(natural, minRegionW);
        totalClamped += regionW[i];
    }
    if (totalClamped > availW) {
        float excess = totalClamped - availW;
        float shrinkable = 0.0f;
        for (size_t i = 0; i < flat.size(); ++i)
            if (regionW[i] > minRegionW) shrinkable += (regionW[i] - minRegionW);
        if (shrinkable > 0.0f) {
            float ratio = std::min(1.0f, excess / shrinkable);
            for (size_t i = 0; i < flat.size(); ++i) {
                if (regionW[i] > minRegionW) regionW[i] -= (regionW[i] - minRegionW) * ratio;
            }
        }
    }
    std::vector<float> regionX0(flat.size()), regionX1(flat.size());
    {
        float curX = origin.x;
        for (size_t i = 0; i < flat.size(); ++i) {
            regionX0[i] = curX;
            regionX1[i] = curX + regionW[i];
            curX += regionW[i];
        }
    }
    const float totalBarW = regionX1.empty() ? 0.0f : (regionX1.back() - origin.x);

    auto darken = [](ImU32 c, int amount) -> ImU32 {
        int r = std::max(0, (int)((c >>  0) & 0xFF) - amount);
        int g = std::max(0, (int)((c >>  8) & 0xFF) - amount);
        int b = std::max(0, (int)((c >> 16) & 0xFF) - amount);
        int a = (c >> 24) & 0xFF;
        return IM_COL32(r, g, b, a);
    };
    auto lighten = [](ImU32 c, int amount) -> ImU32 {
        int r = std::min(255, (int)((c >>  0) & 0xFF) + amount);
        int g = std::min(255, (int)((c >>  8) & 0xFF) + amount);
        int b = std::min(255, (int)((c >> 16) & 0xFF) + amount);
        int a = (c >> 24) & 0xFF;
        return IM_COL32(r, g, b, a);
    };

    // --- Outer frame ---
    dl->AddRect(ImVec2(origin.x - 1, barY0 - 1),
                ImVec2(origin.x + totalBarW + 1, barY1 + 1),
                IM_COL32(80, 80, 80, 255), 2.0f, 0, 1.0f);

    // --- Render the bar ---
    for (size_t ri = 0; ri < flat.size(); ++ri) {
        const auto& fr = flat[ri];
        dl->AddRectFilled(ImVec2(regionX0[ri], barY0), ImVec2(regionX1[ri], barY1), fr.color);
        if (regionW[ri] > 4.0f) {
            dl->AddLine(ImVec2(regionX0[ri] + 0.5f, barY0), ImVec2(regionX0[ri] + 0.5f, barY1),
                        lighten(fr.color, 40), 1.0f);
            dl->AddLine(ImVec2(regionX1[ri] - 0.5f, barY0), ImVec2(regionX1[ri] - 0.5f, barY1),
                        darken(fr.color, 40), 1.0f);
        }
        dl->AddLine(ImVec2(regionX1[ri], barY0), ImVec2(regionX1[ri], barY1),
                    IM_COL32(30, 30, 30, 200), 1.0f);
    }

    auto addrToX1 = [&](uint32_t addr) -> float {
        for (size_t i = 0; i < flat.size(); ++i) {
            uint32_t rStart = flat[i].start;
            uint32_t rEnd   = static_cast<uint32_t>(flat[i].end) + 1;
            if (addr >= rStart && addr < rEnd) {
                float t = static_cast<float>(addr - rStart) / static_cast<float>(rEnd - rStart);
                return regionX0[i] + t * (regionX1[i] - regionX0[i]);
            }
        }
        return regionX1.back();
    };

    // --- Top axis: $0000 left, $FFFF right + intermediate 8 KB ticks ---
    {
        const float endX = origin.x + totalBarW;
        const float startTextW = ImGui::CalcTextSize("$0000").x;
        const float endTextW   = ImGui::CalcTextSize("$FFFF").x;
        // $0000
        dl->AddText(ImVec2(origin.x, origin.y),
                    IM_COL32(180, 180, 180, 255), "$0000");
        // $FFFF
        dl->AddText(ImVec2(endX - endTextW, origin.y),
                    IM_COL32(180, 180, 180, 255), "$FFFF");

        // Intermediate ticks at 8 KB boundaries. Skip a label if it would
        // collide with its neighbours or the corner labels.
        const float minSpacing = 6.0f;
        float lastLabelRight = origin.x + startTextW + minSpacing;
        const float endLabelLeft = endX - endTextW - minSpacing;
        for (int kb = 8; kb < 64; kb += 8) {
            uint32_t addr = static_cast<uint32_t>(kb) * 1024;
            float x = addrToX1(addr);
            // Tick: thin vertical line through the bar, very faint
            dl->AddLine(ImVec2(x, barY0), ImVec2(x, barY1),
                        IM_COL32(255, 255, 255, 28), 1.0f);
            // Label "$XXXX" centered above the bar, only if it fits
            char tick[8];
            snprintf(tick, sizeof(tick), "$%04X", static_cast<uint16_t>(addr));
            float tw = ImGui::CalcTextSize(tick).x;
            float lx = x - tw * 0.5f;
            if (lx >= lastLabelRight && (lx + tw) <= endLabelLeft) {
                dl->AddText(ImVec2(lx, origin.y),
                            IM_COL32(110, 110, 110, 255), tick);
                lastLabelRight = lx + tw + minSpacing;
            }
        }
    }

    // --- Inline region labels: name centred in each bar segment when room ---
    auto luminance = [](ImU32 c) -> int {
        int r = (c >>  0) & 0xFF;
        int g = (c >>  8) & 0xFF;
        int b = (c >> 16) & 0xFF;
        return (r * 299 + g * 587 + b * 114) / 1000;  // BT.601
    };
    for (size_t ri = 0; ri < flat.size(); ++ri) {
        const auto& fr = flat[ri];
        const float w = regionW[ri];
        if (w < 28.0f) continue;  // tiny region: rely on tooltip
        const char* lbl = fr.label;
        float tw = ImGui::CalcTextSize(lbl).x;
        if (tw + 6.0f > w) continue;  // label wouldn't fit: skip
        const float cx = (regionX0[ri] + regionX1[ri]) * 0.5f - tw * 0.5f;
        const float cy = (barY0 + barY1) * 0.5f - textH * 0.5f;
        const ImU32 fg = (luminance(fr.color) >= 128)
                          ? IM_COL32(20, 20, 20, 255)
                          : IM_COL32(235, 235, 235, 255);
        dl->AddText(ImVec2(cx, cy), fg, lbl);
    }

    // --- PC indicator: triangle below the bar pointing up + label centered ---
    {
        float pcX = addrToX1(pc);
        float sz = 5.0f;
        float ay = barY1 + 1.0f;
        dl->AddTriangleFilled(
            ImVec2(pcX - sz, ay + sz),
            ImVec2(pcX + sz, ay + sz),
            ImVec2(pcX,      ay),
            IM_COL32(255, 255, 255, 255));
        char pcLabel[16];
        snprintf(pcLabel, sizeof(pcLabel), "PC $%04X", pc);
        float lw = ImGui::CalcTextSize(pcLabel).x;
        float lx = pcX - lw * 0.5f;
        if (lx < origin.x) lx = origin.x;
        if (lx + lw > origin.x + totalBarW) lx = origin.x + totalBarW - lw;
        float ly = barY1 + sz + 4.0f;
        dl->AddRectFilled(
            ImVec2(lx - 2, ly - 1),
            ImVec2(lx + lw + 2, ly + ImGui::GetTextLineHeight()),
            IM_COL32(40, 40, 50, 220), 2.0f);
        dl->AddText(ImVec2(lx, ly),
                    IM_COL32(255, 255, 255, 255), pcLabel);
    }

    // --- Memory Viewer viewport overlay ---
    if (showMemoryViewer && memoryViewer) {
        auto vp = memoryViewer->getViewportRange();
        float vpX0 = addrToX1(vp.startAddress);
        float vpX1 = addrToX1(vp.endAddress);
        if (vpX1 - vpX0 < 4.0f) vpX1 = vpX0 + 4.0f;
        dl->AddRectFilled(ImVec2(vpX0, barY0), ImVec2(vpX1, barY1),
                          IM_COL32(255, 255, 255, 35));
        dl->AddRect(ImVec2(vpX0, barY0), ImVec2(vpX1, barY1),
                    IM_COL32(255, 255, 255, 200), 0.0f, 0, 1.5f);
        // Bracket wings on top and bottom
        const float wingH = 4.0f;
        dl->AddLine(ImVec2(vpX0, barY0 - wingH), ImVec2(vpX0, barY0),
                    IM_COL32(255, 255, 255, 200), 1.5f);
        dl->AddLine(ImVec2(vpX1, barY0 - wingH), ImVec2(vpX1, barY0),
                    IM_COL32(255, 255, 255, 200), 1.5f);
        dl->AddLine(ImVec2(vpX0, barY1), ImVec2(vpX0, barY1 + wingH),
                    IM_COL32(255, 255, 255, 200), 1.5f);
        dl->AddLine(ImVec2(vpX1, barY1), ImVec2(vpX1, barY1 + wingH),
                    IM_COL32(255, 255, 255, 200), 1.5f);
    }

    // --- Tooltip + click-to-navigate ---
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
        ImVec2 mouse = ImGui::GetMousePos();
        if (mouse.y >= barY0 && mouse.y < barY1 &&
            mouse.x >= origin.x && mouse.x < origin.x + totalBarW) {
            uint32_t hoverAddr = 0xFFFF;
            const char* regionLabel = "Unknown";
            uint16_t regionStart = 0, regionEnd = 0;
            for (size_t i = 0; i < flat.size(); ++i) {
                if (mouse.x >= regionX0[i] && mouse.x < regionX1[i]) {
                    float t = (mouse.x - regionX0[i]) / (regionX1[i] - regionX0[i]);
                    uint32_t span = static_cast<uint32_t>(flat[i].end) - flat[i].start + 1;
                    hoverAddr = flat[i].start + static_cast<uint32_t>(t * span);
                    if (hoverAddr > 0xFFFF) hoverAddr = 0xFFFF;
                    regionLabel = flat[i].label;
                    regionStart = flat[i].start;
                    regionEnd = flat[i].end;
                    break;
                }
            }
            // Vertical hover line through bar 1
            dl->AddLine(ImVec2(mouse.x, barY0), ImVec2(mouse.x, barY1),
                        IM_COL32(255, 255, 255, 100), 1.0f);

            ImGui::BeginTooltip();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.6f, 1.0f), "$%04X",
                               static_cast<uint16_t>(hoverAddr));
            ImGui::Text("%s", regionLabel);
            uint32_t sz = static_cast<uint32_t>(regionEnd) - regionStart + 1;
            if (sz >= 1024)
                ImGui::TextDisabled("$%04X-$%04X (%u KB)", regionStart, regionEnd, (unsigned)(sz / 1024));
            else
                ImGui::TextDisabled("$%04X-$%04X (%u B)", regionStart, regionEnd, (unsigned)sz);
            ImGui::EndTooltip();

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                showMemoryViewer = true;
                memoryViewer->navigateToAddress(static_cast<int>(hoverAddr));
            }
        }
    }

    // Reserve total layout space (top axis + bar + PC arrow + label).
    float totalH = topAxisH + bar1H + 6.0f + textH + 4.0f;
    ImGui::Dummy(ImVec2(totalBarW, totalH));
    if (ImGuiWindow* w = ImGui::GetCurrentWindow()) {
        memoryBarHLastPos = w->Pos;
        memoryBarHLastSize = w->SizeFull;
        memoryBarHLastGeomValid = true;
    }
    ImGui::End();
}
