// basic_float_runtime_test.cpp -- pin the standalone binary32 software-float
// runtime (dev/lib/basicrt/basicrt_float.s), the FLOATING-POINT foundation of the
// native compiler's FP phase (no Applesoft ROM). Assembles the runtime with cc65,
// runs each op on POM1's 6502 core, and checks the result against the host's IEEE
// `float`. Skips (ctest code 77) if cc65 is absent.
//
// Layout: the runtime links at $0300 with FA/FB and scratch in zero page. A small
// stub (LDX #$FD; TXS; JSR op; STA $00F0; JMP self) drives one op per case; we
// detect completion when PC reaches the self-loop, then read FA (or A, captured at
// $F0, for fp_cmp).

#include "Memory.h"
#include "M6502.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>
#include <unistd.h>  // mkdtemp (POSIX; macOS needs the explicit include)

namespace {
constexpr int kSkip = 77;

bool haveCc65() {
    return std::system("ca65 --version >/dev/null 2>&1") == 0 &&
           std::system("ld65 --version >/dev/null 2>&1") == 0;
}
std::string findRoot() {
    for (const char* p : {".", "..", "../.."}) {
        std::ifstream f(std::string(p) + "/dev/lib/basicrt/basicrt_float.s");
        if (f) return p;
    }
    return ".";
}
// Parse a ld65 VICE label file ("al ADDR .name") into name->address.
std::map<std::string, uint16_t> parseLabels(const std::string& path) {
    std::map<std::string, uint16_t> m;
    std::ifstream f(path);
    std::string a, addr, name;
    while (f >> a >> addr >> name) {
        if (a != "al") continue;
        if (!name.empty() && name[0] == '.') name = name.substr(1);
        m[name] = static_cast<uint16_t>(std::strtol(addr.c_str(), nullptr, 16));
    }
    return m;
}

struct Fp {
    Memory mem; uint8_t* m = nullptr;
    std::map<std::string, uint16_t> sym;
    uint16_t FA = 0, FB = 0;

    bool build(const std::string& root, const std::string& dir) {
        const std::string s   = root + "/dev/lib/basicrt/basicrt_float.s";
        const std::string cfg = root + "/dev/lib/basicrt/basicrt_float.cfg";
        const std::string o = dir + "/f.o", bin = dir + "/f.bin", lbl = dir + "/f.lbl";
        if (std::system(("ca65 -DFP_INT -DFP_SQRT -DFP_SIN -o " + o + " " + s + " 2>/dev/null").c_str()) != 0) return false;
        if (std::system(("ld65 -C " + cfg + " -Ln " + lbl + " -o " + bin + " " + o + " 2>/dev/null").c_str()) != 0) return false;
        std::ifstream bf(bin, std::ios::binary);
        std::vector<unsigned char> b((std::istreambuf_iterator<char>(bf)), std::istreambuf_iterator<char>());
        if (b.empty()) return false;
        mem.initMemory(); m = mem.getMemoryPointerMutable();
        std::memcpy(m + 0x0300, b.data(), b.size());
        sym = parseLabels(lbl);
        FA = sym["FA"]; FB = sym["FB"];
        return FA && FB && sym.count("fp_add");
    }
    void wf(uint16_t a, float f) { std::memcpy(m + a, &f, 4); }
    float rf(uint16_t a) { float f; std::memcpy(&f, m + a, 4); return f; }
    void wi16(uint16_t a, int16_t v) { m[a] = v & 0xff; m[a + 1] = (v >> 8) & 0xff; }
    int16_t ri16(uint16_t a) { return static_cast<int16_t>(m[a] | (m[a + 1] << 8)); }

    // run op (label), return the accumulator captured at $F0
    uint8_t run(const std::string& op) {
        uint16_t a = sym[op];
        m[0x0260] = 0xA2; m[0x0261] = 0xFD; m[0x0262] = 0x9A;   // LDX #$FD; TXS
        m[0x0263] = 0x20; m[0x0264] = a & 0xff; m[0x0265] = a >> 8; // JSR op
        m[0x0266] = 0x85; m[0x0267] = 0xF0;                      // STA $F0
        m[0x0268] = 0x4C; m[0x0269] = 0x68; m[0x026A] = 0x02;    // JMP self ($0268)
        M6502 cpu(&mem); cpu.setProgramCounter(0x0260); cpu.start();
        for (int i = 0; i < 100000; ++i) { cpu.run(8); if (cpu.getProgramCounter() == 0x0268) break; }
        return m[0xF0];
    }
};

int fails = 0, total = 0;
bool close(float a, float b) {
    if (a == b) return true;
    float d = std::fabs(a - b), mx = std::fmax(std::fabs(a), std::fabs(b));
    return d <= 2e-3f * mx + 1e-6f;   // 23-bit mantissa, truncating ops
}
void ck(const char* nm, float got, float exp) {
    ++total; if (!close(got, exp)) { ++fails; if (fails <= 15) std::printf("FAIL %s: got %g exp %g\n", nm, got, exp); }
}
} // namespace

