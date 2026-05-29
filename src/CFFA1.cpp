// POM1 Apple 1 Emulator — CFFA1 CompactFlash Interface
// Emulates the ATA/IDE registers used by the CFFA1 firmware to access
// a ProDOS .po disk image via LBA block reads/writes.

#include "CFFA1.h"
#include "Logger.h"
#include "SnapshotIO.h"
#include <cstring>
#include <algorithm>

CFFA1::CFFA1()
{
    rom.resize(kRomSize, 0xFF);
    reset();
}

void CFFA1::reset()
{
    ataError = 0;
    ataFeature = 0;
    ataSectorCnt = 0;
    ataLBA0 = 0;
    ataLBA1 = 0;
    ataLBA2 = 0;
    ataLBA3 = 0;
    ataDevCtrl = 0;
    bufferIndex = 0;
    readActive = false;
    writeActive = false;
    sectorBuffer.fill(0);

    // Status: DRDY + DSC if disk present, $00 if no disk
    ataStatus = hasDiskImage() ? (kStatusDRDY | kStatusDSC) : 0x00;
}

bool CFFA1::loadRom(const uint8_t* data, size_t size)
{
    if (size > kRomSize) size = kRomSize;
    std::memcpy(rom.data(), data, size);
    romLoaded = true;
    return true;
}

bool CFFA1::openDiskImage(const std::string& path)
{
    closeDiskImage();
    diskImagePath = path;
    diskFile.open(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!diskFile.is_open()) {
        pom1::log().warn("CF", "Cannot open disk image: " + path);
        ataStatus = 0x00; // no device
        return false;
    }
    // Determine size in 512-byte blocks
    diskFile.seekg(0, std::ios::end);
    auto fileSize = diskFile.tellg();
    diskFile.seekg(0, std::ios::beg);
    diskSizeBlocks = static_cast<uint32_t>(fileSize / 512);
    ataStatus = kStatusDRDY | kStatusDSC;
    pom1::log().info("CF", "Disk image: " + path + " (" + std::to_string(diskSizeBlocks) +
                           " blocks, " + std::to_string(diskSizeBlocks / 2) + " KB)");
    return true;
}

void CFFA1::closeDiskImage()
{
    if (diskFile.is_open()) {
        diskFile.close();
    }
    diskSizeBlocks = 0;
}

// --- Memory interface ---

uint8_t CFFA1::readByte(uint16_t address)
{
    if (address >= kRegBase && address <= kRegEnd) {
        // CF registers: mask to IOBase offset (A4 not connected → $AFEx mirrors $AFFx)
        uint8_t offset = (address & 0x0F);
        return readRegister(offset);
    }
    // ROM region $9000-$AFDF
    if (address >= kRomBase && address <= kRomEnd) {
        uint16_t romOffset = address - kRomBase;
        if (romOffset < rom.size()) {
            return rom[romOffset];
        }
    }
    return 0xFF;
}

void CFFA1::writeByte(uint16_t address, uint8_t value)
{
    if (address >= kRegBase && address <= kRegEnd) {
        uint8_t offset = (address & 0x0F);
        writeRegister(offset, value);
    }
    // ROM region is read-only — writes ignored
}

// --- ATA register read ---

uint8_t CFFA1::readRegister(uint8_t offset)
{
    switch (offset) {
    case RegDevCtrl: // AltStatus (same as Status)
        return ataStatus;

    case RegData: {
        if (!readActive) return 0xFF;
        // Defensive: a corrupt/hostile snapshot could restore bufferIndex out
        // of [0,512). Abort the transfer rather than index sectorBuffer OOB.
        if (bufferIndex < 0 || bufferIndex >= 512) {
            readActive  = false;
            bufferIndex = 0;
            ataStatus   = kStatusDRDY | kStatusDSC;
            return 0xFF;
        }
        uint8_t val = sectorBuffer[bufferIndex++];
        if (bufferIndex >= 512) {
            readActive = false;
            bufferIndex = 0;
            ataStatus = kStatusDRDY | kStatusDSC; // clear DRQ
        }
        return val;
    }

    case RegError:
        return ataError;

    case RegSectorCnt:
        return ataSectorCnt;

    case RegLBA0:
        return ataLBA0;

    case RegLBA1:
        return ataLBA1;

    case RegLBA2:
        return ataLBA2;

    case RegLBA3:
        return ataLBA3;

    case RegCommand: // Status register when reading
        return ataStatus;

    default:
        return 0xFF;
    }
}

// --- ATA register write ---

