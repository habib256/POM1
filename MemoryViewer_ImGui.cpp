#include "MemoryViewer_ImGui.h"
#include "imgui.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>

MemoryViewer_ImGui::MemoryViewer_ImGui(Memory* mem) : memory(mem)
{
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

    if (showEditPopup) {
        renderEditPopup();
    }
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
    
    // Options d'affichage
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
    ImGui::Checkbox("Auto-refresh", &autoRefresh);

    ImGui::SameLine();
    ImGui::Checkbox("Colorize", &colorizeRegions);
    
    // Raccourcis rapides
    ImGui::Spacing();
    ImGui::Text("Shortcuts:");
    ImGui::SameLine();
    
    if (ImGui::SmallButton("0x0000##shortcut0")) jumpToAddress(0x0000);
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

    // Afficher les bookmarks
    if (!bookmarks.empty()) {
        ImGui::SameLine();
        ImGui::Text("Bookmarks:");
        for (size_t i = 0; i < bookmarks.size() && i < 5; ++i) {
            ImGui::SameLine();
            std::string label = formatAddress(bookmarks[i]) + "##bookmark" + std::to_string(i);
            if (ImGui::SmallButton(label.c_str())) {
                jumpToAddress(bookmarks[i]);
            }
        }
    }
}

void MemoryViewer_ImGui::renderHexView()
{
    ImGui::BeginChild("HexView", ImVec2(0, 0), true);
    
    // En-tête des colonnes
    ImGui::Text("Address ");
    for (int i = 0; i < bytesPerRow; ++i) {
        ImGui::SameLine();
        ImGui::Text("%02X", i);
    }
    if (showAscii) {
        ImGui::SameLine();
        ImGui::Text("  ASCII");
    }
    
    ImGui::Separator();
    
    // Données hexadécimales
    for (int row = 0; row < displayRows; ++row) {
        int address = startAddress + (row * bytesPerRow);
        if (address > 0xFFFF) break;
        
        // Adresse de la ligne
        ImGui::Text("%s", formatAddress(address).c_str());
        
        // Données hex
        std::string asciiLine;
        for (int col = 0; col < bytesPerRow; ++col) {
            int currentAddr = address + col;
            if (currentAddr > 0xFFFF) break;
            
            quint8 value = memory->memRead(currentAddr);

            ImGui::SameLine();

            // Coloration selon la région mémoire
            bool pushedColor = false;
            if (colorizeRegions) {
                ImVec4 color = getColorForAddress(currentAddr);
                if (currentAddr == searchAddress) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f)); // Jaune pour recherche
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, color);
                }
                pushedColor = true;
            } else if (currentAddr == searchAddress) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f)); // Jaune
                pushedColor = true;
            }

            // Rendre la valeur hex cliquable pour édition
            std::string hexStr = formatHex(value);
            std::string selectableId = hexStr + "##" + std::to_string(currentAddr);
            if (ImGui::Selectable(selectableId.c_str(), false, ImGuiSelectableFlags_None, ImVec2(20, 0))) {
                // Ouvrir le popup d'édition
                editAddress = currentAddr;
                snprintf(editBuffer, sizeof(editBuffer), "%02X", value);
                showEditPopup = true;
            }

            if (pushedColor) {
                ImGui::PopStyleColor();
            }
            
            // Construire la ligne ASCII
            if (showAscii) {
                asciiLine += getPrintableChar(value);
            }
        }
        
        // Afficher la colonne ASCII
        if (showAscii && !asciiLine.empty()) {
            ImGui::SameLine();
            ImGui::Text("  %s", asciiLine.c_str());
        }
    }
    
    ImGui::EndChild();
}

