// intel_hex_test.cpp -- pin Intel HEX detection + loading (src/IntelHexFile.h,
// the branch in Memory::loadHexDump).
//
// ".hex" is published in two mutually unintelligible dialects. POM1 only ever
// read the WOZMON one ("AAAA: HH HH ... / AAAAR"); an Intel HEX file
//
//     :10030000A9018510A900851185158512851385140D
//
// has no "AAAA:" address, so the WOZMON parser wrote EVERY digit as data --
// byte count, load address, record type and checksum included -- at whatever
// address happened to be current. No error, no warning: the same class of
// silent corruption as the ".apl" routing bug (see hex_dump_extension_test).
//
// What this pins:
//   1. the standalone parser: records, type 02/04 base, type 03/05 start, EOF;
//   2. detection is STRUCTURAL, not extension-based -- the same bytes load
//      identically as ".hex", ".txt" and ".apl";
//   3. a WOZMON dump named ".hex" is completely unaffected;
//   4. lines that merely start with ':' (WOZMON continuations, TurboType data)
//      are NOT mistaken for Intel HEX;
//   5. a broken Intel HEX is REFUSED, not silently retried through the WOZMON
//      parser -- which is what makes the branch safe to commit to.

#include "TMS9918.h"      // IWYU pragma: keep
#include "WiFiModem.h"    // IWYU pragma: keep
#include "TerminalCard.h" // IWYU pragma: keep
#include "A1IO_RTC.h"     // IWYU pragma: keep
#include "PR40Printer.h"  // IWYU pragma: keep
#include "IntelHexFile.h"
#include "Memory.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {

std::filesystem::path scratchDir()
{
    return std::filesystem::temp_directory_path() / "pom1_intel_hex";
}

// Two disjoint 16-byte blocks at $0300 and $E000 plus a start-address record --
// deliberately the same image as hex_dump_extension_test's WOZMON fixture, so
// the two dialects can be compared byte for byte.
const char* kIntelHex =
    ":020000040000FA\n"                                    // extended linear base = 0
    ":10030000A9018510A900851185158512851385140D\n"        // 16 B at $0300
    ":10E00000A2008A9D00EFE8E080D0F8A2008A29F8FB\n"        // 16 B at $E000
    ":0400000500000300F4\n"                                // start linear address $0300
    ":00000001FF\n";                                       // EOF

// The same image in WOZMON syntax, as published .APL files write it.
const char* kWozmonHex =
    "0300:A9 01 85 10 A9 00 85 11:85 15 85 12 85 13 85 14\n"
    "E000:A2 00 8A 9D 00 EF E8 E0:80 D0 F8 A2 00 8A 29 F8\n"
    "0300R\n";

struct Image {
    int rc = 0;
    int bytes = 0;
    uint16_t runAddr = 0;
    std::vector<std::pair<uint16_t, uint16_t>> zones;
    std::vector<uint8_t> lo, hi;
};

void write(const std::filesystem::path& p, const char* text)
{
    std::ofstream f(p);
    f << text;
}

Image load(const std::filesystem::path& path)
{
    Memory mem;
    Image img;
    uint16_t addr = 0;
    img.rc = mem.loadHexDump(path.string().c_str(), addr, &img.bytes, &img.zones);
    img.runAddr = addr;
    for (int i = 0; i < 16; ++i) img.lo.push_back(mem.getMemoryPointer()[0x0300 + i]);
    for (int i = 0; i < 16; ++i) img.hi.push_back(mem.getMemoryPointer()[0xE000 + i]);
    return img;
}

Image loadText(const char* text, const char* ext)
{
    const std::filesystem::path p = scratchDir() / (std::string("fixture.") + ext);
    write(p, text);
    return load(p);
}

bool sameImage(const Image& a, const Image& b)
{
    return a.rc == b.rc && a.bytes == b.bytes && a.runAddr == b.runAddr &&
           a.zones == b.zones && a.lo == b.lo && a.hi == b.hi;
}

} // namespace

