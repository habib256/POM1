// applesoft_gen2_smoke_test.cpp -- pin the Applesoft GEN2 interpreter sketch.
//
// sketchs/gen2/applesoft_gen2 is Applesoft Lite with CFFA1 removed and the GEN2
// TXT/LGR/HGR command set added (TEXT/GR/HGR/GR2/HGR2/COLOR=/HCOLOR=/PLOT/HLIN/
// VLIN/HPLOT/SHOW/VBL/HOME/HTAB/VTAB/APRINT/MIX/NOMIX + SCRN()), which renumbers
// every operator + function token. Output model: PRINT -> GEN2 text page ($0400),
// APRINT -> Apple-1 terminal ($D012). This test boots the built ROM headlessly
// (GEN2 card UNPLUGGED, so all display pages are plain RAM we can inspect) and
// asserts the renumbered core runs and each new command writes the right memory.
//
// ROM path: $POM1_ASGEN2_ROM, else software/Graphic HGR/applesoft-gen2.bin.
#include "TMS9918.h"      // IWYU pragma: keep
#include "WiFiModem.h"    // IWYU pragma: keep
#include "TerminalCard.h" // IWYU pragma: keep
#include "A1IO_RTC.h"     // IWYU pragma: keep
#include "PR40Printer.h"  // IWYU pragma: keep
#include "Memory.h"
#include "M6502.h"
#include "DisplayDevice.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

class CaptureDisplay : public DisplayDevice {
public:
    void onChar(char c) override { text.push_back(c); }
    std::string text;
};

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
    type("6000R\r");                 // cold-start Applesoft GEN2
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

bool loadRom(Memory& mem)
{
    const char* env = std::getenv("POM1_ASGEN2_ROM");
    const char* path = env ? env : "software/Graphic HGR/applesoft-gen2.bin";
    mem.setWriteInRom(true);
    const int rc = mem.loadBinary(path, 0x6000, nullptr);
    mem.setWriteInRom(false);
    if (rc != 0) {
        std::fprintf(stderr, "cannot load Applesoft GEN2 ROM '%s' (rc=%d). Build the "
                             "sketch and/or set POM1_ASGEN2_ROM.\n", path, rc);
        return false;
    }
    return true;
}

int countNonZero(const uint8_t* m, int lo, int hi)
{
    int n = 0;
    for (int a = lo; a < hi; ++a) if (m[a]) ++n;
    return n;
}

// Find the screen-code bytes for `s` (ASCII+128) anywhere in [lo,hi).
bool findScreenCodes(const uint8_t* m, int lo, int hi, const char* s)
{
    const int len = (int)std::string(s).size();
    for (int a = lo; a + len <= hi; ++a) {
        bool ok = true;
        for (int i = 0; i < len; ++i)
            if (m[a + i] != (uint8_t)(s[i] | 0x80)) { ok = false; break; }
        if (ok) return true;
    }
    return false;
}

int fail(const char* msg) { std::fprintf(stderr, "%s\n", msg); return 1; }

} // namespace

