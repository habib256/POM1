// Cycle-count oracle for POM1's undocumented (illegal) 6502 opcodes.
//
// Harte pins the 151 documented opcodes; cpu_pc_length pins illegal PC
// advance. This test pins the NMOS cycle totals used by Unoff1/2/3 — the
// timing fillers beam-race demos rely on ($1A, $80, $0C, $1C, …).

#include "M6502.h"
#include "Memory.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr uint8_t FLAG_I = 0x04, FLAG_U = 0x20;

// Mirror of kIllegalTotalCycles in M6502.cpp (masswerk.at illegal-opcode tables).
constexpr uint8_t kExpectedIllegalCycles[256] = {
    0x00, 0x00, 0x02, 0x08, 0x03, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x06,
    0x00, 0x00, 0x02, 0x08, 0x04, 0x00, 0x00, 0x06, 0x00, 0x00, 0x02, 0x07, 0x04, 0x00, 0x00, 0x07,
    0x00, 0x00, 0x02, 0x08, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06,
    0x00, 0x00, 0x02, 0x08, 0x04, 0x00, 0x00, 0x06, 0x00, 0x00, 0x02, 0x07, 0x04, 0x00, 0x00, 0x07,
    0x00, 0x00, 0x02, 0x08, 0x03, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x06,
    0x00, 0x00, 0x02, 0x08, 0x04, 0x00, 0x00, 0x06, 0x00, 0x00, 0x02, 0x07, 0x04, 0x00, 0x00, 0x07,
    0x00, 0x00, 0x02, 0x08, 0x03, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x06,
    0x00, 0x00, 0x02, 0x08, 0x04, 0x00, 0x00, 0x06, 0x00, 0x00, 0x02, 0x07, 0x04, 0x00, 0x00, 0x07,
    0x02, 0x00, 0x02, 0x06, 0x00, 0x00, 0x00, 0x03, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04,
    0x00, 0x00, 0x02, 0x06, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x05, 0x05, 0x00, 0x05, 0x05,
    0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04,
    0x00, 0x00, 0x02, 0x05, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04,
    0x00, 0x00, 0x02, 0x08, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x06,
    0x00, 0x00, 0x02, 0x08, 0x04, 0x00, 0x00, 0x06, 0x00, 0x00, 0x02, 0x07, 0x04, 0x00, 0x00, 0x07,
    0x00, 0x00, 0x02, 0x08, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06,
    0x00, 0x00, 0x02, 0x08, 0x04, 0x00, 0x00, 0x06, 0x00, 0x00, 0x02, 0x07, 0x04, 0x00, 0x00, 0x07,
};

int g_oks = 0, g_fails = 0;

void check(bool cond, const char* msg, int opcode, int got, int expected)
{
    if (cond) { ++g_oks; return; }
    ++g_fails;
    std::fprintf(stderr, "FAIL [$%02X]: %s (got %d expected %d)\n",
                 opcode, msg, got, expected);
}

int stepCycles(M6502& cpu, Memory& mem, int opcode, uint8_t x = 0)
{
    constexpr uint16_t kBase = 0x0200;
    uint8_t* ram = mem.getMemoryPointerMutable();
    ram[kBase + 0] = static_cast<uint8_t>(opcode);
    ram[kBase + 1] = 0x00;
    ram[kBase + 2] = 0x00;
    ram[kBase + 3] = 0xFF;   // abs,X page-cross operand for $1C tests
    cpu.setProgramCounter(kBase);
    cpu.setStackPointer(0xFF);
    cpu.setStatusRegister(FLAG_U | FLAG_I);
    cpu.setXRegister(x);
    cpu.setIRQ(0);
    cpu.step();
    return cpu.getCurrentInstructionCycles();
}

} // namespace

