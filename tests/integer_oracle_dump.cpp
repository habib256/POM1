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
    "10 PRINT 42\n",
    "10 PRINT 6*7\n",
    "10 LET A=5\n20 PRINT A\n",
    "10 FOR I=1 TO 10\n20 PRINT I\n30 NEXT I\n",
    "10 IF A#3 THEN 100\n",
    "10 PRINT \"HELLO\"\n",
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
