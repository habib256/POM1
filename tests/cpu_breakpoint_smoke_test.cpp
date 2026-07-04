// CLI `--break <addr>` smoke test — pins M6502 PC-matched halt semantics.
//
// What this exercises:
//   * setBreakpoint() arms a single PC. M6502::run() halts BEFORE the
//     instruction at that address executes (no side effects from the
//     breakpoint instruction itself).
//   * isBreakpointTripped() flips true on hit; running flag drops to 0.
//   * clearBreakpoint() lets execution resume normally.
//   * Manual stepCpu past the breakpoint + start() also resumes.
//   * hardReset() disarms — preset switches don't carry stale breakpoints.
//   * No-breakpoint path: when not armed, run() ignores PC entirely
//     (sanity check that the gate doesn't false-positive).
//
// Self-contained: M6502 + Memory + the unique_ptr peripheral set Memory's
// destructor reaches transitively (no PeripheralBus action — Memory runs
// in test mode = flat 64 KB RAM, same harness Klaus uses).

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
#include <cstring>

namespace {

// Tiny program at $0400. The breakpoint will be armed at $0404, so the
// LDA #$03 instruction at that address must NOT execute — A should
// observe $02 from the previous LDA.
//
//   $0400  A9 01     LDA #$01    ; A := $01
//   $0402  A9 02     LDA #$02    ; A := $02
//   $0404  A9 03     LDA #$03    ; <-- breakpoint, must not execute
//   $0406  A9 04     LDA #$04    ; A := $04
//   $0408  4C 08 04  JMP $0408   ; spin trap
constexpr uint16_t kEntry  = 0x0400;
constexpr uint16_t kBpAddr = 0x0404;
constexpr uint16_t kSpin   = 0x0408;
constexpr uint8_t  kProgram[] = {
    0xA9, 0x01,         // LDA #$01
    0xA9, 0x02,         // LDA #$02
    0xA9, 0x03,         // LDA #$03      <-- breakpoint
    0xA9, 0x04,         // LDA #$04
    0x4C, 0x08, 0x04,   // JMP $0408
};

void loadProgram(Memory& mem) {
    std::memcpy(mem.getMemoryPointerMutable() + kEntry,
                kProgram, sizeof(kProgram));
}

} // namespace

