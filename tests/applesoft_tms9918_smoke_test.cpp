// applesoft_tms9918_smoke_test.cpp -- pin the Applesoft TMS9918 interpreter sketch.
//
// sketchs/tms9918/applesoft_tms9918 is Applesoft Lite with the SAME command set +
// token layout as the GEN2 build (TEXT/GR/HGR/GR2/HGR2/COLOR=/HCOLOR=/PLOT/HLIN/
// VLIN/HPLOT/SHOW/VBL/HOME/HTAB/VTAB/APRINT/MIX/NOMIX + SCRN()), but every
// primitive drives the P-LAB TMS9918 VDP instead of a memory-mapped framebuffer.
// The interpreter is a CodeTank ROM cartridge: it runs in place from $4000-$7FFF
// (cold-start 4000R) and the user's BASIC program lives in the 8 KB dual-bank RAM.
//
// Output model (identical to GEN2): PRINT -> TMS text screen, APRINT -> Apple-1
// terminal ($D012). Because the TMS9918 keeps all pixels in its private 16 KB
// VRAM (no RAM framebuffer), this test PLUGS the card and inspects VRAM through
// TMS9918::copySnapshot() -- the dual of the GEN2 test's RAM-page inspection.
//
//   VRAM layout per mode (set by tmsgfx.inc):
//     TEXT : font  $0100   name table $0800 (40x24, raw ASCII codes)
//     GR   : colour $0000  name table $0800 (multicolor 64x48 nibble buffer)
//     HGR  : pattern $0000  colour $2000  name $3800 (Graphics II 256x192)
//
// ROM path: $POM1_ASTMS_ROM, else roms/codetank/CODETANKDEV.rom (upper bank).
#include "TMS9918.h"
#include "WiFiModem.h"    // IWYU pragma: keep
#include "TerminalCard.h" // IWYU pragma: keep
#include "A1IO_RTC.h"     // IWYU pragma: keep
#include "PR40Printer.h"  // IWYU pragma: keep
#include "Memory.h"
#include "M6502.h"
#include "DisplayDevice.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <iterator>
#include <algorithm>

namespace {

class CaptureDisplay : public DisplayDevice {
public:
    void onChar(char c) override { text.push_back(c); }
    std::string text;
};

bool loadRom(Memory& mem)
{
    // The interpreter lives in the UPPER bank of CODETANKDEV.rom (the unified TMS
    // DevBench cartridge); take bytes [$4000:$8000] into the $4000 CodeTank window.
    // A <=16 KB standalone .bin (via POM1_ASTMS_ROM) is taken whole.
    const char* env = std::getenv("POM1_ASTMS_ROM");
    const std::string path = env ? env : "roms/codetank/CODETANKDEV.rom";
    std::ifstream f(path, std::ios::binary);
    std::vector<unsigned char> buf;
    if (f) buf.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    if (buf.empty()) {
        std::fprintf(stderr, "cannot load Applesoft TMS9918 ROM '%s'. Build it with "
                             "tools/build_codetank_rom.py --rom dev (or set POM1_ASTMS_ROM).\n",
                             path.c_str());
        return false;
    }
    const size_t off = (buf.size() >= 0x8000) ? 0x4000 : 0;   // 32K cart -> upper bank
    const size_t n   = std::min<size_t>(buf.size() - off, 0x4000);
    std::memcpy(mem.getMemoryPointerMutable() + 0x4000, buf.data() + off, n);
    return true;
}

// Boot the CodeTank-ROM interpreter at $4000 (4000R), type the listing + RUN, run
// the CPU until `token` shows on the Apple-1 display (or the budget is spent).
// Leaves the TMS9918 plugged so its VRAM holds whatever the program drew.
std::string runProgram(Memory& mem, const char* listing, const std::string& token)
{
    CaptureDisplay disp;
    mem.setDisplayDevice(&disp);
    M6502 cpu(&mem);
    cpu.setProgramCounter(0xFF00);   // WOZ Monitor reset entry
    cpu.start();
    cpu.run(400000);                 // settle at the WOZ GETLN prompt

    auto type = [&](const char* s) { for (const char* p = s; *p; ++p) mem.setKeyPressed(*p); };
    type("\r");
    type("4000R\r");                 // cold-start Applesoft TMS9918 (CodeTank ROM)
    type(listing);
    type("RUN\r");

    const long long kBudget = 200000000;
    const int kSlice = 100000;
    for (long long c = 0; c < kBudget; c += kSlice) {
        cpu.run(kSlice);
        if (!token.empty() && disp.text.find(token) != std::string::npos) break;
    }
    mem.setDisplayDevice(nullptr);
    return disp.text;
}

int countNonZero(const uint8_t* m, int lo, int hi)
{
    int n = 0;
    for (int a = lo; a < hi; ++a) if (m[a]) ++n;
    return n;
}

// Find the raw ASCII bytes for `s` anywhere in VRAM [lo,hi). The TMS text name
// table stores raw char codes (the font at $0100 is ASCII-indexed), unlike the
// Apple-II $0400 page's screen codes (ASCII+128).
bool findAscii(const uint8_t* v, int lo, int hi, const char* s)
{
    const int len = (int)std::string(s).size();
    for (int a = lo; a + len <= hi; ++a) {
        bool ok = true;
        for (int i = 0; i < len; ++i)
            if (v[a + i] != (uint8_t)s[i]) { ok = false; break; }
        if (ok) return true;
    }
    return false;
}

int fail(const char* msg) { std::fprintf(stderr, "%s\n", msg); return 1; }

// Snapshot the live VRAM of the plugged TMS9918.
void grabVram(Memory& mem, TMS9918::Snapshot& snap) { mem.getTMS9918().copySnapshot(snap); }

} // namespace

