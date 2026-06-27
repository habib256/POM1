#ifndef MEMORYVIEWER_IMGUI_H
#define MEMORYVIEWER_IMGUI_H

#include "Memory.h"
#include "CodeTank.h"
#include "JukeBox.h"
#include "Disassembler6502.h"
#include "Symbols.h"
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
    void updateLiveMemory(const std::vector<uint8_t>& memoryImage);

    struct ViewportRange { int startAddress, endAddress; };
    ViewportRange getViewportRange() const {
        int end = startAddress + bytesPerRow * displayRows;
        return { startAddress, end > 0x10000 ? 0x10000 : end };
    }
    void setWriteCallback(std::function<void(uint16_t, uint8_t)> callback);
    void setGraphicsCardEnabled(bool enabled) { gen2Enabled = enabled; }
    void setTMS9918Enabled(bool enabled) { tms9918Enabled = enabled; }
    void setSIDEnabled(bool enabled) { sidEnabled = enabled; }
    void setMicroSDEnabled(bool enabled) { microSDEnabled = enabled; }
    void setWiFiModemEnabled(bool enabled) { wifiModemEnabled = enabled; }
    void setTerminalCardEnabled(bool enabled) { terminalCardEnabled = enabled; }
    void setACIEnabled(bool enabled) { aciEnabled = enabled; }
    void setJukeBoxEnabled(bool enabled) { jukeBoxEnabled = enabled; }
    void setJukeBoxState(uint8_t page, uint8_t subPage, uint8_t pgCount,
                         JukeBox::Jumper jmp, JukeBox::ChipMode chip) {
        jbCurrentPage = page; jbCurrentSubPage = subPage;
        jbPageCount = pgCount; jbJumper = jmp; jbChipMode = chip;
    }
    void setCodeTankEnabled(bool enabled) { codeTankEnabled = enabled; }
    void setCodeTankJumper(CodeTank::Jumper j) { codeTankJumper = j; }
    // Current 6502 PC, pushed each frame by the host. The disassembly view
    // highlights the instruction at this address and (with Follow PC on)
    // re-anchors so it stays visible.
    void setCurrentPC(uint16_t pc) { currentPC = pc; }
    /// Merge user symbols from a file into the disassembler's table (on top of
    /// the built-in Apple-1 defaults). Returns symbols added; sets `err` on
    /// failure. See SymbolTable::loadFile for the accepted formats.
    int loadSymbolsFile(const std::string& path, std::string& err) {
        return symbols.loadFile(path, err);
    }

    struct RomRegion { uint16_t start, end; };
    void setLoadedRoms(const std::vector<RomRegion>& roms) { romRegions = roms; }

private:
    Memory* memory;
    const std::vector<uint8_t>* liveMemory = nullptr;
    std::function<void(uint16_t, uint8_t)> writeCallback;

    // Interface state
    int startAddress = 0x0000;
    int bytesPerRow = 16;
    int displayRows = 32;
    bool showAscii = true;
    bool showDisasm = false;
    bool showChanges = true;
    bool autoRefresh = false;
    bool colorizeRegions = true;
    bool gen2Enabled = false;
    bool tms9918Enabled = false;
    bool sidEnabled = false;
    bool microSDEnabled = false;
    bool wifiModemEnabled = false;
    bool terminalCardEnabled = false;
    bool aciEnabled = true;
    bool jukeBoxEnabled = false;
    uint8_t jbCurrentPage = 0;
    uint8_t jbCurrentSubPage = 0;
    uint8_t jbPageCount = 0;
    JukeBox::Jumper jbJumper = JukeBox::Jumper::RAM16_ROM32;
    JukeBox::ChipMode jbChipMode = JukeBox::ChipMode::Flash;
    bool codeTankEnabled = false;
    CodeTank::Jumper codeTankJumper = CodeTank::Jumper::Lower16;
    std::vector<RomRegion> romRegions;

    // Disassembly PC marker / follow
    uint16_t currentPC = 0;
    bool followPC = false;

    // Disassembly symbols (built-in Apple-1 defaults, loaded in the ctor).
    pom1::SymbolTable symbols;
    bool showSymbols = true;

    // Auto-refresh: snapshot taken when autoRefresh is off
    std::vector<uint8_t> snapshot;
    bool snapshotValid = false;
    void takeSnapshot();
    uint8_t readByte(int address) const;
    const uint8_t* getMemoryPointer() const;

    // Change highlighting: per-byte frame counter tracking last modification
    std::vector<uint8_t> prevMemory;
    std::vector<uint32_t> changeFrame;
    uint32_t frameCounter = 0;
    static constexpr uint32_t kChangeFadeFrames = 45; // ~0.75 s at 60 fps
    void detectChanges();

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
        uint16_t address;
        uint8_t oldValue;
        uint8_t newValue;
    };
    std::vector<EditRecord> undoStack;
    std::vector<EditRecord> redoStack;
    void applyEdit(uint16_t address, uint8_t newValue);
    void undo();
    void redo();

    // Bookmarks
    std::vector<int> bookmarks;

    // Utility functions
    void renderHexView();
    void renderDisasmView();
    void renderRegionBanner();
    void renderControls();
    void renderSearchDialog();
    void jumpToAddress(int address);
    void searchMemory();
    void searchAsciiString();
    void handleNavigation();

    // Helper functions
    char getPrintableChar(uint8_t value);

    // Single source of truth for memory-region name + colour. resolveRegion()
    // walks ONE priority-ordered cascade (highest priority first) and returns
    // both the display name and the colour from the same matched entry, so the
    // hex-cell colour and the region banner can never disagree (they used to:
    // two parallel cascades ordered differently put "A1-SID" in the banner
    // while the cells were TMS9918-blue at $CC00). getColorForAddress() and
    // getRegionName() are thin wrappers kept for their existing call sites.
    struct RegionInfo { const char* name; ImVec4 color; };
    RegionInfo resolveRegion(int address) const;
    ImVec4 getColorForAddress(int address) const { return resolveRegion(address).color; }
    const char* getRegionName(int address) const { return resolveRegion(address).name; }
    bool isROM(int address);
    bool isIO(int address);
};

#endif // MEMORYVIEWER_IMGUI_H
