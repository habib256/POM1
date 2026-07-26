// Disassembler6502 — instruction-length cross-check against the CPU.
//
// Disassembler6502 had no dedicated test: symbols_smoke_test.cpp calls
// disassemble6502() 16 times but only ever looks at the returned string, never
// at `instrLen`. That leaves POM1's single most desync-prone invariant unpinned
// (CLAUDE.md › M6502):
//
//   "Undocumented multi-byte opcodes advance PC by their real NMOS operand
//    length (Unoff2/Unoff3 dispatch + matching disassembler addressing mode,
//    mnemonic ???) — never the no-op 1-byte fallback that desynced the stream."
//
// Two independent implementations encode that length: the CPU's dispatch (what
// PC actually does) and the disassembler's addressing-mode table (what the
// debugger shows). If they disagree, the Debug Console renders a byte stream
// shifted from the one being executed — every subsequent line is garbage, and
// nothing else in the suite notices. So rather than restate a length table here
// (which would just be a third copy to keep in sync), this test EXECUTES each
// opcode and compares the CPU's real PC advance against the disassembler's
// instrLen.
//
// Control-flow opcodes are excluded by design: for a taken branch, a jump or a
// return, PC advance is not the instruction length. Those are covered by the
// Klaus functional test and cpu_harte_smoke (cycle-exact per-opcode oracle).

#include "Disassembler6502.h"
#include "M6502.h"
#include "Memory.h"

// Memory forward-declares its cards via unique_ptr; instantiating one here
// needs the full types so the destructors are emitted.
#include "TMS9918.h"
#include "WiFiModem.h"
#include "TerminalCard.h"
#include "A1IO_RTC.h"
#include "PR40Printer.h"
#include "GT6144.h"
#include "JukeBox.h"
#include "CodeTank.h"
#include "CFFA1.h"
#include "MicroSD.h"
#include "SID.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>

namespace {

// Opcodes whose PC after execution is not "start + length".
bool isControlFlow(uint8_t op)
{
    switch (op) {
        case 0x4C: case 0x6C:              // JMP abs / JMP (ind)
        case 0x20:                          // JSR
        case 0x60: case 0x40:              // RTS / RTI
        case 0x00:                          // BRK (vector + pushes)
        case 0x10: case 0x30: case 0x50: case 0x70:   // BPL BMI BVC BVS
        case 0x90: case 0xB0: case 0xD0: case 0xF0:   // BCC BCS BNE BEQ
            return true;
        default:
            return false;
    }
}

// NMOS "KIL"/"JAM" undocumented opcodes wedge the CPU: PC does not advance at
// all on real silicon. They are 1-byte, but executing them proves nothing about
// length, so they are checked for length only, not by execution.
bool isJam(uint8_t op)
{
    switch (op) {
        case 0x02: case 0x12: case 0x22: case 0x32:
        case 0x42: case 0x52: case 0x62: case 0x72:
        case 0x92: case 0xB2: case 0xD2: case 0xF2:
            return true;
        default:
            return false;
    }
}

} // namespace

int main()
{
    Memory mem;
    M6502  cpu(&mem);
    // Flat 64 KB, no MMIO / ROM write-protect — we need to plant arbitrary
    // opcode bytes anywhere and read PC back cleanly.
    mem.setTestMode(true);
    cpu.start();

    // Operand bytes are chosen to keep every addressing mode inside RAM and
    // away from $D0xx (PIA) so nothing has a side effect that could disturb PC.
    constexpr uint16_t kAt = 0x0300;
    constexpr uint8_t  kOperandLo = 0x40;
    constexpr uint8_t  kOperandHi = 0x02;   // → $0240, plain RAM

    int checked = 0, lengthOnly = 0;
    for (int i = 0; i < 256; ++i) {
        const uint8_t op = static_cast<uint8_t>(i);

        // Plant the instruction plus two operand bytes.
        uint8_t* ram = mem.getMemoryPointerMutable();
        ram[kAt]     = op;
        ram[kAt + 1] = kOperandLo;
        ram[kAt + 2] = kOperandHi;

        int instrLen = 0;
        const std::string text = pom1::disassemble6502(mem.getMemoryPointer(), kAt, instrLen);

        // Every opcode must report a legal NMOS length. A 0 or >3 here means
        // the disassembler fell through its table.
        if (instrLen < 1 || instrLen > 3) {
            std::fprintf(stderr,
                "opcode $%02X: disassembler returned instrLen=%d (%s)\n",
                op, instrLen, text.c_str());
            return 1;
        }

        if (isControlFlow(op) || isJam(op)) {
            ++lengthOnly;
            continue;
        }

        // Execute exactly one instruction and see where PC lands.
        cpu.setProgramCounter(kAt);
        cpu.setStackPointer(0xFF);
        cpu.step();
        const uint16_t after = cpu.getProgramCounter();
        const int advanced = static_cast<int>(after) - static_cast<int>(kAt);

        if (advanced != instrLen) {
            std::fprintf(stderr,
                "opcode $%02X (%s): CPU advanced PC by %d byte(s) but the "
                "disassembler reports %d — the Debug Console would desync "
                "from the executed stream here\n",
                op, text.c_str(), advanced, instrLen);
            return 1;
        }
        ++checked;
    }

    // Guard against the check silently evaporating: if a future change made
    // isControlFlow()/isJam() over-broad, `checked` would collapse and the test
    // would still "pass" while verifying almost nothing.
    assert(checked + lengthOnly == 256);
    assert(checked >= 220 && "too many opcodes excluded — the cross-check is hollow");

    // Spot-pin the specific case the invariant is about: an undocumented
    // multi-byte opcode must NOT be treated as a 1-byte no-op. $0C is NMOS
    // "NOP abs" (3 bytes) and $04 is "NOP zp" (2 bytes); the historical bug
    // rendered both as 1 byte and shifted everything after them.
    {
        int len = 0;
        mem.getMemoryPointerMutable()[kAt] = 0x0C;
        pom1::disassemble6502(mem.getMemoryPointer(), kAt, len);
        assert(len == 3 && "undocumented $0C (NOP abs) must consume 3 bytes");

        mem.getMemoryPointerMutable()[kAt] = 0x04;
        pom1::disassemble6502(mem.getMemoryPointer(), kAt, len);
        assert(len == 2 && "undocumented $04 (NOP zp) must consume 2 bytes");
    }

    std::printf("disassembler_smoke: %d opcodes cross-checked against the CPU, "
                "%d length-only (control flow + JAM)\n", checked, lengthOnly);
    return 0;
}
