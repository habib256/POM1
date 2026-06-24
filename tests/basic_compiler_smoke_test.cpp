// basic_compiler_smoke_test.cpp -- pin the Applesoft "BASIC compiler" (src/
// BasicTokeniserApplesoft.cpp): an Applesoft listing is COMPILED ahead of time into a 6502
// memory image (tokenized program at $0801 + a launcher stub) and LOADED + RUN
// directly, with NO keyboard "injection" of the listing into the interpreter.
//
// Success criterion (the feature's goal): sketchs/basic_applesoft/3DHat.apf -- a
// floating-point hidden-line 3D plot (FOR/NEXT, GOSUB, IF/GOTO, SIN/SQR/INT,
// HGR2/HCOLOR/HPLOT) -- compiles and EXECUTES on BOTH the GEN2 HGR card and the
// TMS9918 card, drawing into the respective framebuffer.
//
// Flow per card (injection-free):
//   1. Load the interpreter ROM; cold-start it by jumping to its cold entry
//      ($4000 TMS / $9800 GEN2) and running until the `]` prompt -- this only
//      boots the ROM (sets up zero page, vectors, HIMEM); it types nothing.
//   2. basic::compile() the listing; poke the program image + launcher into RAM.
//   3. Jump to the launcher and run; assert the framebuffer fills with plotted
//      pixels (TMS: Graphics II pattern table in VRAM; GEN2: hi-res page-2 RAM).
//
// ROM paths mirror the sibling smoke tests (POM1_ASTMS_ROM / POM1_ASGEN2_ROM).

#include "BasicTokeniserApplesoft.h"
#include "TMS9918.h"
#include "WiFiModem.h"    // IWYU pragma: keep
#include "TerminalCard.h" // IWYU pragma: keep
#include "A1IO_RTC.h"     // IWYU pragma: keep
#include "PR40Printer.h"  // IWYU pragma: keep
#include "Memory.h"
#include "M6502.h"
#include "DisplayDevice.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr int kSkip = 77;   // ctest SKIP_RETURN_CODE (ROM/source absent)

class CaptureDisplay : public DisplayDevice {
public:
    void onChar(char c) override { text.push_back(c); }
    std::string text;
};

std::string readFile(const std::string& path)
{
    for (const char* pre : {"", "../", "../../"}) {
        std::ifstream f(pre + path, std::ios::binary);
        if (f) return std::string((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
    }
    return {};
}

bool loadBinaryAt(Memory& mem, const std::string& path, uint16_t addr)
{
    for (const char* pre : {"", "../", "../../"}) {
        mem.setWriteInRom(true);
        const int rc = mem.loadBinary((pre + path).c_str(), addr, nullptr);
        mem.setWriteInRom(false);
        if (rc == 0) return true;
    }
    return false;
}

// Load the TMS9918 Applesoft interpreter (upper bank of CODETANKDEV.rom) into the
// $4000 CodeTank window. Returns false if the cartridge isn't present.
bool loadTmsApplesoft(Memory& mem)
{
    const char* env = std::getenv("POM1_ASTMS_ROM");
    const std::string base = env ? env : "roms/codetank/CODETANKDEV.rom";
    std::vector<unsigned char> buf;
    for (const char* pre : {"", "../", "../../"}) {
        std::ifstream f(pre + base, std::ios::binary);
        if (f) { buf.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()); break; }
    }
    if (buf.empty()) return false;
    const size_t off = (buf.size() >= 0x8000) ? 0x4000 : 0;   // 32K cart -> upper bank
    const size_t n   = std::min<size_t>(buf.size() - off, 0x4000);
    std::memcpy(mem.getMemoryPointerMutable() + 0x4000, buf.data() + off, n);
    return true;
}

// Cold-start the resident interpreter by jumping to `coldEntry` and running until
// the `]` prompt appears on the Apple-1 terminal (cap as a safety net). Boots the
// ROM only -- it types nothing.
void coldStart(Memory& mem, M6502& cpu, CaptureDisplay& disp, uint16_t coldEntry)
{
    cpu.setProgramCounter(coldEntry);
    cpu.start();
    const long long cap = 40000000;
    for (long long c = 0; c < cap; c += 200000) {
        cpu.run(200000);
        if (disp.text.find(']') != std::string::npos) break;
    }
}

// Poke a compiled program (image + launcher) into RAM and jump to its entry, then
// run until `framebuffer [lo,hi)` accumulates `want` non-zero bytes, or `budget`
// cycles elapse. Returns the final non-zero count. `vramSource` selects whether
// the framebuffer is plain RAM (GEN2 page 2) or the TMS9918's private VRAM.
enum class Fb { Ram, TmsVram };

int runCompiled(Memory& mem, M6502& cpu, const basic::Result& prog,
                Fb fb, int lo, int hi, int want, long long budget)
{
    uint8_t* ram = mem.getMemoryPointerMutable();
    for (const basic::Zone& z : prog.zones)
        std::memcpy(ram + z.addr, z.bytes.data(), z.bytes.size());

    cpu.setProgramCounter(prog.entry);
    cpu.start();

    auto count = [&]() -> int {
        if (fb == Fb::TmsVram) {
            TMS9918::Snapshot snap;
            mem.getTMS9918().copySnapshot(snap);
            int n = 0;
            for (int a = lo; a < hi; ++a) if (snap.vram[a]) ++n;
            return n;
        }
        int n = 0;
        for (int a = lo; a < hi; ++a) if (ram[a]) ++n;
        return n;
    };

    const int kSlice = 200000;
    int last = 0;
    for (long long c = 0; c < budget; c += kSlice) {
        cpu.run(kSlice);
        last = count();
        if (last >= want) break;
    }
    return last;
}

int fail(const char* msg) { std::fprintf(stderr, "FAIL: %s\n", msg); return 1; }

} // namespace