int main()
{
    if (!haveCc65()) { std::fprintf(stderr, "SKIP: cc65 (ca65/ld65) not on PATH\n"); return kSkip; }
    const std::string root = findRoot();
    char tmpl[] = "/tmp/basicfpXXXXXX";
    if (!mkdtemp(tmpl)) { std::fprintf(stderr, "SKIP: no temp dir\n"); return kSkip; }
    Fp fp;
    if (!fp.build(root, tmpl)) { std::fprintf(stderr, "SKIP: cc65 build of basicrt_float failed\n"); return kSkip; }

    // int <-> float conversions
    for (int v = -30000; v <= 30000; v += 211) {
        fp.wi16(fp.FA, (int16_t)v); fp.run("fp_fromint16"); ck("fromint", fp.rf(fp.FA), (float)v);
    }
    for (int k = -2000; k <= 2000; k += 7) {
        float f = k * 13.7f; fp.wf(fp.FA, f); fp.run("fp_toint16");
        ck("toint", (float)fp.ri16(fp.FA), (float)((int16_t)std::trunc(f)));
    }

    // arithmetic over a value grid
    const float vals[] = {0,1,-1,2,0.5f,3.25f,-7.5f,100,-100,0.001f,12345,-0.25f,42.5f,1000,-3.14159f,0.1f,7,-2};
    for (float a : vals) for (float b : vals) {
        fp.wf(fp.FA, a); fp.wf(fp.FB, b); fp.run("fp_add"); ck("add", fp.rf(fp.FA), a + b);
        fp.wf(fp.FA, a); fp.wf(fp.FB, b); fp.run("fp_sub"); ck("sub", fp.rf(fp.FA), a - b);
        fp.wf(fp.FA, a); fp.wf(fp.FB, b); fp.run("fp_mul"); ck("mul", fp.rf(fp.FA), a * b);
        if (b != 0) { fp.wf(fp.FA, a); fp.wf(fp.FB, b); fp.run("fp_div"); ck("div", fp.rf(fp.FA), a / b); }
    }

    // comparison: A = 0 (a<b) / 1 (==) / 2 (a>b)
    for (float a : vals) for (float b : vals) {
        fp.wf(fp.FA, a); fp.wf(fp.FB, b);
        uint8_t got = fp.run("fp_cmp");
        uint8_t exp = (a < b) ? 0 : (a == b) ? 1 : 2;
        ++total; if (got != exp) { ++fails; if (fails <= 15) std::printf("FAIL cmp %g,%g: got %d exp %d\n", a, b, got, exp); }
    }

    // INT (truncate toward zero)
    for (float a : vals) { fp.wf(fp.FA, a); fp.run("fp_int"); ck("int", fp.rf(fp.FA), std::trunc(a)); }
    for (int k = -500; k <= 500; ++k) { float f = k * 0.37f; fp.wf(fp.FA, f); fp.run("fp_int"); ck("int", fp.rf(fp.FA), std::trunc(f)); }

    // SQRT (Newton-Raphson) -- defined for a >= 0
    for (float a : vals) if (a >= 0) { fp.wf(fp.FA, a); fp.run("fp_sqrt"); ck("sqrt", fp.rf(fp.FA), std::sqrt(a)); }
    for (int k = 0; k <= 4000; k += 3) { float f = k * 0.5f; fp.wf(fp.FA, f); fp.run("fp_sqrt"); ck("sqrt", fp.rf(fp.FA), std::sqrt(f)); }

    // SIN (range reduction + Taylor); args within ~+-5000 (k must fit 16 bits)
    for (int k = -3140; k <= 3140; k += 7) {
        float f = k * 0.01f; fp.wf(fp.FA, f); fp.run("fp_sin"); ck("sin", fp.rf(fp.FA), std::sin(f));
    }
    for (float f : {-4000.f,-1000.f,-12.5f,-6.28318f,-3.14159f,-1.f,0.f,1.f,3.14159f,6.28318f,12.5f,100.f,1000.f,4000.f}) {
        fp.wf(fp.FA, f); fp.run("fp_sin"); ck("sin", fp.rf(fp.FA), std::sin(f));
    }

    if (fails) { std::printf("basic_float_runtime: %d/%d FAILED\n", fails, total); return 1; }
    std::printf("basic_float_runtime: OK (%d cases)\n", total);
    return 0;
}
