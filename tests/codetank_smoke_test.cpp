// Pin the P-LAB CodeTank daughterboard (rides the TMS9918 Graphic Card on
// real silicon): 32 kB ROM load, lower/upper jumper math, and the read-only
// EEPROM invariant POM1 enforces. Memory-level cascade with TMS9918 lives
// in codetank_tms9918_dependency_test.cpp.
//
// Self-contained — only CodeTank + Logger. Mirrors gt6144_smoke_test in style.

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
    const std::string romPath = (argc > 1) ? argv[1] : "roms/codetank.rom";

    CodeTank ct;
    std::string err;
    const bool ok = ct.loadRomFile(romPath, err);
    // Build the assert message in a named local so its storage outlives the
    // mustBeTrue call (passing a temporary's c_str() printed an empty string
    // because the std::string was already destroyed by the time fprintf ran).
    const std::string loadFailMsg = "loadRomFile failed: " + err;
    mustBeTrue(ok, loadFailMsg.c_str());
    mustBeTrue(ct.getRomSize() == CodeTank::kRomSize,
               "CodeTank image should be exactly 32 kB");
    mustBeTrue(ct.hasRom(),
               "hasRom() should be true after loadRomFile");
    const uint8_t* buf = ct.getRomPointer();

    // Lower jumper: file offset 0..$3FFF visible at $4000..$7FFF.
    ct.setJumper(CodeTank::Jumper::Lower16);
    mustBeTrue(ct.readByte(0x4000) == buf[0x0000],
               "lower jumper $4000 did not map to file offset 0");
    mustBeTrue(ct.readByte(0x5000) == buf[0x1000],
               "lower jumper $5000 did not map to file offset $1000");
    mustBeTrue(ct.readByte(0x7FFF) == buf[0x3FFF],
               "lower jumper $7FFF did not map to file offset $3FFF");

    // Upper jumper: file offset $4000..$7FFF visible at $4000..$7FFF.
    ct.setJumper(CodeTank::Jumper::Upper16);
    mustBeTrue(ct.readByte(0x4000) == buf[0x4000],
               "upper jumper $4000 did not map to file offset $4000");
    mustBeTrue(ct.readByte(0x7FFF) == buf[0x7FFF],
               "upper jumper $7FFF did not map to file offset $7FFF");

    // POM1 models the 28c256 as read-only — writeByte must be a no-op.
    const uint8_t before = ct.readByte(0x4000);
    ct.writeByte(0x4000, static_cast<uint8_t>(~before));
    mustBeTrue(ct.readByte(0x4000) == before,
               "CodeTank writeByte must be a no-op (read-only EEPROM in POM1)");

    // Oversized file is rejected with previous contents preserved.
    {
        // Copy the legacy ROM path for the rejection probe (Juke-Box's 256 kB).
        const std::string oversize = (argc > 2) ? argv[2] : "roms/jukebox.rom";
        CodeTank ct2;
        std::string err2;
        mustBeTrue(ct2.loadRomFile(romPath, err2),
                   "baseline load failed");
        const uint8_t baseline = ct2.readByte(0x4000);
        const bool rejected = !ct2.loadRomFile(oversize, err2);
        mustBeTrue(rejected, "loadRomFile must reject sizes != 32 kB");
        mustBeTrue(ct2.readByte(0x4000) == baseline,
                   "rejected load must preserve previous contents");
    }

    // Snapshot round-trip: jumper + path + loaded flag.
    {
        CodeTank::Snapshot snap;
        ct.setJumper(CodeTank::Jumper::Upper16);
        ct.copySnapshot(snap);
        mustBeTrue(snap.jumper == CodeTank::Jumper::Upper16,
                   "snapshot.jumper round-trip failed");
        mustBeTrue(snap.romPath == romPath,
                   "snapshot.romPath round-trip failed");
        mustBeTrue(snap.loaded,
                   "snapshot.loaded must reflect hasRom()");
        mustBeTrue(snap.romSize == CodeTank::kRomSize,
                   "snapshot.romSize must equal 32 kB");
    }

    std::printf("codetank_smoke: OK\n");
    return 0;
}
