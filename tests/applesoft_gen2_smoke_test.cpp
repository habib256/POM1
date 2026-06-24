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
// ROM path: $POM1_ASGEN2_ROM, else roms/applesoft-gen2.rom.
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

std::string runProgram(Memory& mem, const char* listing, const std::string& token,
                       const char* coldStart = "9800R")
{
    CaptureDisplay disp;
    mem.setDisplayDevice(&disp);
    M6502 cpu(&mem);
    cpu.setProgramCounter(0xFF00);   // WOZ Monitor reset entry
    cpu.start();
    cpu.run(400000);                 // settle at the WOZ GETLN prompt

    auto type = [&](const char* s) { for (const char* p = s; *p; ++p) mem.setKeyPressed(*p); };
    type("\r");
    type(coldStart); type("\r");      // cold-start the interpreter (9800R/E000R/4000R)
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
    const char* path = env ? env : "roms/applesoft-gen2.rom";
    mem.setWriteInRom(true);
    const int rc = mem.loadBinary(path, 0x9800, nullptr);
    mem.setWriteInRom(false);
    if (rc != 0) {
        std::fprintf(stderr, "cannot load Applesoft GEN2 ROM '%s' (rc=%d). Build the "
                             "sketch and/or set POM1_ASGEN2_ROM.\n", path, rc);
        return false;
    }
    return true;
}

// Load any interpreter image into RAM at `addr` (write-protect lifted). Returns
// false if the file isn't present (so the caller can skip).
bool loadRomAt(Memory& mem, const char* path, uint16_t addr)
{
    mem.setWriteInRom(true);
    const int rc = mem.loadBinary(path, addr, nullptr);
    mem.setWriteInRom(false);
    return rc == 0;
}

