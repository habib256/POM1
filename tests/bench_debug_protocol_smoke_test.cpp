// bench_debug_protocol_smoke — the DevBench source-level debugging protocol,
// executed against the REAL 6502 instead of reasoned about.
//
// The four preceding bug hunts on this feature were all static reads, and the
// two nastiest findings (the dead ▶ after a hit, and the fact that re-arming
// wipes the trip latch the ▶ fix depended on) live in Pom1BenchHost, which
// needs a live MainWindow and therefore cannot be reached by ctest. What CAN
// be reached is the protocol underneath it: parse -> line -> address -> arm ->
// run -> halt -> resume. This test performs exactly the sequence the host
// performs, on a real program built by the real toolchain, so the reasoning
// behind those fixes stops being a claim.
//
// Pinned here (each numbered assertion group below):
//   1. an address taken from a SOURCE LINE is one the PC genuinely reaches;
//   2. the halt lands before the line's first byte executes, and maps back to
//      that same line;
//   3. resuming naively from a hit RE-TRIPS without advancing — the defect
//      that made the Bench's ▶ look dead (hunt #1);
//   4. step-then-run (the fix) really advances and re-arms for the next pass;
//   5. setBreakpoint() CLEARS the trip latch — which is why the ▶ condition
//      must not be written in terms of isBreakpointTripped() (hunt #5);
//   6. a data line has no address to arm (hunt #1's second finding), verified
//      against the real assembler rather than a hand-written record.
//
// cc65-gated + POSIX (mkdtemp), skip 77 like the other toolchain tests.

#include "DbgFile.h"
#include "M6502.h"
#include "Memory.h"
// Memory's unique_ptr<Peripheral> members need complete types at destruction.
#include "A1IO_RTC.h"
#include "CFFA1.h"
#include "MicroSD.h"
#include "SID.h"
#include "TMS9918.h"
#include "TerminalCard.h"
#include "WiFiModem.h"
#include "PR40Printer.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>  // mkdtemp (POSIX; macOS needs the explicit include)

namespace {

constexpr int kSkip = 77;
constexpr uint16_t kLoadAddr = 0x0300;

// Line numbers are load-bearing — the assertions below name them.
//
//   line 1  .setcpu                      (no code)
//   line 2  start:  ldx #$00     $0300
//   line 3  loop:   inx          $0302    <- breakpoint target, hit 5x
//   line 4          cpx #$05     $0303
//   line 5          bne loop     $0305
//   line 6  table:  .byte ...    $0307    <- DATA: no address to arm
//   line 7  done:   jmp done     $030B
const char kProgS[] =
    "        .setcpu \"6502\"\n"
    "start:  ldx #$00\n"
    "loop:   inx\n"
    "        cpx #$05\n"
    "        bne loop\n"
    "table:  .byte $11, $22, $33, $44\n"
    "done:   jmp done\n";

const char kCfg[] =
    "MEMORY { MAIN: start=$0300, size=$1000, file=%O; }\n"
    "SEGMENTS { CODE: load=MAIN, type=rw; }\n";

bool haveCc65()
{
    return std::system("ca65 --version >/dev/null 2>&1") == 0 &&
           std::system("ld65 --version >/dev/null 2>&1") == 0;
}

// Run until the CPU halts itself (breakpoint) or the budget runs out. Mirrors
// what EmulationController's slice loop does: start, then run in chunks.
// Returns total cycles executed.
int runUntilHalt(M6502& cpu, int budget)
{
    int spent = 0;
    cpu.start();
    while (spent < budget) {
        const int did = cpu.run(64);
        spent += did;
        if (cpu.isBreakpointTripped())
            break;
        if (did == 0)
            break;                       // made no progress: nothing left to do
    }
    return spent;
}

} // namespace

