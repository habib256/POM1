// POM1 Apple 1 Emulator - P-LAB Apple-1 Juke-Box implementation.
//
// See JukeBox.h for the card description and scope. This TU is deliberately
// small — the 28c256 configuration is a direct memory-mapped EEPROM, so
// there is no bank register, no MCU bridge, no handshake.

#include "JukeBox.h"

#include "Logger.h"

#include <fstream>

JukeBox::JukeBox()
{
    clearRom();
}

void JukeBox::reset()
{
    // Leave the ROM contents and romPath alone — a reset on real hardware
    // doesn't wipe the EEPROM. The writable flag and jumper are driven by
    // the UI/preset, not by CPU reset.
}

void JukeBox::clearRom()
{
    rom.fill(0xFF);
    romPath.clear();
    romSize = 0;
}

bool JukeBox::loadRomFile(const std::string& path, std::string& error)
{
    error.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        error = "Cannot open Juke-Box ROM: " + path;
        return false;
    }
    file.seekg(0, std::ios::end);
    auto sz = static_cast<std::streamoff>(file.tellg());
    file.seekg(0, std::ios::beg);
    if (sz <= 0) {
        error = "Juke-Box ROM is empty: " + path;
        return false;
    }
    if (static_cast<size_t>(sz) > kRomFileSize) {
        error = "Juke-Box ROM too large (" + std::to_string(sz) +
                " bytes, max " + std::to_string(kRomFileSize) + ")";
        return false;
    }
    rom.fill(0xFF);
    file.read(reinterpret_cast<char*>(rom.data()), sz);
    romPath = path;
    romSize = static_cast<size_t>(sz);
    pom1::log().info("JukeBox",
        "ROM loaded: " + path + " (" + std::to_string(romSize) + " bytes)"
        + (hasFirmware() ? " [firmware present]" : " [NO firmware at $BD00]"));
    return true;
}

bool JukeBox::hasFirmware() const
{
    return rom[kProgramManagerOffset] == kProgramManagerSignature;
}

uint16_t JukeBox::fileOffsetForAddress(uint16_t address) const
{
    // RAM-16/ROM-32 mode: ROM window is $4000-$BFFF, file maps 1:1.
    //   $4000 -> offset 0
    //   $BD00 -> offset $7D00 (Program Manager)
    //
    // RAM-32/ROM-16 mode: ROM window is $8000-$BFFF (upper half of file).
    //   $8000 -> offset $4000
    //   $BD00 -> offset $7D00 (Program Manager still lands here -- that's
    //   why the Program Manager is placed in the upper half of the 32 kB
    //   file: it stays reachable at $BD00 regardless of jumper position).
    if (jumper == Jumper::RAM16_ROM32) {
        return static_cast<uint16_t>(address - kRom32Base);
    }
    return static_cast<uint16_t>((address - kRom16Base) + 0x4000);
}

uint8_t JukeBox::readByte(uint16_t address) const
{
    return rom[fileOffsetForAddress(address)];
}

void JukeBox::writeByte(uint16_t address, uint8_t value)
{
    if (!writable) return;
    const uint16_t offset = fileOffsetForAddress(address);
    if (rom[offset] == value) return; // no-op, skip disk write
    rom[offset] = value;
    flushRomToFile();
}

void JukeBox::flushRomToFile() const
{
    if (romPath.empty()) return;
    // Write the full buffer each time. Juke-Box writes are rare (user saves
    // a BASIC program from the # prompt — once per save), so the cost of
    // flushing 32 kB is negligible compared to the emulated 25 s the real
    // EEPROM would take. Keeping the file closed between writes avoids
    // keeping an fstream open across the application lifetime.
    std::ofstream out(romPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        pom1::log().warn("JukeBox", "Cannot open ROM for write: " + romPath);
        return;
    }
    const size_t bytesToWrite = (romSize > 0 ? romSize : kRomFileSize);
    out.write(reinterpret_cast<const char*>(rom.data()),
              static_cast<std::streamsize>(bytesToWrite));
}

void JukeBox::copySnapshot(Snapshot& out) const
{
    out.romPath         = romPath;
    out.romSize         = romSize;
    out.jumper          = jumper;
    out.writable        = writable;
    out.firmwarePresent = hasFirmware();
}
