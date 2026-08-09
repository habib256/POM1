// msbasic_smoke_test.cpp -- Microsoft BASIC 6502 on the Apple-1, end to end.
//
// roms/msbasic.rom is the OSI-derived 8 KB Microsoft BASIC (floating point)
// ported to the Apple-1's PIA, sharing the $E000 window with Woz's Integer
// BASIC. Provenance and the reproducible build are in dev/msbasic/README.md.
//
// This runs it the way a user does -- flash the ROM, jump to the cold-start
// entry, and read what comes out of $D012 -- so it pins the whole chain at once:
//   * loadMsBasic maps the image AND restores POM1's own Woz Monitor over the
//     replica's bundled copy (the ROM is an 8 KB $E000-$FFFF EPROM image);
//   * the interpreter's own PIA I/O (KBDCR/KBDD/DSP) talks to POM1's PIA;
//   * the documented entry points really are $E000 (cold) and $E003 (warm);
//   * unloadBasic clears the FULL $E000-$FEFF window -- MS BASIC is nearly
//     twice the size of Integer BASIC, and clearing only $E000-$EFFF used to
//     leave 3 KB of the previous interpreter behind when switching flavours.
//
// It also answers a question no checksum can: that the image POM1 ships is a
// working interpreter, not just bytes that hash correctly.

#include "TMS9918.h"      // IWYU pragma: keep
#include "WiFiModem.h"    // IWYU pragma: keep
#include "TerminalCard.h" // IWYU pragma: keep
#include "A1IO_RTC.h"     // IWYU pragma: keep
#include "PR40Printer.h"  // IWYU pragma: keep
#include "DisplayDevice.h"
#include "M6502.h"
#include "Memory.h"

#include <cassert>
#include <cstdio>
#include <string>

namespace {

class CaptureDisplay : public DisplayDevice {
public:
    void onChar(char c) override { text.push_back(c); }
    std::string text;
};

bool contains(const std::string& hay, const char* needle)
{
    return hay.find(needle) != std::string::npos;
}

// Run until `stop` shows up in the captured output, or the budget runs out.
void runUntil(M6502& cpu, const CaptureDisplay& disp, const char* stop, long long budget)
{
    const int kSlice = 50000;
    for (long long c = 0; c < budget; c += kSlice) {
        cpu.run(kSlice);
        if (contains(disp.text, stop)) return;
    }
}

} // namespace

int main()
{
    Memory mem;
    mem.initMemory();

    if (mem.loadMsBasic() != 0) {
        std::fprintf(stderr, "cannot load roms/msbasic.rom: %s\n"
                             "  (run ctest from the repo root)\n",
                     mem.getLastError().c_str());
        return 1;
    }

    const uint8_t* m = mem.getMemoryPointer();

    // ---- 1. The window, and what must NOT be in it -------------------------
    // $E000/$E003 are the two documented entry points, both JMPs.
    assert(m[0xE000] == 0x4C && "cold-start entry $E000 must be a JMP");
    assert(m[0xE003] == 0x4C && "warm-start entry $E003 must be a JMP");
    // POM1's own Woz Monitor, not the replica's bundled copy: the image carries
    // one at $FF00 and loadMsBasic must put ours back over it. $D8 $58 = CLD/CLI,
    // the Woz Monitor's reset-entry signature (see Memory's reload guards).
    assert(m[0xFF00] == 0xD8 && m[0xFF01] == 0x58 &&
           "loadMsBasic must restore POM1's Woz Monitor over the ROM's own copy");
    // Authentic vectors survive (configureResetVectors only owns RES).
    assert(m[0xFFFA] == 0x00 && m[0xFFFB] == 0x0F && "NMI stays $0F00");
    assert(m[0xFFFE] == 0x00 && m[0xFFFF] == 0x00 && "IRQ stays $0000");

    // ---- 2. Cold start ------------------------------------------------------
    CaptureDisplay disp;
    mem.setDisplayDevice(&disp);
    M6502 cpu(&mem);
    cpu.setProgramCounter(0xE000);
    cpu.start();

    // The cold start asks for the memory size and terminal width before it
    // prints its banner; empty lines (CR) accept the defaults.
    for (int i = 0; i < 3; ++i) {
        mem.setKeyPressed('\r');
        runUntil(cpu, disp, "BYTES FREE", 8000000);
        if (contains(disp.text, "BYTES FREE")) break;
    }

    if (!contains(disp.text, "BYTES FREE")) {
        std::fprintf(stderr,
                     "  → Microsoft BASIC never reached its sign-on banner.\n"
                     "    Captured %zu chars:\n%s\n",
                     disp.text.size(), disp.text.c_str());
        return 1;
    }
    // The two cold-start questions ARE this interpreter's signature: Woz's
    // Integer BASIC asks neither, so a mis-flashed $E000 window cannot reach
    // here by accident. (This OSI-derived build has no copyright sign-on line —
    // CONFIG_SMALL drops it to fit 8 KB — so "BYTES FREE" is the banner.)
    if (!contains(disp.text, "MEMORY SIZE?") || !contains(disp.text, "TERMINAL WIDTH?")) {
        std::fprintf(stderr, "  → banner reached but the cold-start prompts are missing:\n%s\n",
                     disp.text.c_str());
        return 1;
    }

    // ---- 3. It actually computes, in FLOATING POINT ------------------------
    // The reason MS BASIC is worth having next to Integer BASIC: Woz's
    // interpreter has no floats at all, so this expression is the feature.
    disp.text.clear();
    const char* line = "PRINT 1/4\r";
    for (const char* p = line; *p; ++p) {
        mem.setKeyPressed(*p);
        cpu.run(200000);
    }
    runUntil(cpu, disp, ".25", 8000000);
    if (!contains(disp.text, ".25")) {
        std::fprintf(stderr,
                     "  → \"PRINT 1/4\" did not produce .25 (floating point broken or the "
                     "interpreter is not accepting input). Captured:\n%s\n",
                     disp.text.c_str());
        return 1;
    }
    mem.setDisplayDevice(nullptr);

    // ---- 4. unloadBasic clears the WHOLE shared window ----------------------
    // Integer BASIC is 4 KB, MS BASIC 7.75 KB. Clearing only $E000-$EFFF (what
    // unloadBasic did while Integer BASIC was the only occupant) would leave
    // $F000-$FEFF full of the previous interpreter when the user switches.
    mem.unloadBasic();
    for (uint32_t a = 0xE000; a <= 0xFEFF; ++a) {
        if (m[a] != 0x00) {
            std::fprintf(stderr,
                         "  → unloadBasic left $%04X = $%02X; it must clear the full "
                         "$E000-$FEFF window that MS BASIC occupies\n", a, m[a]);
            return 1;
        }
    }
    // ...and must not have touched the Woz Monitor above it.
    assert(m[0xFF00] == 0xD8 && "unloadBasic must stop below $FF00");

    std::printf("msbasic_smoke: OK (cold start, sign-on banner, PRINT 1/4 = .25, "
                "unload clears $E000-$FEFF)\n");
    return 0;
}