int main()
{
    // A) renumbered core executes; APRINT reaches the Apple-1 terminal ($D012).
    {
        Memory mem; mem.initMemory(); mem.setTMS9918Enabled(true);
        if (!loadRom(mem)) return 1;
        const std::string out = runProgram(mem, "10 APRINT 1000+7\r20 END\r", "1007");
        if (out.find("1007") == std::string::npos)
            return fail("CORE/APRINT FAILED: '1007' not on the Apple-1 display.");
        std::printf("core+aprint: OK (APRINT 1000+7 -> 1007 on Apple-1)\n");
    }

    // B) PRINT writes to the TMS text page (name table $0800), NOT the Apple-1.
    {
        Memory mem; mem.initMemory(); mem.setTMS9918Enabled(true);
        if (!loadRom(mem)) return 1;
        runProgram(mem, "10 PRINT 1000+7\r", "");
        TMS9918::Snapshot snap; grabVram(mem, snap);
        if (!findAscii(snap.vram.data(), 0x0800, 0x0828, "1007"))
            return fail("PRINT->TMS FAILED: '1007' not on the $0800 text name table.");
        std::printf("print->tms: OK (PRINT 1000+7 -> $0800 text name table)\n");
    }

    // C) HGR + HCOLOR + HPLOT line into the Graphics II pattern table ($0000).
    {
        Memory mem; mem.initMemory(); mem.setTMS9918Enabled(true);
        if (!loadRom(mem)) return 1;
        runProgram(mem, "10 HGR\r20 HCOLOR=3\r30 HPLOT 0,0 TO 255,191\r", "");
        TMS9918::Snapshot snap; grabVram(mem, snap);
        const int lit = countNonZero(snap.vram.data(), 0x0000, 0x1800);  // pattern table
        if (lit < 100) return fail("HGR FAILED: too few pattern bytes after HPLOT line.");
        std::printf("hgr: OK (HPLOT line set %d pattern bytes @ VRAM $0000)\n", lit);
    }

    // D) GR + COLOR= + PLOT/HLIN/VLIN into the multicolor pattern table ($0000).
    {
        Memory mem; mem.initMemory(); mem.setTMS9918Enabled(true);
        if (!loadRom(mem)) return 1;
        runProgram(mem, "10 GR\r20 COLOR=13\r30 PLOT 20,20\r"
                        "40 HLIN 0,39 AT 10\r50 VLIN 0,47 AT 5\r", "");
        TMS9918::Snapshot snap; grabVram(mem, snap);
        const int lit = countNonZero(snap.vram.data(), 0x0000, 0x0600);  // 1536-byte MC buffer
        if (lit < 20) return fail("LGR FAILED: too few multicolor bytes after PLOT/HLIN/VLIN.");
        std::printf("lgr: OK (PLOT+HLIN+VLIN set %d multicolor bytes @ VRAM $0000)\n", lit);
    }

    // E) HGR2 aliases HGR on the single-page TMS card (one 16 KB VRAM bitmap).
    {
        Memory mem; mem.initMemory(); mem.setTMS9918Enabled(true);
        if (!loadRom(mem)) return 1;
        runProgram(mem, "10 HGR2\r20 HCOLOR=3\r30 HPLOT 0,0 TO 255,191\r", "");
        TMS9918::Snapshot snap; grabVram(mem, snap);
        const int lit = countNonZero(snap.vram.data(), 0x0000, 0x1800);
        if (lit < 100) return fail("HGR2 FAILED: pattern table empty after HPLOT line.");
        std::printf("hgr2: OK (HGR2 aliases HGR, set %d pattern bytes)\n", lit);
    }

    // F) HOME/VTAB/HTAB position the TMS text cursor; PRINT writes there.
    // VTAB 3 -> row 2 (name base $0800 + 40*2 = $0850), HTAB 5 -> col 4 -> $0854.
    // The name table holds raw ASCII, so 'X' = $58 (not the $D8 screen code).
    {
        Memory mem; mem.initMemory(); mem.setTMS9918Enabled(true);
        if (!loadRom(mem)) return 1;
        runProgram(mem, "10 HOME\r20 VTAB 3\r30 HTAB 5\r40 PRINT \"X\"\r", "");
        TMS9918::Snapshot snap; grabVram(mem, snap);
        const uint8_t got = snap.vram[0x0854];
        if (got != 0x58) {
            std::fprintf(stderr, "HTAB/VTAB FAILED: VRAM $0854 = %02X (expected 58 = 'X').\n", got);
            return 1;
        }
        std::printf("home/htab/vtab: OK ('X' at row 2 col 4 -> VRAM $0854)\n");
    }

    // G) SCRN(x,y) reads back a lo-res block colour; APRINT it to the Apple-1.
    {
        Memory mem; mem.initMemory(); mem.setTMS9918Enabled(true);
        if (!loadRom(mem)) return 1;
        const std::string out =
            runProgram(mem, "10 GR\r20 COLOR=13\r30 PLOT 5,5\r40 APRINT SCRN(5,5)\r", "13");
        if (out.find("13") == std::string::npos)
            return fail("SCRN FAILED: SCRN(5,5) did not read back colour 13.");
        std::printf("scrn: OK (SCRN(5,5) -> 13)\n");
    }

    std::printf("applesoft_tms9918_smoke: OK\n");
    return 0;
}
