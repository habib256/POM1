#include "MemoryViewer_ImGui.h"
#include "imgui.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <charconv>
#include <cstring>

MemoryViewer_ImGui::MemoryViewer_ImGui(Memory* mem)
    : memory(mem)
{
    snapshot.resize(0x10000);
    prevMemory.resize(0x10000, 0);
    changeFrame.resize(0x10000, 0);
}

// Read a byte: live from raw pointer (no I/O side effects) or from snapshot
uint8_t MemoryViewer_ImGui::readByte(int address) const
{
    if ((autoRefresh || !snapshotValid) && liveMemory && !liveMemory->empty())
        return (*liveMemory)[address & 0xFFFF];
    return snapshot[address & 0xFFFF];
}

void MemoryViewer_ImGui::takeSnapshot()
{
    if (liveMemory && liveMemory->size() >= 0x10000) {
        std::copy(liveMemory->begin(), liveMemory->begin() + 0x10000, snapshot.begin());
        snapshotValid = true;
    }
}

const uint8_t* MemoryViewer_ImGui::getMemoryPointer() const
{
    if ((autoRefresh || !snapshotValid) && liveMemory && !liveMemory->empty())
        return liveMemory->data();
    return snapshot.data();
}

void MemoryViewer_ImGui::detectChanges()
{
    const uint8_t* mem = getMemoryPointer();
    if (!mem) return;
    ++frameCounter;
    for (int i = 0; i < 0x10000; ++i) {
        if (mem[i] != prevMemory[i]) {
            changeFrame[i] = frameCounter;
            prevMemory[i] = mem[i];
        }
    }
}

void MemoryViewer_ImGui::render()
{
    if (showChanges)
        detectChanges();

    handleNavigation();
    renderControls();
    ImGui::Separator();
    renderRegionBanner();
    if (showDisasm)
        renderDisasmView();
    else
        renderHexView();

    if (showSearch) {
        renderSearchDialog();
    }
}

void MemoryViewer_ImGui::navigateToAddress(int address)
{
    jumpToAddress(address);
}

void MemoryViewer_ImGui::updateLiveMemory(const std::vector<uint8_t>& memoryImage)
{
    liveMemory = &memoryImage;
    // Deliberately no per-frame snapshot copy. readByte() already reads
    // straight from liveMemory whenever autoRefresh is on or the snapshot
    // has never been taken (line ~18 of this file), so the only callers
    // that need a snapshot copy are explicit user actions: toggling
    // autoRefresh off, clicking Refresh, or jumpToAddress() in frozen
    // mode. Each of those calls takeSnapshot() itself, so this hot path
    // (hit every UI frame from MainWindow_ImGui::render) stays free of
    // the 64 KB std::copy that previously ran whenever autoRefresh was
    // enabled — pure dead work, since readByte under autoRefresh reads
    // liveMemory directly anyway.
}

void MemoryViewer_ImGui::setWriteCallback(std::function<void(uint16_t, uint8_t)> callback)
{
    writeCallback = std::move(callback);
}

