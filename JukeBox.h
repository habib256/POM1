// POM1 Apple 1 Emulator - P-LAB Apple-1 Juke-Box
//
// Claudio Parmigiani's Apple-1 Juke-Box: a storage ROM card (EPROM / EEPROM /
// FLASH, 16 kB to 512 kB) that replaces cassette loads with an instant menu
// of bundled programs + BASIC + the Program Manager firmware. POM1 v1
// models the default shipping configuration: a single 32 kB EEPROM (28c256)
// wired directly to the address bus at $4000-$BFFF, with a runtime-toggleable
// jumper that alternates between:
//
//   - RAM 16 kB / ROM 32 kB: ROM window $4000-$BFFF, full 32 kB visible.
//     User RAM capped at $3FFF (16 kB).
//   - RAM 32 kB / ROM 16 kB: ROM window $8000-$BFFF, only upper 16 kB of
//     the file visible. User RAM extends to $7FFF (32 kB).
//
// Multi-page 29c020/29c040/39sf040 support (P0..PF command) is NOT modelled:
// the MMIO bank-select register address isn't documented on P-LAB's public
// page. Same for the 16 kB logical mapping S0/S1 sub-page toggle.
//
// The Program Manager sits at a fixed offset inside the ROM file:
//   $BD00 = file offset $7D00 (both in RAM-16/ROM-32 and in RAM-32/ROM-16
//   because the upper half of the file is what's visible in the 16 kB
//   window). Its first byte is $A5 (LDA zp) -- used as a firmware-present
//   signature so the Hardware window can warn if the user loads a blank
//   or non-firmware blob.
//
// Build a ROM with P-LAB's freely-distributed EPROM_CREATOR script pack
// (`1-stripper.sh` + `2-packer.sh`). The packer auto-embeds the Program
// Manager + Save Program + the BASIC interpreter; the user only supplies
// programs to bundle. Drop the resulting MYROM_0.BIN in as `roms/jukebox.rom`.

#ifndef POM1_JUKEBOX_H
#define POM1_JUKEBOX_H

#include <array>
#include <cstdint>
#include <string>

class JukeBox {
public:
    // Jumper position: which part of the 32 kB ROM file is visible in the
    // CPU address space, and how much contiguous RAM the Apple-1 sees.
    enum class Jumper : uint8_t {
        RAM16_ROM32 = 0,  // ROM window $4000-$BFFF (32 kB), RAM up to $3FFF
        RAM32_ROM16 = 1,  // ROM window $8000-$BFFF (16 kB), RAM up to $7FFF
    };

    static constexpr size_t   kRomFileSize            = 0x8000;   // 32 kB
    static constexpr uint16_t kRom32Base              = 0x4000;
    static constexpr uint16_t kRom32End               = 0xBFFF;
    static constexpr uint16_t kRom16Base              = 0x8000;
    static constexpr uint16_t kRom16End               = 0xBFFF;
    static constexpr uint16_t kProgramManagerAddr     = 0xBD00;
    static constexpr uint16_t kProgramManagerOffset   = 0x7D00;   // file offset
    static constexpr uint16_t kSaveProgramAddr        = 0xB800;
    static constexpr uint16_t kSaveProgramOffset      = 0x7800;
    // Expected first byte of the Program Manager (LDA zp). The RW manual
    // shows "BD00: A5" right after BD00R — we use that as the firmware
    // signature.
    static constexpr uint8_t  kProgramManagerSignature = 0xA5;

    JukeBox();

    // Reset any write-back state. The ROM image itself stays loaded —
    // matches how a real EEPROM keeps its contents across a reset.
    void reset();

    // Memory interface. Dispatch for the current jumper position is done
    // inside readByte/writeByte; Memory registers two PeripheralBus entries
    // (one per jumper window) and enables exactly one at a time.
    uint8_t readByte(uint16_t address) const;
    void    writeByte(uint16_t address, uint8_t value);

    // Load a ROM file (up to 32 kB) from disk. Shorter files are accepted
    // and padded with $FF (matches a blank EPROM). `error` is populated on
    // failure; the previous ROM contents are left untouched.
    bool loadRomFile(const std::string& path, std::string& error);

    // Fill the ROM buffer with $FF. Clears romPath and romSize. Used when
    // the user unplugs the card or wants to start from a blank slate.
    void clearRom();

    // Firmware-present heuristic: byte at file offset $7D00 equals $A5
    // (opcode LDA zp — first byte of the Program Manager per the RW manual
    // signon "BD00: A5"). Returns false for a blank EPROM ($FF everywhere)
    // or an arbitrary binary that happens to fit in 32 kB.
    bool hasFirmware() const;

    Jumper getJumper() const { return jumper; }
    void   setJumper(Jumper j) { jumper = j; }

    // EEPROM write-protect jumper. When false, writes in the ROM window
    // are silently dropped (matches a real EPROM or an EEPROM in RO).
    // When true, writes land in the rom buffer and are persisted to the
    // backing file if one was loaded.
    bool   isWritable() const { return writable; }
    void   setWritable(bool w) { writable = w; }

    const std::string& getRomPath() const { return romPath; }
    size_t             getRomSize() const { return romSize; }

    // Direct access to the ROM buffer for bulk operations (snapshot dumps,
    // hex viewer). Always 32 kB — check `getRomSize()` for the actual
    // loaded content length if you need to distinguish loaded from padded.
    const uint8_t* getRomPointer() const { return rom.data(); }

    struct Snapshot {
        std::string romPath;
        size_t      romSize         = 0;
        Jumper      jumper          = Jumper::RAM16_ROM32;
        bool        writable        = false;
        bool        firmwarePresent = false;
    };
    void copySnapshot(Snapshot& out) const;

private:
    // Convert a CPU address (inside the current ROM window) to a ROM file
    // offset. Only called when the PeripheralBus has already routed the
    // access to us, so the address is guaranteed to be in range.
    uint16_t fileOffsetForAddress(uint16_t address) const;

    // Persist the ROM buffer to `romPath`. No-op if the path is empty.
    // Called after every write in writable mode — simple and correct.
    // The 28c256 takes ~25 s to save 4 KB on real hardware; POM1 just
    // flushes synchronously (the plan explicitly does not model the
    // EEPROM write timing).
    void flushRomToFile() const;

    std::array<uint8_t, kRomFileSize> rom{};
    Jumper      jumper   = Jumper::RAM16_ROM32;
    bool        writable = false;
    std::string romPath;
    size_t      romSize  = 0;
};

#endif // POM1_JUKEBOX_H
