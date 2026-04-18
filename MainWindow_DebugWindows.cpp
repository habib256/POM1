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
                hardReset();
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

    const uint32_t ramCeiling32 = static_cast<uint32_t>(presetRamKB) * 1024;
    const bool fullRam = (ramCeiling32 >= 0x10000);
    const ImU32 ramColor   = IM_COL32( 80, 200,  80, 255);
    const ImU32 unmapColor = IM_COL32( 40,  40,  40, 255);

    // --- Layer 0: base (User RAM + Unmapped) ---
    std::vector<MemRegion> regions;
    if (fullRam) {
        regions.push_back({ 0x0000, 0xFFFF, ramColor, "User RAM" });
    } else {
        quint16 ramTop = static_cast<quint16>(ramCeiling32);
        if (ramTop > 0)
            regions.push_back({ 0x0000, (quint16)(ramTop - 1), ramColor, "User RAM" });
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

    // P-LAB Juke-Box: the EEPROM is split into three logical zones, shown in
    // graded shades of violet so they read as "same card" but distinguishable.
    //   - deep violet   : user ROM programs (the bulk, $4000 or $8000 .. $BBFF)
    //   - medium violet : PAT directory ($BC00-$BCFF) -- the 15-byte entries
    //                     patched by the EPROM creator scripts; the Program
    //                     Manager reads them to build the D)IR listing.
    //   - bright violet : Program Manager firmware ($BD00-$BFFF, starts with
    //                     signature $A5).
    if (jukeBoxEnabled) {
        const ImU32 jbRomPrograms = IM_COL32(120,  80, 180, 255); // deep violet
        const ImU32 jbRomPat      = IM_COL32(180, 130, 220, 255); // medium violet
        const ImU32 jbProgMgr     = IM_COL32(230, 180, 255, 255); // bright lavender
        const quint16 romStart = (jukeBoxJumper == JukeBox::Jumper::RAM16_ROM32)
                                 ? 0x4000 : 0x8000;
        const char* programsLabel = (jukeBoxJumper == JukeBox::Jumper::RAM16_ROM32)
            ? "Juke-Box ROM - programs (32 kB)"
            : "Juke-Box ROM - programs (16 kB)";
        regions.push_back({ romStart, 0xBBFF, jbRomPrograms, programsLabel });
        regions.push_back({ 0xBC00,   0xBCFF, jbRomPat,      "Juke-Box PAT (directory)" });
        regions.push_back({ 0xBD00,   0xBFFF, jbProgMgr,     "Juke-Box Program Manager" });
    }

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

    // --- Layer 4: loaded programs (top priority) ---
    static std::vector<std::array<char, 64>> progLabels;
    progLabels.resize(loadedPrograms.size());
    for (size_t i = 0; i < loadedPrograms.size(); ++i) {
        snprintf(progLabels[i].data(), 64, "%s", loadedPrograms[i].name.c_str());
        regions.push_back({ loadedPrograms[i].start, loadedPrograms[i].end,
                            IM_COL32(100, 230, 100, 255), progLabels[i].data() });
    }

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
        if (jukeBoxEnabled) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "  P-LAB Juke-Box (28c256 EEPROM)");
            if (jukeBoxJumper == JukeBox::Jumper::RAM16_ROM32) {
                ImGui::BulletText("$4000-$BFFF  ROM window (32 kB, RAM16/ROM32)");
            } else {
                ImGui::BulletText("$8000-$BFFF  ROM window (16 kB, RAM32/ROM16)");
            }
            ImGui::BulletText("$B800  Save Program  (needs RW jumper)");
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
