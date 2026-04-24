// POM1 Apple 1 Emulator - P-LAB Apple-1 Juke-Box implementation.
// Hardware design: Claudio Parmigiani and Jacopo Rosselli (P-LAB, 2020-26).
// See JukeBox.h for the card description and scope.

#include "JukeBox.h"

#include "Logger.h"

#include <fstream>

namespace {
bool isPowerOfTwo(size_t n)
{
    return n > 0 && (n & (n - 1)) == 0;
}
} // namespace

JukeBox::JukeBox()
{
    initBlankFlash();
}

void JukeBox::reset()
{
    // Real card: $CA00 latch powers up in an undefined state. We re-seat
    // to the known-good boot page so BD00R always lands at the `&' prompt.
    pickDefaultBootPage();
}

void JukeBox::initBlankFlash()
{
    rom.assign(kPageSize, 0xFF);
    pageCount    = 1;
    bankRegister = 0;
    bootPage     = 0;
    romPath.clear();
    romSize = 0;
}

void JukeBox::initBlankEeprom()
{
    rom.assign(kEepromFileSize, 0xFF);
    pageCount    = 1;
    bankRegister = 0;
    bootPage     = 0;
    romPath.clear();
    romSize = 0;
}

void JukeBox::clearRom()
{
    if (chipMode == ChipMode::EEPROM28C256) {
        initBlankEeprom();
    } else {
        initBlankFlash();
    }
}

void JukeBox::setChipMode(ChipMode m)
{
    if (chipMode == m) return;
    chipMode = m;
    // Switching modes clears the buffer — a physical chip swap is
    // equivalent to losing the previous chip's contents from POM1's
    // viewpoint. The caller should reload a ROM via Memory::loadJukeBoxRom.
    clearRom();
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
    const size_t rawSize = static_cast<size_t>(sz);

    if (chipMode == ChipMode::EEPROM28C256) {
        if (rawSize != kEepromFileSize) {
            error = "EEPROM ROM must be exactly 32 kB (" +
                    std::to_string(rawSize) + " bytes provided)";
            return false;
        }
        rom.assign(kEepromFileSize, 0xFF);
        file.read(reinterpret_cast<char*>(rom.data()),
                  static_cast<std::streamsize>(kEepromFileSize));
        pageCount = 1;
    } else {
        if (rawSize < kMinRomFileSize || rawSize > kMaxRomFileSize) {
            error = "Flash ROM size out of range (" +
                    std::to_string(rawSize) + " bytes; expected " +
                    std::to_string(kMinRomFileSize) + ".." +
                    std::to_string(kMaxRomFileSize) + ")";
            return false;
        }
        // Pad up to the nearest 32 kB boundary. The real card mirrors
        // smaller chips (e.g. a 16 kB EPROM shows the same contents in
        // pages 0 and 1); we pad with $FF, which is close enough for a
        // chip that has no page decode.
        size_t paddedSize = (rawSize + kPageSize - 1) & ~(kPageSize - 1);
        if (paddedSize < kPageSize) paddedSize = kPageSize;
        if (!isPowerOfTwo(paddedSize / kPageSize)) {
            // Mustn't happen with valid ROMs (16 kB padded = 32 kB = 1 page).
            // Round up defensively.
            size_t pages = paddedSize / kPageSize;
            while (!isPowerOfTwo(pages)) ++pages;
            paddedSize = pages * kPageSize;
        }
        rom.assign(paddedSize, 0xFF);
        file.read(reinterpret_cast<char*>(rom.data()),
                  static_cast<std::streamsize>(rawSize));
        pageCount = static_cast<uint8_t>(paddedSize / kPageSize);
    }

    romPath = path;
    romSize = rawSize;
    bankRegister = 0;
    pickDefaultBootPage();

    pom1::log().info("JukeBox",
        "ROM loaded: " + path +
        " (" + std::to_string(romSize) + " bytes, " +
        std::to_string(static_cast<int>(pageCount)) + " page(s), mode=" +
        (chipMode == ChipMode::EEPROM28C256 ? "EEPROM" : "Flash") + ")" +
        (hasFirmware()
             ? " [firmware at page " + std::to_string(static_cast<int>(bootPage)) + "]"
             : " [NO firmware signature]"));
    return true;
}

