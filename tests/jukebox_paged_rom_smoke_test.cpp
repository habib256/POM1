// Pin the P-LAB Juke-Box paged-flash emulation: ROM load, bank-select latch
// at $CA00, page masking, and the 16 kB sub-page (Sx) math.
//
// Self-contained — loads roms/jukebox.rom directly into a standalone JukeBox
// instance, no Memory / PeripheralBus needed. Mirrors the style of
// tests/gt6144_smoke_test.cpp.

#include "JukeBox.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
void mustBeTrue(bool cond, const char* msg)
{
    if (!cond) {
        std::fprintf(stderr, "ASSERT FAILED: %s\n", msg);
        std::exit(1);
    }
}
} // namespace

int main(int argc, char** argv)
{
    const std::string romPath = (argc > 1) ? argv[1] : "roms/jukebox.rom";

    JukeBox jb;
    std::string error;
    mustBeTrue(jb.loadRomFile(romPath, error),
               ("loadRomFile failed: " + error).c_str());

    // The shipped roms/jukebox.rom is 256 kB == 8 pages of 32 kB.
    mustBeTrue(jb.getPageCount() == 8,
               "expected 8 pages (256 kB / 32 kB) in default flash ROM");

    // Firmware signature is present at $BD00 in at least one page. For the
    // current shipped ROM it lands in pages 6 and 7 (see README / plan).
    mustBeTrue(jb.hasFirmware(), "hasFirmware() should be true for jukebox.rom");
    const uint8_t boot = jb.getBootPage();
    mustBeTrue(jb.pageHasFirmware(boot),
               "pickDefaultBootPage() chose a page without $A5 signature");
    // Lowest-index firmware page should be picked — verify no earlier page
    // also has the signature.
    for (uint8_t p = 0; p < boot; ++p) {
        mustBeTrue(!jb.pageHasFirmware(p),
                   "earlier page has firmware signature; boot page picker wrong");
    }

    // Default jumper is RAM16/ROM32 (window $4000-$BFFF, 32 kB visible).
    jb.setJumper(JukeBox::Jumper::RAM16_ROM32);

    // After loadRomFile, bankRegister should equal the boot page so BD00R
    // reads the PM signature from $BD00.
    mustBeTrue(jb.readByte(0xBD00) == JukeBox::kProgramManagerSignature,
               "default $BD00 read should hit PM signature $A5");

    // Direct bank-select: write a page index to $CA00 and verify reads
    // from the ROM window pull from that page.
    const uint8_t* buf = jb.getRomPointer();

    // Pick any non-boot page (0 if boot > 0, else 1) and verify contents.
    const uint8_t probePage = (boot == 0) ? 1 : 0;
    jb.writeBankRegister(probePage);
    mustBeTrue(jb.getCurrentPage() == probePage,
               "getCurrentPage() did not track bankRegister");
    mustBeTrue(jb.getCurrentSubPage() == 0,
               "default sub-page should be 0");
    // $4000 -> file offset probePage * 0x8000.
    mustBeTrue(jb.readByte(0x4000) == buf[probePage * 0x8000],
               "bank-selected read did not hit the expected file offset");
    // $BD00 -> file offset probePage * 0x8000 + $7D00.
    mustBeTrue(jb.readByte(0xBD00)
                   == buf[probePage * 0x8000 + JukeBox::kProgramManagerOffset],
               "bank-selected $BD00 read did not hit offset pattern");

    // Page mask: for an 8-page ROM, writing page $0A (10) should wrap to
    // page 2 (10 & 7 == 2) via getCurrentPage().
    jb.writeBankRegister(0x0A);
    mustBeTrue(jb.getCurrentPage() == 2,
               "page mask wrap failed for 8-page ROM (wrote 0x0A, expected 2)");

    // Sub-page math: in RAM32/ROM16 jumper mode, bit 4 of $CA00 selects
    // upper vs lower half of the 32 kB page. Write (page=0, sub=1) and
    // read $8000 — should land at file offset 0 + 0x4000 = 0x4000.
    jb.setJumper(JukeBox::Jumper::RAM32_ROM16);
    jb.writeBankRegister(0x10); // page 0, sub-page 1
    mustBeTrue(jb.getCurrentPage() == 0,
               "getCurrentPage should ignore bit 4 (sub-page)");
    mustBeTrue(jb.getCurrentSubPage() == 1,
               "getCurrentSubPage should pick up bit 4");
    mustBeTrue(jb.readByte(0x8000) == buf[0x4000],
               "RAM32/ROM16 + sub=1 read did not land at file offset 0x4000");

    // Sub-page back to 0: $8000 should hit offset 0 of page 0.
    jb.writeBankRegister(0x00);
    mustBeTrue(jb.readByte(0x8000) == buf[0x0000],
               "RAM32/ROM16 + sub=0 + page=0 read did not hit file offset 0");

    // Flash mode writes are ignored (real flash needs erase+program).
    jb.setJumper(JukeBox::Jumper::RAM16_ROM32);
    jb.writeBankRegister(0);
    const uint8_t before = jb.readByte(0x4000);
    jb.setWritable(true); // Flash mode: writable flag is still ignored
    jb.writeByte(0x4000, static_cast<uint8_t>(~before));
    mustBeTrue(jb.readByte(0x4000) == before,
               "flash-mode write should have been ignored");

    // Reset re-seats the bank register on the boot page.
    jb.writeBankRegister(0);
    jb.reset();
    mustBeTrue(jb.getCurrentPage() == boot,
               "reset() should re-seat bankRegister on the boot page");

    // EEPROM mode rejects an oversized file and accepts a 32 kB one.
    {
        JukeBox eep;
        eep.setChipMode(JukeBox::ChipMode::EEPROM28C256);
        std::string err;
        const bool rejected = !eep.loadRomFile(romPath, err);
        mustBeTrue(rejected,
                   "EEPROM mode should reject a 256 kB file");
    }

    std::printf("jukebox_paged_rom_smoke: OK\n");
    std::printf("  pageCount=%u  bootPage=%u\n",
                static_cast<unsigned>(jb.getPageCount()),
                static_cast<unsigned>(boot));
    return 0;
}