void MemoryViewer_ImGui::renderControls()
{
    // Navigation
    ImGui::Text("Navigation:");
    ImGui::SameLine();

    static char addressBuffer[8] = "0000";
    auto parseHexAddress = [](const char* buf, unsigned& out) -> bool {
        const char* end = buf + std::strlen(buf);
        auto [p, ec] = std::from_chars(buf, end, out, 16);
        return ec == std::errc{} && p == end;
    };

    ImGui::SetNextItemWidth(80);
    if (ImGui::InputText("##Address", addressBuffer, sizeof(addressBuffer), ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase)) {
        unsigned addr = 0;
        if (parseHexAddress(addressBuffer, addr)) {
            jumpToAddress(static_cast<int>(addr));
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Go##gotoAddr")) {
        unsigned addr = 0;
        if (parseHexAddress(addressBuffer, addr)) {
            jumpToAddress(static_cast<int>(addr));
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Search##toggleSearch")) {
        showSearch = !showSearch;
    }

    // Undo/Redo
    ImGui::SameLine();
    ImGui::BeginDisabled(undoStack.empty());
    if (ImGui::Button("Undo")) { undo(); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(redoStack.empty());
    if (ImGui::Button("Redo")) { redo(); }
    ImGui::EndDisabled();

    // Display options
    ImGui::Spacing();
    ImGui::Text("Display:");
    ImGui::SameLine();

    ImGui::SetNextItemWidth(60);
    ImGui::SliderInt("##BytesPerRow", &bytesPerRow, 8, 32, "%d bytes/row");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    ImGui::SliderInt("##DisplayRows", &displayRows, 16, 64, "%d rows");

    ImGui::SameLine();
    ImGui::Checkbox("ASCII", &showAscii);

    ImGui::SameLine();
    if (ImGui::Checkbox("Auto-refresh", &autoRefresh)) {
        if (!autoRefresh) {
            takeSnapshot(); // freeze current state
        }
    }

    if (!autoRefresh) {
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
            takeSnapshot();
        }
    }

    ImGui::SameLine();
    ImGui::Checkbox("Colorize", &colorizeRegions);

    ImGui::SameLine();
    ImGui::Checkbox("Changes", &showChanges);

    ImGui::SameLine();
    ImGui::Checkbox("Disasm", &showDisasm);

    // Quick shortcuts
    ImGui::Spacing();
    ImGui::Text("Shortcuts:");
    ImGui::SameLine();

    if (ImGui::SmallButton("0x0000##shortcut0")) jumpToAddress(0x0000);
    ImGui::SameLine();
    if (ImGui::SmallButton("0x0200##shortcutBuf")) jumpToAddress(0x0200);
    ImGui::SameLine();
    if (ImGui::SmallButton("0x0300##shortcut1")) jumpToAddress(0x0300);
    ImGui::SameLine();
    if (ImGui::SmallButton("0xA000##shortcut2")) jumpToAddress(0xA000);
    ImGui::SameLine();
    if (ImGui::SmallButton("0xE000##shortcut3")) jumpToAddress(0xE000);
    ImGui::SameLine();
    if (ImGui::SmallButton("0xFF00##shortcut4")) jumpToAddress(0xFF00);

    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Bookmark")) {
        if (std::find(bookmarks.begin(), bookmarks.end(), startAddress) == bookmarks.end()) {
            bookmarks.push_back(startAddress);
        }
    }

    // Display bookmarks
    if (!bookmarks.empty()) {
        ImGui::SameLine();
        ImGui::Text("Bookmarks:");
        for (size_t i = 0; i < bookmarks.size() && i < 5; ++i) {
            ImGui::SameLine();
            char label[32];
            snprintf(label, sizeof(label), "0x%04X##bookmark%zu", bookmarks[i], i);
            if (ImGui::SmallButton(label)) {
                jumpToAddress(bookmarks[i]);
            }
        }
    }
}

void MemoryViewer_ImGui::renderHexView()
{
    ImGui::BeginChild("HexView", ImVec2(0, 0), true);

    // Compute column positions so header and data rows are perfectly aligned
    float addrW = ImGui::CalcTextSize("0x0000  ").x;
    float cellW = ImGui::CalcTextSize("FF").x + ImGui::GetStyle().ItemSpacing.x;
    float hexStartX = ImGui::GetCursorPosX() + addrW;

    // Column header
    ImGui::Text("Address");
    for (int i = 0; i < bytesPerRow; ++i) {
        ImGui::SameLine(hexStartX + i * cellW);
        ImGui::Text("%02X", i);
    }
    if (showAscii) {
        ImGui::SameLine(hexStartX + bytesPerRow * cellW + ImGui::GetStyle().ItemSpacing.x);
        ImGui::Text("ASCII");
    }

    ImGui::Separator();

    // Hex data — ImGuiListClipper skips rendering of rows scrolled outside
    // the child window's viewport. Each row has ~80 ImGui calls (per-cell
    // Selectable + PushStyleColor), so at displayRows=64 the clipped-out
    // rows used to burn ~5 000 widget operations for nothing. Clipper brings
    // per-frame cost back down to what's actually visible.
    const int maxRowsAddressable = (0x10000 - startAddress + bytesPerRow - 1) / bytesPerRow;
    const int totalRows = std::max(0, std::min(displayRows, maxRowsAddressable));
    ImGuiListClipper clipper;
    clipper.Begin(totalRows);
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            int address = startAddress + (row * bytesPerRow);

            // Row address
            ImGui::Text("0x%04X", address);

        // Hex bytes
        char asciiLine[33]; // max bytesPerRow=32 + null
        int asciiIdx = 0;
        for (int col = 0; col < bytesPerRow; ++col) {
            int currentAddr = address + col;
            if (currentAddr > 0xFFFF) break;

            uint8_t value = readByte(currentAddr);

            ImGui::SameLine(hexStartX + col * cellW);

            // Change highlight: orange background flash for recently modified bytes
            if (showChanges && frameCounter > 0) {
                uint32_t age = frameCounter - changeFrame[currentAddr];
                if (age < kChangeFadeFrames) {
                    float alpha = 1.0f - static_cast<float>(age) / kChangeFadeFrames;
                    ImVec2 cellPos = ImGui::GetCursorScreenPos();
                    float cellH = ImGui::GetTextLineHeight();
                    float cellWPx = cellW - ImGui::GetStyle().ItemSpacing.x;
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        cellPos,
                        ImVec2(cellPos.x + cellWPx, cellPos.y + cellH),
                        IM_COL32(255, 120, 40, static_cast<int>(alpha * 160)));
                }
            }

            // Color by memory region
            bool pushedColor = false;
            if (currentAddr == searchAddress) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
                pushedColor = true;
            } else if (colorizeRegions) {
                ImGui::PushStyleColor(ImGuiCol_Text, getColorForAddress(currentAddr));
                pushedColor = true;
            }

            // Inline editing on double-click
            if (editAddress == currentAddr) {
                // Render inline InputText for editing
                ImGui::SetNextItemWidth(cellW - ImGui::GetStyle().ItemSpacing.x);
                if (!editFocusSet) {
                    ImGui::SetKeyboardFocusHere();
                    editFocusSet = true;
                }
                char inputId[16];
                snprintf(inputId, sizeof(inputId), "##e%04X", currentAddr);
                bool enterPressed = ImGui::InputText(inputId, editBuffer, sizeof(editBuffer),
                    ImGuiInputTextFlags_CharsHexadecimal |
                    ImGuiInputTextFlags_CharsUppercase |
                    ImGuiInputTextFlags_EnterReturnsTrue |
                    ImGuiInputTextFlags_AutoSelectAll);
                if (enterPressed) {
                    unsigned parsed = 0;
                    const char* begin = editBuffer;
                    const char* end   = editBuffer + std::strlen(editBuffer);
                    auto [p, ec] = std::from_chars(begin, end, parsed, 16);
                    if (ec == std::errc{} && p == end && parsed <= 0xFF) {
                        applyEdit(static_cast<uint16_t>(editAddress),
                                  static_cast<uint8_t>(parsed));
                    }
                    editAddress = -1;
                } else if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                           (!ImGui::IsItemActive() && editFocusSet && ImGui::IsMouseClicked(0) && !ImGui::IsItemHovered())) {
                    editAddress = -1;
                }
            } else {
                // Normal display — double-click to edit
                char selectableId[16];
                snprintf(selectableId, sizeof(selectableId), "%02X##%04X", value, currentAddr);
                if (ImGui::Selectable(selectableId, false, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(cellW - ImGui::GetStyle().ItemSpacing.x, 0))) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        editAddress = currentAddr;
                        snprintf(editBuffer, sizeof(editBuffer), "%02X", value);
                        editFocusSet = false;
                    }
                }
            }

            if (pushedColor) {
                ImGui::PopStyleColor();
            }

            if (showAscii) {
                asciiLine[asciiIdx++] = getPrintableChar(value);
            }
        }

        // ASCII column
        if (showAscii && asciiIdx > 0) {
            asciiLine[asciiIdx] = '\0';
            ImGui::SameLine(hexStartX + bytesPerRow * cellW + ImGui::GetStyle().ItemSpacing.x);
            ImGui::Text("%s", asciiLine);
        }
        }   // end for (row)
    }       // end while (clipper.Step())

    ImGui::EndChild();
}

