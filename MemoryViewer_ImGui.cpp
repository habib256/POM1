#include "MemoryViewer_ImGui.h"
#include "imgui.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>

MemoryViewer_ImGui::MemoryViewer_ImGui(Memory* mem)
    : memory(mem)
{
    snapshot.resize(0x10000);
}

// Read a byte: live from raw pointer (no I/O side effects) or from snapshot
quint8 MemoryViewer_ImGui::readByte(int address) const
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

void MemoryViewer_ImGui::render()
{
    handleNavigation();
    renderControls();
    ImGui::Separator();
    renderHexView();

    if (showSearch) {
        renderSearchDialog();
    }

}

void MemoryViewer_ImGui::navigateToAddress(int address)
{
    jumpToAddress(address);
}

void MemoryViewer_ImGui::updateLiveMemory(const std::vector<quint8>& memoryImage)
{
    liveMemory = &memoryImage;
    if (autoRefresh || !snapshotValid) {
        takeSnapshot();
    }
}

void MemoryViewer_ImGui::setWriteCallback(std::function<void(quint16, quint8)> callback)
{
    writeCallback = std::move(callback);
}

void MemoryViewer_ImGui::renderControls()
{
    // Navigation
    ImGui::Text("Navigation:");
    ImGui::SameLine();

    static char addressBuffer[8] = "0000";
    ImGui::SetNextItemWidth(80);
    if (ImGui::InputText("##Address", addressBuffer, sizeof(addressBuffer), ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase)) {
        int addr = 0;
        if (sscanf(addressBuffer, "%X", &addr) == 1) {
            jumpToAddress(addr);
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Go##gotoAddr")) {
        int addr = 0;
        if (sscanf(addressBuffer, "%X", &addr) == 1) {
            jumpToAddress(addr);
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

    // Hex data
    for (int row = 0; row < displayRows; ++row) {
        int address = startAddress + (row * bytesPerRow);
        if (address > 0xFFFF) break;

        // Row address
        ImGui::Text("0x%04X", address);

        // Hex bytes
        char asciiLine[33]; // max bytesPerRow=32 + null
        int asciiIdx = 0;
        for (int col = 0; col < bytesPerRow; ++col) {
            int currentAddr = address + col;
            if (currentAddr > 0xFFFF) break;

            quint8 value = readByte(currentAddr);

            ImGui::SameLine(hexStartX + col * cellW);

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
                    quint8 newValue = 0;
                    if (sscanf(editBuffer, "%hhX", &newValue) == 1) {
                        applyEdit(static_cast<quint16>(editAddress), newValue);
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
    }

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
    quint8 pattern[128];
    int patternLen = 0;
    const char* p = searchBuffer;
    while (*p && patternLen < 128) {
        while (*p == ' ') p++;
        if (!*p) break;
        unsigned int val;
        if (sscanf(p, "%2X", &val) != 1) break;
        pattern[patternLen++] = (quint8)val;
        // advance past the parsed hex chars
        if (p[0] && p[1] && p[1] != ' ') p += 2;
        else if (p[0]) p += 1;
        while (*p && *p != ' ') p++;
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
void MemoryViewer_ImGui::applyEdit(quint16 address, quint8 newValue)
{
    quint8 oldValue = readByte(address);
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

char MemoryViewer_ImGui::getPrintableChar(quint8 value)
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
    if (address >= 0xC100 && address <= 0xC1FF) return true;
    if (address >= 0xA000 && address <= 0xBFFF) return true;
    return false;
}

bool MemoryViewer_ImGui::isIO(int address)
{
    return ((address >= 0xC000 && address <= 0xC0FF) ||
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
    if (gen2Enabled && address >= 0x2000 && address <= 0x3FFF)
        return ImVec4(0.0f, 1.0f, 0.78f, 1.0f);     // GEN2 HGR Framebuffer - cyan/teal
    if (address <= 0x9FFF)
        return ImVec4(0.31f, 0.78f, 0.31f, 1.0f);   // User RAM - green
    if (address <= 0xBFFF)
        return ImVec4(0.78f, 0.31f, 0.78f, 1.0f);   // Krusader ROM - purple
    if (address >= 0xC000 && address <= 0xC0FF)
        return ImVec4(1.0f, 0.50f, 0.31f, 1.0f);    // Cassette I/O - orange/red
    if (address >= 0xC100 && address <= 0xC1FF)
        return ImVec4(1.0f, 0.70f, 0.31f, 1.0f);    // ACI ROM - amber
    if (address >= 0xD000 && address <= 0xD0FF)
        return ImVec4(1.0f, 0.31f, 0.31f, 1.0f);    // I/O (KBD/DSP) - red
    if (address >= 0xE000 && address <= 0xEFFF)
        return ImVec4(1.0f, 1.0f, 0.31f, 1.0f);     // BASIC ROM - yellow
    if (address >= 0xFF00)
        return ImVec4(0.0f, 0.78f, 1.0f, 1.0f);     // Woz Monitor ROM - cyan
    return ImVec4(0.4f, 0.4f, 0.4f, 1.0f);          // Unused
}
