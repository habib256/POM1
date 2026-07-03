// bench_logo_inject_smoke_test.cpp -- pin the DevBench "LOGO" language path.
//
// The Bench's LOGO targets (Pom1BenchHost::injectLogo, mode 6) don't compile a
// memory image like Applesoft: an APPLE-1 LOGO procedure is stored as its RAW
// ASCII source in the interpreter's proc_table (the interpreter re-parses each body
// line at run time). LogoProgramLoader turns a listing into proc_table writes + one
// ENTRY line; injectLogo cold-starts the resident interpreter, pokes the table while
// the CPU is parked, feeds the entry line over the keyboard FIFO, and resumes.
//
// This test exercises that mechanism headlessly at the Memory + M6502 level, on BOTH
// LOGO cards, and asserts the TURTLE actually drew:
//   * LOGO TMS9918 — Codetank_GAME3.rom LOWER bank @ $4000 (cold 4000R), proc_table
//     $E431 / n_procs $0260, turtle into the Graphics-II pattern table (TMS VRAM).
//   * LOGO GEN2 HGR — roms/logo-gen2.rom @ $6000 (cold 6000R), proc_table $B431 /
//     n_procs $02E3, turtle into the HGR page-1 framebuffer ($2000-$3FFF RAM).
//
// A square is defined as a procedure (TO SQ … END) and CALLED via the entry line —
// so a lit framebuffer proves the interpreter FOUND the poked procedure (n_procs +
// proc_table addressing) and RAN it. The proc body itself never travels the keyboard.
//
// Pins (fail here on an interpreter relink that moves the RAM tables — same spirit as
// basic_compiler_smoke pinning the Applesoft cold-start entries):
//   proc_table $E431 (TMS) / $B431 (GEN2), n_procs $0260 / $02E3, cold $4000 / $6000.

#include "LogoProgramLoader.h"
#include "TMS9918.h"
#include "WiFiModem.h"    // IWYU pragma: keep
#include "TerminalCard.h" // IWYU pragma: keep
#include "A1IO_RTC.h"     // IWYU pragma: keep
#include "PR40Printer.h"  // IWYU pragma: keep
#include "Memory.h"
#include "M6502.h"
#include "DisplayDevice.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr int kSkip = 77;   // ctest SKIP_RETURN_CODE (ROM absent)

class NullDisplay : public DisplayDevice {
public:
    void onChar(char /*c*/) override {}
};

// Read a binary ROM, trying the build dir and one/two levels up (tests run from
// build/). Returns empty on miss.
std::vector<unsigned char> readRom(const std::string& path)
{
    for (const char* pre : {"", "../", "../../"}) {
        std::ifstream f(pre + path, std::ios::binary);
        if (f) return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)),
                                                 std::istreambuf_iterator<char>());
    }
    return {};
}

// Cold-start the resident interpreter: jump to `coldEntry` and run `cycles` so
// `main` sets up zero page, the turtle and the LFSR, zeroes n_procs, and parks in
// the REPL keyboard poll. Types nothing.
void coldStart(M6502& cpu, uint16_t coldEntry, long long cycles)
{
    cpu.setProgramCounter(coldEntry);
    cpu.start();
    const long long kSlice = 500000;
    for (long long c = 0; c < cycles; c += kSlice) cpu.run(kSlice);
}

// Apply the LogoProgramLoader writes straight into RAM (bypassing write-protect /
// OOR, exactly what EmulationController::writeMemoryBatch does under the hood while
// the CPU is parked), then feed the entry line over the keyboard FIFO.
void injectAndFeed(Memory& mem, const logo::Result& prog)
{
    uint8_t* ram = mem.getMemoryPointerMutable();
    for (const logo::Write& w : prog.writes) ram[w.addr] = w.value;
    for (char c : prog.entry) mem.setKeyPressed(c);
    if (!prog.entry.empty()) mem.setKeyPressed('\r');
}

int fail(const char* msg) { std::fprintf(stderr, "FAIL: %s\n", msg); return 1; }

// The listing: a rosette procedure (nested REPEAT, one level — the deepest the
// interpreter supports), called by name. The whole body is poked into proc_table;
// only the bare "MAIN" call is fed over the keyboard. Turns use TR (turn right) —
// this dialect has NO RT/LT. Drawing the rosette proves the interpreter FOUND the
// poked procedure, RAN it, and executed the nested REPEAT correctly.
const char* kRosette =
    "TO MAIN\n"
    "  CS\n"
    "  REPEAT 12 [ REPEAT 4 [ FD 40 TR 90 ] TR 30 ]\n"
    "END\n"
    "MAIN\n";

} // namespace

