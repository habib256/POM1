// bench_basic_tokenize_smoke_test.cpp -- pin the DevBench "tokeniser" launch path
// for every Applesoft target (GEN2 $9800, TMS9918 $4000, microSD $6000, CFFA1 $E000).
//
// Pom1BenchHost::injectBasic routes the Applesoft targets through the host-side
// tokeniser (BasicTokeniserApplesoft) instead of keyboard injection: it compiles the listing
// into a $0801 program image + a $0280 launcher (basic::Result::hex, a Wozmon dump
// whose run address is the launcher), cold-starts the interpreter, then LOADS that
// hex via Memory::loadHexDump and jumps to the launcher.
//
// basic_compiler_smoke pins the compiler + the direct memcpy+jump run for GEN2/TMS.
// THIS test pins the exact data path the Bench wiring uses -- round-trip the
// compiled `hex` through loadHexDump and run from the returned address -- AND that
// each per-card target's SETPTRS/NEWSTT entry points are correct at runtime (a
// wrong address makes the launcher crash, so reaching real output proves them):
//   * GEN2 ($9800)   -> a compiled HPLOT lights the hi-res page-2 framebuffer
//   * microSD ($6000) and CFFA1 ($E000) -> a compiled PRINT 6*7 reaches the display
//
// Memory.h forward-declares card types via unique_ptr; pull in the full
// definitions so Memory's destructor is emitted (as the sibling smoke tests do).
#include "TMS9918.h"      // IWYU pragma: keep
#include "WiFiModem.h"    // IWYU pragma: keep
#include "TerminalCard.h" // IWYU pragma: keep
#include "A1IO_RTC.h"     // IWYU pragma: keep
#include "PR40Printer.h"  // IWYU pragma: keep
#include "Memory.h"
#include "M6502.h"
#include "DisplayDevice.h"
#include "BasicTokeniserApplesoft.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr int kSkip = 77;   // ctest SKIP_RETURN_CODE (ROM absent)

class CaptureDisplay : public DisplayDevice {
public:
    void onChar(char c) override { text.push_back(c); }
    std::string text;
};

bool loadRomAt(Memory& mem, const std::string& path, uint16_t addr)
{
    for (const char* pre : {"", "../", "../../"}) {
        mem.setWriteInRom(true);
        const int rc = mem.loadBinary((std::string(pre) + path).c_str(), addr, nullptr);
        mem.setWriteInRom(false);
        if (rc == 0) return true;
    }
    return false;
}

// Cold-start the resident interpreter (PC=coldEntry, run to the `]` prompt), then
// load the compiled image via Memory::loadHexDump and jump to its run address --
// exactly the sequence Pom1BenchHost::injectBasic drives (runFromSync +
// EmulationController::loadHexDump). Returns false on a setup problem.
bool coldStartAndLaunch(Memory& mem, M6502& cpu, CaptureDisplay& disp,
                        uint16_t coldEntry, const basic::Result& prog)
{
    namespace fs = std::filesystem;
    cpu.setProgramCounter(coldEntry);
    cpu.start();
    for (long long c = 0; c < 40000000LL; c += 200000) {
        cpu.run(200000);
        if (disp.text.find(']') != std::string::npos) break;
    }
    if (disp.text.find(']') == std::string::npos) return false;   // never reached `]`

    const fs::path tmp = fs::temp_directory_path() / "pom1_bench_tokenize_test.txt";
    { std::ofstream o(tmp, std::ios::binary);
      o.write(prog.hex.data(), static_cast<std::streamsize>(prog.hex.size())); }
    uint16_t runAddr = 0; int loaded = 0;
    const int rc = mem.loadHexDump(tmp.string().c_str(), runAddr, &loaded, nullptr);
    std::error_code ec; fs::remove(tmp, ec);
    if (rc != 0 || runAddr != prog.entry || loaded <= 0) return false;

    cpu.setProgramCounter(runAddr);   // jump to the launcher (preserves the cold ZP)
    cpu.start();
    return true;
}

