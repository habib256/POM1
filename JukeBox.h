// POM1 Apple 1 Emulator - P-LAB Apple-1 Juke-Box
//
// Claudio Parmigiani and Jacopo Rosselli's Apple-1 Juke-Box (P-LAB, 2020-26):
// a storage ROM card that replaces cassette loads with an instant menu of
// bundled programs + BASIC + the Program Manager firmware. The card accepts
// two physically distinct chip variants:
//
//   - FLASH mode (default): paged read-only memory, 16 kB to 512 kB
//     (27c128..27c020, 29c020, 29c040, 39sf040). Divided into 32 kB pages;
//     the `Px` command (x in 0..F) writes the page number to a write-only
//     latch at $CA00 which drives the flash chip's high address lines.
//     Up to 16 pages x 32 kB = 512 kB addressable. With ROM MAP 16 kB
//     logical jumper the `Sx` command (x in 0..1) additionally selects
//     upper/lower 16 kB half of the current 32 kB page, via bit 4 of $CA00.
//
//   - EEPROM mode: a 32 kB 28c256 swapped in for save-capable single-page
//     operation. Writes from the Save Program at $B800 persist to the
//     backing file. Only one page; Px/Sx are no-ops. Real hardware takes
//     ~25 s to save 4 kB (we flush synchronously, no delay modelled).
//
// Physical jumper selects where the ROM window sits on the Apple-1 bus:
//
//   - RAM16/ROM32: window $4000-$BFFF (full 32 kB page visible).
//     User RAM capped at $3FFF.
//   - RAM32/ROM16: window $8000-$BFFF (upper or lower 16 kB half only,
//     selected by the Sx sub-page bit). User RAM extends to $7FFF.
//
// The Program Manager sits at file offset $7D00 within each page. Its
// first byte is $A5 (LDA zp); we use that as the firmware-present signature
// and scan all pages at plug-in to pick the lowest page with a valid
// signature as the default boot page. Guarantees BD00R drops the user at
// the `&' prompt on any well-formed ROM, even if the ROM builder left some
// pages without the Program Manager (as happens with build_jukebox_rom.py).
//
// Build roms/jukebox.rom via doc/JUKEBOX_ROM_CREATOR/build_jukebox_rom.py
// (Python, deterministic). P-LAB's 2-packer.sh is the original but depends
// on GNU-only tools.

#ifndef POM1_JUKEBOX_H
#define POM1_JUKEBOX_H

#include <cstdint>
#include <string>
#include <vector>

class JukeBox {
public:
    // Jumper position: which part of a 32 kB page is visible in the
    // CPU address space, and how much contiguous RAM the Apple-1 sees.
    enum class Jumper : uint8_t {
        RAM16_ROM32 = 0,  // Window $4000-$BFFF (32 kB), RAM up to $3FFF
        RAM32_ROM16 = 1,  // Window $8000-$BFFF (16 kB), RAM up to $7FFF
    };

    // Physical chip socketed on the card. Per Parmigiani/Rosselli you
    // physically swap between one and the other; POM1 exposes it as a
    // user-selectable mode because the emulator has no socket.
    enum class ChipMode : uint8_t {
        Flash       = 0,  // Paged read-only, 16 kB to 512 kB (default).
        EEPROM28C256 = 1, // Single-page 32 kB 28c256, writable.
    };

    static constexpr size_t   kPageSize              = 0x8000;   // 32 kB
    static constexpr size_t   kSubPageSize           = 0x4000;   // 16 kB
    static constexpr size_t   kMinRomFileSize        = 0x4000;   // 16 kB
    static constexpr size_t   kMaxRomFileSize        = 0x80000;  // 512 kB
    static constexpr size_t   kEepromFileSize        = 0x8000;   // 32 kB

    static constexpr uint16_t kRom32Base             = 0x4000;
    static constexpr uint16_t kRom32End              = 0xBFFF;
    static constexpr uint16_t kRom16Base             = 0x8000;
    static constexpr uint16_t kRom16End              = 0xBFFF;
    static constexpr uint16_t kBankRegisterAddr      = 0xCA00;   // Px / Sx latch
    static constexpr uint16_t kProgramManagerAddr    = 0xBD00;
    static constexpr uint16_t kProgramManagerOffset  = 0x7D00;   // within a page
    static constexpr uint16_t kSaveProgramAddr       = 0xB800;
    static constexpr uint16_t kSaveProgramOffset     = 0x7800;
    // First byte of the Program Manager (LDA zp). Per the RW manual the
    // signon prints "BD00: A5" right after BD00R.
    static constexpr uint8_t  kProgramManagerSignature = 0xA5;

    JukeBox();

    // Reset any transient state. Does NOT wipe the ROM buffer or reset
    // romPath — matches real flash / EEPROM keeping contents across reset.
    // Does re-derive the default boot page (the CPU latch at $CA00 powers
    // up in an undefined state on the real card; we re-seat to a known-good
    // page so BD00R works immediately on hard reset).
    void reset();

    // Memory interface. Dispatched by PeripheralBus; the two address-space
    // handles (RAM16/ROM32 window + RAM32/ROM16 window) are registered by
    // Memory::setJukeBoxEnabled() and exactly one is active at a time.
    uint8_t readByte(uint16_t address) const;
    void    writeByte(uint16_t address, uint8_t value);

