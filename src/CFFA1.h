// POM1 Apple 1 Emulator — CFFA1 CompactFlash Interface
// Emulates Rich Dreher's CFFA1 card: 8 KB EEPROM firmware ($9000-$AFFF)
// with ATA/IDE registers ($AFE0-$AFFF) backed by a ProDOS .po disk image.
//
// Reference: CFFA1 Manual v1.1, firmware source (cc65), doc/reference/CFFA1_cdromv1.1.zip

#ifndef CFFA1_H
#define CFFA1_H

#include "Peripheral.h"

#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <string_view>
#include <fstream>

class CFFA1 : public pom1::Peripheral {
public:
    std::string_view name() const override { return "CFFA1"; }

    // Address ranges
    static constexpr uint16_t kRomBase = 0x9000;
    static constexpr uint16_t kRomEnd  = 0xAFDF;
    static constexpr uint16_t kRegBase = 0xAFE0; // CF registers (mirrored: $AFE0=$AFF0)
    static constexpr uint16_t kRegEnd  = 0xAFFF;
    static constexpr size_t   kRomSize = 0x1FE0; // 8160 bytes

    // ID verification
    static constexpr uint16_t kId1Addr = 0xAFDC; // must read $CF
    static constexpr uint16_t kId2Addr = 0xAFDD; // must read $FA

    // IOBase for ATA registers (A4 not connected in CPLD, so $AFE0 mirrors $AFF0)
    static constexpr uint16_t kIOBase = 0xAFF0;

    CFFA1();
    void reset();

    // Memory interface
    uint8_t readByte(uint16_t address);
    void    writeByte(uint16_t address, uint8_t value);

    // ROM management
    bool loadRom(const uint8_t* data, size_t size);
    bool hasRom() const { return romLoaded; }

    // Disk image management
    bool openDiskImage(const std::string& path);
    void closeDiskImage();
    bool hasDiskImage() const { return diskFile.is_open(); }
    void setDiskImagePath(const std::string& path) { diskImagePath = path; }
    const std::string& getDiskImagePath() const { return diskImagePath; }

    // Snapshot round-trip: ATA register state + sector buffer + transfer
    // flags. The disk image (path + open file) is owned by Memory at
    // construction time and intentionally NOT serialized — the .po on disk
    // holds the persistent storage; this captures only the in-flight
    // CompactFlash controller state.
    void serialize(pom1::SnapshotWriter& writer) const override;
    void deserialize(pom1::SnapshotReader& reader) override;

private:
    // ATA register offsets from IOBase
    enum ATAReg : uint8_t {
        RegSetCSMask   = 1,  // $AFF1 write-only strobe
        RegClearCSMask = 2,  // $AFF2 write-only strobe
        RegDevCtrl     = 6,  // $AFF6 write / AltStatus read
        RegData        = 8,  // $AFF8 R/W
        RegError       = 9,  // $AFF9 read / Feature write
        RegSectorCnt   = 10, // $AFFA R/W
        RegLBA0        = 11, // $AFFB R/W
        RegLBA1        = 12, // $AFFC R/W
        RegLBA2        = 13, // $AFFD R/W
        RegLBA3        = 14, // $AFFE R/W
        RegCommand     = 15, // $AFFF write / Status read
    };

    // ATA commands used by CFFA1 firmware
    static constexpr uint8_t kCmdReadSector  = 0x20;
    static constexpr uint8_t kCmdWriteSector = 0x30;
    static constexpr uint8_t kCmdSetFeature  = 0xEF;

    // ATA status bits
    static constexpr uint8_t kStatusBSY  = 0x80;
    static constexpr uint8_t kStatusDRDY = 0x40;
    static constexpr uint8_t kStatusDSC  = 0x10;
    static constexpr uint8_t kStatusDRQ  = 0x08;
    static constexpr uint8_t kStatusERR  = 0x01;

    // Register state
    uint8_t ataError      = 0;
    uint8_t ataFeature    = 0;
    uint8_t ataSectorCnt  = 0;
    uint8_t ataLBA0       = 0;
    uint8_t ataLBA1       = 0;
    uint8_t ataLBA2       = 0;
    uint8_t ataLBA3       = 0;
    uint8_t ataStatus     = 0;
    uint8_t ataDevCtrl    = 0;

    // Sector buffer for data transfers
    std::array<uint8_t, 512> sectorBuffer{};
    int bufferIndex  = 0;
    bool readActive  = false;  // DRQ set, reading from buffer
    bool writeActive = false;  // DRQ set, writing to buffer

    // ROM ($9000-$AFDF)
    std::vector<uint8_t> rom;
    bool romLoaded = false;

    // Disk image
    std::string diskImagePath;
    std::fstream diskFile;
    uint32_t diskSizeBlocks = 0;

    // Internal
    uint32_t getLBA() const;
    void executeCommand(uint8_t cmd);
    void doReadSector();
    void doWriteSector();
    void flushSectorBuffer();
    uint8_t readRegister(uint8_t offset);
    void    writeRegister(uint8_t offset, uint8_t value);
};

#endif // CFFA1_H
