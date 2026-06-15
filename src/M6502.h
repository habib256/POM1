// Pom1 Apple 1 Emulator
// Copyright (C) 2012 John D. Corrado
// Copyright (C) 2000-2026 Verhille Arnaud
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#ifndef M6502_H
#define M6502_H

#include "Memory.h"

#include <atomic>

namespace pom1 { class SnapshotWriter; class SnapshotReader; }

class M6502
{
public:
    /// Bits du registre P (éviter #define N,C,I,… qui cassent les en-têtes Windows).
    struct Status {
        static constexpr uint8_t N = 0x80;
        static constexpr uint8_t V = 0x40;
        static constexpr uint8_t B = 0x10;
        static constexpr uint8_t D = 0x08;
        static constexpr uint8_t I = 0x04;
        static constexpr uint8_t Z = 0x02;
        static constexpr uint8_t C = 0x01;
    };

    M6502();
    M6502(Memory* mem);

    void start(void);
    void stop(void);
    void softReset(void);
    void hardReset(void);
    void setIRQ(int state);
    void setNMI(void);
    void dumpPcTrace(const char* tag);

    /// Debug: when true, BRK logs a full CPU+stack dump + recent control-flow
    /// trace + bus state on every execution. Off by default. The `dumpPcTrace`
    /// ring buffer is always live (cheap) and can be dumped on demand.
    void setDebugBrkTrace(bool enabled) { debugBrkTrace = enabled; }
    bool getDebugBrkTrace() const { return debugBrkTrace; }
    uint16_t memReadAbsolute(uint16_t adr);
    
    // Nouvelles méthodes pour l'exécution et l'affichage
    void step(void);  // Exécuter une instruction
    /// Run until at least `maxCycles` 6502 cycles have elapsed (or `stop()`
    /// is called). Per-instruction granularity means we typically overshoot
    /// by 1-6 cycles; the actual cycle count is returned so callers pacing
    /// against a wallclock budget can deduct what was really consumed
    /// (otherwise the overshoot accumulates and the CPU runs faster than
    /// the nominal POM1_CPU_CLOCK_HZ rate).
    int run(int maxCycles);
    bool isRunning(void) const { return running.load(std::memory_order_relaxed) != 0; }

    // -------- Apple-1 DRAM refresh stall ----------------------------------
    //
    // Per UncleBernie's applefritter analysis (Jan 2022): the Apple-1's
    // refresh controller halts the 6502 during 4 of every 65 cycles (the
    // H10·H6 NAND slot in each scanline). Refresh is NON-transparent —
    // the CPU stalls, tight cycle-counted code runs slower on silicon
    // than the emulator. Enable to reproduce silicon-side timing bugs
    // (Wozmon ACI cassette, Disk II Woz Machine).
    //
    // Implemented as a Bresenham accumulator after every instruction:
    // refreshAccum += instrCycles * 4; while (refreshAccum >= 65)
    // inject one stall cycle. Cumulative ratio is exactly 4:65.
    void setDramRefreshEnabled(bool enabled) { dramRefreshEnabled = enabled; }
    bool isDramRefreshEnabled() const { return dramRefreshEnabled; }
    uint64_t getDramRefreshStallCount() const { return dramRefreshStallCount; }
    void resetDramRefreshStallCount() { dramRefreshStallCount = 0; dramRefreshAccum = 0; }
    
    // Accesseurs pour les registres (pour le débogueur)
    uint8_t getAccumulator(void) const { return accumulator; }
    uint8_t getXRegister(void) const { return xRegister; }
    uint8_t getYRegister(void) const { return yRegister; }
    uint8_t getStatusRegister(void) const { return statusRegister; }
    uint8_t getStackPointer(void) const { return stackPointer; }
    uint16_t getProgramCounter(void) const { return programCounter; }
    /// Cycles accumulated so far by the instruction currently executing
    /// (opcode fetch + addressing mode + operation as they run). Sampled by
    /// Memory's GEN2 soft-switch handler to timestamp a $C25x access at its
    /// in-instruction cycle instead of the instruction's start — the POM2
    /// `getCurrentInstructionCycles()` beam-racing idiom. Between
    /// instructions it holds the LAST instruction's total; only consult it
    /// from inside a memRead/memWrite issued by the CPU.
    int getCurrentInstructionCycles(void) const { return cycles; }
    /// Jump the PC to an arbitrary address without going through RESET.
    /// Used by the Klaus Dormann functional test harness (the test binary
    /// sets its reset vector to an error trap and expects callers to jump
    /// directly into $0400).
    void setProgramCounter(uint16_t pc) { programCounter = pc; }