int fail(const char* msg) { std::fprintf(stderr, "FAIL: %s\n", msg); return 1; }

} // namespace

int main()
{
    // ----- GEN2 HGR ($9800): a compiled HPLOT must light hi-res page 2 -----------
    {
        const std::string src = "10 HGR2\n20 HCOLOR=3\n30 HPLOT 0,0 TO 250,180\n";
        basic::Result prog = basic::compile(src, basic::targetGen2());
        if (!prog.ok) return fail(("GEN2 compile: " + prog.error).c_str());

        Memory mem; mem.initMemory();
        if (!loadRomAt(mem, "roms/applesoft-gen2.rom", 0x9800)) {
            std::fprintf(stderr, "SKIP: roms/applesoft-gen2.rom not found\n");
            return kSkip;
        }
        CaptureDisplay disp; mem.setDisplayDevice(&disp);
        M6502 cpu(&mem);
        if (!coldStartAndLaunch(mem, cpu, disp, 0x9800, prog))
            return fail("GEN2: cold-start/loadHexDump/launch failed");

        const uint8_t* m = mem.getMemoryPointer();
        int lit = 0;
        for (long long c = 0; c < 200000000LL; c += 200000) {
            cpu.run(200000);
            lit = 0; for (int a = 0x4000; a < 0x6000; ++a) if (m[a]) ++lit;
            if (lit >= 10) break;
        }
        mem.setDisplayDevice(nullptr);
        if (lit < 10) return fail("GEN2: compiled HPLOT drew too few page-2 bytes");
        std::printf("GEN2: OK (%d hi-res page-2 bytes via tokeniser load+launch)\n", lit);
    }

    // ----- text Applesoft (microSD $6000, CFFA1 $E000): compiled PRINT reaches the
    //       display. "10 PRINT 6*7" -> "42" never appears in the source, so a match
    //       proves the launcher ran the program through the interpreter. -----------
    struct TextCase { const char* name; const char* rom; uint16_t romAddr, cold; basic::Target tgt; };
    const TextCase cases[] = {
        { "microSD", "roms/applesoft-lite-microsd.rom", 0x6000, 0x6000, basic::targetMicrosd() },
        { "CFFA1",   "roms/applesoft-lite-cffa1.rom",   0xE000, 0xE000, basic::targetCffa1()   },
    };
    for (const TextCase& tc : cases) {
        basic::Result prog = basic::compile("10 PRINT 6*7\n", tc.tgt);
        if (!prog.ok) return fail((std::string(tc.name) + " compile: " + prog.error).c_str());

        Memory mem; mem.initMemory();
        if (!loadRomAt(mem, tc.rom, tc.romAddr)) {
            std::fprintf(stderr, "SKIP: %s not found\n", tc.rom);
            return kSkip;
        }
        CaptureDisplay disp; mem.setDisplayDevice(&disp);
        M6502 cpu(&mem);
        if (!coldStartAndLaunch(mem, cpu, disp, tc.cold, prog))
            return fail((std::string(tc.name) + ": cold-start/loadHexDump/launch failed").c_str());

        bool found = false;
        for (long long c = 0; c < 200000000LL && !found; c += 200000) {
            cpu.run(200000);
            found = disp.text.find("42") != std::string::npos;
        }
        mem.setDisplayDevice(nullptr);
        if (!found)
            return fail((std::string(tc.name) + ": compiled PRINT 6*7 never produced 42 "
                         "(tokeniser dialect or SETPTRS/NEWSTT wrong?)").c_str());
        std::printf("%s: OK (compiled PRINT 6*7 -> 42 via tokeniser, SETPTRS=$%04X NEWSTT=$%04X)\n",
                    tc.name, tc.tgt.setptrs, tc.tgt.newstt);
    }

    std::printf("bench_basic_tokenize_smoke: OK\n");
    return 0;
}
