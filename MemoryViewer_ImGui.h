#ifndef MEMORYVIEWER_IMGUI_H
#define MEMORYVIEWER_IMGUI_H

#include "Memory.h"
#include "imgui.h"
#include <vector>
#include <string>

class MemoryViewer_ImGui
{
public:
    explicit MemoryViewer_ImGui(Memory* memory);
    ~MemoryViewer_ImGui() = default;

    void render();

private:
    Memory* memory;
    
    // Interface state
    int startAddress = 0x0000;
    int bytesPerRow = 16;
    int displayRows = 32;
    bool showAscii = true;
    bool autoRefresh = false;
    bool colorizeRegions = true;

    // Search functionality
    char searchBuffer[256] = {0};
    int searchAddress = -1;
    bool showSearch = false;
    bool searchAscii = false; // Search for ASCII strings

    // Edit functionality
    bool showEditPopup = false;
    int editAddress = -1;
    char editBuffer[4] = {0};

    // Bookmarks
    std::vector<int> bookmarks;
    
    // Utility functions
    void renderHexView();
    void renderControls();
    void renderSearchDialog();
    void renderEditPopup();
    void jumpToAddress(int address);
    void searchMemory();
    void searchAsciiString();
    void handleNavigation();

    // Helper functions
    std::string formatHex(quint8 value, int width = 2);
    std::string formatAddress(int address);
    char getPrintableChar(quint8 value);
    ImVec4 getColorForAddress(int address);
    bool isROM(int address);
    bool isIO(int address);
};

#endif // MEMORYVIEWER_IMGUI_H 