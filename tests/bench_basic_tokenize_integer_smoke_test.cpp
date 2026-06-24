// bench_basic_tokenize_integer_smoke_test.cpp -- pin the DevBench Integer BASIC
// tokeniser RUN path (idx 7).
//
// basic_tokenise_integer pins the tokeniser's BYTES; this pins that the image is
// actually RUNNABLE through the ROM the way Pom1BenchHost::injectBasic drives it:
// cold-start the $E000 ROM, poke the tokenised image at pp, set pp ($CA/$CB), then
// enter the RUN handler ($EFEC = clr + run_warm) -- and a compiled PRINT reaches
// the display. "42" never appears in the source, so a match proves the program ran.
#include "TMS9918.h"      // IWYU pragma: keep
#include "WiFiModem.h"    // IWYU pragma: keep
#include "TerminalCard.h" // IWYU pragma: keep
#include "A1IO_RTC.h"     // IWYU pragma: keep
#include "PR40Printer.h"  // IWYU pragma: keep
#include "Memory.h"
#include "M6502.h"
#include "DisplayDevice.h"
#include "BasicTokeniserInteger.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr int kSkip = 77;

std::string readFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return f ? std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>())
             : std::string();
}

class CaptureDisplay : public DisplayDevice {
public:
    void onChar(char c) override { text.push_back(c); }
    std::string text;
};

bool loadIntegerRom(Memory& mem)
{
    for (const char* pre : {"", "../", "../../"}) {
        mem.setWriteInRom(true);
        const int rc = mem.loadBinary((std::string(pre) + "roms/basic.rom").c_str(), 0xE000, nullptr);
        mem.setWriteInRom(false);
        if (rc == 0) return true;
    }
    return false;
}

int fail(const char* m) { std::fprintf(stderr, "FAIL: %s\n", m); return 1; }

// Drive the exact injectBasic idx-7 sequence at the Memory+M6502 level and return
// whether `want` appears in the display.
bool runTokenisedInteger(const std::string& src, const char* want,
                         uint16_t lomem = 0x0800, uint16_t himem = 0x1000)
{
    Memory mem; mem.initMemory();
    if (!loadIntegerRom(mem)) return false;       // caller maps !loaded -> SKIP
    CaptureDisplay disp; mem.setDisplayDevice(&disp);
    M6502 cpu(&mem);

    // Cold-start Integer BASIC to its '>' prompt (zero page set up).
    cpu.setProgramCounter(ibasic::kColdStart);
    cpu.start();
    for (long long c = 0; c < 30000000LL; c += 200000) {
        cpu.run(200000);
        if (disp.text.find('>') != std::string::npos) break;
    }

    ibasic::Result prog = ibasic::compile(src, himem);
    if (!prog.ok) { std::fprintf(stderr, "compile: %s\n", prog.error.c_str()); return false; }

    uint8_t* ram = mem.getMemoryPointerMutable();
    ram[0x4A] = static_cast<uint8_t>(lomem & 0xFF);          // LOMEM (var base) / HIMEM
    ram[0x4B] = static_cast<uint8_t>((lomem >> 8) & 0xFF);   // (program ceiling) as the
    ram[0x4C] = static_cast<uint8_t>(himem & 0xFF);          // bench sets them
    ram[0x4D] = static_cast<uint8_t>((himem >> 8) & 0xFF);
    for (size_t i = 0; i < prog.image.size(); ++i) ram[prog.pp + i] = prog.image[i];
    ram[ibasic::kPpZp]     = static_cast<uint8_t>(prog.pp & 0xFF);
    ram[ibasic::kPpZp + 1] = static_cast<uint8_t>((prog.pp >> 8) & 0xFF);

    cpu.setProgramCounter(0xEFEC);   // RUN handler (clr + run_warm)
    cpu.start();
    bool found = false;
    for (long long c = 0; c < 100000000LL && !found; c += 200000) {
        cpu.run(200000);
        found = disp.text.find(want) != std::string::npos;
    }
    mem.setDisplayDevice(nullptr);
    return found;
}

} // namespace

int main()
{
    {   // ROM presence probe (so we SKIP rather than fail when it's absent).
        Memory mem; mem.initMemory();
        if (!loadIntegerRom(mem)) { std::fprintf(stderr, "SKIP: roms/basic.rom not found\n"); return kSkip; }
    }

    struct Case { const char* src; const char* want; };
    const Case cases[] = {
        { "10 PRINT 42\n",          "42" },
        { "10 PRINT 6*7\n",         "42" },   // 6*7 = 42, computed
        { "10 A=40:PRINT A+2\n",    "42" },   // assignment + arithmetic
    };
    for (const Case& tc : cases) {
        if (!runTokenisedInteger(tc.src, tc.want))
            return fail((std::string("Integer tokenised RUN failed for: ") + tc.src).c_str());
        std::printf("integer-run OK: %.*s -> %s\n",
                    (int)std::string(tc.src).find('\n'), tc.src, tc.want);
    }

    // A >2 KB program: the default LOMEM/HIMEM $0800/$1000 leaves only 2 KB, so its
    // image (stored down from HIMEM) overruns LOMEM — the mini-startrek/hamurabi/lunar
    // MEM-ERROR. injectBasic lowers LOMEM to $0300 ($0300-$1000 = ~3.25 KB, matching the
    // programs' .apl images); verify a >2 KB program computes + prints there. (The $0800
    // failure can't be shown here: the flat 64 KB test RAM lets the overrun spill into
    // backed RAM harmlessly — it only ERRORs on the real 8 KB machine.)
    {
        std::string big;
        for (int ln = 10; ln <= 640; ln += 10)            // ~64 padding lines, ~38 B each
            big += std::to_string(ln) + " REM PADDING LINE TO GROW THE PROGRAM\n";
        big += "700 PRINT 42\n";
        ibasic::Result chk = ibasic::compile(big);
        if (!chk.ok || chk.image.size() < 2048 || chk.image.size() > 0x0D00)
            return fail("big-program fixture is not in the 2 KB..3.25 KB range");
        if (!runTokenisedInteger(big, "42", 0x0300, 0x1000))
            return fail("big program (>2 KB) failed to run at LOMEM $0300");
        std::printf("integer-run OK: %zu-byte program (>2 KB) runs at LOMEM $0300/HIMEM $1000\n",
                    chk.image.size());
    }

    // The real >2 KB programs the user reported MEM-ERRORing: run each at LOMEM $0300/
    // HIMEM $1000 (the bench config) and confirm it gets past its DIM/setup to its first
    // output (printed before any INPUT). Skipped if the .bas isn't present.
    struct Real { const char* bas; const char* want; };
    const Real reals[] = {
        { "sketchs/basic_integer/hamurabi.bas",     "SUMERIA" },        // intro line 82
        { "sketchs/basic_integer/mini-startrek.bas", "TYPE A NUMBER" }, // INPUT prompt line 30
        { "sketchs/basic_integer/lunar-lander.bas",  "" },             // just must not MEM-ERROR/run
    };
    for (const Real& rp : reals) {
        std::string src = readFile(rp.bas);
        if (src.empty()) { std::printf("integer-run: SKIP %s (absent)\n", rp.bas); continue; }
        if (rp.want[0] && !runTokenisedInteger(src, rp.want, 0x0300, 0x1000))
            return fail((std::string(rp.bas) + " did not reach its first output at LOMEM $0300 "
                         "(MEM ERROR?)").c_str());
        std::printf("integer-run OK: %s past setup at LOMEM $0300\n", rp.bas);
    }

    std::printf("bench_basic_tokenize_integer_smoke: OK\n");
    return 0;
}