// Load the TMS9918 Applesoft interpreter into the $4000-$7FFF window. It lives in
// the UPPER bank of the unified CODETANKDEV cartridge, so we take bytes
// [$4000:$8000] of the 32 KB image (a <=16 KB standalone .bin is taken whole).
// Returns false if the cartridge isn't present (so the caller can skip).
bool loadTmsApplesoft(Memory& mem)
{
    const char* env = std::getenv("POM1_ASTMS_ROM");
    const std::string path = env ? env : "roms/codetank/CODETANKDEV.rom";
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
    if (buf.empty()) return false;
    const size_t off = (buf.size() >= 0x8000) ? 0x4000 : 0;   // 32K cart -> upper bank
    const size_t n   = std::min<size_t>(buf.size() - off, 0x4000);
    std::memcpy(mem.getMemoryPointerMutable() + 0x4000, buf.data() + off, n);
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

    // I) Applesoft TMS9918 interpreter core runs: load its CodeTank image at $4000
    // (bare RAM here — the VDP card isn't needed for a non-graphics core check),
    // cold-start 4000R, APRINT 1000+7 -> 1007 on the Apple-1 display.
    {
        Memory mem; mem.initMemory();
        if (loadTmsApplesoft(mem)) {
            const std::string out = runProgram(mem, "10 APRINT 1000+7\r20 END\r", "1007", "4000R");
            if (out.find("1007") == std::string::npos)
                return fail("TMS9918 core FAILED: '1007' not printed (renumber/interpreter broken?).");
            std::printf("tms9918-core: OK (4000R + APRINT 1000+7 -> 1007)\n");
        } else std::printf("tms9918-core: skipped (applesoft-tms9918.bin not built)\n");
    }

    // K) Applesoft TMS9918 HGR with the real VDP: HGR must park the sprites (SAT
    // $3B00 sprite-0 Y = $D0) and HPLOT to x=279 must clamp to the 256-wide screen
    // (reach the right-edge cells) rather than wrapping to a narrow x=23 line. The
    // interpreter runs from $4000 RAM here (no CodeTank card needed for the bitmap).
    {
        Memory mem; mem.initMemory();
        mem.setTMS9918Enabled(true);
        if (loadTmsApplesoft(mem)) {
            runProgram(mem, "10 HGR : HCOLOR=3\r20 HPLOT 0,0 TO 279,191\r", "", "4000R");
            TMS9918::Snapshot snap;
            mem.getTMS9918().copySnapshot(snap);
            if (snap.vram[0x3B00] != 0xD0)
                return fail("TMS9918 HGR FAILED: sprites not parked (SAT $3B00 != $D0).");
            int rightLit = 0;                       // rightmost cell column (addr lo $F8-$FF)
            for (int hi = 0; hi <= 0x17; ++hi)
                for (int lo = 0xF8; lo <= 0xFF; ++lo)
                    if (snap.vram[(hi << 8) | lo]) ++rightLit;
            if (rightLit == 0)
                return fail("TMS9918 HGR FAILED: X did not reach the right edge "
                            "(x=279 not clamped to 255).");
            std::printf("tms9918-hgr: OK (sprites parked; X reaches right edge, %d bytes)\n", rightLit);
        } else std::printf("tms9918-hgr: skipped\n");
    }

    // J) Applesoft Lite (Apple-1, CFFA1 flavour) core: stock Applesoft, PRINT goes
    // to the Apple-1 terminal. Load roms/applesoft-lite-cffa1.rom at $E000, E000R.
    {
        Memory mem; mem.initMemory();
        if (loadRomAt(mem, "roms/applesoft-lite-cffa1.rom", 0xE000)) {
            const std::string out = runProgram(mem, "10 PRINT 1000+7\r20 END\r", "1007", "E000R");
            if (out.find("1007") == std::string::npos)
                return fail("CFFA1 Applesoft core FAILED: '1007' not printed.");
            std::printf("cffa1-core: OK (E000R + PRINT 1000+7 -> 1007)\n");
        } else std::printf("cffa1-core: skipped (rom not present)\n");
    }

    // L) math2026 trig: SIN/COS/TAN/ATN restored. APRINT INT(fn*1000) to the
    // Apple-1 terminal. SIN(1)=.8414->841, COS(0)=1->1000, ATN(1)=.7853->785,
    // TAN(1)=1.5574->1557. Wrong constants would miss these by a wide margin.
    {
        Memory mem; mem.initMemory();
        if (!loadRom(mem)) return 1;
        const std::string out = runProgram(mem,
            "10 APRINT INT(SIN(1)*1000)\r20 APRINT INT(COS(0)*1000)\r"
            "30 APRINT INT(ATN(1)*1000)\r40 APRINT INT(TAN(1)*1000)\r", "1557");
        if (out.find("841") == std::string::npos || out.find("1000") == std::string::npos ||
            out.find("785") == std::string::npos || out.find("1557") == std::string::npos)
            return fail("TRIG FAILED: SIN/COS/ATN/TAN did not yield 841/1000/785/1557.");
        std::printf("trig: OK (SIN/COS/ATN/TAN)\n");
    }

    // M) math2026 DEF FN / FN user functions: FN SQ(7) = 49.
    {
        Memory mem; mem.initMemory();
        if (!loadRom(mem)) return 1;
        const std::string out = runProgram(mem,
            "10 DEF FN SQ(X) = X * X\r20 APRINT FN SQ(7)\r", "49");
        if (out.find("49") == std::string::npos)
            return fail("DEF FN FAILED: FN SQ(7) did not yield 49.");
        std::printf("def fn: OK (FN SQ(7) -> 49)\n");
    }

    // N) math2026 TAB( in PRINT: TAB(5) pads to column 5 (1-based) -> 0-based col 4
    // on row 0 -> $0404. 'Z' = $5A -> screen code $DA.
    {
        Memory mem; mem.initMemory();
        if (!loadRom(mem)) return 1;
        runProgram(mem, "10 HOME\r20 PRINT TAB(5);\"Z\"\r", "");
        const uint8_t got = mem.getMemoryPointer()[0x0404];
        if (got != 0xDA) {
            std::fprintf(stderr, "TAB( FAILED: $0404 = %02X (expected DA = 'Z' at col 4).\n", got);
            return 1;
        }
        std::printf("tab(: OK ('Z' tabbed to col 4 -> $0404)\n");
    }

    // O) math2026 INVERSE/NORMAL: INVERSE 'A' lands as screen code $01 ($C1 & $3F);
    // NORMAL restores so 'B' on row 1 ($0480) is $C2.
    {
        Memory mem; mem.initMemory();
        if (!loadRom(mem)) return 1;
        runProgram(mem, "10 HOME\r20 INVERSE\r30 PRINT \"A\"\r40 NORMAL\r50 PRINT \"B\"\r", "");
        const uint8_t* m = mem.getMemoryPointer();
        if (m[0x0400] != 0x01 || m[0x0480] != 0xC2) {
            std::fprintf(stderr, "INVERSE/NORMAL FAILED: $0400=%02X (want 01), $0480=%02X (want C2).\n",
                         m[0x0400], m[0x0480]);
            return 1;
        }
        std::printf("inverse/normal: OK (inverse 'A'=$01, normal 'B'=$C2)\n");
    }

    // P) math2026 on the TMS9918 interpreter (same renumbered body as GEN2,
    // ROM-resident at $4000): trig + DEF FN run. SIN(1)*1000->841, FN D(21)->42.
    {
        Memory mem; mem.initMemory();
        if (loadTmsApplesoft(mem)) {
            const std::string out = runProgram(mem,
                "10 APRINT INT(SIN(1)*1000)\r20 DEF FN D(X) = X + X\r30 APRINT FN D(21)\r",
                "42", "4000R");
            if (out.find("841") == std::string::npos || out.find("42") == std::string::npos)
                return fail("TMS9918 math2026 FAILED: SIN/DEF FN not working on TMS port.");
            std::printf("tms9918-math2026: OK (SIN(1)->841, FN D(21)->42)\n");
        } else std::printf("tms9918-math2026: skipped\n");
    }

    // Q) math2026 INVERSE on the TMS9918 (with the VDP): INVERSE 'A' writes name
    // byte $C1 ('A'|$80), and TMS_BOOT uploads the inverse glyph set at pattern
    // $0500 (space->$FF). No C++ renderer change: inverse is real white-on-black
    // glyphs in VRAM. (FLASH falls back to inverse on this card — see tmsgfx.inc.)
    {
        Memory mem; mem.initMemory();
        mem.setTMS9918Enabled(true);
        if (loadTmsApplesoft(mem)) {
            runProgram(mem, "10 HOME\r20 INVERSE\r30 PRINT \"A\"\r", "", "4000R");
            TMS9918::Snapshot snap;
            mem.getTMS9918().copySnapshot(snap);
            if (snap.vram[0x0800] != 0xC1)
                return fail("TMS INVERSE FAILED: name $0800 != C1 (inverse 'A').");
            int invGlyph = 0;
            for (int a = 0x0500; a < 0x0800; ++a) if (snap.vram[a]) ++invGlyph;
            if (invGlyph == 0)
                return fail("TMS INVERSE FAILED: inverse font missing at pattern $0500.");
            std::printf("tms9918-inverse: OK (name=$C1; inverse font %d bytes @ $0500)\n", invGlyph);
        } else std::printf("tms9918-inverse: skipped\n");
    }

    std::printf("applesoft_gen2_smoke: OK\n");
    return 0;
}