    /// PC-matched halt for headless/scripted debugging. When `active` is
    /// true and `programCounter == address` at the top of `run()`'s loop,
    /// the CPU stops itself (one log line at WARN) before the instruction
    /// at that address executes. The check sits behind `breakpointActive`,
    /// so the off-path cost is a single load + branch — no overhead when
    /// the breakpoint isn't armed. Single-breakpoint by design (TODO entry
    /// `[S · solid]`); multi-PC support can be added behind the same flag
    /// later. Cleared on hardReset() so a preset switch doesn't carry it.
    void setBreakpoint(uint16_t address) {
        breakpointAddress = address;
        breakpointActive  = true;
        breakpointTripped = false;
    }
    void clearBreakpoint() {
        breakpointActive  = false;
        breakpointTripped = false;
    }
    bool hasBreakpoint() const { return breakpointActive; }
    uint16_t getBreakpoint() const { return breakpointAddress; }
    /// True iff the most recent `run()` exit was triggered by the
    /// breakpoint firing (not by a `stop()` call or by the cycle budget
    /// expiring). Self-cleared on the next `setBreakpoint` /
    /// `clearBreakpoint` / `start()`. Lets the EmulationController and
    /// the CLI dispatcher tell "halted at breakpoint" from "halted by
    /// the user" without inspecting PC by hand.
    bool isBreakpointTripped() const { return breakpointTripped; }

    // Snapshot round-trip — written into the "CPU" section of a .snap file.
    // Captures only architecturally-visible state (PC, A, X, Y, SR, SP,
    // IRQ/NMI lines, cycle counter). Mid-instruction scratch (op, tmp,
    // running) and debug toggles (breakpoint*, debugBrkTrace) are session-
    // local and intentionally skipped — `loadSnapshot` always lands on an
    // instruction boundary.
    void serialize(pom1::SnapshotWriter& writer) const;
    void deserialize(pom1::SnapshotReader& reader);

private:


private :

    Memory *memory;

    bool debugBrkTrace = false;   // see setDebugBrkTrace()

    // Single-PC breakpoint — see setBreakpoint() above. `breakpointActive`
    // is the hot-path gate (one branch in `run()`). `breakpointTripped`
    // latches on hit so callers can tell "halted at breakpoint" from
    // "halted by stop()" without inspecting PC.
    bool     breakpointActive   = false;
    bool     breakpointTripped  = false;
    uint16_t breakpointAddress  = 0;

    uint8_t accumulator, xRegister, yRegister, statusRegister, stackPointer;
    int IRQ, NMI;
    uint16_t programCounter;
    uint16_t op;
    int tmp;
    int cycles;
    // `run()` polls this every loop iteration; `stop()` clears it. It is
    // written lock-free by EmulationController::stopCpu() (off the emulation
    // thread) to abort a slice already inside run() within one instruction,
    // so a Stop/Step click while free-running can't burn the rest of the
    // ~6000-cycle slice before the single step. Atomic to keep that
    // cross-thread read/write well-defined (relaxed: no other state is
    // published through it — the stateMutex still orders the actual stop).
    std::atomic<int> running;

    // DRAM refresh emulation (see setDramRefreshEnabled).
    bool     dramRefreshEnabled    = false;
    int      dramRefreshAccum      = 0;
    uint64_t dramRefreshStallCount = 0;

    void pushProgramCounter(void);
    void popProgramCounter(void);
    void handleIRQ(void);
    void handleNMI(void);
    void Imp(void);
    void Imm(void);
    void Zero(void);
    void ZeroX(void);
    void ZeroY(void);
    void Abs(void);
    void AbsX(void);
    void AbsY(void);
    void Ind(void);
    void IndZeroX(void);
    void IndZeroY(void);
    void Rel(void);
    void WAbsX(void);
    void WAbsY(void);
    void WIndZeroY(void);
    void setStatusRegisterNZ(unsigned char val);
    void LDA(void);
    void LDX(void);
    void LDY(void);
    void STA(void);
    void STX(void);
    void STY(void);
    void setFlagCarry(int val);
    void ADC(void);
    void setFlagBorrow(int val);
    void SBC(void);
    void CMP(void);
    void CPX(void);
    void CPY(void);
    void AND(void);
    void ORA(void);
    void EOR(void);
    void ASL(void);
    void ASL_A(void);
    void LSR(void);
    void LSR_A(void);
    void ROL(void);
    void ROL_A(void);
    void ROR(void);
    void ROR_A(void);
    void INC(void);
    void DEC(void);
    void INX(void);
    void INY(void);
    void DEX(void);
    void DEY(void);
    void BIT(void);
    void PHA(void);
    void PHP(void);
    void PLA(void);
    void PLP(void);
    void BRK(void);
    void RTI(void);
    void JMP(void);
    void RTS(void);
    void JSR(void);
    void branch(void);
    void BNE(void);
    void BEQ(void);
    void BVC(void);
    void BVS(void);
    void BCC(void);
    void BCS(void);
    void BPL(void);
    void BMI(void);
    void TAX(void);
    void TXA(void);
    void TAY(void);
    void TYA(void);
    void TXS(void);
    void TSX(void);
    void CLC(void);
    void SEC(void);
    void CLI(void);
    void SEI(void);
    void CLV(void);
    void CLD(void);
    void SED(void);
    void NOP(void);
    void Unoff(void);
    void Unoff1(void);
    void Unoff2(void);
    void Unoff3(void);
    void Hang(void);
    void executeOpcode(void);

    struct OpcodeEntry {
        void (M6502::*addrMode)();
        void (M6502::*operation)();
    };
    static const OpcodeEntry opcodeTable[256];




};

#endif // M6502_H