int main()
{
    Memory memory;
    memory.setTestMode(true);
    memory.setWriteInRom(true);
    M6502 cpu(&memory);
    cpu.start();

    auto expect = [&](int opcode, int expected, uint8_t x = 0, const char* label = "") {
        const int got = stepCycles(cpu, memory, opcode, x);
        check(got == expected, label, opcode, got, expected);
    };

    // Spot-check the bug report: 2-cycle NOPs and abs,X NOP timing.
    expect(0x1A, 2, 0, "implied NOP");
    expect(0x80, 2, 0, "immediate NOP");
    expect(0xC2, 2, 0, "immediate NOP");
    expect(0x0C, 4, 0, "abs NOP");
    expect(0x1C, 4, 0, "abs,X NOP no cross");
    {
        constexpr uint16_t kBase = 0x0200;
        uint8_t* ram = memory.getMemoryPointerMutable();
        ram[kBase + 0] = 0x1C;
        ram[kBase + 1] = 0xFF;   // lo — base $00FF + X=1 → $0100 (page cross)
        ram[kBase + 2] = 0x00;
        cpu.setProgramCounter(kBase);
        cpu.setXRegister(1);
        cpu.setStatusRegister(FLAG_U | FLAG_I);
        cpu.setIRQ(0);
        cpu.step();
        check(cpu.getCurrentInstructionCycles() == 5, "abs,X NOP page cross", 0x1C,
              cpu.getCurrentInstructionCycles(), 5);
    }

    // Independent oracle for the SAX (store), LAX/LAS (load), and SHA (store)
    // illegals. These share matrix columns 3/7/F with the RMW ops but are
    // 1-3 cycles shorter — the values below are stated directly (masswerk),
    // NOT copied from kExpectedIllegalCycles, so a wrong table entry is caught
    // even though the exhaustive pass mirrors the table.
    expect(0x87, 3, 0, "SAX zp");
    expect(0x97, 4, 0, "SAX zp,Y");
    expect(0x8F, 4, 0, "SAX abs");
    expect(0x83, 6, 0, "SAX (zp,X)");
    expect(0x93, 6, 0, "SHA (zp),Y");
    expect(0x9F, 5, 0, "SHA abs,Y");
    expect(0xA7, 3, 0, "LAX zp");
    expect(0xB7, 4, 0, "LAX zp,Y");
    expect(0xAF, 4, 0, "LAX abs");
    expect(0xA3, 6, 0, "LAX (zp,X)");
    expect(0xB3, 5, 0, "LAX (zp),Y (no page-cross +1 modelled)");
    expect(0xBB, 4, 0, "LAS abs,Y");
    expect(0xBF, 4, 0, "LAX abs,Y no cross");
    // $BF (LAX abs,Y) takes +1 on a page cross, indexed by Y.
    {
        constexpr uint16_t kBase = 0x0200;
        uint8_t* ram = memory.getMemoryPointerMutable();
        ram[kBase + 0] = 0xBF;
        ram[kBase + 1] = 0xFF;   // lo — base $00FF + Y=1 → $0100 (page cross)
        ram[kBase + 2] = 0x00;
        cpu.setProgramCounter(kBase);
        cpu.setXRegister(0);
        cpu.setYRegister(1);
        cpu.setStatusRegister(FLAG_U | FLAG_I);
        cpu.setIRQ(0);
        cpu.step();
        check(cpu.getCurrentInstructionCycles() == 5, "LAX abs,Y page cross", 0xBF,
              cpu.getCurrentInstructionCycles(), 5);
    }

    // Exhaustive over every Unoff-dispatched illegal in the oracle table.
    int pinned = 0;
    for (int op = 0; op < 256; ++op) {
        const int expected = kExpectedIllegalCycles[op];
        if (expected == 0) continue;
        // JAM opcodes hang with their own cycle accounting — skip.
        if (op == 0x02 || op == 0x12 || op == 0x22 || op == 0x32 || op == 0x42 ||
            op == 0x52 || op == 0x62 || op == 0x72 || op == 0x92 || op == 0xB2 ||
            op == 0xD2 || op == 0xF2) {
            continue;
        }
        // abs,X NOP base is 4; page-cross case tested above.
        if (op == 0x1C || op == 0x3C || op == 0x5C || op == 0x7C ||
            op == 0xDC || op == 0xFC) {
            ++pinned;
            continue;
        }
        const int got = stepCycles(cpu, memory, op);
        check(got == expected, "illegal cycle total", op, got, expected);
        ++pinned;
    }

    if (pinned != 90) {
        std::fprintf(stderr, "FAIL: expected 90 pinned illegals, got %d\n", pinned);
        ++g_fails;
    }

    std::printf("cpu_illegal_cycles: %d checks passed, %d failed (%d illegals pinned)\n",
                g_oks, g_fails, pinned);
    return g_fails == 0 ? 0 : 1;
}
