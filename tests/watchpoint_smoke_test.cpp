// Memory watchpoint smoke test — pins read/write watch detection and the
// M6502 run-loop halt-after-access semantics.
//
// What this exercises:
//   * Memory::setWatchpoint arms per-address read/write flags; the hot path
//     latches the FIRST matching access into watchHit().
//   * Write-watch ignores reads and vice-versa.
//   * watchpointCount() tracks armed addresses; clearWatchpoint/clearAll reset.
//   * M6502::run() halts AFTER the instruction that touched a watched address
//     (PC has advanced past it), with the access still observable.
//
// NOTE: watchpoints are deliberately INACTIVE in Memory testMode (the Klaus
// flat-RAM mode), so this test runs with testMode OFF — the default 64 KB /
// non-strict configuration executes a small RAM program fine.
//
// Self-contained: M6502 + Memory + the unique_ptr peripheral set Memory's
// destructor reaches transitively.

#include "M6502.h"
#include "Memory.h"
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
#include <cstring>

int main() {
    // ---- Test 1: Memory-level read/write watch detection --------------
    {
        Memory mem;   // testMode off, presetRamKB=64, oorStrictMode=false

        mem.setWatchpoint(0x0050, /*read*/false, /*write*/true);
        assert(mem.hasWatchpoints());
        assert(mem.watchpointCount() == 1);
        assert(mem.watchpointFlags(0x0050) == 0x02);
        assert(!mem.isWatchpointTripped());

        // A read of a write-only watch must NOT trip.
        (void)mem.memRead(0x0050);
        assert(!mem.isWatchpointTripped());

        // A write trips, recording address + direction.
        mem.memWrite(0x0050, 0xAB);
        assert(mem.isWatchpointTripped());
        assert(mem.watchHit().address == 0x0050);
        assert(mem.watchHit().write == true);

        // Latch holds the first access until cleared.
        mem.clearWatchTrip();
        assert(!mem.isWatchpointTripped());

        // Re-arm as read-watch and confirm reads trip, writes don't.
        mem.setWatchpoint(0x0060, /*read*/true, /*write*/false);
        assert(mem.watchpointCount() == 2);
        mem.memWrite(0x0060, 0x11);
        assert(!mem.isWatchpointTripped());
        (void)mem.memRead(0x0060);
        assert(mem.isWatchpointTripped() && mem.watchHit().address == 0x0060 &&
               mem.watchHit().write == false);

        // Clear individual + all.
        mem.clearWatchpoint(0x0050);
        assert(mem.watchpointCount() == 1);
        mem.clearAllWatchpoints();
        assert(!mem.hasWatchpoints() && mem.watchpointCount() == 0 &&
               !mem.isWatchpointTripped());
        std::printf("[T1] OK — read/write watch detection + count tracking\n");
    }

    // ---- Test 2: M6502 halts AFTER the watched write ------------------
    {
        //   $0400  A9 AB     LDA #$AB
        //   $0402  85 50     STA $50      <-- write-watch; halt AFTER this
        //   $0404  A9 04     LDA #$04     (must NOT execute yet)
        //   $0406  4C 06 04  JMP $0406    spin trap
        constexpr uint8_t prog[] = {
            0xA9, 0xAB, 0x85, 0x50, 0xA9, 0x04, 0x4C, 0x06, 0x04,
        };
        Memory mem;
        mem.setWriteInRom(true);
        std::memcpy(mem.getMemoryPointerMutable() + 0x0400, prog, sizeof(prog));

        M6502 cpu(&mem);
        cpu.setProgramCounter(0x0400);
        mem.setWatchpoint(0x0050, false, true);
        cpu.start();
        cpu.run(200);

        // Halted right after STA $50: PC at $0404, A holds $AB (LDA #$04 not run).
        assert(cpu.getProgramCounter() == 0x0404);
        assert(cpu.getAccumulator() == 0xAB);
        assert(!cpu.isRunning());
        assert(mem.isWatchpointTripped());
        assert(mem.watchHit().address == 0x0050 && mem.watchHit().write);
        assert(mem.memRead(0x0050) == 0xAB);

        // Resume after clearing the latch + disarming: runs to the spin trap.
        mem.clearWatchTrip();
        mem.clearAllWatchpoints();
        cpu.start();
        cpu.run(200);
        assert(cpu.getProgramCounter() == 0x0406);
        assert(cpu.getAccumulator() == 0x04);
        std::printf("[T2] OK — run() halted after STA $50, resumed to $%04X\n",
                    cpu.getProgramCounter());
    }

    // ---- Test 3: no-watch sanity (gate must not false-positive) -------
    {
        constexpr uint8_t prog[] = { 0xA9, 0x04, 0x4C, 0x02, 0x04 }; // LDA #$04; JMP $0402
        Memory mem;
        mem.setWriteInRom(true);
        std::memcpy(mem.getMemoryPointerMutable() + 0x0400, prog, sizeof(prog));

        M6502 cpu(&mem);
        cpu.setProgramCounter(0x0400);
        cpu.start();
        cpu.run(200);
        assert(cpu.getProgramCounter() == 0x0402);
        assert(!mem.isWatchpointTripped());
        std::printf("[T3] OK — no-watch path clean\n");
    }

    std::printf("OK\n");
    return 0;
}