int main()
{
    const std::string src = readFile("sketchs/basic_applesoft/3DHat.apf");
    if (src.empty()) {
        std::fprintf(stderr, "SKIP: sketchs/basic_applesoft/3DHat.apf not found\n");
        return kSkip;
    }

    // ----- TMS9918 -----------------------------------------------------------
    {
        basic::Result prog = basic::compile(src, basic::targetTms());
        if (!prog.ok) return fail(("TMS compile: " + prog.error).c_str());
        std::printf("3DHat -> TMS: %d lines, image+launcher compiled, run @ $%04X\n",
                    prog.lineCount, prog.entry);

        Memory mem; mem.initMemory(); mem.setTMS9918Enabled(true);
        if (!loadTmsApplesoft(mem)) {
            std::fprintf(stderr, "SKIP: CODETANKDEV.rom (TMS Applesoft) not found\n");
            return kSkip;
        }
        CaptureDisplay disp; mem.setDisplayDevice(&disp);
        M6502 cpu(&mem);
        coldStart(mem, cpu, disp, 0x4000);
        if (disp.text.find(']') == std::string::npos)
            return fail("TMS: interpreter never reached the `]` prompt on cold start");

        // HGR2 aliases HGR on the single-page card: pixels land in the Graphics II
        // pattern table at VRAM $0000-$1800. The finished hat lights ~450 distinct
        // pattern bytes; 300 is reached early (~250M cycles) yet is unambiguous --
        // a cleared HGR screen has zero, so any plotting at all only adds bytes.
        const int lit = runCompiled(mem, cpu, prog, Fb::TmsVram, 0x0000, 0x1800,
                                    /*want=*/300, /*budget=*/1200000000LL);
        mem.setDisplayDevice(nullptr);
        if (lit < 300)
            return fail(("TMS: 3DHat drew too few pattern bytes (" + std::to_string(lit) + " < 300)").c_str());
        std::printf("3DHat on TMS9918: OK (%d VRAM pattern bytes plotted)\n", lit);
    }

    // ----- GEN2 HGR ----------------------------------------------------------
    {
        basic::Result prog = basic::compile(src, basic::targetGen2());
        if (!prog.ok) return fail(("GEN2 compile: " + prog.error).c_str());
        std::printf("3DHat -> GEN2: %d lines, image+launcher compiled, run @ $%04X\n",
                    prog.lineCount, prog.entry);

        // GEN2 card left unplugged (as in applesoft_gen2_smoke): the hi-res pages
        // are plain RAM we can inspect directly. 3DHat uses HGR2 -> page 2 ($4000).
        Memory mem; mem.initMemory();
        if (!loadBinaryAt(mem, "roms/applesoft-gen2.rom", 0x9800)) {
            std::fprintf(stderr, "SKIP: roms/applesoft-gen2.rom not found\n");
            return kSkip;
        }
        CaptureDisplay disp; mem.setDisplayDevice(&disp);
        M6502 cpu(&mem);
        coldStart(mem, cpu, disp, 0x9800);
        if (disp.text.find(']') == std::string::npos)
            return fail("GEN2: interpreter never reached the `]` prompt on cold start");

        const int lit = runCompiled(mem, cpu, prog, Fb::Ram, 0x4000, 0x6000,
                                    /*want=*/300, /*budget=*/1200000000LL);
        mem.setDisplayDevice(nullptr);
        if (lit < 300)
            return fail(("GEN2: 3DHat drew too few page-2 bytes (" + std::to_string(lit) + " < 300)").c_str());
        std::printf("3DHat on GEN2 HGR: OK (%d hi-res page-2 bytes plotted)\n", lit);
    }

    std::printf("basic_compiler_smoke: OK\n");
    return 0;
}
