// integer_oracle_dump.cpp -- DEV ORACLE for the Integer BASIC tokeniser.
//
// Feeds Integer BASIC source lines to the REAL $E000 ROM (roms/basic.rom) running
// headless, lets the ROM's own syntax-table parser tokenise them into the program
// area ($0800..pp), then dumps that tokenised image as hex. This is the byte-exact
// ground truth the C++ port (BasicTokeniserInteger) must reproduce.
//
// Not a ctest -- a developer dumper. Build: `make test_integer_oracle`, run:
// `./build/tests/test_integer_oracle`. Edit kSamples to probe more programs.
#include "TMS9918.h"      // IWYU pragma: keep
#include "WiFiModem.h"    // IWYU pragma: keep
#include "TerminalCard.h" // IWYU pragma: keep
#include "A1IO_RTC.h"     // IWYU pragma: keep
#include "PR40Printer.h"  // IWYU pragma: keep
#include "Memory.h"
#include "M6502.h"
#include "DisplayDevice.h"

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace {

class CaptureDisplay : public DisplayDevice {
public:
    void onChar(char c) override { text.push_back(c); }
    std::string text;
};

bool loadIntegerRom(Memory& mem)
{
    for (const char* pre : {"", "../", "../../"}) {
        mem.setWriteInRom(true);
        const int rc = mem.loadBinary((std::string(pre) + "roms/basic.rom").c_str(), 0xE000, nullptr);
        mem.setWriteInRom(false);
        if (rc == 0) return true;
    }
    return false;
}

// Cold-start Integer BASIC and type `src` (one line per '\n'), letting the ROM
// tokenise it. Returns the tokenised program bytes [lomem, pp) + sets lomem/pp.
std::vector<uint8_t> tokeniseViaRom(Memory& mem, M6502& cpu, CaptureDisplay& disp,
                                    const std::string& src, uint16_t& lomem, uint16_t& pp)
{
    cpu.setProgramCounter(0xE000);   // Integer BASIC cold start
    cpu.start();
    // Run to the '>' prompt.
    for (long long c = 0; c < 20000000LL; c += 100000) {
        cpu.run(100000);
        if (disp.text.find('>') != std::string::npos) break;
    }
    // Type the listing, a line at a time, running the CPU so the parser consumes it.
    auto typeKey = [&](char ch) {
        mem.setKeyPressed(ch);
        for (int i = 0; i < 40; ++i) cpu.run(100000);   // let read_line + parse settle
    };
    for (char ch : src) typeKey(ch == '\n' ? '\r' : ch);
    if (!src.empty() && src.back() != '\n') typeKey('\r');

    // Integer BASIC stores the PROGRAM from himem DOWNWARD; pp is its low end (text
    // start). The program text is [pp, himem). (Variables grow up from lomem.)
    const uint8_t* m = mem.getMemoryPointer();
    lomem = m[0x4A] | (m[0x4B] << 8);
    const uint16_t himem = m[0x4C] | (m[0x4D] << 8);
    pp    = m[0xCA] | (m[0xCB] << 8);
    std::vector<uint8_t> out;
    for (uint16_t a = pp; a < himem; ++a) out.push_back(m[a]);
    return out;
}

const char* const kSamples[] = {
    "10 A1=5\n",           // PROBE: letter+digit
    "10 A9=5\n",           // PROBE: letter+digit
    "10 AB=5\n",           // PROBE: letter+letter
    "10 A1B=5\n",          // PROBE: letter+digit+letter
    "10 SCORE=5\n",        // PROBE: long name
    "10 PRINT 42\n",
    "10 PRINT 6*7\n",
    "10 LET A=5\n20 PRINT A\n",
    "10 FOR I=1 TO 10\n20 PRINT I\n30 NEXT I\n",
    "10 IF A#3 THEN 100\n",
    "10 PRINT \"HELLO\"\n",
    "10 A=5\n",                       // implicit LET
    "10 GOTO 100\n",
    "10 GOSUB 200\n20 RETURN\n",
    "10 END\n",
    "20 PRINT A;B\n",                 // ; separator + multiple numeric
    "30 PRINT \"X=\";A\n",            // string then ; then numeric
    "10 A=B+C-D\n",                   // arithmetic chain
    "10 IF A<3 THEN 100\n",           // < comparison
    "10 IF A>3 THEN 100\n",           // > comparison
    "10 A=A+1: PRINT A\n",            // multi-statement (':')
    "10 INPUT A\n",
    "10 DIM A(5)\n",
    "10 PRINT A(3)\n",                // array subscript
    "10 REM HELLO\n",                 // REM
    "10 POKE 0,1\n",
    "10 A$=\"HI\"\n",                 // string assignment
    "10 A=PEEK(0)\n",                 // PEEK function
    // --- PRINT separator probes ($45/$46/$47/$48/$49) ---
    "10 PRINT A,B\n",                 // numeric , numeric
    "10 PRINT \"A\",\"B\"\n",         // string , string
    "10 PRINT \"A\";\"B\"\n",         // string ; string
    "10 PRINT A;\n",                  // trailing ; after numeric
    "10 PRINT \"A\";\n",              // trailing ; after string
    "10 PRINT \"A\",B\n",             // string , numeric
    "10 PRINT A;\"B\"\n",             // numeric ; string
    "10 PRINT A,\"B\"\n",             // numeric , string
    // --- REM leading-space probes ---
    "1 REM HI\n",                     // REM with single space before text (no space before REM)
    "1  REM HI\n",                    // extra space BEFORE rem
    "1 REM  HI\n",                    // two spaces after REM
    // --- TAB with parens ---
    "10 TAB (4): PRINT \"X\"\n",      // TAB (n) parenthesised
    "10 TAB 11\n",                    // TAB n bare
    // --- string array element dest A$(1)="FOO" ---
    "10 A$(1)=\"FOO\"\n",             // string-array dest "(" $42
    "10 A$(I)=\"X\"\n",
    // --- resistor / startrek style constructs ---
    "10 IF A=1 THEN PRINT \"X\": PRINT \"Y\"\n", // THEN stmt then : stmt
    "10 PRINT \"VAL=\",A\n",
    "10 A=B*C/D\n",
    "10 FOR I=1 TO N STEP 2\n",
    "10 IF A<>B THEN 50\n",
    // --- EXACT failing source lines (verify .bas vs .apl drift) ---
    "1 REM \"STOPWATCH\"\n",                                  // stopwatch L1 (no trailing space in src)
    "11 PRINT \" --- 4 BAND CODE ---\"\n",                   // resistor L11 (1 leading space in src)
    "210 PRINT \"YOU'D BETTER TRY HARDER,\";C$;\".\"\n",     // blackjack L210 (no trailing space in src)
    "900 POKE 751,1: CALL 750: RETURN\n",                    // lunar L900 (,1 in src)
    "20 Q=640:S=704:D$=\" * >!<+++<*>\"\n",                   // startrek L20 (1 leading space in src)
};

} // namespace

