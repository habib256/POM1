// hex_dump_extension_test.cpp -- pin which extensions route to the Wozmon-hex
// parser (pom1::isHexDumpPath, src/HexDumpFile.h).
//
// Uncle Bernie's canonical Apple-1 convention names a WOZMON-keystroke file
// ".APL". POM1 keyed the hex path on ".txt"/".hex" alone, so the SAME file
// renamed to ".apl" fell through to Memory::loadBinary and its ASCII text was
// written verbatim into RAM -- no error, no warning:
//
//     --load 0280:2048.apl.txt  ->  (hex) run $0280, 1962 bytes, 4 zones   OK
//     --load 0280:2048.apl      ->  $0280, 6143 bytes                      GARBAGE
//
// Every extension check in POM1 (CliDispatcher --load, the native Load/Save
// Memory filters, the ImGui fallback browser's directory listing, the DevBench
// quick-load target) now goes through the one predicate this test pins. The
// end-to-end half asserts that a dump renamed to each accepted extension loads
// byte-for-byte identically to the .txt original -- i.e. the predicate and the
// parser agree.
//
// ".tur" (TurboType) is in the list too: its turbo payload turned out to be
// plain unspaced hex rather than a compressed block, so the same parser reads
// it — but only via the line-structured branch, pinned by
// hex_dump_turbotype_smoke. The end-to-end fixture there loads a .TUR through
// EVERY extension covered here, so this test does not repeat it.

// Memory.h forward-declares the cards it owns via unique_ptr to avoid a header
// avalanche; instantiating Memory here needs the full types so the unique_ptr
// destructors are emitted (same pattern as hex_dump_multi_zone).
#include "TMS9918.h"      // IWYU pragma: keep
#include "WiFiModem.h"    // IWYU pragma: keep
#include "TerminalCard.h" // IWYU pragma: keep
#include "A1IO_RTC.h"     // IWYU pragma: keep
#include "PR40Printer.h"  // IWYU pragma: keep
#include "HexDumpFile.h"
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
    return std::filesystem::temp_directory_path() / "pom1_hex_dump_extension";
}

// A small but structurally real dump: two disjoint zones + a trailing run
// address, in the single-line "group separator colon" style the published .APL
// corpus uses (2048.apl.txt, twinkle.apl.txt). Written to `path` verbatim so
// every renamed copy is byte-identical and any difference in the loaded image
// can only come from the routing decision.
void writeFixture(const std::filesystem::path& path)
{
    std::ofstream f(path);
    f << "0300:A9 01 85 10 A9 00 85 11:85 15 85 12 85 13 85 14\n"
         "E000:A2 00 8A 9D 00 EF E8 E0:80 D0 F8 A2 00 8A 29 F8\n"
         "0300R\n";
}

struct Image {
    int bytes = 0;
    uint16_t runAddr = 0;
    std::vector<std::pair<uint16_t, uint16_t>> zones;
    std::vector<uint8_t> lo;   // the 16 bytes at $0300
    std::vector<uint8_t> hi;   // the 16 bytes at $E000
};

// Load `path` through the hex parser and snapshot everything a caller could
// observe. rc != 0 is fatal: it means the parser found no address AND no run
// address, i.e. it did not recognise the file as a dump at all.
Image loadThroughHexParser(const std::filesystem::path& path)
{
    Memory mem;
    Image img;
    uint16_t addr = 0;
    int rc = mem.loadHexDump(path.string().c_str(), addr, &img.bytes, &img.zones);
    assert(rc == 0 && "loadHexDump rejected a well-formed dump");
    img.runAddr = addr;
    for (int i = 0; i < 16; ++i) img.lo.push_back(mem.getMemoryPointer()[0x0300 + i]);
    for (int i = 0; i < 16; ++i) img.hi.push_back(mem.getMemoryPointer()[0xE000 + i]);
    return img;
}

bool sameImage(const Image& a, const Image& b)
{
    return a.bytes == b.bytes && a.runAddr == b.runAddr &&
           a.zones == b.zones && a.lo == b.lo && a.hi == b.hi;
}

} // namespace

int main()
{
    // ---- 1. The predicate itself -------------------------------------------
    // Every advertised extension is accepted, in any case. kHexDumpExtensions
    // is the single list; iterating it means adding one there without teaching
    // the call sites cannot silently pass this test.
    for (int i = 0; i < pom1::kHexDumpExtensionCount; ++i) {
        const std::string ext = pom1::kHexDumpExtensions[i];
        assert(pom1::isHexDumpPath("prog." + ext) &&
               "advertised hex extension not accepted by isHexDumpPath");
        assert(pom1::isHexDumpExtension("." + ext) &&
               "isHexDumpExtension must tolerate a leading dot");
    }
    // The regression that motivated the header: .apl and .mon, both cases.
    assert(pom1::isHexDumpPath("Intruder.apl"));
    assert(pom1::isHexDumpPath("Intruder.APL"));
    assert(pom1::isHexDumpPath("Intruder.Mon"));
    // The bundled corpus keeps the double extension — still just ".txt".
    assert(pom1::isHexDumpPath("2048.apl.txt"));

    // .tur (TurboType) reads through the same parser — its turbo payload is
    // plain unspaced hex, not compressed. The line-structured branch that makes
    // that work is pinned separately by hex_dump_turbotype_smoke.
    assert(pom1::isHexDumpPath("prog.tur"));
    assert(pom1::isHexDumpPath("prog.TUR"));

    // Not hex dumps.
    assert(!pom1::isHexDumpPath("prog.bin"));
    assert(!pom1::isHexDumpPath("prog.snap"));
    assert(!pom1::isHexDumpPath("prog"));            // no extension at all
    // A dot in a DIRECTORY name must not be mistaken for the file's extension,
    // or "software/v1.txt/prog" would route as hex.
    assert(!pom1::isHexDumpPath("software/v1.apl/prog"));
    assert(pom1::lowerExtension("software/v1.apl/prog").empty());

    // ---- 2. Predicate and parser agree end-to-end --------------------------
    std::error_code ec;
    std::filesystem::create_directories(scratchDir(), ec);
    // Distinct stem from the copies below: one of them IS "fixture.txt", and
    // copy_file onto its own source is an error, not a no-op.
    const std::filesystem::path ref = scratchDir() / "reference.txt";
    writeFixture(ref);
    const Image expected = loadThroughHexParser(ref);
    assert(expected.bytes == 32 && "fixture should load 16 lo + 16 hi bytes");
    assert(expected.runAddr == 0x0300 && "trailing 0300R sets the run address");
    assert(expected.zones.size() == 2 && "lo + hi are disjoint zones");
    assert(expected.lo[0] == 0xA9 && expected.hi[0] == 0xA2);

    for (int i = 0; i < pom1::kHexDumpExtensionCount; ++i) {
        const std::filesystem::path copy =
            scratchDir() / (std::string("fixture.") + pom1::kHexDumpExtensions[i]);
        std::filesystem::copy_file(ref, copy,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        assert(!ec && "could not stage the renamed fixture");
        if (!sameImage(loadThroughHexParser(copy), expected)) {
            std::fprintf(stderr,
                         "  → .%s loaded differently from .txt; the parser and the "
                         "extension list disagree\n", pom1::kHexDumpExtensions[i]);
            assert(false);
        }
    }

    std::filesystem::remove_all(scratchDir(), ec);
    std::printf("hex_dump_extension_smoke: OK (%d extensions route to the hex parser)\n",
                pom1::kHexDumpExtensionCount);
    return 0;
}
