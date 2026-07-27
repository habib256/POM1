// hex_dump_turbotype_test.cpp -- pin .TUR (Uncle Bernie's TurboType) loading.
//
// A .TUR is a hybrid transfer file. On real hardware it is fed through the
// keyboard port in two phases:
//
//     0003 / :A2 FF 9A 4C 1A FF / 0003R    stack-reset stub, plain WOZMON syntax
//     0100 / :A2 FE E8 ... / 0100R         the serial RECEIVER, autotyped, then RUN
//     T                                    switch the sender to turbo mode
//     0300                                 block address, on its own line
//     :D8A2FF9AA92A851A204604A97C8518A9    16 bytes per line, UNSPACED
//     ...                                  further blocks, same shape
//     X                                    end of the turbo stream
//     015ER                                run
//
// POM1 injects memory directly, so the transfer never happens: the receiver at
// $0100 is written to RAM and never executed. What makes that SAFE rather than
// approximate is the file's last run address -- it points at the CRC-CCITT
// checker, not at the program. Running it on the emulated 6502 recomputes the
// checksum over the injected image, jumps to the program on match, and prints
// "EE" on mismatch. This test therefore does not need a golden byte array: the
// file checks POM1's own parser, and the game's banner appearing IS the proof
// that every byte of both blocks landed where the sender intended.
//
// The regression it guards: loadHexDump's 'X' branch used to swallow the hex
// run behind the marker, eating "015E" and orphaning the "R". The load then
// fell back to the PREVIOUS run address -- "0100R", the serial receiver -- and
// POM1 jumped into a routine that spins waiting for bytes that direct injection
// never sends. Silent: the load itself reported success and the right byte
// count. See also hex_dump_extension_smoke for the ".tur" extension routing.

#include "TMS9918.h"      // IWYU pragma: keep
#include "WiFiModem.h"    // IWYU pragma: keep
#include "TerminalCard.h" // IWYU pragma: keep
#include "A1IO_RTC.h"     // IWYU pragma: keep
#include "PR40Printer.h"  // IWYU pragma: keep
#include "DisplayDevice.h"
#include "HexDumpFile.h"
#include "M6502.h"
#include "Memory.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

class CaptureDisplay : public DisplayDevice {
public:
    void onChar(char c) override { text.push_back(c); }
    std::string text;
};

// The 15 Puzzle as published by apple1software.com's serial-transfer dialog
// with TurboType enabled -- a real file, not a hand-written approximation.
const char* kFixture = "tests/fixtures/15puzzle.tur";

bool contains(const std::string& hay, const char* needle)
{
    return hay.find(needle) != std::string::npos;
}

} // namespace

int main()
{
    if (!std::filesystem::exists(kFixture)) {
        std::fprintf(stderr, "missing fixture '%s' (run ctest from the repo root)\n", kFixture);
        return 1;
    }

    // ---- 1. .tur routes to the hex parser ----------------------------------
    assert(pom1::isHexDumpPath(kFixture));
    assert(pom1::isHexDumpPath("PROG.TUR"));

    // ---- 2. Parse ----------------------------------------------------------
    Memory mem;
    uint16_t runAddr = 0;
    int bytes = 0;
    std::vector<std::pair<uint16_t, uint16_t>> zones;
    const int rc = mem.loadHexDump(kFixture, runAddr, &bytes, &zones);
    assert(rc == 0 && "loadHexDump rejected a well-formed .TUR");

    // THE regression: the last R in the file is "015ER" (the CRC checker), not
    // the "0100R" that starts the serial receiver on real hardware.
    if (runAddr != 0x015E) {
        std::fprintf(stderr,
                     "  → run address is $%04X, expected $015E. The 'X' end-marker "
                     "branch is eating the run line again.\n", runAddr);
        assert(false);
    }

    // Four blocks, delivered in this order:
    //   $0003-$0008    6 B   stack-reset stub      autotyped
    //   $0100-$015D   94 B   serial receiver       autotyped
    //   $0300-$06E5  998 B   the 15 Puzzle         turbo
    //   $015E-$01BC   95 B   CRC-CCITT checker     turbo
    // The receiver and the checker are contiguous ($015D/$015E) but arrive in
    // different phases, so they stay separate zones. Before the line-structured
    // parse the program shredded into 60+ bogus zones ($18A9, $4159, $F460, …)
    // because each 32-digit turbo line had its last 4 digits taken as an
    // address, and the count came out at 1059.
    const uint8_t* m = mem.getMemoryPointer();
    assert(bytes == 1193 && "expected 6 + 94 + 998 + 95 bytes");
    assert(zones.size() == 4 && "one zone per block, none shredded");
    assert(m[0x0003] == 0xA2 && m[0x0004] == 0xFF && "the $0003 stack-reset stub");
    assert(m[0x0100] == 0xA2 && m[0x0101] == 0xFE && "the $0100 serial receiver");
    assert(m[0x015E] == 0xA0 && m[0x015F] == 0xFF && "the $015E CRC checker");
    assert(m[0x01BC] == 0xB2 && "last byte of the CRC checker");
    assert(m[0x0300] == 0xD8 && m[0x0301] == 0xA2 && "the program at $0300");
    assert(m[0x06E5] == 0x00 && m[0x06E0] == 0x59 && "last bytes of the program");

    // ---- 3. Run the file's own CRC checker ---------------------------------
    // $015E walks $0300-$06E5, compares the CRC-CCITT against $1DC6, then
    // JMP $0300 on match / LDA #$EE + JSR PRBYTE on mismatch. Anything POM1
    // mis-parsed inside the program blocks shows up here as "EE".
    CaptureDisplay disp;
    mem.setDisplayDevice(&disp);
    M6502 cpu(&mem);
    cpu.setProgramCounter(runAddr);
    cpu.start();

    // The checker is ~740 iterations of a bit-serial CRC; the game then prints
    // its banner and stops at the "INSTRUCTIONS (Y/N)?" prompt with no key
    // queued, so the budget only has to be generous, not tuned.
    const long long kBudget = 40000000;
    const int kSlice = 100000;
    for (long long c = 0; c < kBudget; c += kSlice) {
        cpu.run(kSlice);
        if (contains(disp.text, "INSTRUCTIONS")) break;
    }
    mem.setDisplayDevice(nullptr);

    // Reaching the banner IS the CRC verdict: a mismatch jumps back to the
    // monitor and never executes a byte of the program. The "EE" probe is only
    // used to explain a failure, never to declare success — the game's own
    // instructions text contains "BETWEEN", so "EE" is not a safe negative.
    if (!contains(disp.text, "15 PUZZLE")) {
        if (contains(disp.text, "EE"))
            std::fprintf(stderr,
                         "  → the .TUR's own CRC-CCITT check FAILED (printed EE): the "
                         "injected image at $0300-$06E5 is not what the sender encoded.\n");
        else
            std::fprintf(stderr,
                         "  → the CRC checker never reached a verdict (%zu chars of "
                         "output). Captured:\n%s\n", disp.text.size(), disp.text.c_str());
        assert(false);
    }
    assert(contains(disp.text, "JEFF JETTON") && "the program's own author line");

    std::printf("hex_dump_turbotype_smoke: OK (CRC-CCITT verified the injected "
                "image, run $%04X -> $0300, %zu chars of program output)\n",
                runAddr, disp.text.size());
    return 0;
}