int main()
{
    for (const char* src : kSamples) {
        Memory mem; mem.initMemory();
        if (!loadIntegerRom(mem)) { std::fprintf(stderr, "SKIP: roms/basic.rom not found\n"); return 77; }
        CaptureDisplay disp; mem.setDisplayDevice(&disp);
        M6502 cpu(&mem);
        uint16_t lomem = 0, pp = 0;
        std::vector<uint8_t> img = tokeniseViaRom(mem, cpu, disp, src, lomem, pp);
        mem.setDisplayDevice(nullptr);
        {
            std::string t = disp.text; std::string vis;
            for (char c : t) { if (c=='\r'||c=='\n') vis+="\\n"; else if (c>=32&&c<127) vis+=c; else { char b[8]; std::snprintf(b,8,"[%02X]",(unsigned char)c); vis+=b; } }
            std::printf("   DISPLAY: %s\n", vis.c_str());
        }

        std::printf("---- source: ");
        for (const char* p = src; *p; ++p) std::printf("%s", *p == '\n' ? " / " : std::string(1, *p).c_str());
        std::printf("\n   lomem=$%04X pp=$%04X  (%zu bytes)\n   ", lomem, pp, img.size());
        for (size_t i = 0; i < img.size(); ++i) {
            std::printf("%02X ", img[i]);
            if ((i & 15) == 15) std::printf("\n   ");
        }
        std::printf("\n");
    }
    return 0;
}
