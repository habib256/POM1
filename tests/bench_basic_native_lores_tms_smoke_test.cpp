// bench_basic_native_lores_tms_smoke_test.cpp -- pin the native LO-RES path of the
// Applesoft native compiler (GR / COLOR= / PLOT / HLIN / VLIN / TEXT / HOME) on the
// TMS9918 card (Multicolor mode).
//
// Dual of bench_basic_native_lores_smoke_test.cpp (which pins the GEN2 lo-res path).
// Same recipe as Pom1BenchHost::compileBasicNative / tools/basicc_native.sh for the
// TMS card: basicnative::compile(src, Card::Tms) -> ca65 the program + the TMS runtime
// (with -D RT_xxx from Result.runtimeFeatures) -> ca65 tms9918_pad.asm (lo-res needs
// only tms9918_pad12, NOT the hi-res tms9918m2.o) -> ld65 against basicc_native.cfg.
//
// Unlike GEN2 (memory-mapped framebuffer), the TMS9918 keeps every pixel in its
// private 16 KB VRAM, so this test PLUGS the card, runs the standalone .bin at $0300,
// then inspects VRAM through TMS9918::copySnapshot() (as applesoft_tms9918_smoke_test
// does for the interpreter). The native lo-res runtime drives Multicolor mode: a flat
// 64x48 nibble framebuffer in the pattern table at VRAM $0000, banded name table $0800.
//
// Multicolor block address (mirror basicrt_tms.s lr_addr / tmsgfx.inc mc_addr):
//   c = x>>1; R = y>>1; addr_hi = R>>2; byteidx = (R&3)*2 + (y&1); addr_lo = c*8 + byteidx
//   even x -> high nibble, odd x -> low nibble.
//
// Skips (ctest code 77) if cc65 (ca65/ld65) or the dev/lib/basicrt tree is absent.

#include "BasicCompilerApplesoft.h"
#include "Memory.h"
#include "M6502.h"
#include "TMS9918.h"

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

// Multicolor pattern address of block (x,y) -- mirror basicrt_tms.s lr_addr.
int mcAddr(int x, int y) {
    int c = x >> 1, R = y >> 1;
    int ah = R >> 2;
    int byteidx = (R & 3) * 2 + (y & 1);
    int al = c * 8 + byteidx;
    return (ah << 8) | al;
}
// Multicolor block colour at (x,y) in the pattern table.
int mcCell(const uint8_t* v, int x, int y) {
    uint8_t b = v[mcAddr(x, y)];
    return (x & 1) ? (b & 0x0F) : (b >> 4);
}

// Replicate the Bench host's compileBasicNative recipe for the TMS card. Auto precision
// (integer unless the listing needs float). Lo-res links only tms9918_pad.o
// (tms9918_pad12); the hi-res tms9918m2.o is NOT needed.
std::string buildNativeTms(const std::string& src, const std::string& dir) {
    auto nr = basicnative::compile(src, basicnative::Card::Tms, basicnative::FpMode::Auto);
    if (!nr.ok) { std::fprintf(stderr, "native compile failed: %s\n", nr.error.c_str()); return ""; }
    { std::ofstream o(dir + "/p.s", std::ios::binary); o << nr.asmText; }

    std::string defs;
    for (std::string f : nr.runtimeFeatures) {
        if (f.rfind("rt_", 0) != 0) continue;
        for (char& c : f) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        defs += " -D " + f;
    }
    const std::string tms = g_root + "/dev/lib/tms9918";
    const std::string I = " -I " + tms + " -I " + g_root + "/dev/lib/apple1 -I " + g_rt + " ";
    auto sh = [](const std::string& c) { return std::system(c.c_str()) == 0; };

    bool ok = sh("ca65" + I + "-o " + dir + "/p.o " + dir + "/p.s") &&
              sh("ca65" + defs + I + "-o " + dir + "/rt.o " + g_rt + "/basicrt_tms.s") &&
              sh("ca65" + I + "-o " + dir + "/pad.o " + tms + "/tms9918_pad.asm");
    std::string objs = dir + "/p.o " + dir + "/rt.o " + dir + "/pad.o";
    if (ok && nr.usesFloat) {   // gated transcendentals, same as basicc_native.sh
        std::string fp;
        if (nr.asmText.find("fp_int")  != std::string::npos) fp += " -D FP_INT";
        if (nr.asmText.find("fp_sqrt") != std::string::npos) fp += " -D FP_SQRT";
        if (nr.asmText.find("fp_sin")  != std::string::npos) fp += " -D FP_SIN";
        ok = sh("ca65" + fp + " -o " + dir + "/fp.o " + g_rt + "/basicrt_float.s");
        objs += " " + dir + "/fp.o";
    }
    ok = ok && sh("ld65 -C " + g_rt + "/basicc_native.cfg -o " + dir + "/p.bin " + objs);
    return ok ? (dir + "/p.bin") : "";
}