bool JukeBox::pageHasFirmware(uint8_t page) const
{
    if (page >= pageCount) return false;
    const size_t off = static_cast<size_t>(page) * kPageSize + kProgramManagerOffset;
    if (off >= rom.size()) return false;
    return rom[off] == kProgramManagerSignature;
}

bool JukeBox::hasFirmware() const
{
    for (uint8_t p = 0; p < pageCount; ++p) {
        if (pageHasFirmware(p)) return true;
    }
    return false;
}

bool JukeBox::pickDefaultBootPage()
{
    for (uint8_t p = 0; p < pageCount; ++p) {
        if (pageHasFirmware(p)) {
            bootPage = p;
            bankRegister = p;   // seat the $CA00 latch so BD00R works
            return true;
        }
    }
    bootPage = 0;
    bankRegister = 0;
    return false;
}

void JukeBox::writeBankRegister(uint8_t value)
{
    bankRegister = value;
}

uint8_t JukeBox::getCurrentPage() const
{
    const uint8_t requested = bankRegister & 0x0F;
    if (pageCount == 0) return 0;
    return static_cast<uint8_t>(requested & (pageCount - 1));
}

uint8_t JukeBox::getCurrentSubPage() const
{
    return (bankRegister >> 4) & 0x01;
}

size_t JukeBox::fileOffsetForAddress(uint16_t address) const
{
    const size_t pageBase = static_cast<size_t>(getCurrentPage()) * kPageSize;
    if (jumper == Jumper::RAM16_ROM32) {
        // Full 32 kB page visible at $4000-$BFFF. Sub-page bit ignored on
        // real hardware because both halves are simultaneously exposed.
        return pageBase + static_cast<size_t>(address - kRom32Base);
    }
    // RAM32/ROM16: only 16 kB visible at $8000-$BFFF. Sub-page bit picks
    // upper or lower half of the 32 kB page.
    const size_t subBase = static_cast<size_t>(getCurrentSubPage()) * kSubPageSize;
    return pageBase + subBase + static_cast<size_t>(address - kRom16Base);
}

uint8_t JukeBox::readByte(uint16_t address) const
{
    const size_t off = fileOffsetForAddress(address);
    if (off >= rom.size()) return 0xFF;
    return rom[off];
}

void JukeBox::writeByte(uint16_t address, uint8_t value)
{
    if (chipMode == ChipMode::Flash) return;          // flash: read-only
    if (!writable) return;                            // RW jumper off
    const size_t off = fileOffsetForAddress(address);
    if (off >= rom.size()) return;
    if (rom[off] == value) return;                    // no-op, skip disk write
    rom[off] = value;
    flushRomToFile();
}

void JukeBox::flushRomToFile() const
{
    if (romPath.empty()) return;
    // EEPROM mode only (writeByte early-exits otherwise). Writes are rare
    // (user saves a BASIC program from the # prompt — once per save), so
    // flushing the full 32 kB per write is cheap compared to the ~25 s the
    // real 28c256 would take. Keeping the file closed between writes avoids
    // holding an fstream open across the application lifetime.
    std::ofstream out(romPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        pom1::log().warn("JukeBox", "Cannot open ROM for write: " + romPath);
        return;
    }
    out.write(reinterpret_cast<const char*>(rom.data()),
              static_cast<std::streamsize>(rom.size()));
}

void JukeBox::copySnapshot(Snapshot& out) const
{
    out.romPath         = romPath;
    out.romSize         = romSize;
    out.pageCount       = pageCount;
    out.bankRegister    = bankRegister;
    out.currentPage     = getCurrentPage();
    out.currentSubPage  = getCurrentSubPage();
    out.bootPage        = bootPage;
    out.jumper          = jumper;
    out.chipMode        = chipMode;
    out.writable        = writable;
    out.firmwarePresent = hasFirmware();
}