    // Bank-select latch at $CA00. Write-only on real hardware; POM1 returns
    // $FF on read since the Program Manager never reads it back (no
    // `AD 00 CA` in the disassembled firmware). Bits 0-3 = Px page, bit 4
    // = Sx sub-page; upper bits ignored.
    void writeBankRegister(uint8_t value);

    uint8_t getBankRegister() const { return bankRegister; }
    uint8_t getCurrentPage() const;      // 0..pageCount-1 (wraps for undersized ROMs)
    uint8_t getCurrentSubPage() const;   // 0 (lower) or 1 (upper)

    // Duplicate one 32 kB page over another inside the in-memory ROM buffer.
    // POM1-only authoring helper — real flash needs erase+program sequences
    // and a dedicated programmer; the EEPROM 28c256 has only one page so
    // this returns false there. Changes are RAM-only until the user saves
    // the ROM back to disk.
    bool copyPage(uint8_t fromPage, uint8_t toPage, std::string& error);

    // Persist the current in-memory ROM buffer back to `path` (or `romPath`
    // when `path` is empty). Used by the UI to commit page-copy edits.
    bool saveRomFile(const std::string& path, std::string& error) const;

    // Load a ROM file from disk. Accepts 16 kB..512 kB for Flash mode,
    // exactly 32 kB for EEPROM mode. Shorter flash files are padded with
    // $FF up to the nearest page boundary. `error` is populated on failure;
    // previous contents are preserved on error. Picks default boot page
    // on success.
    bool loadRomFile(const std::string& path, std::string& error);

    // Empty the ROM buffer. Drops romPath and pageCount. For EEPROM mode
    // this prepares an empty 32 kB slate; for Flash mode we start with a
    // single $FF page so the card is "installed but blank".
    void clearRom();

    // Firmware-present scan: returns true iff at least one page carries
    // `$A5` at file offset $7D00 within the page.
    bool hasFirmware() const;

    // True if the given page (0..15) has the Program Manager signature.
    // Out-of-range pages return false.
    bool pageHasFirmware(uint8_t page) const;

    // Lowest page index that passes `pageHasFirmware`, or 0 if none.
    uint8_t getBootPage() const { return bootPage; }

    // Re-scan the ROM for firmware pages and seat the bank register on
    // the lowest match. Called by loadRomFile() and reset(). Returns true
    // if a firmware page was found.
    bool pickDefaultBootPage();

    Jumper getJumper() const { return jumper; }
    void   setJumper(Jumper j) { jumper = j; }

    ChipMode getChipMode() const { return chipMode; }
    void     setChipMode(ChipMode m);

    // EEPROM write-protect jumper. Only meaningful in EEPROM mode. When
    // false, writes in the ROM window are silently dropped. When true and
    // chipMode == EEPROM28C256, writes land in the rom buffer and are
    // persisted to the backing file. In Flash mode writes are always
    // dropped regardless of this flag (real flash needs erase + program
    // command sequences, not modelled).
    bool   isWritable() const { return writable; }
    void   setWritable(bool w) { writable = w; }

    const std::string& getRomPath() const { return romPath; }
    size_t             getRomSize() const { return romSize; }
    uint8_t            getPageCount() const { return pageCount; }

    // Direct access to the ROM buffer. Size is `rom.size()` = at least
    // `kPageSize`, up to `kMaxRomFileSize`. Used by the Memory Viewer
    // and snapshot dumps.
    const uint8_t* getRomPointer() const { return rom.data(); }
    size_t         getRomBufferSize() const { return rom.size(); }

    struct Snapshot {
        std::string romPath;
        size_t      romSize         = 0;
        uint8_t     pageCount       = 0;
        uint8_t     bankRegister    = 0;
        uint8_t     currentPage     = 0;
        uint8_t     currentSubPage  = 0;
        uint8_t     bootPage        = 0;
        Jumper      jumper          = Jumper::RAM16_ROM32;
        ChipMode    chipMode        = ChipMode::Flash;
        bool        writable        = false;
        bool        firmwarePresent = false;
    };
    void copySnapshot(Snapshot& out) const;

private:
    // Convert a CPU address (inside the current ROM window) to an offset
    // into the `rom` buffer, honouring the current page + sub-page and the
    // physical jumper position. PeripheralBus has already routed the
    // access to us, so the address is guaranteed to sit inside the window.
    size_t fileOffsetForAddress(uint16_t address) const;

    // Persist the full rom buffer to `romPath` (EEPROM mode only). No-op
    // if romPath is empty. Called after every successful EEPROM write.
    void flushRomToFile() const;

    // Initialise the buffer for Flash mode (single blank page, $FF filled).
    void initBlankFlash();
    // Initialise the buffer for EEPROM mode (32 kB, $FF filled).
    void initBlankEeprom();

    std::vector<uint8_t> rom;
    Jumper      jumper        = Jumper::RAM16_ROM32;
    ChipMode    chipMode      = ChipMode::Flash;
    bool        writable      = false;
    uint8_t     bankRegister  = 0;    // $CA00 latch value
    uint8_t     pageCount     = 0;    // rom.size() / kPageSize
    uint8_t     bootPage      = 0;    // chosen by pickDefaultBootPage
    std::string romPath;
    size_t      romSize       = 0;    // original loaded file size (pre-pad)
};

#endif // POM1_JUKEBOX_H
