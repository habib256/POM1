// POM1 Apple 1 Emulator - P-LAB CodeTank implementation.
// See CodeTank.h for the card description.

#include "CodeTank.h"

#include "Logger.h"
#include "SnapshotIO.h"

#include <fstream>

CodeTank::CodeTank()
{
    rom.assign(kRomSize, 0xFF);
}

uint8_t CodeTank::readByte(uint16_t address) const
{
    const size_t halfBase = (jumper == Jumper::Upper16) ? kHalfSize : 0u;
    const size_t off      = halfBase + static_cast<size_t>(address - kBase);
    if (off >= rom.size()) return 0xFF;
    return rom[off];
}

bool CodeTank::loadRomFile(const std::string& path, std::string& error)
{
    error.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        error = "Cannot open CodeTank ROM: " + path;
        return false;
    }
    file.seekg(0, std::ios::end);
    auto sz = static_cast<std::streamoff>(file.tellg());
    file.seekg(0, std::ios::beg);
    if (sz != static_cast<std::streamoff>(kRomSize)) {
        error = "CodeTank ROM must be exactly 32 kB (got " +
                std::to_string(sz) + " bytes): " + path;
        return false;
    }
    rom.assign(kRomSize, 0xFF);
    file.read(reinterpret_cast<char*>(rom.data()),
              static_cast<std::streamsize>(kRomSize));
    romPath = path;
    pom1::log().info("CodeTank",
        "ROM loaded: " + path + " (32 kB, two 16 kB halves)");
    return true;
}

void CodeTank::clearRom()
{
    rom.assign(kRomSize, 0xFF);
    romPath.clear();
}

void CodeTank::copySnapshot(Snapshot& out) const
{
    out.romPath = romPath;
    out.romSize = rom.size();
    out.jumper  = jumper;
    out.loaded  = !romPath.empty();
}

void CodeTank::serialize(pom1::SnapshotWriter& w) const
{
    w.writeU8(static_cast<uint8_t>(jumper));
}

void CodeTank::deserialize(pom1::SnapshotReader& r)
{
    jumper = static_cast<Jumper>(r.readU8());
}