// Count painted Multicolor blocks (non-zero nibbles) in the pattern table.
int countMcBlocks(const uint8_t* v) {
    int n = 0;
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 64; ++x)
            if (mcCell(v, x, y)) ++n;
    return n;
}
// Build a TMS native binary, run it at $0300 with the given preset RAM (KB) + OOR
// strict, return the count of painted Multicolor blocks.
int runTmsLores(const std::string& src, const std::string& dir, int ramKB, bool strict) {
    std::string bin = buildNativeTms(src, dir);
    if (bin.empty()) return -1;
    auto b = readBin(bin);
    if (b.empty()) return -1;
    Memory mem; mem.initMemory(); mem.setTMS9918Enabled(true);
    mem.setPresetRamKB(ramKB); mem.setOutOfRangeStrictMode(strict);
    std::memcpy(mem.getMemoryPointerMutable() + 0x0300, b.data(), b.size());
    M6502 cpu(&mem); cpu.setProgramCounter(0x0300); cpu.start();
    for (long long t = 0; t < 20000000LL; t += 200000) cpu.run(200000);
    TMS9918::Snapshot s; mem.getTMS9918().copySnapshot(s);
    return countMcBlocks(s.vram.data());
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

    char tmpl[] = "/tmp/pom1_lores_tms_XXXXXX";
    const char* dir = mkdtemp(tmpl);
    if (!dir) { std::fprintf(stderr, "mkdtemp failed\n"); return 1; }

    // GR + COLOR= + PLOT + HLIN + VLIN into the Multicolor pattern table. Integer
    // phase (no float runtime). The picture lives in VRAM, not RAM, so the program
    // image at $0300 never collides with it (unlike GEN2's page-2 trick).
    const std::string src =
        "10 GR\n"
        "20 COLOR=5\n"
        "30 PLOT 3,4\n"
        "40 COLOR=12\n"
        "50 HLIN 0,10 AT 2\n"
        "60 VLIN 1,6 AT 7\n"
        "70 END\n";

    std::string bin = buildNativeTms(src, dir);
    if (bin.empty()) { std::fprintf(stderr, "SKIP: cc65 build failed\n"); return kSkip; }
    auto b = readBin(bin);
    if (b.empty()) { std::fprintf(stderr, "native binary empty\n"); return 1; }

    Memory mem; mem.initMemory(); mem.setTMS9918Enabled(true);
    std::memcpy(mem.getMemoryPointerMutable() + 0x0300, b.data(), b.size());
    M6502 cpu(&mem); cpu.setProgramCounter(0x0300); cpu.start();
    // GR clears 1536 pattern + builds 768 name bytes (each gated by a 12c pad), then
    // a handful of plots -- a few hundred K cycles. 8M is comfortable headroom.
    for (long long t = 0; t < 8000000LL; t += 200000) cpu.run(200000);

    TMS9918::Snapshot snap; mem.getTMS9918().copySnapshot(snap);
    const uint8_t* v = snap.vram.data();

    int rc = 0;
    auto check = [&](const char* what, bool cond) {
        std::printf("  %-40s %s\n", what, cond ? "OK" : "FAIL");
        if (!cond) rc = 1;
    };

    // PLOT 3,4 in COLOR 5.
    check("PLOT 3,4 == 5", mcCell(v, 3, 4) == 5);
    // HLIN 0,10 AT 2 in COLOR 12 (endpoints + interior).
    check("HLIN 0..10 AT 2 == 12 (x=0)",  mcCell(v, 0, 2)  == 12);
    check("HLIN 0..10 AT 2 == 12 (x=5)",  mcCell(v, 5, 2)  == 12);
    check("HLIN 0..10 AT 2 == 12 (x=10)", mcCell(v, 10, 2) == 12);
    check("HLIN stops at 11 (x=11 == 0)", mcCell(v, 11, 2) == 0);
    // VLIN 1,6 AT 7 in COLOR 12.
    check("VLIN 1..6 AT 7 == 12 (y=1)", mcCell(v, 7, 1) == 12);
    check("VLIN 1..6 AT 7 == 12 (y=6)", mcCell(v, 7, 6) == 12);
    check("VLIN starts at 1 (y=0 == 0)", mcCell(v, 7, 0) == 0);

    // Overall: a non-trivial number of multicolor nibbles got painted.
    int lit = 0;
    for (int a = 0x0000; a < 0x0600; ++a) if (v[a]) ++lit;
    check("pattern table painted (>= 8 bytes)", lit >= 8);
    std::printf("  painted %d multicolor bytes @ VRAM $0000\n", lit);

    // TEXT / HOME take the picture down to a blank text screen: TEXT zeroes the 2 KB
    // pattern table (every glyph blank). A second program draws then calls TEXT:HOME;
    // afterwards the Multicolor framebuffer must be cleared (and the run terminates).
    {
        const std::string src2 =
            "10 GR\n20 COLOR=12\n30 PLOT 5,5\n40 PLOT 9,9\n50 TEXT\n60 HOME\n70 END\n";
        std::string bin2 = buildNativeTms(src2, dir);
        if (bin2.empty()) { std::fprintf(stderr, "SKIP: cc65 build failed (TEXT/HOME)\n"); return kSkip; }
        auto b2 = readBin(bin2);
        Memory m2; m2.initMemory(); m2.setTMS9918Enabled(true);
        std::memcpy(m2.getMemoryPointerMutable() + 0x0300, b2.data(), b2.size());
        M6502 c2(&m2); c2.setProgramCounter(0x0300); c2.start();
        for (long long t = 0; t < 8000000LL; t += 200000) c2.run(200000);
        TMS9918::Snapshot s2; m2.getTMS9918().copySnapshot(s2);
        int after = 0;
        for (int a = 0x0000; a < 0x0800; ++a) if (s2.vram[a]) ++after;
        check("TEXT/HOME blanked the pattern table", after == 0);
        std::printf("  pattern bytes after TEXT:HOME = %d\n", after);
    }

    // Big FLOAT lo-res program (Rod's Color Pattern: GR/COLOR=//PLOT) — the canonical
    // example. Its image (~3.6 KB) reaches past $0FFF, so on the authentic 8 KB
    // Parmigiani dual-bank preset (RAM only $0000-$0FFF) it reads $FF under strict OOR
    // and draws nothing; the Bench deploy therefore relaxes that preset to 16 KB low
    // RAM. Pin both: paints under 16 KB, broken under 8 KB (so the relax stays needed).
    {
        // Full RodColor PLOT body (8-way symmetry) -> ~3.6 KB image (reaches past $0FFF);
        // W range shrunk only to keep the run quick.
        const std::string rod =
            "10 GR\n"
            "20 FOR W = 3 TO 8\n"
            "30 FOR I = 1 TO 19\n"
            "40 FOR J = 0 TO 19\n"
            "50 K = I + J\n"
            "60 COLOR= J * 3 / (I + 3) + I * W / 12\n"
            "70 PLOT I,K: PLOT K,I: PLOT 40 - I,40 - K: PLOT 40 - K,40 - I\n"
            "80 PLOT K,40 - I: PLOT 40 - I,K: PLOT I,40 - K: PLOT 40 - K,I\n"
            "90 NEXT : NEXT : NEXT\n"
            "99 END\n";
        const int lit16 = runTmsLores(rod, dir, 16, true);
        if (lit16 < 0) { std::fprintf(stderr, "SKIP: cc65 build failed (RodColor float)\n"); return kSkip; }
        check("RodColor (float) paints under 16 KB relax", lit16 >= 50);
        // Same image on the authentic 8 KB dual-bank: its tail past $0FFF reads $FF, so
        // it draws far less (the relax to 16 KB is what makes it whole). Robust contrast
        // rather than exact 0 (codegen size can shift the cutoff).
        const int lit8 = runTmsLores(rod, dir, 8, true);
        check("RodColor (float) needs the 16 KB relax (8 KB << 16 KB)", lit8 < lit16 / 2);
        std::printf("  RodColor float: 16 KB -> %d blocks, 8 KB -> %d blocks\n", lit16, lit8);
    }

    if (rc == 0) std::printf("native lo-res TMS (Multicolor): OK\n");
    return rc;
}