void MemoryViewer_ImGui::renderSearchDialog()
{
    ImGui::SetNextWindowSize(ImVec2(450, 250), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Memory Search", &showSearch)) {
        ImGui::Checkbox("ASCII search (text)", &searchAscii);

        ImGui::Spacing();
        if (searchAscii) {
            ImGui::Text("Search for a string:");
        } else {
            ImGui::Text("Search for a hex value:");
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
    // Aligner sur une ligne
    startAddress = (startAddress / bytesPerRow) * bytesPerRow;
}

void MemoryViewer_ImGui::searchMemory()
{
    if (strlen(searchBuffer) == 0) return;
    
    // Convertir la chaîne hex en valeur
    quint8 searchValue = 0;
    if (sscanf(searchBuffer, "%hhX", &searchValue) != 1) return;
    
    // Rechercher à partir de l'adresse actuelle
    int searchStart = (searchAddress >= 0) ? searchAddress + 1 : startAddress;
    
    for (int addr = searchStart; addr <= 0xFFFF; ++addr) {
        if (memory->memRead(addr) == searchValue) {
            searchAddress = addr;
            return;
        }
    }
    
    // Si pas trouvé, rechercher depuis le début
    for (int addr = 0; addr < searchStart; ++addr) {
        if (memory->memRead(addr) == searchValue) {
            searchAddress = addr;
            return;
        }
    }
    
    // Pas trouvé
    searchAddress = -1;
}

std::string MemoryViewer_ImGui::formatHex(quint8 value, int width)
{
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0') << std::setw(width) << static_cast<int>(value);
    return ss.str();
}

std::string MemoryViewer_ImGui::formatAddress(int address)
{
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << address;
    return ss.str();
}

char MemoryViewer_ImGui::getPrintableChar(quint8 value)
{
    return (value >= 32 && value <= 126) ? static_cast<char>(value) : '.';
}

void MemoryViewer_ImGui::renderEditPopup()
{
    if (showEditPopup) {
        ImGui::OpenPopup("Edit Memory");
        showEditPopup = false;
    }

    if (ImGui::BeginPopupModal("Edit Memory", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Address: %s", formatAddress(editAddress).c_str());
        ImGui::Text("Current value: 0x%02X (%d)", memory->memRead(editAddress), memory->memRead(editAddress));

        ImGui::Spacing();
        ImGui::Text("New value (hex):");
        ImGui::SetNextItemWidth(60);
        bool enterPressed = ImGui::InputText("##EditValue", editBuffer, sizeof(editBuffer),
                                            ImGuiInputTextFlags_CharsHexadecimal |
                                            ImGuiInputTextFlags_CharsUppercase |
                                            ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::Spacing();
        if (ImGui::Button("Write##writeBtn", ImVec2(120, 0)) || enterPressed) {
            quint8 newValue = 0;
            if (sscanf(editBuffer, "%hhX", &newValue) == 1) {
                memory->memWrite(editAddress, newValue);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##cancelBtn", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void MemoryViewer_ImGui::handleNavigation()
{
    // Navigation avec PageUp/PageDown
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

void MemoryViewer_ImGui::searchAsciiString()
{
    if (strlen(searchBuffer) == 0) return;

    int searchLen = strlen(searchBuffer);
    int searchStart = (searchAddress >= 0) ? searchAddress + 1 : startAddress;

    // Rechercher la chaîne ASCII
    for (int addr = searchStart; addr <= 0xFFFF - searchLen; ++addr) {
        bool found = true;
        for (int i = 0; i < searchLen; ++i) {
            if (memory->memRead(addr + i) != (quint8)searchBuffer[i]) {
                found = false;
                break;
            }
        }
        if (found) {
            searchAddress = addr;
            return;
        }
    }

    // Si pas trouvé, rechercher depuis le début
    for (int addr = 0; addr < searchStart && addr <= 0xFFFF - searchLen; ++addr) {
        bool found = true;
        for (int i = 0; i < searchLen; ++i) {
            if (memory->memRead(addr + i) != (quint8)searchBuffer[i]) {
                found = false;
                break;
            }
        }
        if (found) {
            searchAddress = addr;
            return;
        }
    }

    // Pas trouvé
    searchAddress = -1;
}

bool MemoryViewer_ImGui::isROM(int address)
{
    // WozMonitor: 0xFF00-0xFFFF
    if (address >= 0xFF00) return true;
    // BASIC: 0xE000-0xEFFF
    if (address >= 0xE000 && address <= 0xEFFF) return true;
    // Krusader: 0xA000-0xBFFF
    if (address >= 0xA000 && address <= 0xBFFF) return true;
    return false;
}

bool MemoryViewer_ImGui::isIO(int address)
{
    // Memory-mapped I/O: 0xD010-0xD013
    return (address >= 0xD010 && address <= 0xD013);
}

ImVec4 MemoryViewer_ImGui::getColorForAddress(int address)
{
    // Colors match the Memory Map window
    if (address <= 0x00FF)
        return ImVec4(0.39f, 0.39f, 1.0f, 1.0f);  // Zero Page - blue
    if (address <= 0x01FF)
        return ImVec4(1.0f, 0.65f, 0.0f, 1.0f);    // Stack - orange
    if (address <= 0x9FFF)
        return ImVec4(0.31f, 0.78f, 0.31f, 1.0f);   // User RAM - green
    if (address <= 0xBFFF)
        return ImVec4(0.78f, 0.31f, 0.78f, 1.0f);   // Krusader ROM - purple
    if (address >= 0xD000 && address <= 0xD0FF)
        return ImVec4(1.0f, 0.31f, 0.31f, 1.0f);    // I/O (KBD/DSP) - red
    if (address >= 0xE000 && address <= 0xEFFF)
        return ImVec4(1.0f, 1.0f, 0.31f, 1.0f);     // BASIC ROM - yellow
    if (address >= 0xFF00)
        return ImVec4(0.0f, 0.78f, 1.0f, 1.0f);     // Woz Monitor ROM - cyan
    // Unused regions
    return ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
} 