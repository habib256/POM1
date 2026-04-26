// POM1 Apple 1 Emulator - P-LAB CodeTank
//
// Originally a daughterboard option of the P-LAB Apple-1 Juke-Box
// (Parmigiani / Rosselli), the CodeTank is now a card in its own right in
// POM1: a 32 kB 28c256 EEPROM split into two independent 16 kB halves. A
// single physical board jumper selects which half is mapped at $4000-$7FFF.
//
//   - Fixed address decode: $4000-$7FFF (16 kB, contiguous).
//   - No paging, no $CA00 latch, no Program Manager. The selected half is
//     directly visible from reset.
//   - The 28c256 stores two 16 kB programs back-to-back; the user picks which
//     one runs by moving the lower/upper jumper. Real-hardware-wise that is a
//     power-off operation; POM1 hot-swaps via the Hardware window.
//   - Designed to ship Apple-1 software that targets the P-LAB TMS9918 Graphic
//     Card (CC00/CC01) in real silicon — a TMS game can live on its own
//     CodeTank ROM and run on a P-LAB stack without depending on the cassette
//     deck or the microSD. CodeTank's $4000-$7FFF window does not collide with
//     TMS9918, A1-AUDIO SE ($CC00-$CC1F) or any of the I/O cards above $C800,
//     so the two cards can coexist freely on a real Apple-1 bus.
//   - POM1 models the 28c256 as read-only: a real CodeTank can be flashed with
//     a programmer between sessions, but the emulator never writes back.
//
// Mutually exclusive with the Juke-Box's RAM16/ROM32 jumper (both claim
// $4000-$7FFF) and with anything else that wants $4000-$7FFF; coexists with
// the Juke-Box's RAM32/ROM16 jumper which only uses $8000-$BFFF — POM1's
// `Memory::setCodeTankEnabled` enforces the conservative subset (one ROM card
// at a time) for clarity.

#ifndef POM1_CODETANK_H
#define POM1_CODETANK_H

#include <cstdint>
#include <string>
#include <vector>

class CodeTank {
public:
    // Physical board jumper: which half of the 32 kB 28c256 is wired into
    // the fixed 16 kB CPU window. Real hardware needs a power-off and
    // jumper move; POM1 hot-swaps because there is no socket.
    enum class Jumper : uint8_t {
        Lower16 = 0,  // file offset $0000-$3FFF visible at $4000-$7FFF
        Upper16 = 1,  // file offset $4000-$7FFF visible at $4000-$7FFF
    };

    static constexpr size_t   kRomSize  = 0x8000;   // 32 kB (28c256)
    static constexpr size_t   kHalfSize = 0x4000;   // 16 kB (one bank)
    static constexpr uint16_t kBase     = 0x4000;
    static constexpr uint16_t kEnd      = 0x7FFF;

    CodeTank();

    // Reset only re-seats transient state. The ROM buffer is preserved
    // across reset (matching real EEPROM persistence).
    void reset() {}

    // Memory interface dispatched by PeripheralBus when the card is plugged.
    // Reads return $FF when no ROM has been loaded.
    uint8_t readByte(uint16_t address) const;
    // Writes are always dropped — POM1 models the CodeTank as read-only.
    void    writeByte(uint16_t /*address*/, uint8_t /*value*/) {}

    Jumper getJumper() const { return jumper; }
    void   setJumper(Jumper j) { jumper = j; }

    // Load a 32 kB ROM file. Anything other than exactly kRomSize is
    // rejected with `error` populated; previous contents are preserved on
    // failure. Empty file path resets to the blank $FF buffer.
    bool loadRomFile(const std::string& path, std::string& error);
    // Empty the buffer to all $FF (a "freshly programmed" 28c256 baseline)
    // and drop the romPath. Used when the user explicitly unloads the card.
    void clearRom();
    // True when the buffer has been populated from an actual file (vs.
    // power-on $FF pattern). Drives the Hardware window's status row.
    bool hasRom() const { return !romPath.empty(); }

    const std::string& getRomPath() const { return romPath; }
    size_t             getRomSize() const { return rom.size(); }
    const uint8_t*     getRomPointer() const { return rom.data(); }

    // Snapshot for the UI thread. Tiny — copied every frame by the
    // SnapshotPublisher under stateMutex.
    struct Snapshot {
        std::string romPath;
        size_t      romSize = 0;
        Jumper      jumper  = Jumper::Lower16;
        bool        loaded  = false;
    };
    void copySnapshot(Snapshot& out) const;

private:
    std::vector<uint8_t> rom;
    Jumper      jumper = Jumper::Lower16;
    std::string romPath;
};

#endif // POM1_CODETANK_H