int main()
{
    std::error_code ec;
    std::filesystem::create_directories(scratchDir(), ec);

    // ---- 1. The standalone parser ------------------------------------------
    pom1::IntelHexImage ihx;
    std::string err;
    assert(pom1::parseIntelHex(kIntelHex, ihx, &err) && "well-formed Intel HEX rejected");
    assert(ihx.records.size() == 2 && "two type-00 data records");
    assert(ihx.records[0].addr == 0x0300 && ihx.records[0].data.size() == 16);
    assert(ihx.records[1].addr == 0xE000 && ihx.records[1].data[0] == 0xA2);
    assert(ihx.hasStart && ihx.start == 0x0300 && "type-05 start record");
    assert(ihx.sawEof && "type-01 EOF record");

    // The x86 inheritance: a type-04 base shifts records by 64 KB, a type-02
    // base by 16 bytes. Both must actually MOVE the record, or a file carrying
    // one would load its data over page zero.
    assert(pom1::parseIntelHex(":020000040001F9\n"
                               ":04028000DEADBEEF42\n"
                               ":00000001FF\n", ihx, &err));
    assert(ihx.records[0].addr == 0x10280 && "type-04 base must offset by 64 KB");
    assert(pom1::parseIntelHex(":020000020030CC\n"      // segment $0030 -> base $0300
                               ":02000000AABB99\n"
                               ":00000001FF\n", ihx, &err));
    assert(ihx.records[0].addr == 0x0300 && "type-02 base must offset by (segment << 4)");

    // ---- 2. Detection is structural, never extension-based ------------------
    const Image intelHex = loadText(kIntelHex, "hex");
    assert(intelHex.rc == 0 && "loadHexDump rejected a well-formed Intel HEX");
    assert(intelHex.bytes == 32 && "16 lo + 16 hi bytes");
    assert(intelHex.zones.size() == 2 && "lo and hi are disjoint zones");
    assert(intelHex.runAddr == 0x0300 && "the type-05 record is the run address");
    assert(intelHex.lo[0] == 0xA9 && intelHex.lo[15] == 0x14);
    assert(intelHex.hi[0] == 0xA2 && intelHex.hi[15] == 0xF8);

    // Published under any of POM1's hex extensions, it is still Intel HEX.
    if (!sameImage(loadText(kIntelHex, "txt"), intelHex) ||
        !sameImage(loadText(kIntelHex, "apl"), intelHex) ||
        !sameImage(loadText(kIntelHex, "mon"), intelHex)) {
        std::fprintf(stderr, "  → Intel HEX detection depends on the extension; it must "
                             "depend on the file's shape only\n");
        assert(false);
    }

    // ---- 3. The WOZMON dialect is untouched --------------------------------
    // THE regression guard: the same image in WOZMON syntax, named ".hex",
    // must still take the legacy parser and produce the identical bytes.
    const Image wozmon = loadText(kWozmonHex, "hex");
    assert(wozmon.rc == 0 && wozmon.bytes == 32 && wozmon.runAddr == 0x0300);
    assert(wozmon.lo == intelHex.lo && wozmon.hi == intelHex.hi &&
           "the two dialects must load the same image");
    assert(wozmon.zones == intelHex.zones);

    // ---- 4. A leading ':' alone is not Intel HEX ----------------------------
    // A WOZMON continuation line and a TurboType data line both open with ':'.
    // Only the byte count / record type / checksum agreeing together commits.
    assert(!pom1::looksLikeIntelHex(":85 15 85 12 85 13 85 14\n"));
    assert(!pom1::looksLikeIntelHex(":D8A2FF9AA92A851A204604A97C8518A9\n"));
    assert(!pom1::looksLikeIntelHex("0300:A9 01 85 10\n"));
    assert(!pom1::looksLikeIntelHex(""));
    assert(pom1::looksLikeIntelHex("\n\n  :00000001FF\r\n") && "blank lines and CRLF");

    // ---- 5. A broken Intel HEX is refused, not reinterpreted ----------------
    // Once the first record validates, POM1 has committed to the dialect. A
    // later bad checksum must FAIL the load: falling back to the WOZMON parser
    // would resume writing count/address/type digits into RAM, which is the
    // whole failure this branch exists to prevent.
    const Image broken = loadText(":020000040000FA\n"
                                  ":10030000A9018510A900851185158512851385140E\n"  // checksum +1
                                  ":00000001FF\n", "hex");
    assert(broken.rc != 0 && "a bad checksum must fail the load");
    assert(broken.bytes == 0 && "nothing may be written from a rejected file");

    // A record beyond $FFFF targets something that is not a 6502.
    const Image tooHigh = loadText(":020000040001F9\n"
                                   ":04028000DEADBEEF42\n"
                                   ":00000001FF\n", "hex");
    assert(tooHigh.rc != 0 && "a record past $FFFF must be refused, not wrapped");

    // Missing EOF: loaded anyway (with a warning), because the data records are
    // self-describing -- refusing would reject files real assemblers emit.
    const Image noEof = loadText(":10030000A9018510A900851185158512851385140D\n", "hex");
    assert(noEof.rc == 0 && noEof.bytes == 16 && noEof.runAddr == 0x0300);

    std::filesystem::remove_all(scratchDir(), ec);
    std::printf("intel_hex_smoke: OK (Intel HEX detected by shape, WOZMON untouched, "
                "malformed refused)\n");
    return 0;
}