int main()
{
    if (!haveCc65()) {
        std::fprintf(stderr, "SKIP: cc65 (ca65/ld65) not on PATH\n");
        return kSkip;
    }
    char tmpl[] = "/tmp/pom1_dbgproto_XXXXXX";
    if (!mkdtemp(tmpl)) {
        std::fprintf(stderr, "SKIP: no temp dir\n");
        return kSkip;
    }
    const std::string dir(tmpl);
    const std::string srcS = dir + "/prog.s", cfg = dir + "/prog.cfg";
    const std::string obj = dir + "/prog.o", bin = dir + "/prog.bin";
    const std::string dbg = dir + "/prog.dbg";
    std::ofstream(srcS) << kProgS;
    std::ofstream(cfg) << kCfg;

    if (std::system(("ca65 -g -o " + obj + " " + srcS).c_str()) != 0) {
        std::fprintf(stderr, "FAIL: ca65 -g\n");
        return 1;
    }
    if (std::system(("ld65 -C " + cfg + " --dbgfile " + dbg + " -o " + bin +
                     " " + obj).c_str()) != 0) {
        std::fprintf(stderr, "FAIL: ld65 --dbgfile\n");
        return 1;
    }

    // ---- The Bench's parse step ----
    std::string dbgText;
    { std::ifstream f(dbg); std::stringstream ss; ss << f.rdbuf(); dbgText = ss.str(); }
    const pom1::DbgLineInfo info = pom1::parseDbgFile(dbgText, srcS);
    if (!info.ok) {
        std::fprintf(stderr, "FAIL: parseDbgFile: %s\n", info.error.c_str());
        return 1;
    }

    // ---- Load the program into a real machine ----
    std::vector<uint8_t> image;
    { std::ifstream f(bin, std::ios::binary);
      image.assign(std::istreambuf_iterator<char>(f),
                   std::istreambuf_iterator<char>()); }
    assert(!image.empty());

    Memory memory;
    memory.setTestMode(true);            // flat 64 KB, same harness as Klaus
    for (size_t i = 0; i < image.size(); ++i)
        memory.memWrite(static_cast<uint16_t>(kLoadAddr + i), image[i]);
    M6502 cpu(&memory);
    cpu.setProgramCounter(kLoadAddr);

    // ── 1. A source line yields an address the PC genuinely reaches ──────
    // This is the assertion no static read can make: the parser can look
    // perfectly consistent and still hand out an address the program never
    // executes (exactly what the data-line defect did).
    uint16_t loopAddr = 0;
    int loopLine = -1;
    assert(info.addrForLine(3, loopAddr, loopLine));
    assert(loopLine == 3);
    cpu.setBreakpoint(loopAddr);
    const int spent = runUntilHalt(cpu, 10000);
    assert(spent > 0);
    assert(cpu.isBreakpointTripped());

    // ── 2. Halt is BEFORE the line's first byte, and maps back to it ─────
    assert(cpu.getProgramCounter() == loopAddr);
    assert(info.lineForAddr(cpu.getProgramCounter()) == 3);
    // `inx` has not run yet on this first pass: X is still the ldx #$00 value.
    assert(cpu.getXRegister() == 0x00);

    // ── 3. Naive resume RE-TRIPS without advancing (the ▶-looked-dead bug) ─
    // M6502::run tests the breakpoint at the TOP of its loop, so resuming
    // while parked on the armed address halts again before executing
    // anything. The Bench's ▶ did exactly this and looked broken.
    const uint16_t pcBefore = cpu.getProgramCounter();
    const uint8_t xBefore = cpu.getXRegister();
    runUntilHalt(cpu, 10000);
    assert(cpu.getProgramCounter() == pcBefore);   // did not move
    assert(cpu.getXRegister() == xBefore);         // executed nothing
    assert(cpu.isBreakpointTripped());

    // ── 4. step-then-run (the fix) advances and re-arms for the next pass ─
    cpu.step();                                    // past the armed address
    assert(cpu.getXRegister() == static_cast<uint8_t>(xBefore + 1));  // inx ran
    assert(cpu.getProgramCounter() != loopAddr);
    runUntilHalt(cpu, 10000);
    assert(cpu.isBreakpointTripped());
    assert(cpu.getProgramCounter() == loopAddr);   // caught the next iteration
    assert(cpu.getXRegister() == static_cast<uint8_t>(xBefore + 1));

    // ── 5. setBreakpoint() CLEARS the trip latch ─────────────────────────
    // This is why the ▶ condition must be "stopped AND parked on the armed
    // address", never "isBreakpointTripped()": a rebuild re-arms the
    // breakpoint, which erases the very latch the older condition tested,
    // and the resume would insta-retrip again.
    assert(cpu.isBreakpointTripped());
    cpu.setBreakpoint(loopAddr);                   // same address, re-armed
    assert(!cpu.isBreakpointTripped());            // latch gone...
    assert(cpu.getProgramCounter() == loopAddr);   // ...but still parked on it
    // Proof that the latch's absence means nothing for what happens next:
    // resuming from here still halts immediately without advancing.
    const uint8_t xAtRearm = cpu.getXRegister();
    runUntilHalt(cpu, 10000);
    assert(cpu.getProgramCounter() == loopAddr);
    assert(cpu.getXRegister() == xAtRearm);

    // ── 6. A data line has no address to arm ─────────────────────────────
    // Line 6 is the .byte table; ca65 emits a line record for it, so a naive
    // parser hands back an address the PC never reaches — a breakpoint that
    // silently never fires. Snapping must reach line 7 instead.
    uint16_t dataAddr = 0;
    int dataLine = -1;
    assert(info.addrForLine(6, dataAddr, dataLine));
    assert(dataLine == 7);                         // snapped past the data
    assert(info.lineForAddr(dataAddr) == 7);
    // And the table's own bytes map to no line at all.
    uint16_t doneAddr = 0; int doneLine = -1;
    assert(info.addrForLine(7, doneAddr, doneLine) && doneLine == 7);
    assert(doneAddr > kLoadAddr);
    for (uint16_t a = 0x0307; a < doneAddr; ++a)
        assert(info.lineForAddr(a) == -1);

    std::printf("bench_debug_protocol_smoke: OK "
                "(line->addr->halt->resume verified on the real 6502)\n");
    return 0;
}
