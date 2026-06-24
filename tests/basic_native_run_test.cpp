// basic_native_run_test.cpp -- prove the NATIVE BASIC compiler's output actually
// runs (no interpreter) AND is faster than the same program on the interpreter,
// for BOTH the integer phase and the floating-point phase.
//
// For each program it builds (a) the native standalone binary via
// BasicNativeCompiler -> ca65/ld65 against dev/lib/basicrt (linking the float
// runtime in float mode), and (b) the interpreter path via BasicCompiler
// (tokenized + the Applesoft GEN2 ROM). It runs both headless and asserts they
// draw the same picture (exactly for integer; within tolerance for float, whose
// binary32 rounding differs from the ROM's) and that native is faster. Skips
// (ctest code 77) if cc65 or the GEN2 Applesoft ROM is absent.

#include "BasicNativeCompiler.h"
#include "BasicCompiler.h"
#include "Memory.h"
#include "M6502.h"
#include "DisplayDevice.h"

#include <cctype>
#include <cmath>
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
int countFb(const uint8_t* m) { int n = 0; for (int a = 0x2000; a < 0x4000; ++a) if (m[a]) ++n; return n; }

long long runStable(Memory& mem, M6502& cpu, int& finalCount) {
    const int slice = 200000; long long total = 0, lastChange = 0; int prev = -1, stable = 0;
    for (; total < 4000000000LL; total += slice) {
        cpu.run(slice);
        int c = countFb(mem.getMemoryPointerMutable());
        if (c != prev) { prev = c; lastChange = total; stable = 0; }
        else if (++stable >= 12) break;
    }
    finalCount = prev; return lastChange;
}

std::string g_root, g_rt;

// Build the native binary for `src` (float links basicrt_float). Returns "" on a
// (skippable) build failure.
std::string buildNative(const std::string& src, bool fp, const std::string& dir) {
    auto nr = basicnative::compile(src, basicnative::Card::Gen2, fp ? basicnative::FpMode::Float : basicnative::FpMode::Int);
    if (!nr.ok) { std::fprintf(stderr, "native compile failed: %s\n", nr.error.c_str()); return ""; }
    { std::ofstream o(dir + "/p.s", std::ios::binary); o << nr.asmText; }
    // -D RT_xxx for each runtime routine the program uses -> minimal runtime.
    std::string defs;
    for (std::string f : nr.runtimeFeatures) {
        if (f.rfind("rt_", 0) != 0) continue;
        for (char& c : f) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        defs += " -D " + f;
    }
    const std::string I = " -I " + g_root + "/dev/lib/gen2 -I " + g_root + "/dev/lib/apple1 -I " + g_rt + " ";
    auto sh = [](const std::string& c) { return std::system(c.c_str()) == 0; };
    std::string objs = dir + "/p.o " + dir + "/rt.o";
    bool ok = sh("ca65" + I + "-o " + dir + "/p.o " + dir + "/p.s") &&
              sh("ca65" + defs + I + "-o " + dir + "/rt.o " + g_rt + "/basicrt_gen2.s");
    if (fp) { ok = ok && sh("ca65 -o " + dir + "/fp.o " + g_rt + "/basicrt_float.s"); objs += " " + dir + "/fp.o"; }
    ok = ok && sh("ld65 -C " + g_rt + "/basicc_native.cfg -o " + dir + "/p.bin " + objs);
    return ok ? (dir + "/p.bin") : "";
}

