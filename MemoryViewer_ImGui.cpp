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
    if (ImGui::Button("Aller##gotoAddr")) {
        int addr = 0;
        if (sscanf(addressBuffer, "%X", &addr) == 1) {
            jumpToAddress(addr);
        }
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Rechercher##toggleSearch")) {
        showSearch = !showSearch;
    }
    
    // Options d'affichage
    ImGui::Spacing();
    ImGui::Text("Affichage:");
    ImGui::SameLine();
    
    ImGui::SetNextItemWidth(60);
    ImGui::SliderInt("##BytesPerRow", &bytesPerRow, 8, 32, "%d octets/ligne");
    
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    ImGui::SliderInt("##DisplayRows", &displayRows, 16, 64, "%d lignes");
    
    ImGui::SameLine();
    ImGui::Checkbox("ASCII", &showAscii);
    
    ImGui::SameLine();
    ImGui::Checkbox("Auto-actualisation", &autoRefresh);

    ImGui::SameLine();
    ImGui::Checkbox("Coloriser", &colorizeRegions);
    
    // Raccourcis rapides
    ImGui::Spacing();
    ImGui::Text("Raccourcis:");
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
        ImGui::Text("Favoris:");
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
    ImGui::Text("Adresse ");
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
    if (ImGui::Begin("Recherche en mémoire", &showSearch)) {
        ImGui::Checkbox("Recherche ASCII (texte)", &searchAscii);

        ImGui::Spacing();
        if (searchAscii) {
            ImGui::Text("Rechercher une chaîne de caractères:");
        } else {
            ImGui::Text("Rechercher une valeur hexadécimale:");
        }

        ImGui::SetNextItemWidth(-1);
        ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue;
        if (!searchAscii) {
            flags |= ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase;
        }
        bool searchTriggered = ImGui::InputText("##SearchInput", searchBuffer, sizeof(searchBuffer), flags);

        if (ImGui::Button("Rechercher##searchBtn") || searchTriggered) {
            if (searchAscii) {
                searchAsciiString();
            } else {
                searchMemory();
            }
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Fermer##closeBtn")) {
            showSearch = false;
        }
        
        if (searchAddress >= 0) {
            ImGui::Spacing();
            ImGui::Text("Trouvé à l'adresse: 0x%04X", searchAddress);
            ImGui::SameLine();
            if (ImGui::Button("Aller à##gotoBtn")) {
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
        ImGui::OpenPopup("Éditer Mémoire");
        showEditPopup = false; // Reset pour la prochaine fois
    }

    if (ImGui::BeginPopupModal("Éditer Mémoire", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Adresse: %s", formatAddress(editAddress).c_str());
        ImGui::Text("Valeur actuelle: 0x%02X (%d)", memory->memRead(editAddress), memory->memRead(editAddress));

        ImGui::Spacing();
        ImGui::Text("Nouvelle valeur (hex):");
        ImGui::SetNextItemWidth(60);
        bool enterPressed = ImGui::InputText("##EditValue", editBuffer, sizeof(editBuffer),
                                            ImGuiInputTextFlags_CharsHexadecimal |
                                            ImGuiInputTextFlags_CharsUppercase |
                                            ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::Spacing();
        if (ImGui::Button("Écrire##writeBtn", ImVec2(120, 0)) || enterPressed) {
            quint8 newValue = 0;
            if (sscanf(editBuffer, "%hhX", &newValue) == 1) {
                memory->memWrite(editAddress, newValue);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler##cancelBtn", ImVec2(120, 0))) {
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
    if (isROM(address)) {
        return ImVec4(1.0f, 0.6f, 0.6f, 1.0f); // Rouge clair pour ROM
    } else if (isIO(address)) {
        return ImVec4(1.0f, 1.0f, 0.6f, 1.0f); // Jaune clair pour I/O
    } else {
        return ImVec4(0.8f, 1.0f, 0.8f, 1.0f); // Vert clair pour RAM
    }
} 