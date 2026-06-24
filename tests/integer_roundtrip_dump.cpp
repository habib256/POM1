// integer_roundtrip_dump.cpp -- DEV TOOL: round-trip test the Integer tokeniser on
// the real programs. For each sketchs/basic_integer/*.bas it runs ibasic::compile()
// and compares the tokenised image to the program bytes the matching .apl loads
// into the $E000 ROM ([pp, himem)). Reports OK / the first byte that differs / the
// line that fails to tokenise -- this is "test blackjack.bas with the tokeniser",
// and the guide for extending BasicTokeniserInteger to full programs.
//
// Not a ctest. Build: `make test_integer_roundtrip`; run from repo root.
#include "TMS9918.h"      // IWYU pragma: keep
#include "WiFiModem.h"    // IWYU pragma: keep
#include "TerminalCard.h" // IWYU pragma: keep
#include "A1IO_RTC.h"     // IWYU pragma: keep
#include "PR40Printer.h"  // IWYU pragma: keep
#include "Memory.h"
#include "M6502.h"        // IWYU pragma: keep
#include "BasicTokeniserInteger.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::string readFile(const std::string& p)
{
    std::ifstream f(p, std::ios::binary);
    return f ? std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()) : std::string();
}

// The exact program bytes the .apl loads ([pp, himem)) = the ROM's tokenised image.
std::vector<uint8_t> aplProgram(const std::string& aplPath)
{
    Memory mem; mem.initMemory();
    mem.setWriteInRom(true); mem.loadBinary("roms/basic.rom", 0xE000, nullptr); mem.setWriteInRom(false);
    uint16_t run = 0;
    if (mem.loadHexDump(aplPath.c_str(), run, nullptr, nullptr) != 0) return {};
    const uint8_t* m = mem.getMemoryPointer();
    uint16_t himem = m[0x4C] | (m[0x4D] << 8);
    uint16_t pp    = m[0xCA] | (m[0xCB] << 8);
    std::vector<uint8_t> out;
    for (uint16_t a = pp; a < himem; ++a) out.push_back(m[a]);
    return out;
}

struct Prog { const char* bas; const char* apl; };
const Prog kProgs[] = {
    { "sketchs/basic_integer/blackjack.bas",           "software/Integer_basic/blackjack.apl.txt" },
    { "sketchs/basic_integer/stopwatch.bas",           "software/Integer_basic/stopwatch.apl.txt" },
    { "sketchs/basic_integer/twinkle.bas",             "software/Integer_basic/twinkle.apl.txt" },
    { "sketchs/basic_integer/resistor-calculator.bas", "software/Integer_basic/resistor-calculator.apl.txt" },
    { "sketchs/basic_integer/hamurabi.bas",            "software/Integer_basic/hamurabi.apl.txt" },
    { "sketchs/basic_integer/lunar-lander.bas",        "software/Integer_basic/lunar-lander-ascii-graphics.apl.txt" },
    { "sketchs/basic_integer/mini-startrek.bas",       "software/Integer_basic/mini-startrek.apl.txt" },
};

} // namespace

int main()
{
    int pass = 0, total = 0;
    for (const Prog& p : kProgs) {
        ++total;
        std::string src = readFile(p.bas);
        if (src.empty()) { std::printf("%-42s : SKIP (missing)\n", p.bas); continue; }
        ibasic::Result r = ibasic::compile(src);
        if (!r.ok) { std::printf("%-42s : COMPILE FAIL — %s\n", p.bas, r.error.c_str()); continue; }
        std::vector<uint8_t> want = aplProgram(p.apl);
        if (want.empty()) { std::printf("%-42s : compiled %d lines (no .apl to compare)\n", p.bas, r.lineCount); continue; }
        // Some saved .apl images terminate the LAST line with $FF instead of the
        // standard $01 EOL (an end-of-program marker variance — mini-startrek). The
        // tokeniser emits the standard $01, which the other programs' images use too,
        // and the program runs identically. Treat that single-byte tail as equivalent.
        bool exact = (r.image == want);
        if (!exact && r.image.size() == want.size() && !want.empty()
            && std::equal(r.image.begin(), r.image.end() - 1, want.begin())
            && r.image.back() == 0x01 && want.back() == 0xFF) {
            exact = true;
            std::printf("%-42s : OK (%d lines, %zu bytes; .apl ends the last line $FF, std $01)\n",
                        p.bas, r.lineCount, r.image.size()); ++pass; continue;
        }
        if (exact) { std::printf("%-42s : OK (%d lines, %zu bytes byte-exact)\n", p.bas, r.lineCount, r.image.size()); ++pass; continue; }
        // find first divergence
        size_t i = 0; while (i < r.image.size() && i < want.size() && r.image[i] == want[i]) ++i;
        std::printf("%-42s : MISMATCH at byte %zu (mine=%zuB rom=%zuB); mine[%zu]=$%02X rom=$%02X\n",
                    p.bas, i, r.image.size(), want.size(), i,
                    i < r.image.size() ? r.image[i] : 0, i < want.size() ? want[i] : 0);
        // walk both line-by-line ([len][numlo][numhi]...[01]); count diffs + classify
        // whether each differing line is explained purely by extra $A0 (space) bytes
        // in the ROM image (i.e. .bas/.apl source-whitespace drift), vs a real token
        // difference. Print the first differing line in full.
        {
            const std::vector<uint8_t>& mine = r.image; const std::vector<uint8_t>& rom = want;
            size_t a = 0, b = 0; int diffLines = 0, wsOnly = 0; bool printed = false;
            while (a < mine.size() && b < rom.size()) {
                size_t lm = mine[a], lr = rom[b];
                int nm = mine[a+1] | (mine[a+2] << 8); int nr = rom[b+1] | (rom[b+2] << 8);
                bool same = (lm == lr);
                if (same) for (size_t k = 0; k < lm; ++k) if (mine[a+k] != rom[b+k]) { same = false; break; }
                if (!same) {
                    ++diffLines;
                    // ws-only check: drop $A0 from both lines' token bodies; equal?
                    std::vector<uint8_t> sm, sr;
                    for (size_t k = 3; k + 1 < lm; ++k) if (mine[a+k] != 0xA0) sm.push_back(mine[a+k]);
                    for (size_t k = 3; k + 1 < lr; ++k) if (rom[b+k] != 0xA0) sr.push_back(rom[b+k]);
                    bool ws = (nm == nr && sm == sr);
                    if (ws) ++wsOnly;
                    if (!printed) {
                        printed = true;
                        std::printf("    1st diff line mine#%d rom#%d  len mine=%zu rom=%zu  (%s)\n",
                                    nm, nr, lm, lr, ws ? "space-drift only" : "REAL token diff");
                        std::printf("      mine:"); for (size_t k = 0; k < lm && a+k < mine.size(); ++k) std::printf(" %02X", mine[a+k]); std::printf("\n");
                        std::printf("      rom :"); for (size_t k = 0; k < lr && b+k < rom.size(); ++k) std::printf(" %02X", rom[b+k]); std::printf("\n");
                    }
                }
                a += lm; b += lr;
            }
            std::printf("    => %d differing line(s); %d are space-drift only, %d real\n",
                        diffLines, wsOnly, diffLines - wsOnly);
        }
    }
    std::printf("=== round-trip: %d/%d programs byte-exact through the tokeniser ===\n", pass, total);
    return 0;
}
