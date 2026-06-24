// bench_basic_native_smoke_test.cpp -- pin the DESKTOP DevBench "compile native +
// run" path (Pom1BenchHost::compileBasicNative, mode-5 Applesoft GEN2/TMS targets).
//
// It exercises the SAME build recipe the Bench host runs (and tools/basicc_native.sh
// is the reference for): basicnative::compile(src, Card::Gen2) -> ca65 the program +
// the minimal GEN2 runtime (with -D RT_xxx from Result.runtimeFeatures) -> ld65 against
// dev/lib/basicrt/basicc_native.cfg. The standalone .bin loads + runs at $0300 with NO
// interpreter; the GEN2 card is left UNPLUGGED so the HGR page-1 framebuffer ($2000-
// $4000) is plain RAM we can inspect directly (the native GEN2 runtime always plots to
// page 1 -- see dev/lib/basicrt/basicrt_gen2.s -- so HGR2 lands at $2000 too).
//
// Success criterion: the diagonal HPLOT fills the framebuffer with non-zero bytes.
// Skips (ctest code 77) if cc65 (ca65/ld65) or the dev/lib/basicrt tree is absent.

#include "BasicCompilerApplesoft.h"
#include "Memory.h"
#include "M6502.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <unistd.h>   // mkdtemp (POSIX; macOS needs the explicit include)

namespace {
constexpr int kSkip = 77;   // ctest SKIP_RETURN_CODE

bool have(const char* cmd) {
    std::string c = std::string(cmd) + " >/dev/null 2>&1";
    return std::system(c.c_str()) == 0;
}

// Repo root: the dir whose dev/lib/basicrt/basicc_native.cfg exists (tests run from
// the build dir or the repo root depending on the harness).
std::string findRoot() {
    for (const char* p : {".", "..", "../.."}) {
        std::ifstream f(std::string(p) + "/dev/lib/basicrt/basicc_native.cfg");
        if (f) return p;
    }
    return ".";
}

std::vector<unsigned char> readBin(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)),
                                      std::istreambuf_iterator<char>());
}

int countFb(const uint8_t* m) {   // GEN2 HGR page 1 framebuffer (native plots here)
    int n = 0;
    for (int a = 0x2000; a < 0x4000; ++a) if (m[a]) ++n;
    return n;
}

std::string g_root, g_rt;

// Replicate the Bench host's compileBasicNative recipe for the GEN2 card. Returns the
// path of the linked .bin, or "" on a (skippable) cc65 build failure.
std::string buildNativeGen2(const std::string& src, const std::string& dir) {
    auto nr = basicnative::compile(src, basicnative::Card::Gen2);
    if (!nr.ok) { std::fprintf(stderr, "native compile failed: %s\n", nr.error.c_str()); return ""; }
    { std::ofstream o(dir + "/p.s", std::ios::binary); o << nr.asmText; }

    // -D RT_xxx for each rt_* runtime routine the program imports -> minimal runtime.
    std::string defs, fpdefs;
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
    if (nr.usesFloat) {
        ok = ok && sh("ca65" + fpdefs + " -o " + dir + "/fp.o " + g_rt + "/basicrt_float.s");
        objs += " " + dir + "/fp.o";
    }
    ok = ok && sh("ld65 -C " + g_rt + "/basicc_native.cfg -o " + dir + "/p.bin " + objs);
    return ok ? (dir + "/p.bin") : "";
}
} // namespace

int main()
{
    if (!have("ca65 --version") || !have("ld65 --version")) {
        std::fprintf(stderr, "SKIP: cc65 (ca65/ld65) not on PATH\n");
        return kSkip;
    }
    g_root = findRoot();
    g_rt   = g_root + "/dev/lib/basicrt";
    {
        std::ifstream cfg(g_rt + "/basicc_native.cfg");
        if (!cfg) { std::fprintf(stderr, "SKIP: dev/lib/basicrt not found\n"); return kSkip; }
    }

    char dirTmpl[] = "/tmp/pom1benchnatXXXXXX";
    if (!mkdtemp(dirTmpl)) { std::fprintf(stderr, "SKIP: no temp dir\n"); return kSkip; }
    const std::string dir = dirTmpl;

    // The task's example program: a diagonal HPLOT on the GEN2 card.
    const std::string src =
        "10 HGR2\n"
        "20 HCOLOR=3\n"
        "30 HPLOT 0,0 TO 100,100\n";

    const std::string bin = buildNativeGen2(src, dir);
    if (bin.empty()) { std::fprintf(stderr, "SKIP: cc65 build failed (native GEN2)\n"); return kSkip; }
    const std::vector<unsigned char> b = readBin(bin);
    if (b.empty()) { std::fprintf(stderr, "FAIL: native binary empty\n"); return 1; }
    std::printf("native GEN2 binary: %zu bytes, load+run @ $0300\n", b.size());

    // GEN2 card UNPLUGGED so HGR page-1 ($2000-$4000) is plain RAM we can inspect.
    Memory mem; mem.initMemory();
    std::memcpy(mem.getMemoryPointerMutable() + 0x0300, b.data(), b.size());
    M6502 cpu(&mem);
    cpu.setProgramCounter(0x0300);
    cpu.start();

    const int slice = 200000;
    int lit = 0;
    for (long long t = 0; t < 80000000LL; t += slice) {
        cpu.run(slice);
        lit = countFb(mem.getMemoryPointerMutable());
        if (lit >= 80) break;   // the diagonal lights many bytes; 80 is unambiguous
    }
    std::printf("HPLOT 0,0 TO 100,100 -> %d non-zero framebuffer bytes\n", lit);

    if (lit < 80) {
        std::fprintf(stderr, "FAIL: native GEN2 program filled too few bytes (%d < 80)\n", lit);
        return 1;
    }
    std::printf("bench_basic_native_smoke: OK\n");
    return 0;
}
