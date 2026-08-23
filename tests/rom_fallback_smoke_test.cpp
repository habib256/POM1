// rom_fallback_smoke_test.cpp -- POM1 must boot a working machine even when it
// cannot find its own roms/ directory, and must SAY so.
//
// WHY THIS EXISTS
//   Launched from anywhere but its own folder — the ordinary consequence of
//   copying POM1.exe somewhere convenient, or of a shortcut with the wrong
//   working directory — POM1 used to log
//       [Mem] ERROR: Cannot find ROM file: WozMonitor.rom
//   and carry on booting a machine with $FF00 empty: no reset vector, no
//   prompt, nothing. A black window whose only explanation sat in a log file
//   the user had no reason to open. Measured on 2026-08-22 by running the
//   binary from an empty directory: three ROM errors, exit code 0.
//
//   The ACI already had a compiled-in fallback; the Monitor — the one ROM
//   without which nothing at all happens — did not. It does now, and this test
//   pins both halves:
//
//   1. the compiled-in copy is BYTE-IDENTICAL to roms/WozMonitor.rom, so the
//      two cannot drift (the fallback silently serving a different Monitor
//      than the shipped file would be worse than no fallback at all);
//   2. with no roms/ reachable, loadWozMonitor() still leaves a usable Monitor
//      at $FF00 and REPORTS the substitution through romFallbacksUsed(), which
//      is what the UI banner reads.
//
// Memory.h forward-declares card types via unique_ptr; full definitions are
// needed here for the destructors to be emitted.
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
#include <vector>

namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> readShippedMonitor(const fs::path& repoRoot)
{
    const fs::path p = repoRoot / "roms" / "WozMonitor.rom";
    std::ifstream f(p, std::ios::binary);
    assert(f.is_open() && "roms/WozMonitor.rom must exist in the repo");
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
    assert(bytes.size() == 0x100 && "the Woz Monitor is exactly 256 bytes");
    return bytes;
}

bool listed(const std::vector<std::string>& v, const std::string& name)
{
    for (const auto& s : v) if (s == name) return true;
    return false;
}

} // namespace

int main()
{
    // ctest runs us from the repo root (WORKING_DIRECTORY), which is where the
    // real roms/ lives — capture it before wandering off.
    const fs::path repoRoot = fs::current_path();
    const std::vector<uint8_t> shipped = readShippedMonitor(repoRoot);

    // -- 1. The normal case: the file is found, nothing is substituted --------
    {
        Memory mem;
        const int rc = mem.loadWozMonitor();
        assert(rc == 0);
        assert(mem.romFallbacksUsed().empty() &&
               "a launch that finds its ROMs must report no substitution");
        const uint8_t* m = mem.getMemoryPointer();
        for (size_t i = 0; i < shipped.size(); ++i) assert(m[0xFF00 + i] == shipped[i]);
    }

    // -- 2. No roms/ anywhere: the built-in copy takes over, and says so ------
    const fs::path sandbox = fs::temp_directory_path() / "pom1_rom_fallback_smoke";
    fs::remove_all(sandbox);
    fs::create_directories(sandbox);
    fs::current_path(sandbox);

    {
        Memory mem;
        const int rc = mem.loadWozMonitor();
        assert(rc == 0 && "a missing Monitor file must NOT fail the load any more");

        const uint8_t* m = mem.getMemoryPointer();

        // Byte-identical to the shipped ROM — this is the drift guard. Rebuild
        // roms/WozMonitor.rom and you must regenerate kWozMonitorRom in
        // Memory.cpp, or the fallback machine stops being the shipped machine.
        for (size_t i = 0; i < shipped.size(); ++i) {
            assert(m[0xFF00 + i] == shipped[i] &&
                   "kWozMonitorRom drifted from roms/WozMonitor.rom");
        }

        // The two bytes the rest of POM1 uses as the Monitor's signature (see
        // the redundant-ROM-load guards in CLAUDE.md): CLD / CLI at $FF00.
        assert(m[0xFF00] == 0xD8);
        assert(m[0xFF01] == 0x58);

        // ...and the substitution is REPORTED. A silent fallback would trade a
        // black screen for a machine quietly missing its BASIC — no better.
        assert(listed(mem.romFallbacksUsed(), "WozMonitor.rom"));

        // Deduplicated: a preset switch re-loads every ROM, and the banner must
        // not grow a line each time.
        mem.loadWozMonitor();
        mem.loadWozMonitor();
        size_t occurrences = 0;
        for (const auto& f : mem.romFallbacksUsed())
            if (f == "WozMonitor.rom") ++occurrences;
        assert(occurrences == 1);
    }

    fs::current_path(repoRoot);
    fs::remove_all(sandbox);

    std::printf("rom_fallback_smoke: OK\n");
    return 0;
}