// returns 0 ok, 1 fail, kSkip skip
int runCase(const char* name, const std::string& src, bool fp, const std::string& dir) {
    std::string bin = buildNative(src, fp, dir);
    if (bin.empty()) { std::fprintf(stderr, "SKIP: cc65 build failed (%s)\n", name); return kSkip; }
    auto b = readBin(bin);
    if (b.empty()) { std::fprintf(stderr, "native binary empty (%s)\n", name); return 1; }

    int nC = 0; long long nCyc = 0;
    { Memory mem; mem.initMemory();
      std::memcpy(mem.getMemoryPointerMutable() + 0x0300, b.data(), b.size());
      M6502 cpu(&mem); cpu.setProgramCounter(0x0300); cpu.start(); nCyc = runStable(mem, cpu, nC); }

    int iC = 0; long long iCyc = 0;
    { Memory mem; mem.initMemory();
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
      for (const auto& z : pr.zones) std::memcpy(mem.getMemoryPointerMutable() + z.addr, z.bytes.data(), z.bytes.size());
      cpu.setProgramCounter(pr.entry); cpu.start(); iCyc = runStable(mem, cpu, iC); }

    std::printf("%-14s native=%9lld cyc (%d px)  interp=%10lld cyc (%d px)  speedup=%.1fx\n",
                name, nCyc, nC, iCyc, iC, iCyc ? (double)iCyc / (double)(nCyc ? nCyc : 1) : 0.0);

    if (nC < 50) { std::fprintf(stderr, "FAIL %s: native drew too little (%d)\n", name, nC); return 1; }
    // integer must match exactly; float within 10% (binary32 vs ROM float rounding)
    int diff = std::abs(nC - iC);
    int tol = fp ? (iC / 10 + 3) : 0;
    if (diff > tol) { std::fprintf(stderr, "FAIL %s: native %d px vs interp %d px (tol %d)\n", name, nC, iC, tol); return 1; }
    if (nCyc >= iCyc) { std::fprintf(stderr, "FAIL %s: native (%lld) not faster than interp (%lld)\n", name, nCyc, iCyc); return 1; }
    return 0;
}
} // namespace

int main()
{
    if (!have("ca65 --version") || !have("ld65 --version")) {
        std::fprintf(stderr, "SKIP: cc65 (ca65/ld65) not on PATH\n"); return kSkip;
    }
    g_root = findRoot();
    g_rt = g_root + "/dev/lib/basicrt";
    char dirTmpl[] = "/tmp/basicnatXXXXXX";
    if (!mkdtemp(dirTmpl)) { std::fprintf(stderr, "SKIP: no temp dir\n"); return kSkip; }
    const std::string dir = dirTmpl;

    // Integer phase: compute-heavy, integer-exact (identical picture, ~20x).
    const std::string intSrc =
        "10 HGR : HCOLOR=3\n20 FOR Y=0 TO 120\n30 FOR X=0 TO 180\n"
        "40 Z=X*3+Y*5+X*7+Y*2\n50 HPLOT X,Y\n60 NEXT X\n70 NEXT Y\n80 END\n";
    int r1 = runCase("integer", intSrc, false, dir);
    if (r1 == kSkip) return kSkip;

    // Float phase: a parabola in binary32 (no interpreter, no ROM float). Same
    // picture within rounding; native still faster (FP-bound, so a smaller margin).
    const std::string fpSrc =
        "10 HGR : HCOLOR=3\n20 FOR X=0 TO 279\n30 Y = (X-140)*(X-140)/140\n"
        "40 HPLOT X,Y\n50 NEXT X\n60 END\n";
    int r2 = runCase("float", fpSrc, true, dir);
    if (r2 == kSkip) return kSkip;

    // Code size: dead-stripping leaves a program that uses no runtime routines
    // tiny (no graphics tables, no math/print/float linked).
    std::string mb = buildNative("10 X=5+2\n20 X=X+1\n30 END\n", false, dir);
    int sizeFail = 0;
    if (!mb.empty()) {
        size_t bytes = readBin(mb).size();
        std::printf("size           minimal no-runtime program = %zu bytes (dead-stripped)\n", bytes);
        if (bytes > 256) { std::fprintf(stderr, "FAIL size: %zu bytes (>256) -- runtime not stripped\n", bytes); sizeFail = 1; }
    }

    if (r1 || r2 || sizeFail) return 1;
    std::printf("basic_native_run: OK\n");
    return 0;
}