int main()
{
    // A) renumbered core executes; APRINT reaches the Apple-1 terminal ($D012).
    {
        Memory mem; mem.initMemory();
        if (!loadRom(mem)) return 1;
        const std::string out = runProgram(mem, "10 APRINT 1000+7\r20 END\r", "1007");
        if (out.find("1007") == std::string::npos)
            return fail("CORE/APRINT FAILED: '1007' not on the Apple-1 display.");
        std::printf("core+aprint: OK (APRINT 1000+7 -> 1007 on Apple-1)\n");
    }

    // B) PRINT writes to the GEN2 text page ($0400), NOT the Apple-1 terminal.
    {
        Memory mem; mem.initMemory();
        if (!loadRom(mem)) return 1;
        runProgram(mem, "10 PRINT 1000+7\r", "");
        const uint8_t* m = mem.getMemoryPointer();
        if (!findScreenCodes(m, 0x0400, 0x0428, "1007"))
            return fail("PRINT->GEN2 FAILED: '1007' screen codes not on the $0400 text page.");
        std::printf("print->gen2: OK (PRINT 1000+7 -> $0400 text page)\n");
    }

    // C) HGR + HCOLOR + HPLOT line into the $2000 hi-res framebuffer.
    {
        Memory mem; mem.initMemory();
        if (!loadRom(mem)) return 1;
        runProgram(mem, "10 HGR\r20 HCOLOR=3\r30 HPLOT 0,0 TO 279,191\r", "");
        const int lit = countNonZero(mem.getMemoryPointer(), 0x2000, 0x4000);
        if (lit < 100) return fail("HGR FAILED: too few framebuffer bytes after HPLOT line.");
        std::printf("hgr: OK (HPLOT line set %d bytes @ $2000)\n", lit);
    }

    // D) GR + COLOR= + PLOT/HLIN/VLIN into the $0400 lo-res page.
    {
        Memory mem; mem.initMemory();
        if (!loadRom(mem)) return 1;
        runProgram(mem, "10 GR\r20 COLOR=13\r30 PLOT 20,20\r"
                        "40 HLIN 0,39 AT 10\r50 VLIN 0,47 AT 5\r", "");
        const int lit = countNonZero(mem.getMemoryPointer(), 0x0400, 0x0800);
        if (lit < 50) return fail("LGR FAILED: too few lo-res bytes after PLOT/HLIN/VLIN.");
        std::printf("lgr: OK (PLOT+HLIN+VLIN set %d bytes @ $0400)\n", lit);
    }

    // E) HGR2 draws into the page-2 framebuffer ($4000).
    {
        Memory mem; mem.initMemory();
        if (!loadRom(mem)) return 1;
        runProgram(mem, "10 HGR2\r20 HCOLOR=3\r30 HPLOT 0,0 TO 279,191\r", "");
        const int lit = countNonZero(mem.getMemoryPointer(), 0x4000, 0x6000);
        if (lit < 100) return fail("HGR2 FAILED: page-2 framebuffer ($4000) empty.");
        std::printf("hgr2: OK (HPLOT on page 2 set %d bytes @ $4000)\n", lit);
    }

    // F) HOME/VTAB/HTAB position the GEN2 text cursor; PRINT writes there.
    // VTAB 3 -> row 2 (base $0500), HTAB 5 -> col 4 -> $0504. 'X' = $D8.
    {
        Memory mem; mem.initMemory();
        if (!loadRom(mem)) return 1;
        runProgram(mem, "10 HOME\r20 VTAB 3\r30 HTAB 5\r40 PRINT \"X\"\r", "");
        const uint8_t got = mem.getMemoryPointer()[0x0504];
        if (got != 0xD8) {
            std::fprintf(stderr, "HTAB/VTAB FAILED: $0504 = %02X (expected D8 = 'X').\n", got);
            return 1;
        }
        std::printf("home/htab/vtab: OK ('X' at row 2 col 4 -> $0504)\n");
    }

    // G) SCRN(x,y) reads back a lo-res colour; APRINT it to the Apple-1 display.
    {
        Memory mem; mem.initMemory();
        if (!loadRom(mem)) return 1;
        const std::string out =
            runProgram(mem, "10 GR\r20 COLOR=13\r30 PLOT 5,5\r40 APRINT SCRN(5,5)\r", "13");
        if (out.find("13") == std::string::npos)
            return fail("SCRN FAILED: SCRN(5,5) did not read back colour 13.");
        std::printf("scrn: OK (SCRN(5,5) -> 13)\n");
    }

    // H) Inject the shipped Tortue.apf lo-res drawing end-to-end (optional — the
    // .apf is a user sketch). Proves a real multi-line GR/COLOR=/HLIN/VLIN/PLOT
    // program (with ':' separators and "COLOR= n" spacing) runs via injection.
    {
        std::ifstream in("sketchs/gen2/applesoft_gen2/Tortue.apf", std::ios::binary);
        if (in) {
            std::string prog((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            Memory mem; mem.initMemory();
            if (!loadRom(mem)) return 1;
            runProgram(mem, prog.c_str(), "");
            const int lit = countNonZero(mem.getMemoryPointer(), 0x0400, 0x0800);
            if (lit < 100) return fail("Tortue.apf FAILED: lo-res page nearly empty after RUN.");
            std::printf("tortue.apf: OK (lo-res drawing set %d bytes @ $0400)\n", lit);
        } else {
            std::printf("tortue.apf: skipped (file not present)\n");
        }
    }

    std::printf("applesoft_gen2_smoke: OK\n");
    return 0;
}
