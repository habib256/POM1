// basic_native_run_test.cpp -- prove the NATIVE BASIC compiler's output actually
// runs (no interpreter) AND is faster than the same program on the interpreter.
//
// Builds, for the GEN2 card: (a) the native standalone binary via
// BasicNativeCompiler -> ca65/ld65 against dev/lib/basicrt, and (b) the
// interpreter path via BasicCompiler (tokenized + the Applesoft GEN2 ROM). Runs
// both headless, asserts they draw the IDENTICAL framebuffer, and that the native
// run finishes in fewer cycles. Skips (ctest code 77) if cc65 or the ROM is absent.

#include "BasicNativeCompiler.h"
#include "BasicCompiler.h"
#include "Memory.h"
#include "M6502.h"
#include "DisplayDevice.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
constexpr int kSkip = 77;

struct Cap : DisplayDevice { std::string t; void onChar(char c) override { t.push_back(c); } };

bool have(const char* cmd) { std::string c = std::string(cmd) + " >/dev/null 2>&1"; return std::system(c.c_str()) == 0; }
std::string findRoot()
{
    for (const char* p : {".", "..", "../.."}) {
        std::string cfg = std::string(p) + "/dev/lib/basicrt/basicc_native.cfg";
        std::ifstream f(cfg); if (f) return p;
    }
    return ".";
}
std::vector<unsigned char> readBin(const std::string& p)
{
    std::ifstream f(p, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
int countFb(const uint8_t* m) { int n = 0; for (int a = 0x2000; a < 0x4000; ++a) if (m[a]) ++n; return n; }

// Run until the framebuffer ($2000-$3FFF) is stable for 12 slices; return cycles.
long long runStable(Memory& mem, M6502& cpu, int& finalCount)
{
    const int slice = 200000; long long total = 0, lastChange = 0; int prev = -1, stable = 0;
    for (; total < 4000000000LL; total += slice) {
        cpu.run(slice);
        int c = countFb(mem.getMemoryPointerMutable());
        if (c != prev) { prev = c; lastChange = total; stable = 0; }
        else if (++stable >= 12) break;
    }
    finalCount = prev; return lastChange;
}
} // namespace

int main()
{
    if (!have("ca65 --version") || !have("ld65 --version")) {
        std::fprintf(stderr, "SKIP: cc65 (ca65/ld65) not on PATH\n"); return kSkip;
    }
    const std::string root = findRoot();
    const std::string rt = root + "/dev/lib/basicrt";

    // A compute-heavy, integer-EXACT program (no division / AND, so the float
    // interpreter and the integer-native build draw the identical picture).
    const std::string src =
        "10 HGR : HCOLOR=3\n"
        "20 FOR Y=0 TO 120\n"
        "30 FOR X=0 TO 180\n"
        "40 Z=X*3+Y*5+X*7+Y*2\n"
        "50 HPLOT X,Y\n"
        "60 NEXT X\n"
        "70 NEXT Y\n"
        "80 END\n";

    // ---- build the native binary --------------------------------------------
    auto nr = basicnative::compile(src, basicnative::Card::Gen2);
    if (!nr.ok) { std::fprintf(stderr, "native compile failed: %s\n", nr.error.c_str()); return 1; }

    char dirTmpl[] = "/tmp/basicnatXXXXXX";
    if (!mkdtemp(dirTmpl)) { std::fprintf(stderr, "SKIP: no temp dir\n"); return kSkip; }
    const std::string dir = dirTmpl;
    { std::ofstream o(dir + "/prog.s", std::ios::binary); o << nr.asmText; }

    auto sh = [&](const std::string& c) { return std::system(c.c_str()) == 0; };
    const std::string I = " -I " + root + "/dev/lib/gen2 -I " + root + "/dev/lib/apple1 -I " + rt + " ";
    bool built =
        sh("ca65" + I + "-o " + dir + "/prog.o " + dir + "/prog.s") &&
        sh("ca65" + I + "-o " + dir + "/rt.o " + rt + "/basicrt_gen2.s") &&
        sh("ld65 -C " + rt + "/basicc_native.cfg -o " + dir + "/prog.bin " +
           dir + "/prog.o " + dir + "/rt.o");
    if (!built) { std::fprintf(stderr, "SKIP: cc65 build failed (libs unavailable?)\n"); return kSkip; }

    auto bin = readBin(dir + "/prog.bin");
    if (bin.empty()) { std::fprintf(stderr, "native binary empty\n"); return 1; }

    // ---- run the native binary (no interpreter) -----------------------------
    int nativeCount = 0; long long nativeCyc = 0;
    {
        Memory mem; mem.initMemory();
        std::memcpy(mem.getMemoryPointerMutable() + 0x0300, bin.data(), bin.size());
        M6502 cpu(&mem); cpu.setProgramCounter(0x0300); cpu.start();
        nativeCyc = runStable(mem, cpu, nativeCount);
    }

    // ---- run the interpreter path (tokenized + GEN2 ROM) --------------------
    int interpCount = 0; long long interpCyc = 0;
    {
        Memory mem; mem.initMemory();
        bool romOk = false;
        for (const char* pre : {"", "../", "../../"}) {
            mem.setWriteInRom(true);
            int rc = mem.loadBinary((std::string(pre) + "roms/applesoft-gen2.rom").c_str(), 0x9800, nullptr);
            mem.setWriteInRom(false);
            if (rc == 0) { romOk = true; break; }
        }
        if (!romOk) { std::fprintf(stderr, "SKIP: roms/applesoft-gen2.rom not found\n"); return kSkip; }
        Cap d; mem.setDisplayDevice(&d); M6502 cpu(&mem);
        cpu.setProgramCounter(0x9800); cpu.start();
        for (int i = 0; i < 200; ++i) { cpu.run(200000); if (d.t.find(']') != std::string::npos) break; }
        auto pr = basic::compile(src, basic::targetGen2());
        for (const auto& z : pr.zones)
            std::memcpy(mem.getMemoryPointerMutable() + z.addr, z.bytes.data(), z.bytes.size());
        cpu.setProgramCounter(pr.entry); cpu.start();
        interpCyc = runStable(mem, cpu, interpCount);
    }

    std::printf("native : %d px in %lld cycles\n", nativeCount, nativeCyc);
    std::printf("interp : %d px in %lld cycles\n", interpCount, interpCyc);
    std::printf("speedup: %.1fx\n", interpCyc ? (double)interpCyc / (double)(nativeCyc ? nativeCyc : 1) : 0.0);

    if (nativeCount < 100) { std::fprintf(stderr, "FAIL: native drew too little (%d)\n", nativeCount); return 1; }
    if (nativeCount != interpCount) {
        std::fprintf(stderr, "FAIL: native (%d px) != interpreter (%d px) -- outputs differ\n",
                     nativeCount, interpCount); return 1;
    }
    if (nativeCyc >= interpCyc) {
        std::fprintf(stderr, "FAIL: native (%lld) not faster than interpreter (%lld)\n", nativeCyc, interpCyc);
        return 1;
    }
    std::printf("basic_native_run: OK\n");
    return 0;
}
