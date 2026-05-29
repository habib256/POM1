// POM1 Apple 1 Emulator - P-LAB Apple-1 Juke-Box implementation.
// Hardware design: Claudio Parmigiani and Jacopo Rosselli (P-LAB, 2020-26).
// See JukeBox.h for the card description and scope.

#include "JukeBox.h"

#include "Logger.h"
#include "SnapshotIO.h"

#include <cstring>
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

    // 28c256 byte-write cycle: ~10 ms during which the chip is internally
    // programming the storage cell. A second byte arriving in that window
    // is silently rejected on real silicon. Strict mode honours this; in
    // permissive mode every write lands instantly (legacy POM1 behaviour
    // so the UI Page-Copy / Save-ROM tools are not throttled).
    if (siliconStrictMode && writeBusyCycles > 0) {
        ++eepromWritesDropped;
        return;
    }
    rom[off] = value;
    ++eepromWritesTotal;
    if (siliconStrictMode) writeBusyCycles = writeCycleCpu;
    flushRomToFile();
}

void JukeBox::advanceCycles(int cycles)
{
    if (writeBusyCycles > 0 && cycles > 0) {
        writeBusyCycles = std::max(0, writeBusyCycles - cycles);
    }
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

bool JukeBox::copyPage(uint8_t fromPage, uint8_t toPage, std::string& error)
{
    if (chipMode != ChipMode::Flash) {
        error = "Page copy is a flash-mode operation (EEPROM has only one page).";
        return false;
    }
    if (pageCount <= 1) {
        error = "ROM has only one page — nothing to copy.";
        return false;
    }
    if (fromPage >= pageCount || toPage >= pageCount) {
        error = "Page index out of range.";
        return false;
    }
    if (fromPage == toPage) {
        error = "Source and destination pages are identical.";
        return false;
    }
    const size_t fromOff = static_cast<size_t>(fromPage) * kPageSize;
    const size_t toOff   = static_cast<size_t>(toPage)   * kPageSize;
    if (fromOff + kPageSize > rom.size() || toOff + kPageSize > rom.size()) {
        error = "ROM buffer too small for page copy.";
        return false;
    }
    std::memmove(rom.data() + toOff, rom.data() + fromOff, kPageSize);
    return true;
}

bool JukeBox::saveRomFile(const std::string& path, std::string& error) const
{
    const std::string& target = path.empty() ? romPath : path;
    if (target.empty()) {
        error = "No ROM file path set.";
        return false;
    }
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        error = "Cannot open " + target + " for writing.";
        return false;
    }
    out.write(reinterpret_cast<const char*>(rom.data()),
              static_cast<std::streamsize>(rom.size()));
    if (!out.good()) {
        error = "Write to " + target + " failed.";
        return false;
    }
    return true;
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

void JukeBox::serialize(pom1::SnapshotWriter& w) const
{
    w.writeU8 (static_cast<uint8_t>(jumper));
    w.writeU8 (static_cast<uint8_t>(chipMode));
    w.writeU8 (writable ? 1 : 0);
    w.writeU8 (bankRegister);
    w.writeU8 (pageCount);
    w.writeU8 (bootPage);
    w.writeU32(static_cast<uint32_t>(romSize));
    w.writeByteVector(rom);
    w.writeString(romPath);
}

void JukeBox::deserialize(pom1::SnapshotReader& r)
{
    jumper        = static_cast<Jumper>  (r.readU8());
    chipMode      = static_cast<ChipMode>(r.readU8());
    writable      = r.readU8() != 0;
    bankRegister  = r.readU8();
    pageCount     = r.readU8();
    bootPage      = r.readU8();
    romSize       = static_cast<size_t>(r.readU32());
    rom           = r.readByteVector();
    // romPath read for round-trip completeness, but the actual on-disk path
    // is owned by Memory's preset / UI loader — we keep the saved value
    // visible via copySnapshot but don't reopen the file.
    romPath       = r.readString();
}