int main()
{
    // ---- LOGO TMS9918 (Codetank_GAME3.rom lower bank @ $4000) ---------------
    {
        logo::Target tgt = logo::targetTms();
        assert(tgt.coldEntry == 0x4000 && "LOGO TMS cold-start moved");
        assert(tgt.procTable == 0xE431 && "LOGO TMS proc_table moved (interpreter relink?)");
        assert(tgt.nProcs    == 0x0260 && "LOGO TMS n_procs moved (interpreter relink?)");

        logo::Result prog = logo::compile(kRosette, tgt);
        if (!prog.ok) return fail(("TMS LOGO parse: " + prog.error).c_str());

        std::vector<unsigned char> rom = readRom("roms/codetank/Codetank_GAME3.rom");
        if (rom.empty()) {
            std::fprintf(stderr, "SKIP: roms/codetank/Codetank_GAME3.rom not found\n");
            return kSkip;
        }
        Memory mem; mem.initMemory(); mem.setTMS9918Enabled(true);
        // Lower bank (jumper Lower16): file offset $0000-$3FFF -> $4000-$7FFF = LOGO.
        std::memcpy(mem.getMemoryPointerMutable() + 0x4000, rom.data(), 0x4000);
        // PROCBSS lives in the $E000-$EFFF Parmigiani high bank, which initMemory
        // seeded with Integer BASIC ROM. Make it plain RAM (zero + writable) so the
        // interpreter's proc_table + control-stack writes land, mirroring the live
        // 8 KB dual-bank machine (where $E000 is RAM, not ROM).
        std::memset(mem.getMemoryPointerMutable() + 0xE000, 0, 0x1000);
        mem.setWriteInRom(true);

        NullDisplay disp; mem.setDisplayDevice(&disp);
        M6502 cpu(&mem);
        coldStart(cpu, tgt.coldEntry, 20000000);
        if (mem.getMemoryPointerMutable()[tgt.nProcs] != 0)
            return fail("TMS: cold-start did not zero n_procs (interpreter never reached the REPL)");

        injectAndFeed(mem, prog);

        auto litVram = [&]() {
            TMS9918::Snapshot snap; mem.getTMS9918().copySnapshot(snap);
            int n = 0; for (int a = 0; a < 0x1800; ++a) if (snap.vram[a]) ++n; return n;
        };
        int lit = 0;
        const long long kBudget = 400000000, kSlice = 500000;
        for (long long c = 0; c < kBudget; c += kSlice) {
            cpu.run(kSlice);
            lit = litVram();
            if (lit >= 60) break;
        }
        mem.setDisplayDevice(nullptr);
        // Confirm the interpreter registered the poked procedure.
        if (mem.getMemoryPointerMutable()[tgt.nProcs] != 1)
            return fail("TMS: n_procs != 1 after injection (proc table not accepted)");
        if (lit < 60)
            return fail(("TMS: turtle drew too few VRAM pattern bytes (" +
                         std::to_string(lit) + " < 60) — proc not found/run, or nested REPEAT broke?").c_str());
        std::printf("logo-tms inject: OK (MAIN poked @ $E431, run via 'MAIN' -> %d VRAM bytes)\n", lit);
    }

    // ---- LOGO GEN2 HGR (roms/logo-gen2.rom @ $6000) -------------------------
    {
        logo::Target tgt = logo::targetGen2();
        assert(tgt.coldEntry == 0x6000 && "LOGO GEN2 cold-start moved");
        assert(tgt.procTable == 0xB431 && "LOGO GEN2 proc_table moved (interpreter relink?)");
        assert(tgt.nProcs    == 0x02E3 && "LOGO GEN2 n_procs moved (interpreter relink?)");

        logo::Result prog = logo::compile(kRosette, tgt);
        if (!prog.ok) return fail(("GEN2 LOGO parse: " + prog.error).c_str());

        std::vector<unsigned char> rom = readRom("roms/logo-gen2.rom");
        if (rom.empty()) {
            std::fprintf(stderr, "SKIP: roms/logo-gen2.rom not found\n");
            return kSkip;
        }
        // GEN2 card left unplugged (as in basic_compiler_smoke): the HGR page-1
        // framebuffer ($2000-$3FFF) is plain RAM we can inspect directly. The
        // interpreter ($6000-$AFFF) + PROCBSS ($B000) sit in ordinary RAM.
        Memory mem; mem.initMemory();
        std::memcpy(mem.getMemoryPointerMutable() + 0x6000, rom.data(),
                    std::min<size_t>(rom.size(), 0x5000));

        NullDisplay disp; mem.setDisplayDevice(&disp);
        M6502 cpu(&mem);
        coldStart(cpu, tgt.coldEntry, 20000000);
        if (mem.getMemoryPointerMutable()[tgt.nProcs] != 0)
            return fail("GEN2: cold-start did not zero n_procs (interpreter never reached the REPL)");

        injectAndFeed(mem, prog);

        auto litHgr = [&]() {
            uint8_t* ram = mem.getMemoryPointerMutable();
            int n = 0; for (int a = 0x2000; a < 0x4000; ++a) if (ram[a]) ++n; return n;
        };
        int lit = 0;
        const long long kBudget = 400000000, kSlice = 500000;
        for (long long c = 0; c < kBudget; c += kSlice) {
            cpu.run(kSlice);
            lit = litHgr();
            if (lit >= 60) break;
        }
        mem.setDisplayDevice(nullptr);
        if (mem.getMemoryPointerMutable()[tgt.nProcs] != 1)
            return fail("GEN2: n_procs != 1 after injection (proc table not accepted)");
        if (lit < 60)
            return fail(("GEN2: turtle drew too few HGR bytes (" +
                         std::to_string(lit) + " < 60) — proc not found/run, or nested REPEAT broke?").c_str());
        std::printf("logo-gen2 inject: OK (MAIN poked @ $B431, run via 'MAIN' -> %d HGR bytes)\n", lit);
    }

    std::printf("bench_logo_inject_smoke: OK\n");
    return 0;
}
