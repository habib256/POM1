// hex_dump_multi_zone_test.cpp -- pin Memory::loadHexDump multi-zone behaviour.
//
// games_chess Chess.txt is a Wozmon-hex dump that targets two disjoint memory
// zones: the lo block at $0280-$0DFF (renderer + game loop, ~3 KB) and the hi
// block at $E000-$EFFF (chess engine, ~3 KB). The lines look like:
//
//     0280: A9 5E A2 05 ...
//     ...
//     0DF0: 2E 0D 00 0D 20 3F 0D 00
//     E000: A2 00 8A 9D 00 EF E8 E0
//     ...
//     EAC0: EF 18 60
//     0280R
//
// The zone transition relies on the loader's "merged data + address" branch:
// the last byte of one line gets concatenated with the next line's address
// prefix (e.g. "00E000:") because getline strips the newline and lines are
// joined without a separator. If that merge regressed, the lo block would
// still load (the chess splash would render) but the engine at $E000 would be
// silently dropped, and JSR init_board would jump into stale BASIC ROM.
//
// This test asserts the lo + hi blocks both land in mem[] after loadHexDump
// on a synthetic 2-zone fixture (we don't depend on the games_chess artefact
// being present at test time).

// Memory.h forward-declares the cards it owns via unique_ptr to avoid
// a header avalanche; instantiating Memory here needs the full types so
// the unique_ptr destructors are emitted (same pattern as snapshot_smoke).
#include "TMS9918.h"      // IWYU pragma: keep
#include "WiFiModem.h"    // IWYU pragma: keep
#include "TerminalCard.h" // IWYU pragma: keep
#include "A1IO_RTC.h"     // IWYU pragma: keep
#include "PR40Printer.h"  // IWYU pragma: keep
#include "Memory.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {
// Use std::filesystem::temp_directory_path so the test runs on Windows CI
// (where /tmp doesn't exist) as well as Linux/macOS.
std::string fixturePath()
{
    auto p = std::filesystem::temp_directory_path()
           / "pom1_hex_dump_multi_zone.txt";
    return p.string();
}

void writeFixture(const std::string& path)
{
    std::ofstream f(path);
    // lo block: 16 bytes at $0280
    f << "0280: A9 5E A2 05 20 90 0A 20\n"
         "0288: 77 0A 20 14 04 20 00 E0\n"
    // hi block: 16 bytes at $E000 (mimic the chess engine's first instructions
    // so the test is recognisably the same shape as the real artefact).
         "E000: A2 00 8A 9D 00 EF E8 E0\n"
         "E008: 80 D0 F8 A2 00 8A 29 F8\n"
         "0280R\n";
}
} // namespace

int main()
{
    const std::string path = fixturePath();
    writeFixture(path);

    Memory mem;
    uint16_t startAddr = 0;
    int bytes = 0;
    std::vector<std::pair<uint16_t, uint16_t>> zones;
    int rc = mem.loadHexDump(path.c_str(), startAddr, &bytes, &zones);
    assert(rc == 0 && "loadHexDump returned error on a well-formed 2-zone dump");
    assert(bytes == 32 && "expected 16 lo + 16 hi = 32 bytes loaded");
    assert(startAddr == 0x0280 && "run address should be $0280 (the trailing 0280R)");

    // Zone tracking: the file dialog uses this output to register one
    // loadedPrograms entry per disjoint zone. Without it the Memory Map
    // would draw a bogus contiguous band from $0280 to $0280+totalBytes-1
    // that runs through ROM space. Pin the exact endpoints so a future
    // refactor of the parser's address-flip logic can't regress this.
    assert(zones.size() == 2 && "expected one zone per disjoint address block");
    assert(zones[0].first == 0x0280 && zones[0].second == 0x028F);
    assert(zones[1].first == 0xE000 && zones[1].second == 0xE00F);

    const uint8_t* m = mem.getMemoryPointer();

    // Lo block intact at $0280..$028F.
    const uint8_t expectedLo[16] = {
        0xA9, 0x5E, 0xA2, 0x05, 0x20, 0x90, 0x0A, 0x20,
        0x77, 0x0A, 0x20, 0x14, 0x04, 0x20, 0x00, 0xE0
    };
    for (int i = 0; i < 16; ++i) {
        if (m[0x0280 + i] != expectedLo[i]) {
            std::fprintf(stderr, "Lo block mismatch at $%04X: got 0x%02X expected 0x%02X\n",
                         0x0280 + i, m[0x0280 + i], expectedLo[i]);
            return 1;
        }
    }

    // Hi block intact at $E000..$E00F. This is the load that the user reported
    // as missing — the test pins it explicitly so any regression in the merge
    // logic that handles the lo→hi transition fails loudly.
    const uint8_t expectedHi[16] = {
        0xA2, 0x00, 0x8A, 0x9D, 0x00, 0xEF, 0xE8, 0xE0,
        0x80, 0xD0, 0xF8, 0xA2, 0x00, 0x8A, 0x29, 0xF8
    };
    for (int i = 0; i < 16; ++i) {
        if (m[0xE000 + i] != expectedHi[i]) {
            std::fprintf(stderr, "Hi block mismatch at $%04X: got 0x%02X expected 0x%02X\n",
                         0xE000 + i, m[0xE000 + i], expectedHi[i]);
            std::fprintf(stderr, "  → high zone failed to load; loadHexDump merge regressed\n");
            return 1;
        }
    }

    std::printf("hex_dump_multi_zone: OK (lo=$0280, hi=$E000, run=$0280, %d bytes)\n", bytes);
    std::error_code ec;
    std::filesystem::remove(path, ec);

    // Zero-page address prefix (regression: "40:FF" must parse as addr $40, not
    // a lone data byte 0x40 — the old >=3-digit guard rejected 1–2 digit addrs).
    {
        const std::string zpPath = (std::filesystem::temp_directory_path()
                                  / "pom1_hex_dump_zp_addr.txt").string();
        {
            std::ofstream f(zpPath);
            f << "40: FF A9 00 40R\n";
        }
        Memory memZp;
        uint16_t run = 0;
        int loaded = 0;
        assert(memZp.loadHexDump(zpPath.c_str(), run, &loaded, nullptr) == 0);
        assert(run == 0x0040);
        assert(loaded == 3);
        assert(memZp.memRead(0x0040) == 0xFF);
        assert(memZp.memRead(0x0041) == 0xA9);
        assert(memZp.memRead(0x0042) == 0x00);
        std::error_code ecZp;
        std::filesystem::remove(zpPath, ecZp);
        std::printf("hex_dump_zp_addr: OK ($0040: FF A9 00)\n");
    }

    return 0;
}