void CFFA1::writeRegister(uint8_t offset, uint8_t value)
{
    switch (offset) {
    case RegSetCSMask:
    case RegClearCSMask:
        // SanDisk workaround strobes — accept and ignore
        break;

    case RegDevCtrl:
        ataDevCtrl = value;
        if (value & 0x04) { // SRST — software reset
            reset();
        }
        break;

    case RegData:
        if (!writeActive) return;
        // Defensive: guard against an out-of-range bufferIndex restored from a
        // corrupt/hostile snapshot before indexing sectorBuffer.
        if (bufferIndex < 0 || bufferIndex >= 512) {
            writeActive = false;
            bufferIndex = 0;
            return;
        }
        sectorBuffer[bufferIndex++] = value;
        if (bufferIndex >= 512) {
            flushSectorBuffer();
            writeActive = false;
            bufferIndex = 0;
            ataStatus = kStatusDRDY | kStatusDSC; // clear DRQ
        }
        break;

    case RegError: // Feature register when writing
        ataFeature = value;
        break;

    case RegSectorCnt:
        ataSectorCnt = value;
        break;

    case RegLBA0:
        ataLBA0 = value;
        break;

    case RegLBA1:
        ataLBA1 = value;
        break;

    case RegLBA2:
        ataLBA2 = value;
        break;

    case RegLBA3:
        ataLBA3 = value;
        break;

    case RegCommand:
        executeCommand(value);
        break;
    }
}

// --- ATA command execution ---

uint32_t CFFA1::getLBA() const
{
    return ((uint32_t)(ataLBA3 & 0x0F) << 24) |
           ((uint32_t)ataLBA2 << 16) |
           ((uint32_t)ataLBA1 << 8) |
           (uint32_t)ataLBA0;
}

void CFFA1::executeCommand(uint8_t cmd)
{
    switch (cmd) {
    case kCmdReadSector:
        doReadSector();
        break;

    case kCmdWriteSector:
        doWriteSector();
        break;

    case kCmdSetFeature:
        // Accept silently (8-bit mode, etc.)
        ataStatus = kStatusDRDY | kStatusDSC;
        ataError = 0;
        break;

    default:
        // Unknown command — set error
        ataStatus = kStatusDRDY | kStatusDSC | kStatusERR;
        ataError = 0x04; // abort
        break;
    }
}

void CFFA1::doReadSector()
{
    uint32_t lba = getLBA();

    if (!diskFile.is_open() || lba >= diskSizeBlocks) {
        ataStatus = kStatusDRDY | kStatusDSC | kStatusERR;
        ataError = 0x04; // abort
        return;
    }

    diskFile.seekg(static_cast<std::streamoff>(lba) * 512);
    diskFile.read(reinterpret_cast<char*>(sectorBuffer.data()), 512);

    if (!diskFile) {
        // Read error — partial read or I/O failure
        diskFile.clear();
        ataStatus = kStatusDRDY | kStatusDSC | kStatusERR;
        ataError = 0x40; // uncorrectable
        return;
    }

    bufferIndex = 0;
    readActive = true;
    writeActive = false;
    ataStatus = kStatusDRDY | kStatusDSC | kStatusDRQ;
    ataError = 0;
}

void CFFA1::doWriteSector()
{
    if (!diskFile.is_open()) {
        ataStatus = kStatusDRDY | kStatusDSC | kStatusERR;
        ataError = 0x04;
        return;
    }

    sectorBuffer.fill(0);
    bufferIndex = 0;
    writeActive = true;
    readActive = false;
    ataStatus = kStatusDRDY | kStatusDSC | kStatusDRQ;
    ataError = 0;
}

void CFFA1::flushSectorBuffer()
{
    uint32_t lba = getLBA();

    if (!diskFile.is_open() || lba >= diskSizeBlocks) {
        ataStatus = kStatusDRDY | kStatusDSC | kStatusERR;
        ataError = 0x04;
        return;
    }

    diskFile.seekp(static_cast<std::streamoff>(lba) * 512);
    diskFile.write(reinterpret_cast<const char*>(sectorBuffer.data()), 512);
    diskFile.flush();

    if (!diskFile) {
        diskFile.clear();
        ataStatus = kStatusDRDY | kStatusDSC | kStatusERR;
        ataError = 0x40;
    }
}

void CFFA1::serialize(pom1::SnapshotWriter& w) const
{
    w.writeU8(ataError);
    w.writeU8(ataFeature);
    w.writeU8(ataSectorCnt);
    w.writeU8(ataLBA0);
    w.writeU8(ataLBA1);
    w.writeU8(ataLBA2);
    w.writeU8(ataLBA3);
    w.writeU8(ataStatus);
    w.writeU8(ataDevCtrl);
    w.writeBytes(sectorBuffer.data(), sectorBuffer.size());
    w.writeU32(static_cast<uint32_t>(bufferIndex));
    w.writeU8(readActive  ? 1 : 0);
    w.writeU8(writeActive ? 1 : 0);
}

void CFFA1::deserialize(pom1::SnapshotReader& r)
{
    ataError      = r.readU8();
    ataFeature    = r.readU8();
    ataSectorCnt  = r.readU8();
    ataLBA0       = r.readU8();
    ataLBA1       = r.readU8();
    ataLBA2       = r.readU8();
    ataLBA3       = r.readU8();
    ataStatus     = r.readU8();
    ataDevCtrl    = r.readU8();
    r.readBytes(sectorBuffer.data(), sectorBuffer.size());
    bufferIndex   = static_cast<int>(r.readU32());
    // Untrusted snapshot value — clamp into the sectorBuffer range so a forged
    // index can never drive an out-of-bounds access at $AFE0 (RegData).
    if (bufferIndex < 0 || bufferIndex >= 512) bufferIndex = 0;
    readActive    = r.readU8() != 0;
    writeActive   = r.readU8() != 0;
}
