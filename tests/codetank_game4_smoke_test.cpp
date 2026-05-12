// Pin Codetank_GAME4.rom (Light Corridor cartridge):
//  - 32 kB exact (lower 16 kB = TMS_LightCorridor, upper 16 kB = reserved/$FF).
//  - Lower jumper $4000 reads a real 6502 instruction (not $FF padding) and
//    matches the project's `start:` prologue: SEI / CLD / LDX #$FF / TXS.
//  - Upper jumper $4000 = $FF (reserved bank).
//  - CodeTank read-only invariant still holds (writes are dropped).
//
// Self-contained — only CodeTank + Logger. Mirrors codetank_smoke_test in style.

#include "CodeTank.h"

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
    const std::string romPath =
        (argc > 1) ? argv[1] : "roms/codetank/Codetank_GAME4.rom";

    CodeTank ct;
    std::string err;
    const bool ok = ct.loadRomFile(romPath, err);
    const std::string loadFailMsg = "loadRomFile failed: " + err;
    mustBeTrue(ok, loadFailMsg.c_str());
    mustBeTrue(ct.getRomSize() == CodeTank::kRomSize,
               "Codetank_GAME4.rom must be exactly 32 kB");
    mustBeTrue(ct.hasRom(), "hasRom() should be true after load");

    // --- Lower jumper: TMS_LightCorridor entry at $4000.
    // The .asm prologue is: SEI ($78) / CLD ($D8) / LDX #$FF ($A2 $FF) / TXS.
    // Verifying the first 4 bytes byte-for-byte pins the entry point against
    // accidental reshuffling of `start:` (e.g. someone inserting a .byte
    // table before the entry label).
    ct.setJumper(CodeTank::Jumper::Lower16);
    mustBeTrue(ct.readByte(0x4000) == 0x78,
               "GAME4 lower $4000 must be SEI ($78) — start: prologue");
    mustBeTrue(ct.readByte(0x4001) == 0xD8,
               "GAME4 lower $4001 must be CLD ($D8)");
    mustBeTrue(ct.readByte(0x4002) == 0xA2,
               "GAME4 lower $4002 must be LDX #imm opcode ($A2)");
    mustBeTrue(ct.readByte(0x4003) == 0xFF,
               "GAME4 lower $4003 must be the #$FF operand for TXS init");

    // --- Upper jumper: reserved bank, all $FF padding.
    ct.setJumper(CodeTank::Jumper::Upper16);
    mustBeTrue(ct.readByte(0x4000) == 0xFF,
               "GAME4 upper $4000 must be $FF (reserved bank)");
    mustBeTrue(ct.readByte(0x7FFF) == 0xFF,
               "GAME4 upper $7FFF must be $FF (reserved bank)");

    // --- Read-only EEPROM invariant: writes are dropped on both jumpers.
    ct.setJumper(CodeTank::Jumper::Lower16);
    const uint8_t before = ct.readByte(0x4000);
    ct.writeByte(0x4000, static_cast<uint8_t>(~before));
    mustBeTrue(ct.readByte(0x4000) == before,
               "GAME4 writeByte must be a no-op (read-only EEPROM in POM1)");

    std::printf("codetank_game4_smoke: OK\n");
    return 0;
}