void MemoryViewer_ImGui::renderSearchDialog()
{
    ImGui::SetNextWindowSize(ImVec2(450, 250), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Memory Search", &showSearch)) {
        ImGui::Checkbox("ASCII search (text)", &searchAscii);
        if (searchAscii) {
            ImGui::SameLine();
            ImGui::TextDisabled("(case-insensitive)");
        }

        ImGui::Spacing();
        if (searchAscii) {
            ImGui::Text("Search for a string:");
        } else {
            ImGui::Text("Search for hex bytes (e.g. A9 00 8D):");
        }

        ImGui::SetNextItemWidth(-1);
        ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue;
        if (!searchAscii) {
            flags |= ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase;
        }
        bool searchTriggered = ImGui::InputText("##SearchInput", searchBuffer, sizeof(searchBuffer), flags);

        if (ImGui::Button("Search##searchBtn") || searchTriggered) {
            if (searchAscii) {
                searchAsciiString();
            } else {
                searchMemory();
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Close##closeBtn")) {
            showSearch = false;
        }

        if (searchAddress >= 0) {
            ImGui::Spacing();
            ImGui::Text("Found at address: 0x%04X", searchAddress);
            ImGui::SameLine();
            if (ImGui::Button("Go to##gotoBtn")) {
                jumpToAddress(searchAddress);
                showSearch = false;
            }
        }
    }
    ImGui::End();
}

void MemoryViewer_ImGui::jumpToAddress(int address)
{
    startAddress = std::max(0, std::min(address, 0xFFFF - (displayRows * bytesPerRow)));
    startAddress = (startAddress / bytesPerRow) * bytesPerRow;
    if (!autoRefresh) {
        takeSnapshot();
    }
}

// Parse hex search string into byte pattern, then search using raw pointer
void MemoryViewer_ImGui::searchMemory()
{
    if (strlen(searchBuffer) == 0) return;

    // Parse space-separated hex bytes (e.g. "A9 00 8D" or "A9008D")
    uint8_t pattern[128];
    int patternLen = 0;
    const char* p = searchBuffer;
    const char* const bufEnd = searchBuffer + std::strlen(searchBuffer);
    while (p < bufEnd && patternLen < 128) {
        while (p < bufEnd && *p == ' ') p++;
        if (p >= bufEnd) break;
        unsigned val = 0;
        const char* chunkEnd = p + 2 < bufEnd ? p + 2 : bufEnd;
        auto [ptr, ec] = std::from_chars(p, chunkEnd, val, 16);
        if (ec != std::errc{} || ptr == p) break;
        pattern[patternLen++] = static_cast<uint8_t>(val);
        p = ptr;
        while (p < bufEnd && *p != ' ') p++;
    }
    if (patternLen == 0) return;

    int searchStart = (searchAddress >= 0) ? searchAddress + 1 : startAddress;
    int limit = 0x10000 - patternLen;

    // Search from current position to end, then wrap around
    for (int pass = 0; pass < 2; pass++) {
        int from = (pass == 0) ? searchStart : 0;
        int to   = (pass == 0) ? limit : std::min(searchStart, limit);
        for (int addr = from; addr <= to; ++addr) {
            if (readByte(addr) == pattern[0]) {
                bool matches = true;
                for (int i = 1; i < patternLen; ++i) {
                    if (readByte(addr + i) != pattern[i]) {
                        matches = false;
                        break;
                    }
                }
                if (matches) {
                    searchAddress = addr;
                    return;
                }
            }
        }
    }
    searchAddress = -1;
}

void MemoryViewer_ImGui::searchAsciiString()
{
    if (strlen(searchBuffer) == 0) return;

    int searchLen = (int)strlen(searchBuffer);
    int searchStart = (searchAddress >= 0) ? searchAddress + 1 : startAddress;
    int limit = 0x10000 - searchLen;

    // Convert search string to uppercase for case-insensitive matching
    char upperPattern[256];
    for (int i = 0; i < searchLen && i < 255; ++i) {
        char c = searchBuffer[i];
        upperPattern[i] = (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
    }
    upperPattern[std::min(searchLen, 255)] = '\0';

    auto matchAt = [&](int addr) -> bool {
        for (int i = 0; i < searchLen; ++i) {
            char c = (char)(readByte(addr + i) & 0x7F);
            if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
            if (c != upperPattern[i]) return false;
        }
        return true;
    };

    for (int pass = 0; pass < 2; pass++) {
        int from = (pass == 0) ? searchStart : 0;
        int to   = (pass == 0) ? limit : std::min(searchStart, limit);
        for (int addr = from; addr <= to; ++addr) {
            if (matchAt(addr)) {
                searchAddress = addr;
                return;
            }
        }
    }
    searchAddress = -1;
}

// Edit with undo support
void MemoryViewer_ImGui::applyEdit(uint16_t address, uint8_t newValue)
{
    uint8_t oldValue = readByte(address);
    if (oldValue == newValue) return;
    if (writeCallback) {
        writeCallback(address, newValue);
    } else if (memory) {
        memory->memWrite(address, newValue);
    }
    if (snapshotValid) {
        snapshot[address] = newValue;
    }
    undoStack.push_back({address, oldValue, newValue});
    redoStack.clear();
}

void MemoryViewer_ImGui::undo()
{
    if (undoStack.empty()) return;
    EditRecord rec = undoStack.back();
    undoStack.pop_back();
    if (writeCallback) {
        writeCallback(rec.address, rec.oldValue);
    } else if (memory) {
        memory->memWrite(rec.address, rec.oldValue);
    }
    if (snapshotValid) {
        snapshot[rec.address] = rec.oldValue;
    }
    redoStack.push_back(rec);
    jumpToAddress(rec.address);
}

void MemoryViewer_ImGui::redo()
{
    if (redoStack.empty()) return;
    EditRecord rec = redoStack.back();
    redoStack.pop_back();
    if (writeCallback) {
        writeCallback(rec.address, rec.newValue);
    } else if (memory) {
        memory->memWrite(rec.address, rec.newValue);
    }
    if (snapshotValid) {
        snapshot[rec.address] = rec.newValue;
    }
    undoStack.push_back(rec);
    jumpToAddress(rec.address);
}

// --- Region context banner ---------------------------------------------------

const char* MemoryViewer_ImGui::getRegionName(int address) const
{
    if (address <= 0x00FF) return "Zero Page";
    if (address <= 0x01FF) return "Stack";
    if (address <= 0x027F) return "Keyboard Buffer";
    if (gen2Enabled) {
        if (address >= 0x0400 && address <= 0x07FF) return "GEN2 TEXT Page 1";
        if (address >= 0x0800 && address <= 0x0BFF) return "GEN2 TEXT Page 2";
        if (address >= 0x2000 && address <= 0x3FFF) return "GEN2 HGR Page 1";
        if (address >= 0x4000 && address <= 0x5FFF) return "GEN2 HGR Page 2";
    }
    if (codeTankEnabled && address >= 0x4000 && address <= 0x7FFF) return "CodeTank ROM";
    if (jukeBoxEnabled) {
        int romStart = (jbJumper == JukeBox::Jumper::RAM16_ROM32) ? 0x4000 : 0x8000;
        if (address >= 0xBD00 && address <= 0xBFFF) return "Juke-Box Program Manager";
        if (address >= 0xBC00 && address <= 0xBCFF) return "Juke-Box PAT (directory)";
        if (address >= romStart && address <= 0xBBFF) return "Juke-Box ROM";
    }
    if (microSDEnabled && address >= 0x8000 && address <= 0x9FFF) return "SD CARD OS ROM";
    if (microSDEnabled && address >= 0xA000 && address <= 0xA00F) return "microSD VIA 65C22";
    if (wifiModemEnabled && address >= 0xB000 && address <= 0xB003) return "Wi-Fi Modem ACIA";
    for (const auto& rom : romRegions) {
        if (address >= rom.start && address <= rom.end) return "Loaded ROM";
    }
    if (aciEnabled && address >= 0xC000 && address <= 0xC0FF) return "ACI I/O";
    if (aciEnabled && address >= 0xC100 && address <= 0xC1FF) return "ACI ROM";
    if (sidEnabled && address >= 0xC800 && address <= 0xCFFF) return "A1-SID";
    if (tms9918Enabled && address >= 0xCC00 && address <= 0xCC01) return "TMS9918 VDP";
    if (address >= 0xD000 && address <= 0xD0FF) return "I/O (KBD/DSP)";
    // $E000-$EFFF on real Apple-1 is RAM (Integer BASIC was distributed on
    // cassette and loaded into RAM via Wozmon `E000.EFFR`). POM1 pre-seeds
    // this RAM from basic.rom at boot, but writes are not blocked — programs
    // can use this region as scratch (e.g. dev/projects/games_chess/ engine).
    if (address >= 0xE000 && address <= 0xEFFF) return "Integer BASIC (RAM)";
    if (address >= 0xFF00) return "Woz Monitor ROM";
    return "User RAM";
}

void MemoryViewer_ImGui::renderRegionBanner()
{
    const char* regionName = getRegionName(startAddress);
    ImVec4 regionColor = colorizeRegions ? getColorForAddress(startAddress)
                                         : ImVec4(0.7f, 0.7f, 0.7f, 1.0f);

    // Build info string with JukeBox bank / CodeTank half details when applicable
    char banner[128];
    if (codeTankEnabled && startAddress >= 0x4000 && startAddress <= 0x7FFF) {
        snprintf(banner, sizeof(banner), "%s  %s 16 kB", regionName,
                 codeTankJumper == CodeTank::Jumper::Upper16 ? "upper" : "lower");
    } else if (jukeBoxEnabled) {
        int romStart = (jbJumper == JukeBox::Jumper::RAM16_ROM32) ? 0x4000 : 0x8000;
        if (startAddress >= romStart && startAddress <= 0xBFFF) {
            if (jbJumper == JukeBox::Jumper::RAM16_ROM32)
                snprintf(banner, sizeof(banner), "%s  P%u of %u", regionName, jbCurrentPage, jbPageCount);
            else
                snprintf(banner, sizeof(banner), "%s  P%u:S%u of %u", regionName, jbCurrentPage, jbCurrentSubPage, jbPageCount);
        } else {
            snprintf(banner, sizeof(banner), "%s", regionName);
        }
    } else {
        snprintf(banner, sizeof(banner), "%s", regionName);
    }

    ImGui::PushStyleColor(ImGuiCol_Text, regionColor);
    ImGui::Text("--- %s ---", banner);
    ImGui::PopStyleColor();
}

// --- Disassembly view --------------------------------------------------------

void MemoryViewer_ImGui::renderDisasmView()
{
    ImGui::BeginChild("DisasmView", ImVec2(0, 0), true);

    const uint8_t* mem = getMemoryPointer();
    if (!mem) { ImGui::EndChild(); return; }

    // Column widths
    float addrW = ImGui::CalcTextSize("$0000  ").x;
    float bytesColW = ImGui::CalcTextSize("00 00 00  ").x;
    float mnemonicX = addrW + bytesColW;

    // Header
    ImGui::Text("Address");
    ImGui::SameLine(addrW);
    ImGui::Text("Bytes");
    ImGui::SameLine(mnemonicX);
    ImGui::Text("Instruction");
    ImGui::Separator();

    int pc = startAddress;
    for (int i = 0; i < displayRows && pc <= 0xFFFF; ++i) {
        int instrLen = 1;
        std::string mnemonic = pom1::disassemble6502(mem, static_cast<uint16_t>(pc), instrLen);

        // Change highlight background for the instruction's first byte
        if (showChanges && frameCounter > 0) {
            uint32_t age = frameCounter - changeFrame[pc];
            if (age < kChangeFadeFrames) {
                float alpha = 1.0f - static_cast<float>(age) / kChangeFadeFrames;
                ImVec2 pos = ImGui::GetCursorScreenPos();
                float rowH = ImGui::GetTextLineHeight();
                float rowW = ImGui::GetContentRegionAvail().x;
                ImGui::GetWindowDrawList()->AddRectFilled(
                    pos, ImVec2(pos.x + rowW, pos.y + rowH),
                    IM_COL32(255, 120, 40, static_cast<int>(alpha * 100)));
            }
        }

        // Address
        if (colorizeRegions) {
            ImGui::TextColored(getColorForAddress(pc), "$%04X", pc);
        } else {
            ImGui::Text("$%04X", pc);
        }

        // Hex bytes (padded to 9 chars for alignment: "XX XX XX ")
        ImGui::SameLine(addrW);
        char hexStr[10] = "         ";
        for (int b = 0; b < instrLen && b < 3; ++b) {
            snprintf(hexStr + b * 3, 4, "%02X ", readByte(pc + b));
        }
        hexStr[9] = '\0';
        ImGui::TextDisabled("%s", hexStr);

        // Mnemonic
        ImGui::SameLine(mnemonicX);
        ImGui::Text("%s", mnemonic.c_str());

        pc += instrLen;
    }

    ImGui::EndChild();
}

char MemoryViewer_ImGui::getPrintableChar(uint8_t value)
{
    return (value >= 32 && value <= 126) ? static_cast<char>(value) : '.';
}

// renderEditPopup removed — editing is now inline via double-click

void MemoryViewer_ImGui::handleNavigation()
{
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
            jumpToAddress(startAddress - (bytesPerRow * displayRows));
        }
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
            jumpToAddress(startAddress + (bytesPerRow * displayRows));
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
            jumpToAddress(0x0000);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_End)) {
            jumpToAddress(0xFFFF - (bytesPerRow * displayRows));
        }
    }
}

