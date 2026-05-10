// reset_vectors_smoke_test.cpp -- pin Memory::configureResetVectors scope.
//
// Apple-1's WozMonitor.rom carries authentic interrupt vectors:
//   $FFFA-$FFFB  NMI = $0F00
//   $FFFC-$FFFD  RES = $FF00
//   $FFFE-$FFFF  IRQ = $0000   (deliberate — user installs trampoline at $0000)
//
// `configureResetVectors(addr)` is called by the load path (loadHexDump,
// loadBinary) and EmulationController::softReset to make hard-reset relaunch
// at the loaded program's entry. Historically it overwrote ALL three vectors
// with `addr`, which clobbered NMI/IRQ — breaking any P-LAB program that
// installs its own IRQ handler via the canonical RAM trampoline at $0000
// (every IRQ would fall into the program's reset entry instead of the
// user's handler).
//
// This test pins: configureResetVectors only sets RESET ($FFFC/$FFFD); the
// NMI and IRQ vectors stay at whatever WozMonitor.rom loaded.
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

int main()
{
    Memory mem;
    mem.initMemory();  // loads WozMonitor.rom + applies configureResetVectors($FF00)

    const uint8_t* m = mem.getMemoryPointer();

    // After initMemory: RES forced to $FF00 (Wozmon entry), NMI/IRQ from ROM.
    auto rd16 = [&](uint16_t a) -> uint16_t {
        return static_cast<uint16_t>(m[a] | (m[a + 1] << 8));
    };
    if (rd16(0xFFFC) != 0xFF00) {
        std::fprintf(stderr, "RES vector = $%04X (expected $FF00)\n", rd16(0xFFFC));
        return 1;
    }
    if (rd16(0xFFFA) != 0x0F00) {
        std::fprintf(stderr, "NMI vector = $%04X (expected $0F00 — Wozmon ROM authentic)\n",
                     rd16(0xFFFA));
        return 1;
    }
    if (rd16(0xFFFE) != 0x0000) {
        std::fprintf(stderr, "IRQ vector = $%04X (expected $0000 — Wozmon ROM authentic).\n"
                             "  configureResetVectors regressed: it is overwriting IRQ.\n",
                     rd16(0xFFFE));
        return 1;
    }

    // Now simulate the load path: configureResetVectors($0280) like the user
    // loads a program at $0280. Only RES should change.
    mem.setWriteInRom(true);
    mem.configureResetVectors(0x0280);
    mem.setWriteInRom(false);

    if (rd16(0xFFFC) != 0x0280) {
        std::fprintf(stderr, "RES not redirected: $%04X (expected $0280)\n", rd16(0xFFFC));
        return 1;
    }
    if (rd16(0xFFFA) != 0x0F00) {
        std::fprintf(stderr, "NMI corrupted by configureResetVectors($0280): $%04X (expected $0F00)\n",
                     rd16(0xFFFA));
        return 1;
    }
    if (rd16(0xFFFE) != 0x0000) {
        std::fprintf(stderr, "IRQ corrupted by configureResetVectors($0280): $%04X (expected $0000)\n"
                             "  -> P-LAB IRQ-driven programs would route IRQ to $0280 instead\n"
                             "     of the user's RAM trampoline at $0000.\n",
                     rd16(0xFFFE));
        return 1;
    }

    std::printf("reset_vectors_smoke: OK (RES=$%04X, NMI=$%04X, IRQ=$%04X)\n",
                rd16(0xFFFC), rd16(0xFFFA), rd16(0xFFFE));
    return 0;
}
