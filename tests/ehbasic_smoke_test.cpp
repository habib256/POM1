// ehbasic_smoke_test.cpp -- Lee Davison's Enhanced 6502 BASIC on the Apple-1.
//
// Unlike every other BASIC POM1 ships, this one is OURS: no Apple-1 port of
// EhBASIC existed, so dev/ehbasic/ carries one (the PIA I/O and the entry stub;
// the interpreter itself is untouched). That makes an end-to-end test the only
// real proof the port works -- there is no published image to compare against
// and no upstream to blame.
//
// What it exercises, in order:
//   * the image loads at $5000-$7FFF as plain RAM (NOT a ROM window);
//   * $5000/$5003 really are the cold/warm entries the docs promise;
//   * the port's own V_INPT/V_OUTP reach POM1's PIA -- keystrokes go in through
//     $D010/$D011, characters come out of $D012;
//   * the interpreter runs, sizes its RAM, and computes in floating point;
//   * EhBASIC's licence string is present in the shipped binary, which its
//     terms require of "any binary image distributed".

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

void runUntil(M6502& cpu, const CaptureDisplay& disp, const char* stop, long long budget)
{
    const int kSlice = 50000;
    for (long long c = 0; c < budget; c += kSlice) {
        cpu.run(kSlice);
        if (contains(disp.text, stop)) return;
    }
}

// Type a line the way the user does: one key at a time through the PIA, giving
// the interpreter cycles to drain each one (the Apple-1 keyboard has a single
// latch, so pushing two keys without running the CPU loses the first).
void typeLine(M6502& cpu, Memory& mem, const char* s)
{
    for (const char* p = s; *p; ++p) {
        mem.setKeyPressed(*p);
        cpu.run(100000);
    }
}

} // namespace

int main()
{
    Memory mem;
    mem.initMemory();

    if (mem.loadEhBasic() != 0) {
        std::fprintf(stderr, "cannot load roms/ehbasic.rom: %s\n"
                             "  (run ctest from the repo root)\n",
                     mem.getLastError().c_str());
        return 1;
    }

    const uint8_t* m = mem.getMemoryPointer();

    // ---- 1. Where it landed -------------------------------------------------
    assert(m[0x5000] == 0x4C && "cold-start entry $5000 must be a JMP");
    assert(m[0x5003] == 0x4C && "warm-start entry $5003 must be a JMP");
    // It is loaded into RAM, so nothing may have leaked into the ROM windows
    // above it — in particular the Woz Monitor and its vectors.
    assert(m[0xFF00] == 0xD8 && m[0xFF01] == 0x58 && "Woz Monitor untouched");
    assert(m[0xFFFA] == 0x00 && m[0xFFFB] == 0x0F && "NMI stays $0F00");
    assert(m[0xFFFE] == 0x00 && m[0xFFFF] == 0x00 && "IRQ stays $0000");

    // ---- 2. The licence string is IN the image ------------------------------
    // EhBASIC is free but not copyright free: "any derivative work should
    // include, in any binary image distributed, the string 'Derived from
    // EhBASIC'". The port prints it at sign-on, so it lives in the bytes.
    {
        const std::string image(reinterpret_cast<const char*>(m + 0x5000), 0x3000);
        if (image.find("DERIVED FROM EHBASIC") == std::string::npos) {
            std::fprintf(stderr,
                         "  → the shipped image no longer carries the \"Derived from EhBASIC\" "
                         "string that EhBASIC's licence requires of any distributed binary\n");
            return 1;
        }
    }

    // ---- 3. Cold start ------------------------------------------------------
    CaptureDisplay disp;
    mem.setDisplayDevice(&disp);
    M6502 cpu(&mem);
    cpu.setProgramCounter(0x5000);
    cpu.start();

    // EhBASIC's cold start asks "Memory size ?"; an empty line takes the
    // default, which makes it probe RAM itself up to RAM_TOP. The loop allows
    // for a couple of prompts because 2.22 variants differ on how many they
    // ask -- this build asks exactly one.
    for (int i = 0; i < 4 && !contains(disp.text, "Bytes free"); ++i) {
        mem.setKeyPressed('\r');
        runUntil(cpu, disp, "Bytes free", 20000000);
    }

    if (!contains(disp.text, "Bytes free")) {
        std::fprintf(stderr,
                     "  → EhBASIC never reached its sign-on. Captured %zu chars:\n%s\n",
                     disp.text.size(), disp.text.c_str());
        return 1;
    }
    // The port's own banner, proving we went through OUR stub and not some
    // other interpreter that happened to be sitting at $5000.
    if (!contains(disp.text, "EhBASIC 2.22")) {
        std::fprintf(stderr, "  → sign-on reached but the Apple-1 port's banner is missing:\n%s\n",
                     disp.text.c_str());
        return 1;
    }

    // ---- 4. It computes, in floating point ---------------------------------
    disp.text.clear();
    typeLine(cpu, mem, "PRINT 1/4\r");
    runUntil(cpu, disp, ".25", 20000000);
    if (!contains(disp.text, ".25")) {
        std::fprintf(stderr,
                     "  → \"PRINT 1/4\" did not produce .25 — the port's keyboard or display "
                     "vector is wrong, or the interpreter is not running. Captured:\n%s\n",
                     disp.text.c_str());
        return 1;
    }

    // A transcendental too: EhBASIC's selling point over Integer BASIC is the
    // full float library, and SQR exercises a different code path than divide.
    disp.text.clear();
    typeLine(cpu, mem, "PRINT SQR(2)\r");
    runUntil(cpu, disp, "1.41421", 20000000);
    if (!contains(disp.text, "1.41421")) {
        std::fprintf(stderr, "  → \"PRINT SQR(2)\" did not produce 1.41421...:\n%s\n",
                     disp.text.c_str());
        return 1;
    }

    // ---- 5. A program runs --------------------------------------------------
    // Stored program, FOR loop, string output: enough to show the editor, the
    // tokeniser and the run-time loop all work through the port's I/O.
    disp.text.clear();
    typeLine(cpu, mem, "10 FOR I=1 TO 3\r");
    typeLine(cpu, mem, "20 PRINT \"POM\";I\r");
    typeLine(cpu, mem, "30 NEXT\r");
    typeLine(cpu, mem, "RUN\r");
    runUntil(cpu, disp, "POM 3", 20000000);
    if (!contains(disp.text, "POM 1") || !contains(disp.text, "POM 3")) {
        std::fprintf(stderr, "  → the stored program did not run to completion:\n%s\n",
                     disp.text.c_str());
        return 1;
    }
    mem.setDisplayDevice(nullptr);

    std::printf("ehbasic_smoke: OK (cold start, PRINT 1/4 = .25, SQR(2) = 1.41421, "
                "FOR loop ran)\n");
    return 0;
}