int main() {
    // ---- Test 1: breakpoint fires before the instruction executes -----
    {
        Memory memory;
        memory.setTestMode(true);
        memory.setWriteInRom(true);
        loadProgram(memory);

        M6502 cpu(&memory);
        cpu.setProgramCounter(kEntry);
        cpu.setBreakpoint(kBpAddr);
        assert(cpu.hasBreakpoint());
        assert(cpu.getBreakpoint() == kBpAddr);
        assert(!cpu.isBreakpointTripped());
        cpu.start();

        // Generous cycle budget: the program is 4 instructions to reach
        // the breakpoint (8 cycles total). 200 lets the spin trap run
        // many times if the breakpoint were broken.
        cpu.run(200);

        // PC must be at the breakpoint address, NOT past it. Klaus-style:
        // we want callers to inspect the architectural state right before
        // the trapped instruction commits.
        assert(cpu.getProgramCounter() == kBpAddr);
        assert(cpu.isBreakpointTripped());
        assert(!cpu.isRunning());
        // LDA #$02 was the last instruction to execute — A must hold $02,
        // not $03. If we see $03, the breakpoint fired *after* the
        // instruction, which is the wrong semantics.
        assert(cpu.getAccumulator() == 0x02);

        std::printf("[T1] OK — break at $%04X, A=$%02X, tripped=%d\n",
                    cpu.getProgramCounter(),
                    cpu.getAccumulator(),
                    cpu.isBreakpointTripped());
    }

    // ---- Test 2: clearBreakpoint() resumes normal execution -----------
    {
        Memory memory;
        memory.setTestMode(true);
        memory.setWriteInRom(true);
        loadProgram(memory);

        M6502 cpu(&memory);
        cpu.setProgramCounter(kEntry);
        cpu.setBreakpoint(kBpAddr);
        cpu.start();
        cpu.run(200);
        assert(cpu.isBreakpointTripped());
        assert(cpu.getProgramCounter() == kBpAddr);

        // Disarm + resume — the spin trap at $0408 should be reached.
        cpu.clearBreakpoint();
        assert(!cpu.hasBreakpoint());
        cpu.start();
        cpu.run(200);

        // We should now be sitting on the JMP-to-self.
        assert(cpu.getProgramCounter() == kSpin);
        assert(cpu.getAccumulator() == 0x04);
        assert(!cpu.isBreakpointTripped());
        std::printf("[T2] OK — cleared, ran to $%04X, A=$%02X\n",
                    cpu.getProgramCounter(),
                    cpu.getAccumulator());
    }

    // ---- Test 3: manual step-over past the breakpoint ----------------
    {
        Memory memory;
        memory.setTestMode(true);
        memory.setWriteInRom(true);
        loadProgram(memory);

        M6502 cpu(&memory);
        cpu.setProgramCounter(kEntry);
        cpu.setBreakpoint(kBpAddr);
        cpu.start();
        cpu.run(200);
        assert(cpu.getProgramCounter() == kBpAddr);

        // Manual step-over: step() bypasses the run() gate, executes the
        // breakpoint instruction once, advances PC. After this LDA #$03,
        // A should be $03 and PC at $0406.
        cpu.step();
        assert(cpu.getProgramCounter() == 0x0406);
        assert(cpu.getAccumulator() == 0x03);

        // Resume — breakpoint stays armed, but PC has moved past it, so
        // we now run to the spin trap.
        cpu.start();
        cpu.run(200);
        assert(cpu.getProgramCounter() == kSpin);
        assert(cpu.getAccumulator() == 0x04);
        assert(cpu.hasBreakpoint());  // still armed for any future hit
        std::printf("[T3] OK — stepped past, ran to $%04X, A=$%02X (still armed)\n",
                    cpu.getProgramCounter(),
                    cpu.getAccumulator());
    }

    // ---- Test 4: hardReset() disarms the breakpoint -------------------
    {
        Memory memory;
        memory.setTestMode(true);
        memory.setWriteInRom(true);
        loadProgram(memory);

        M6502 cpu(&memory);
        cpu.setBreakpoint(kBpAddr);
        assert(cpu.hasBreakpoint());

        // hardReset should clear the breakpoint — preset switch must
        // not carry breakpoints from a previous address space.
        cpu.hardReset();
        assert(!cpu.hasBreakpoint());
        assert(!cpu.isBreakpointTripped());

        // Sanity: re-jump to the program, run, no halt should occur at
        // $0404 since the breakpoint is gone.
        cpu.setProgramCounter(kEntry);
        cpu.start();
        cpu.run(200);
        assert(cpu.getProgramCounter() == kSpin);
        assert(cpu.getAccumulator() == 0x04);
        std::printf("[T4] OK — hardReset disarmed, ran through to $%04X\n",
                    cpu.getProgramCounter());
    }

    // ---- Test 5: no-breakpoint sanity (gate must not false-positive) --
    {
        Memory memory;
        memory.setTestMode(true);
        memory.setWriteInRom(true);
        loadProgram(memory);

        M6502 cpu(&memory);
        cpu.setProgramCounter(kEntry);
        // No setBreakpoint call — the gate should be entirely off.
        assert(!cpu.hasBreakpoint());
        cpu.start();
        cpu.run(200);
        assert(cpu.getProgramCounter() == kSpin);
        assert(cpu.getAccumulator() == 0x04);
        assert(!cpu.isBreakpointTripped());
        std::printf("[T5] OK — no-breakpoint path clean\n");
    }

    // ---- Test 6: breakpoint on an ISR entry reached via IRQ -----------
    // Regression: run() must service a pending interrupt (which vectors PC to
    // the handler) and THEN test the breakpoint, so a breakpoint on the ISR
    // entry fires BEFORE the first handler instruction. The old order folded
    // vectoring + first-instruction into one atomic step(), silently stepping
    // over the breakpoint (the "break in my ISR" case).
    {
        Memory memory;
        memory.setTestMode(true);
        memory.setWriteInRom(true);
        uint8_t* ram = memory.getMemoryPointerMutable();

        // Main program spins at $0400; ISR at $0500 sets A=$AA then RTI.
        ram[0x0400] = 0x4C; ram[0x0401] = 0x00; ram[0x0402] = 0x04;  // JMP $0400
        ram[0x0500] = 0xA9; ram[0x0501] = 0xAA;                      // LDA #$AA
        ram[0x0502] = 0x40;                                          // RTI
        ram[0xFFFE] = 0x00; ram[0xFFFF] = 0x05;                      // IRQ vector -> $0500

        M6502 cpu(&memory);
        cpu.setProgramCounter(0x0400);
        cpu.setStackPointer(0xFF);
        cpu.setAccumulator(0x11);          // sentinel: ISR body must not overwrite it
        cpu.setStatusRegister(0x20);       // U set, I CLEAR so the IRQ is serviced
        cpu.setBreakpoint(0x0500);         // breakpoint on the ISR entry
        cpu.setIRQ(1);
        cpu.start();
        cpu.run(200);

        // The breakpoint fired at the ISR entry, before LDA #$AA executed.
        assert(cpu.getProgramCounter() == 0x0500);
        assert(cpu.isBreakpointTripped());
        assert(!cpu.isRunning());
        assert(cpu.getAccumulator() == 0x11);          // ISR body did NOT run
        // The interrupt entry DID happen (3 bytes pushed) — proves we reached
        // $0500 by vectoring, and the I flag was set by IRQ entry.
        assert(cpu.getStackPointer() == 0xFC);
        assert((cpu.getStatusRegister() & 0x04) != 0); // I set by handleIRQ
        std::printf("[T6] OK — IRQ-vectored breakpoint at $%04X, A=$%02X (ISR body skipped)\n",
                    cpu.getProgramCounter(), cpu.getAccumulator());
    }

    std::printf("OK\n");
    return 0;
}
