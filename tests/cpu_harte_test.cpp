// Tom Harte "65x02 ProcessorTests" cycle-exact oracle for POM1's M6502 (Route B).
//
// Reads tests/cpu/harte_6502.bin (built by tools/gen_harte_fixture.py from
// github.com/SingleStepTests/ProcessorTests, 6502/v1 — real-hardware-validated,
// 100 cases for each of the 151 documented opcodes). For every case it seeds the
// CPU + RAM to the given initial state, executes ONE instruction, and asserts the
// final registers, the touched RAM bytes, AND the cycle count all match. This
// validates per-opcode cycle counts (incl. page-cross / branch-taken penalties)
// on top of the functional behaviour Klaus already covers.
//
// Asynchronous IRQ/NMI *line* timing (which Harte's opcode cases don't exercise)
// is covered by the companion Route-A test, cpu_cycle_count_test.cpp.

#include "M6502.h"
#include "Memory.h"
// Same peripheral headers the Klaus runner includes (Memory holds them).
#include "A1IO_RTC.h"
#include "CFFA1.h"
#include "MicroSD.h"
#include "SID.h"
#include "TMS9918.h"
#include "TerminalCard.h"
#include "WiFiModem.h"
#include "PR40Printer.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <utility>
#include <vector>

namespace {

struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;
    uint8_t  u8()  { if (p + 1 > end) { ok = false; return 0; } return *p++; }
    uint16_t u16() { if (p + 2 > end) { ok = false; return 0; } uint16_t v = uint16_t(p[0]) | (uint16_t(p[1]) << 8); p += 2; return v; }
    uint32_t u32() { if (p + 4 > end) { ok = false; return 0; } uint32_t v = uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); p += 4; return v; }
};

struct State {
    uint16_t pc = 0;
    uint8_t  s = 0, a = 0, x = 0, y = 0, p = 0;
    std::vector<std::pair<uint16_t, uint8_t>> ram;
};

State readState(Reader& r) {
    State st;
    st.pc = r.u16(); st.s = r.u8(); st.a = r.u8(); st.x = r.u8(); st.y = r.u8(); st.p = r.u8();
    uint16_t n = r.u16();
    st.ram.reserve(n);
    for (uint16_t i = 0; i < n && r.ok; ++i) { uint16_t a = r.u16(); uint8_t v = r.u8(); st.ram.emplace_back(a, v); }
    return st;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: test_cpu_harte <harte_6502.bin>\n"); return 2; }
    std::ifstream f(argv[1], std::ios::binary);
    if (!f) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    std::vector<uint8_t> blob((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (blob.size() < 8 || std::memcmp(blob.data(), "HRT1", 4) != 0) {
        std::fprintf(stderr, "bad fixture (magic/size)\n"); return 2;
    }

    Reader r{blob.data() + 4, blob.data() + blob.size()};
    const uint32_t count = r.u32();

    Memory memory;
    memory.setTestMode(true);
    memory.setWriteInRom(true);
    uint8_t* ram = memory.getMemoryPointerMutable();
    M6502 cpu(&memory);
    cpu.start();

    uint32_t pass = 0, fail = 0, funcFail = 0, cycOnlyFail = 0;
    int opFail[256] = {0}, opTotal[256] = {0}, opCycFail[256] = {0};
    int shown = 0;
    for (uint32_t i = 0; i < count && r.ok; ++i) {
        const uint8_t  op  = r.u8();
        const State    ini = readState(r);
        const State    fin = readState(r);
        const uint8_t  expCyc = r.u8();
        if (!r.ok) break;
        ++opTotal[op];

        for (const auto& e : ini.ram) ram[e.first] = e.second;
        cpu.setProgramCounter(ini.pc);
        cpu.setStackPointer(ini.s);
        cpu.setAccumulator(ini.a);
        cpu.setXRegister(ini.x);
        cpu.setYRegister(ini.y);
        cpu.setStatusRegister(ini.p);
        cpu.setIRQ(0);

        cpu.step();
        const int gotCyc = cpu.getCurrentInstructionCycles();

        bool regsGood = cpu.getProgramCounter() == fin.pc
                 && cpu.getStackPointer()   == fin.s
                 && cpu.getAccumulator()    == fin.a
                 && cpu.getXRegister()      == fin.x
                 && cpu.getYRegister()      == fin.y
                 && cpu.getStatusRegister() == fin.p;
        bool ramGood = true;
        for (const auto& e : fin.ram) if (ram[e.first] != e.second) { ramGood = false; break; }
        const bool funcGood = regsGood && ramGood;
        const bool cycGood  = (gotCyc == int(expCyc));

        if (funcGood && cycGood) { ++pass; continue; }
        ++fail;
        ++opFail[op];
        if (!funcGood) ++funcFail; else { ++cycOnlyFail; ++opCycFail[op]; }
        static bool opShown[256] = {false};
        if (!opShown[op]) {
            opShown[op] = true; ++shown;
            std::fprintf(stderr,
                "FAIL op=%02X %s pc %04X->%04X(exp %04X) a=%02X(%02X) x=%02X(%02X) y=%02X(%02X) "
                "s=%02X(%02X) p=%02X(%02X) cyc=%d(exp%d)\n",
                op, funcGood ? "[cyc]" : "[FUNC]",
                ini.pc, cpu.getProgramCounter(), fin.pc,
                cpu.getAccumulator(), fin.a, cpu.getXRegister(), fin.x, cpu.getYRegister(), fin.y,
                cpu.getStackPointer(), fin.s, cpu.getStatusRegister(), fin.p, gotCyc, int(expCyc));
        }
    }

    std::printf("Harte 65x02 oracle: %u passed, %u failed (of %u) — functional=%u, cycle-only=%u\n",
                pass, fail, count, funcFail, cycOnlyFail);
    if (fail) {
        std::printf("per-opcode mismatches (op: fail/total, cyc-only):\n");
        for (int o = 0; o < 256; ++o)
            if (opFail[o]) std::printf("  %02X: %d/%d  (cyc %d)\n", o, opFail[o], opTotal[o], opCycFail[o]);
    }
    return fail == 0 ? 0 : 1;
}
