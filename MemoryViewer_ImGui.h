#ifndef MEMORYVIEWER_IMGUI_H
#define MEMORYVIEWER_IMGUI_H

#include "Memory.h"
#include "imgui.h"
#include <functional>
#include <vector>
#include <string>

class MemoryViewer_ImGui
{
public:
    explicit MemoryViewer_ImGui(Memory* memory = nullptr);
    ~MemoryViewer_ImGui() = default;

    void render();
    void navigateToAddress(int address);
    void updateLiveMemory(const std::vector<quint8>& memoryImage);
    void setWriteCallback(std::function<void(quint16, quint8)> callback);
    void setGraphicsCardEnabled(bool enabled) { gen2Enabled = enabled; }
    void setTMS9918Enabled(bool enabled) { tms9918Enabled = enabled; }

private:
    Memory* memory;
    const std::vector<quint8>* liveMemory = nullptr;
    std::function<void(quint16, quint8)> writeCallback;

    // Interface state
    int startAddress = 0x0000;
    int bytesPerRow = 16;
    int displayRows = 32;
    bool showAscii = true;
    bool autoRefresh = false;
    bool colorizeRegions = true;
    bool gen2Enabled = false;
    bool tms9918Enabled = false;

    // Auto-refresh: snapshot taken when autoRefresh is off
    std::vector<quint8> snapshot;
    bool snapshotValid = false;
    void takeSnapshot();
    quint8 readByte(int address) const;

    // Search functionality
    char searchBuffer[256] = {0};
    int searchAddress = -1;
    bool showSearch = false;
    bool searchAscii = false;

    // Edit functionality — inline double-click editing with undo/redo
    int editAddress = -1;
    char editBuffer[4] = {0};
    bool editFocusSet = false;

    struct EditRecord {
        quint16 address;
        quint8 oldValue;
        quint8 newValue;
    };
    std::vector<EditRecord> undoStack;
    std::vector<EditRecord> redoStack;
    void applyEdit(quint16 address, quint8 newValue);
    void undo();
    void redo();

    // Bookmarks
    std::vector<int> bookmarks;

    // Utility functions
    void renderHexView();
    void renderControls();
    void renderSearchDialog();
    void jumpToAddress(int address);
    void searchMemory();
    void searchAsciiString();
    void handleNavigation();

    // Helper functions
    char getPrintableChar(quint8 value);
    ImVec4 getColorForAddress(int address);
    bool isROM(int address);
    bool isIO(int address);
};

#endif // MEMORYVIEWER_IMGUI_H
