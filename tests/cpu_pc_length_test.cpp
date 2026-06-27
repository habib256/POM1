// PC-advance oracle for POM1's undocumented (illegal) 6502 opcodes.
//
// The Harte suite (cpu_harte) is exhaustive on the 151 *documented* opcodes but
// — by construction (tools/gen_harte_fixture.py excludes them) — never touches
// the 105 illegal opcodes. This self-contained test pins the one property that
// matters for stream integrity: an undocumented multi-byte opcode advances PC
// by its real NMOS operand length, NOT the no-op 1-byte fallback that once
// desynced the instruction stream (CLAUDE.md: "Undocumented multi-byte opcodes
// advance PC by their real NMOS operand length ... never the no-op 1-byte
// fallback that desynced the stream").
//
// Cycles and semantics of illegal opcodes are explicitly OUT OF SCOPE — only
// the PC delta (instruction byte length) is asserted. The expected lengths are
// the canonical NMOS 6502 illegal-opcode lengths, encoded here independently of
// POM1's dispatch table (so a table regression is caught, not mirrored).
//
// The 12 JAM/KIL opcodes are special: POM1 deliberately models them as a hang
// (PC decremented back to the opcode so the CPU re-executes it forever), so
// their PC delta is 0 even though the canonical instruction length is 1.

#include "M6502.h"
#include "Memory.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr uint8_t FLAG_I = 0x04, FLAG_U = 0x20;

int g_oks = 0, g_fails = 0;
void check(bool cond, const char* msg, int opcode) {
    if (cond) { ++g_oks; return; }
    ++g_fails;
    std::fprintf(stderr, "FAIL [opcode $%02X]: %s\n", opcode, msg);
}

// Canonical NMOS illegal opcodes that JAM (lock the CPU). Length 1, but POM1
// models the hang → PC delta 0.
const std::vector<int> kJam = {
    0x02, 0x12, 0x22, 0x32, 0x42, 0x52, 0x62, 0x72,
    0x92, 0xB2, 0xD2, 0xF2,
};

// Undocumented 1-byte NOPs (implied).
const std::vector<int> kLen1 = {
    0x1A, 0x3A, 0x5A, 0x7A, 0xDA, 0xFA,
};

// Undocumented 2-byte opcodes: column-3 / column-7 (zp & indexed indirect
// SLO/RLA/SRE/RRA/SAX/LAX/DCP/ISC), the column-4 zp/zpx NOPs, the immediate
// illegals (80/82/89/C2/E2 NOP#, 0B/2B ANC#, 4B ALR#, 6B ARR#, 8B XAA#,
// AB LXA#, CB AXS#, EB SBC#).
const std::vector<int> kLen2 = {
    0x03, 0x04, 0x07, 0x0B, 0x13, 0x14, 0x17, 0x23, 0x27, 0x2B,
    0x33, 0x34, 0x37, 0x43, 0x44, 0x47, 0x4B, 0x53, 0x54, 0x57,
    0x63, 0x64, 0x67, 0x6B, 0x73, 0x74, 0x77, 0x80, 0x82, 0x83,
    0x87, 0x89, 0x8B, 0x93, 0x97, 0xA3, 0xA7, 0xAB, 0xB3, 0xB7,
    0xC2, 0xC3, 0xC7, 0xCB, 0xD3, 0xD4, 0xD7, 0xE2, 0xE3, 0xE7,
    0xEB, 0xF3, 0xF4, 0xF7,
};

// Undocumented 3-byte opcodes: column-C/column-F abs & abs,x/abs,y illegals
// (NOP abs, SLO/RLA/... abs,y, SHY/SHX/SHA/TAS/LAS, SAX/LAX abs).
const std::vector<int> kLen3 = {
    0x0C, 0x0F, 0x1B, 0x1C, 0x1F, 0x2F, 0x3B, 0x3C, 0x3F, 0x4F,
    0x5B, 0x5C, 0x5F, 0x6F, 0x7B, 0x7C, 0x7F, 0x8F, 0x9B, 0x9C,
    0x9E, 0x9F, 0xAF, 0xBB, 0xBF, 0xCF, 0xDB, 0xDC, 0xDF, 0xEF,
    0xFB, 0xFC, 0xFF,
};

// Run the single opcode sitting at $0200 from a clean boundary and return the
// PC delta. Interrupts are held off (NMI clear, I set, IRQ low) so step()
// executes exactly one instruction with no async entry.
int pcDelta(M6502& cpu, Memory& mem, int opcode) {
    constexpr uint16_t kBase = 0x0200;
    uint8_t* ram = mem.getMemoryPointerMutable();
    ram[kBase + 0] = static_cast<uint8_t>(opcode);
    ram[kBase + 1] = 0x00;   // harmless operand bytes
    ram[kBase + 2] = 0x00;
    cpu.setProgramCounter(kBase);
    cpu.setStackPointer(0xFF);
    cpu.setStatusRegister(FLAG_U | FLAG_I);
    cpu.setIRQ(0);
    cpu.step();
    return static_cast<int>(cpu.getProgramCounter()) - static_cast<int>(kBase);
}

} // namespace

int main() {
    Memory memory;
    memory.setTestMode(true);     // flat 64 KB, no MMIO
    memory.setWriteInRom(true);
    M6502 cpu(&memory);
    cpu.start();

    int tested = 0;

    for (int op : kJam) {
        check(pcDelta(cpu, memory, op) == 0, "JAM must hang in place (PC delta 0)", op);
        ++tested;
    }
    for (int op : kLen1) {
        check(pcDelta(cpu, memory, op) == 1, "1-byte illegal must advance PC by 1", op);
        ++tested;
    }
    for (int op : kLen2) {
        check(pcDelta(cpu, memory, op) == 2, "2-byte illegal must advance PC by 2", op);
        ++tested;
    }
    for (int op : kLen3) {
        check(pcDelta(cpu, memory, op) == 3, "3-byte illegal must advance PC by 3", op);
        ++tested;
    }

    // 256 total - 151 documented (Harte) = 105 illegal. Pins the documented/
    // illegal split too: a documented opcode demoted to a fallback (or vice
    // versa) changes this count.
    if (tested != 105) {
        std::fprintf(stderr, "FAIL: expected 105 illegal opcodes, enumerated %d\n", tested);
        ++g_fails;
    }

    std::printf("cpu_pc_length: %d checks passed, %d failed (%d illegal opcodes)\n",
                g_oks, g_fails, tested);
    return g_fails == 0 ? 0 : 1;
}
