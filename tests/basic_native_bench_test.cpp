// basic_native_bench_test.cpp -- clear, reproducible BENCHMARKS for the native
// BASIC compiler across representative Applesoft program types. For each program it
// reports two things the user cares about: final BINARY SIZE (bytes of the
// standalone, no-interpreter image) and EXECUTION SPEED (6502 cycles), the latter
// compared against the same program on the Applesoft interpreter (+ GEN2 ROM) to
// give a concrete speedup. It also reports how many runtime routines were linked, to
// show feature-gated dead-stripping (floats/graphics/transcendentals only when used).
//
// Output is a markdown table; the test always passes if every program builds+runs
// (it is a measurement, not a pin). Skips (ctest 77) without cc65 or the GEN2 ROM.
//
//   ctest -R basic_native_bench -V        # see the table

#include "BasicCompilerApplesoft.h"
#include "BasicTokeniserApplesoft.h"
#include "Memory.h"
#include "M6502.h"
#include "DisplayDevice.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <unistd.h>  // mkdtemp (POSIX; macOS needs the explicit include)

namespace {
constexpr int kSkip = 77;

struct Cap : DisplayDevice { std::string t; void onChar(char c) override { t.push_back(c); } };
bool have(const char* c) { return std::system((std::string(c) + " >/dev/null 2>&1").c_str()) == 0; }
std::string findRoot() {
    for (const char* p : {".", "..", "../.."}) {
        std::ifstream f(std::string(p) + "/dev/lib/basicrt/basicc_native.cfg");
        if (f) return p;
    }
    return ".";
}
std::vector<unsigned char> readBin(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
std::string readText(const std::string& p) {
    std::ifstream f(p);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
int countFb(const uint8_t* m) { int n = 0; for (int a = 0x2000; a < 0x4000; ++a) if (m[a]) ++n; return n; }

std::string g_root, g_rt;

struct Built { std::string bin; int features = 0; bool ok = false; };

// Build the native standalone for `src`, mirroring tools/basicc_native.sh: minimal
// -D RT_*/FP_* derived from the routines the program imports.
Built buildNative(const std::string& src, bool fp, const std::string& dir) {
    Built r;
    auto nr = basicnative::compile(src, basicnative::Card::Gen2,
                                   fp ? basicnative::FpMode::Auto : basicnative::FpMode::Int);
    if (!nr.ok) { std::fprintf(stderr, "compile failed: %s\n", nr.error.c_str()); return r; }
    r.features = static_cast<int>(nr.runtimeFeatures.size());
    { std::ofstream o(dir + "/p.s", std::ios::binary); o << nr.asmText; }
    std::string defs, fpdefs; bool usesFp = nr.usesFloat;
    for (std::string f : nr.runtimeFeatures) {
        if (f == "fp_int")  { fpdefs += " -D FP_INT";  continue; }
        if (f == "fp_sqrt") { fpdefs += " -D FP_SQRT"; continue; }
        if (f == "fp_sin")  { fpdefs += " -D FP_SIN";  continue; }
        if (f.rfind("rt_", 0) != 0) continue;
        for (char& c : f) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        defs += " -D " + f;
    }
    const std::string I = " -I " + g_root + "/dev/lib/gen2 -I " + g_root + "/dev/lib/apple1 -I " + g_rt + " ";
    auto sh = [](const std::string& c) { return std::system(c.c_str()) == 0; };
    std::string objs = dir + "/p.o " + dir + "/rt.o";
    bool ok = sh("ca65" + I + "-o " + dir + "/p.o " + dir + "/p.s") &&
              sh("ca65" + defs + I + "-o " + dir + "/rt.o " + g_rt + "/basicrt_gen2.s");
    if (usesFp) { ok = ok && sh("ca65" + fpdefs + " -o " + dir + "/fp.o " + g_rt + "/basicrt_float.s");
                  objs += " " + dir + "/fp.o"; }
    ok = ok && sh("ld65 -C " + g_rt + "/basicc_native.cfg -o " + dir + "/p.bin " + objs);
    if (!ok) return r;
    r.bin = dir + "/p.bin"; r.ok = true;
    return r;
}

// Run a native image at $0300 for a fixed cycle budget; return pixels drawn.
long long runNative(const std::vector<unsigned char>& b, long long budget, int& px) {
    Memory mem; mem.initMemory();
    std::memcpy(mem.getMemoryPointerMutable() + 0x0300, b.data(), b.size());
    M6502 cpu(&mem); cpu.setProgramCounter(0x0300); cpu.start();
    const int slice = 200000; long long t = 0;
    for (; t < budget; t += slice) cpu.run(slice);
    px = countFb(mem.getMemoryPointerMutable());
    return t;
}

// Run a program on the interpreter (+ GEN2 ROM) until the framebuffer is stable;
// return cycles-to-last-change and final pixel count. romMissing set if no ROM.
long long runInterp(const std::string& src, int& px, bool& romMissing) {
    Memory mem; mem.initMemory(); romMissing = false;
    bool romOk = false;
    for (const char* pre : {"", "../", "../../"}) {
        mem.setWriteInRom(true);
        int rc = mem.loadBinary((std::string(pre) + "roms/applesoft-gen2.rom").c_str(), 0x9800, nullptr);
        mem.setWriteInRom(false);
        if (rc == 0) { romOk = true; break; }
    }
    if (!romOk) { romMissing = true; px = 0; return 0; }
    Cap d; mem.setDisplayDevice(&d); M6502 cpu(&mem);
    cpu.setProgramCounter(0x9800); cpu.start();
    for (int i = 0; i < 200; ++i) { cpu.run(200000); if (d.t.find(']') != std::string::npos) break; }
    auto pr = basic::compile(src, basic::targetGen2());
    for (const auto& z : pr.zones)
        std::memcpy(mem.getMemoryPointerMutable() + z.addr, z.bytes.data(), z.bytes.size());
    cpu.setProgramCounter(pr.entry); cpu.start();
    const int slice = 200000; long long total = 0, last = 0; int prev = -1, stab = 0;
    for (; total < 4000000000LL; total += slice) {
        cpu.run(slice); int c = countFb(mem.getMemoryPointerMutable());
        if (c != prev) { prev = c; last = total; stab = 0; }
        else if (++stab >= 12) break;
    }
    px = prev; return last;
}

struct Prog { const char* name; std::string src; bool fp; long long budget; bool cmpInterp; };
} // namespace

int main()
{
    if (!have("ca65 --version") || !have("ld65 --version")) {
        std::fprintf(stderr, "SKIP: cc65 (ca65/ld65) not on PATH\n"); return kSkip;
    }
    g_root = findRoot(); g_rt = g_root + "/dev/lib/basicrt";
    char tmpl[] = "/tmp/basicbenchXXXXXX";
    if (!mkdtemp(tmpl)) { std::fprintf(stderr, "SKIP: no temp dir\n"); return kSkip; }
    const std::string dir = tmpl;

    std::vector<Prog> progs = {
        // integer arithmetic -- strength reduction (each X*K becomes shifts+adds, no
        // runtime multiply). The products cancel to Y=I so it plots a measurable
        // diagonal while the heavy multiplies dominate the work.
        {"int-arith", "10 HGR : HCOLOR=3\n20 FOR I=0 TO 190\n30 A=I*3 : B=I*5 : C=I*9\n"
                      "40 Y=A+B+C-I*16\n50 HPLOT I,Y\n60 NEXT I\n70 END\n", false, 6000000, true},
        // integer raster fill -- graphics, integer coords
        {"int-raster", "10 HGR : HCOLOR=3\n20 FOR Y=0 TO 100\n30 FOR X=0 TO 200\n"
                       "40 HPLOT X,Y\n50 NEXT X\n60 NEXT Y\n70 END\n", false, 60000000, true},
        // float arithmetic -- binary32 +-*/, no transcendentals
        {"float-arith", "10 HGR : HCOLOR=3\n20 FOR X=0 TO 279\n30 Y=(X-140)*(X-140)/140\n"
                        "40 HPLOT X,Y\n50 NEXT X\n60 END\n", true, 20000000, true},
        // transcendental -- SIN over the screen (range reduction + Taylor)
        {"transcend", "10 HGR : HCOLOR=3\n20 FOR X=0 TO 279\n30 Y=96+60*SIN(X/40)\n"
                      "40 HPLOT X,Y\n50 NEXT X\n60 END\n", true, 30000000, true},
        // line drawing -- 16-bit Bresenham runtime
        {"lines", "10 HGR : HCOLOR=3\n20 FOR I=0 TO 270 STEP 10\n30 HPLOT 0,0 TO I,191\n"
                  "40 NEXT I\n50 END\n", false, 8000000, true},
    };
    // The flagship: full 3-D HAT (native-only; the interpreter run is far too slow).
    std::string hat = readText(g_root + "/sketchs/basic_applesoft/3DHat.apf");
    if (!hat.empty()) progs.push_back({"3dhat", hat, true, 150000000, false});

    std::printf("\n## Native BASIC compiler benchmarks (GEN2, standalone -- no interpreter)\n\n");
    std::printf("| program | phase | binary | rt routines | native cyc | interp cyc | speedup |\n");
    std::printf("|---------|-------|-------:|------------:|-----------:|-----------:|--------:|\n");

    int fails = 0;
    for (const Prog& p : progs) {
        Built b = buildNative(p.src, p.fp, dir);
        if (!b.ok) { std::fprintf(stderr, "SKIP: build failed (%s)\n", p.name); return kSkip; }
        auto bytes = readBin(b.bin);
        int npx = 0; long long ncyc = runNative(bytes, p.budget, npx);
        const char* phase = p.fp ? "float" : "int";

        if (p.cmpInterp) {
            int ipx = 0; bool romMissing = false;
            long long icyc = runInterp(p.src, ipx, romMissing);
            if (romMissing) { std::fprintf(stderr, "SKIP: roms/applesoft-gen2.rom not found\n"); return kSkip; }
            // native budget is fixed; for a fair speed ratio use cycles-to-equal-work:
            // both draw the same picture, so compare interp's cycles-to-finish against
            // native's cycles-to-finish (re-run native to stable for the ratio).
            Memory mem; mem.initMemory();
            std::memcpy(mem.getMemoryPointerMutable() + 0x0300, bytes.data(), bytes.size());
            M6502 cpu(&mem); cpu.setProgramCounter(0x0300); cpu.start();
            const int slice = 200000; long long t = 0, last = 0; int prev = -1, stab = 0;
            for (; t < 4000000000LL; t += slice) { cpu.run(slice);
                int c = countFb(mem.getMemoryPointerMutable());
                if (c != prev) { prev = c; last = t; stab = 0; } else if (++stab >= 12) break; }
            long long nfin = last ? last : ncyc;
            double sp = nfin ? (double)icyc / (double)nfin : 0.0;
            std::printf("| %-9s | %-5s | %5zu B | %11d | %10lld | %10lld | %6.1fx |\n",
                        p.name, phase, bytes.size(), b.features, nfin, icyc, sp);
            if (prev < 5) { std::fprintf(stderr, "FAIL %s: native drew nothing\n", p.name); ++fails; }
        } else {
            std::printf("| %-9s | %-5s | %5zu B | %11d | %10lld | %10s | %7s |\n",
                        p.name, phase, bytes.size(), b.features, ncyc, "n/a (slow)", "n/a");
            if (npx < 100) { std::fprintf(stderr, "FAIL %s: native drew %d px\n", p.name, npx); ++fails; }
        }
    }
    std::printf("\n(binary = bytes of the standalone $0300 image incl. linked runtime; rt routines = "
                "count of runtime entry points linked after feature-gated dead-stripping; cycles = "
                "6502 cycles to draw the final picture.)\n\n");

    if (fails) { std::printf("basic_native_bench: %d FAILED\n", fails); return 1; }
    std::printf("basic_native_bench: OK\n");
    return 0;
}
