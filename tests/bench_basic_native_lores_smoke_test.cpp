// bench_basic_native_lores_smoke_test.cpp -- pin the native LO-RES path of the
// Applesoft native compiler (GR / COLOR= / PLOT / HLIN / VLIN) on the GEN2 card.
//
// Same recipe as bench_basic_native_smoke_test.cpp (Pom1BenchHost::compileBasicNative /
// tools/basicc_native.sh): basicnative::compile(src, Card::Gen2) -> ca65 the program +
// the minimal GEN2 runtime (with -D RT_xxx from Result.runtimeFeatures) -> ld65 against
// dev/lib/basicrt/basicc_native.cfg. The standalone .bin loads + runs at $0300 with the
// GEN2 card UNPLUGGED, so the LO-RES page is plain RAM we can read directly.
//
// The native lo-res runtime always targets lo-res PAGE 2 ($0800-$0BFF), never page 1:
// the program image loads at $0300 and the code already reaches past $0400, so a page-1
// clear/plot would overwrite the running code. Page 2 sits above small lo-res programs,
// so the framebuffer and code don't collide (mirrors how the native HGR runtime always
// uses HGR page 1 $2000). So we inspect $0800 here.
//
// Lo-res layout (standard Apple II, $0800 page): cell (x,y) lives in byte
//   lores_lo[y/2] | (lores_hi[y/2]<<8) + x, even y in the low nibble, odd y in the high.
// COLOR= n stores n in both nibbles, so a plotted cell's nibble == n.
//
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
#include <unistd.h>   // mkdtemp

namespace {
constexpr int kSkip = 77;

bool have(const char* cmd) {
    std::string c = std::string(cmd) + " >/dev/null 2>&1";
    return std::system(c.c_str()) == 0;
}

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

std::string g_root, g_rt;

// Replicate the Bench host's compileBasicNative recipe for the GEN2 card (integer phase).
std::string buildNativeGen2(const std::string& src, const std::string& dir) {
    auto nr = basicnative::compile(src, basicnative::Card::Gen2);
    if (!nr.ok) { std::fprintf(stderr, "native compile failed: %s\n", nr.error.c_str()); return ""; }
    { std::ofstream o(dir + "/p.s", std::ios::binary); o << nr.asmText; }

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

// Lo-res $0800 page row-base tables (Apple II interleave) -- mirror basicrt_gen2.s.
const unsigned char kLoresLo[24] = {
    0x00,0x80,0x00,0x80,0x00,0x80,0x00,0x80,
    0x28,0xA8,0x28,0xA8,0x28,0xA8,0x28,0xA8,
    0x50,0xD0,0x50,0xD0,0x50,0xD0,0x50,0xD0 };
const unsigned char kLoresHi[24] = {
    0x08,0x08,0x09,0x09,0x0A,0x0A,0x0B,0x0B,
    0x08,0x08,0x09,0x09,0x0A,0x0A,0x0B,0x0B,
    0x08,0x08,0x09,0x09,0x0A,0x0A,0x0B,0x0B };

// nibble of lo-res cell (x,y) in the framebuffer.
int loresCell(const uint8_t* m, int x, int y) {
    int row = y / 2;
    int addr = (kLoresHi[row] << 8) | kLoresLo[row];
    addr += x;
    uint8_t b = m[addr];
    return (y & 1) ? (b >> 4) : (b & 0x0F);
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

    char dirTmpl[] = "/tmp/pom1benchloresXXXXXX";
    if (!mkdtemp(dirTmpl)) { std::fprintf(stderr, "SKIP: no temp dir\n"); return kSkip; }
    const std::string dir = dirTmpl;

    // The task's lo-res example program (integer phase): clear to lo-res, set a colour,
    // plot two points, a top edge HLIN, and a right edge VLIN.
    const std::string src =
        "10 GR\n"
        "20 COLOR=13\n"
        "30 PLOT 5,5\n"
        "40 PLOT 10,20\n"
        "50 HLIN 0,39 AT 0\n"
        "60 VLIN 0,47 AT 39\n";

    const std::string bin = buildNativeGen2(src, dir);
    if (bin.empty()) { std::fprintf(stderr, "SKIP: cc65 build failed (native GEN2 lo-res)\n"); return kSkip; }
    const std::vector<unsigned char> b = readBin(bin);
    if (b.empty()) { std::fprintf(stderr, "FAIL: native binary empty\n"); return 1; }
    std::printf("native GEN2 lo-res binary: %zu bytes, load+run @ $0300\n", b.size());

    // GEN2 card UNPLUGGED so the lo-res page ($0400-$07FF) is plain RAM.
    Memory mem; mem.initMemory();
    std::memcpy(mem.getMemoryPointerMutable() + 0x0300, b.data(), b.size());
    M6502 cpu(&mem);
    cpu.setProgramCounter(0x0300);
    cpu.start();

    // The program is straight-line then spins at basic_done; a few hundred K cycles
    // is plenty. Run until the two plotted points appear (or a generous cap).
    const uint8_t* m = mem.getMemoryPointerMutable();
    const int slice = 100000;
    for (long long t = 0; t < 5000000LL; t += slice) {
        cpu.run(slice);
        if (loresCell(m, 5, 5) == 13 && loresCell(m, 10, 20) == 13) break;
    }

    int fail = 0;
    auto check = [&](const char* what, int got, int want) {
        std::printf("  %-22s got %2d want %2d  %s\n", what, got, want, got == want ? "ok" : "FAIL");
        if (got != want) ++fail;
    };
    check("COLOR=13 PLOT 5,5",   loresCell(m, 5, 5),   13);
    check("COLOR=13 PLOT 10,20", loresCell(m, 10, 20), 13);
    // HLIN 0,39 AT 0 -> every cell of row 0 is colour 13.
    check("HLIN 0,39 AT 0 (x=0)",  loresCell(m, 0, 0),  13);
    check("HLIN 0,39 AT 0 (x=20)", loresCell(m, 20, 0), 13);
    check("HLIN 0,39 AT 0 (x=39)", loresCell(m, 39, 0), 13);
    // VLIN 0,47 AT 39 -> every cell of column 39 is colour 13 (covers the trickiest
    // routine: VLIN re-stages the fixed X into rt_x0 and walks Y across both parities).
    check("VLIN 0,47 AT 39 (y=0)",  loresCell(m, 39, 0),  13);
    check("VLIN 0,47 AT 39 (y=23)", loresCell(m, 39, 23), 13);
    check("VLIN 0,47 AT 39 (y=47)", loresCell(m, 39, 47), 13);
    // An untouched cell should still be 0 (GR cleared the page).
    check("untouched (1,1) == 0",  loresCell(m, 1, 1),  0);
    check("untouched (20,30) == 0", loresCell(m, 20, 30), 0);

    if (fail) {
        std::fprintf(stderr, "FAIL: native GEN2 lo-res mismatched %d cell(s)\n", fail);
        return 1;
    }
    std::printf("bench_basic_native_lores_smoke: OK\n");
    return 0;
}