bool MemoryViewer_ImGui::isROM(int address)
{
    if (address >= 0xFF00) return true;
    if (address >= 0xE000 && address <= 0xEFFF) return true;
    if (aciEnabled && address >= 0xC100 && address <= 0xC1FF) return true;
    if (microSDEnabled && address >= 0x8000 && address <= 0x9FFF) return true;
    return false;
}

bool MemoryViewer_ImGui::isIO(int address)
{
    if (microSDEnabled && address >= 0xA000 && address <= 0xA00F) return true;
    return ((aciEnabled && address >= 0xC000 && address <= 0xC0FF) ||
            (address >= 0xD010 && address <= 0xD013));
}

ImVec4 MemoryViewer_ImGui::getColorForAddress(int address)
{
    // Colors match the Memory Map window
    if (address <= 0x00FF)
        return ImVec4(0.39f, 0.39f, 1.0f, 1.0f);  // Zero Page - blue
    if (address <= 0x01FF)
        return ImVec4(1.0f, 0.65f, 0.0f, 1.0f);    // Stack - orange
    if (address <= 0x027F)
        return ImVec4(0.0f, 0.78f, 1.0f, 1.0f);     // Keyboard Buffer - cyan
    if (gen2Enabled && address >= 0x0400 && address <= 0x07FF)
        return ImVec4(0.59f, 0.71f, 1.0f, 1.0f);    // GEN2 TEXT Page 1 - light blue
    if (gen2Enabled && address >= 0x0800 && address <= 0x0BFF)
        return ImVec4(0.43f, 0.55f, 0.86f, 1.0f);   // GEN2 TEXT Page 2 - dim blue
    if (gen2Enabled && address >= 0x2000 && address <= 0x3FFF)
        return ImVec4(0.0f, 1.0f, 0.78f, 1.0f);     // GEN2 HGR Page 1 - cyan/teal
    if (gen2Enabled && address >= 0x4000 && address <= 0x5FFF)
        return ImVec4(0.0f, 0.75f, 0.59f, 1.0f);    // GEN2 HGR Page 2 - dim teal
    if (microSDEnabled && address >= 0x8000 && address <= 0x9FFF)
        return ImVec4(1.0f, 0.78f, 0.31f, 1.0f);    // SD CARD OS ROM - amber
    if (microSDEnabled && address >= 0xA000 && address <= 0xA00F)
        return ImVec4(1.0f, 0.59f, 0.20f, 1.0f);    // VIA 65C22 I/O - dark orange
    if (wifiModemEnabled && address >= 0xB000 && address <= 0xB003)
        return ImVec4(0.0f, 0.78f, 0.78f, 1.0f);    // ACIA 65C51 I/O - teal
    // Loaded ROM overlays (e.g. Applesoft Lite $6000-$7FFF) before generic RAM bands
    for (const auto& rom : romRegions) {
        if (address >= rom.start && address <= rom.end)
            return ImVec4(1.0f, 1.0f, 0.31f, 1.0f); // ROM - yellow
    }
    // P-LAB CodeTank ROM window (deep violet, matches Memory Map)
    if (codeTankEnabled && address >= 0x4000 && address <= 0x7FFF)
        return ImVec4(0.47f, 0.31f, 0.71f, 1.0f);
    // P-LAB Juke-Box ROM window (violet shades matching Memory Map)
    if (jukeBoxEnabled) {
        const int romStart = (jbJumper == JukeBox::Jumper::RAM16_ROM32) ? 0x4000 : 0x8000;
        if (address >= romStart && address <= 0xBFFF) {
            if (address >= 0xBD00)
                return ImVec4(0.90f, 0.71f, 1.0f, 1.0f);  // Program Manager - bright lavender
            if (address >= 0xBC00)
                return ImVec4(0.71f, 0.51f, 0.86f, 1.0f);  // PAT directory - medium violet
            return ImVec4(0.47f, 0.31f, 0.71f, 1.0f);      // Programs - deep violet
        }
    }
    if (address <= 0x9FFF)
        return ImVec4(0.31f, 0.78f, 0.31f, 1.0f);   // User RAM - green
    if (address <= 0xBFFF)
        return ImVec4(0.31f, 0.78f, 0.31f, 1.0f);   // User RAM - green
    if (aciEnabled && address >= 0xC000 && address <= 0xC0FF)
        return ImVec4(1.0f, 0.50f, 0.31f, 1.0f);    // Cassette I/O - orange/red
    if (tms9918Enabled && address >= 0xCC00 && address <= 0xCC01)
        return ImVec4(0.4f, 0.8f, 1.0f, 1.0f);      // TMS9918 I/O - light blue
    if (sidEnabled && address >= 0xC800 && address <= 0xCFFF)
        return ImVec4(0.78f, 0.39f, 1.0f, 1.0f);    // A1-SID I/O - purple
    if (aciEnabled && address >= 0xC100 && address <= 0xC1FF)
        return ImVec4(1.0f, 0.70f, 0.31f, 1.0f);    // ACI ROM - amber
    if (address >= 0xD000 && address <= 0xD0FF)
        return ImVec4(1.0f, 0.31f, 0.31f, 1.0f);    // I/O (KBD/DSP) - red
    return ImVec4(0.4f, 0.4f, 0.4f, 1.0f);          // Unused
}
