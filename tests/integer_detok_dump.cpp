// integer_detok_dump.cpp -- DEV TOOL: detokenise the Integer BASIC programs in
// software/Integer_basic/*.apl.txt back to source listings (.bas).
//
// It loads each Wozmon image into RAM (ZP pointers + tokenised program), reads pp
// ($CA) and himem ($4C), then walks the program bytes [pp, himem) DIRECTLY and emits
// source via a token->text table. This is lossless -- unlike capturing the ROM's
// LIST off the 40-column display, which drops a space wherever a long line wraps
// (corrupting strings). The token values come from the $E000 ROM source comments.
//
// Not a ctest. Build: `make test_integer_detok`; run from the repo root:
// `./build/tests/test_integer_detok`. Writes sketchs/basic_integer/<name>.bas.
#include "TMS9918.h"      // IWYU pragma: keep
#include "WiFiModem.h"    // IWYU pragma: keep
#include "TerminalCard.h" // IWYU pragma: keep
#include "A1IO_RTC.h"     // IWYU pragma: keep
#include "PR40Printer.h"  // IWYU pragma: keep
#include "Memory.h"
#include "M6502.h"        // IWYU pragma: keep

#include <cstdio>
#include <cstdint>
#include <fstream>
#include <string>

namespace {

bool loadIntegerRom(Memory& mem)
{
    mem.setWriteInRom(true);
    const int rc = mem.loadBinary("roms/basic.rom", 0xE000, nullptr);
    mem.setWriteInRom(false);
    return rc == 0;
}

// token -> display text (< $80). "" = unknown (printed as {$XX} so gaps show).
// Words that read better with surrounding spaces are flagged in `spaced`.
const char* tokText(uint8_t t, bool& spacedBefore, bool& spacedAfter)
{
    spacedBefore = spacedAfter = false;
    // A word keyword/function lists with a space on each side (the ROM LIST style).
    auto word = [&](const char* s) { spacedBefore = spacedAfter = true; return s; };
    switch (t) {
        // -- symbol operators / punctuation: no surrounding spaces --
        case 0x03: return ":";
        case 0x12: case 0x35: return "+";
        case 0x13: case 0x36: return "-";
        case 0x14: return "*";
        case 0x15: return "/";
        case 0x16: case 0x39: case 0x56: case 0x70: case 0x71: return "=";
        case 0x17: case 0x3A: return "#";
        case 0x18: return ">=";
        case 0x19: return ">";
        case 0x1A: return "<=";
        case 0x1B: return "<>";
        case 0x1C: return "<";
        case 0x22: case 0x2A: case 0x2D: case 0x34: case 0x38: case 0x3F: case 0x42: return "(";
        case 0x23: case 0x26: case 0x27: case 0x2B: case 0x43: case 0x44: case 0x5A:
        case 0x65: case 0x68: case 0x75: return ",";
        case 0x40: return "$";
        case 0x45: case 0x46: case 0x47: return ";";
        case 0x48: case 0x49: return ",";
        case 0x72: return ")";
        // -- word keywords / functions: spaced both sides --
        case 0x09: return word("DEL");
        case 0x0A: case 0x0E: return ",";
        case 0x0B: return word("NEW");
        case 0x0C: return word("CLR");
        case 0x0D: return word("AUTO");
        case 0x1D: return word("AND");
        case 0x1E: return word("OR");
        case 0x1F: return word("MOD");
        case 0x24: case 0x25: return word("THEN");
        case 0x2E: return word("PEEK");
        case 0x2F: return word("RND");
        case 0x30: return word("SGN");
        case 0x31: return word("ABS");
        case 0x37: return word("NOT");
        case 0x3B: spacedBefore = true; return "LEN(";
        case 0x4D: return word("CALL");
        case 0x4E: case 0x4F: return word("DIM");
        case 0x50: return word("TAB");
        case 0x51: return word("END");
        case 0x52: case 0x53: case 0x54: return word("INPUT");
        case 0x55: return word("FOR");
        case 0x57: return word("TO");
        case 0x58: return word("STEP");
        case 0x59: return word("NEXT");
        case 0x5B: return word("RETURN");
        case 0x5C: return word("GOSUB");
        case 0x5E: return word("LET");
        case 0x5F: return word("GOTO");
        case 0x60: return word("IF");
        case 0x61: case 0x62: case 0x63: return word("PRINT");
        case 0x64: return word("POKE");
        case 0x74: case 0x76: return word("LIST");
        default: return "";
    }
}

void appendSpaced(std::string& out, const char* s, bool before, bool after)
{
    if (before && !out.empty() && out.back() != ' ') out += ' ';
    out += s;
    if (after) out += ' ';
}

// Walk one program line's token bytes (excluding the trailing $01) into source.
std::string detokLine(const uint8_t* t, int n)
{
    std::string out;
    bool remTail = false;   // REM ran to end of line: its trailing space is significant
    int i = 0;
    while (i < n) {
        uint8_t b = t[i];
        if (b == 0x28) {                                   // string literal: " ... "
            out += '"'; ++i;
            while (i < n && t[i] != 0x29) { out += static_cast<char>(t[i] & 0x7F); ++i; }
            out += '"'; if (i < n) ++i;                    // skip $29
        } else if (b == 0x5D) {                            // REM: rest of line is literal text
            out += "REM"; ++i; remTail = true;
            while (i < n) { out += static_cast<char>(t[i] & 0x7F); ++i; }
        } else if (b >= 0xB0 && b <= 0xB9) {               // numeric constant: marker + 16-bit
            int val = (i + 2 < n) ? (t[i + 1] | (t[i + 2] << 8)) : 0;
            std::string num = std::to_string(val);
            // The marker byte is the FIRST digit as typed; if it is '0' but the value
            // doesn't start with '0', the source had a leading zero (e.g. "POKE x,01").
            char mark = static_cast<char>(b & 0x7F);
            if (mark == '0' && !num.empty() && num[0] != '0') num = "0" + num;
            out += num;
            i += 3;
        } else if (b >= 0xC0) {                            // variable: run of high-bit chars
            while (i < n && t[i] >= 0x80) { out += static_cast<char>(t[i] & 0x7F); ++i; }
        } else {                                           // a token
            bool sb, sa;
            const char* s = tokText(b, sb, sa);
            if (*s) appendSpaced(out, s, sb, sa);
            else { char buf[8]; std::snprintf(buf, sizeof(buf), "{$%02X}", b); out += buf; }
            ++i;
        }
    }
    // NOTE: do NOT collapse interior spaces -- string-literal and REM content must
    // stay byte-verbatim (the round-trip compares the tokenised image). Trim a trailing
    // space left by a spaced keyword, but NOT when REM ran to end of line (its trailing
    // space is part of the stored REM text).
    if (!remTail) while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// Load an .apl image and detokenise its program [pp, himem) to a .bas listing.
std::string detokProgram(const std::string& aplPath, int& lineCount)
{
    lineCount = 0;
    Memory mem; mem.initMemory();
    if (!loadIntegerRom(mem)) return {};
    uint16_t runAddr = 0;
    if (mem.loadHexDump(aplPath.c_str(), runAddr, nullptr, nullptr) != 0) return {};

    const uint8_t* m = mem.getMemoryPointer();
    uint16_t himem = m[0x4C] | (m[0x4D] << 8);
    uint16_t pp    = m[0xCA] | (m[0xCB] << 8);
    if (pp == 0 || pp >= himem || himem == 0) return {};

    std::string out;
    uint16_t p = pp;
    while (p < himem) {
        uint8_t len = m[p];
        if (len < 4 || p + len > himem) break;             // malformed
        int lineNo = m[p + 1] | (m[p + 2] << 8);
        int ntok = len - 4;                                // bytes between line# and the $01
        out += "   " + std::to_string(lineNo) + " " + detokLine(m + p + 3, ntok) + "\n";
        ++lineCount;
        p += len;
    }
    return out;
}

struct Prog { const char* apl; const char* bas; };
const Prog kProgs[] = {
    { "software/Integer_basic/hamurabi.apl.txt",                    "sketchs/basic_integer/hamurabi.bas" },
    { "software/Integer_basic/lunar-lander-ascii-graphics.apl.txt", "sketchs/basic_integer/lunar-lander.bas" },
    { "software/Integer_basic/mini-startrek.apl.txt",               "sketchs/basic_integer/mini-startrek.bas" },
    { "software/Integer_basic/resistor-calculator.apl.txt",         "sketchs/basic_integer/resistor-calculator.bas" },
    { "software/Integer_basic/stopwatch.apl.txt",                   "sketchs/basic_integer/stopwatch.bas" },
    { "software/Integer_basic/twinkle.apl.txt",                     "sketchs/basic_integer/twinkle.bas" },
    { "software/Integer_basic/blackjack.apl.txt",                   "sketchs/basic_integer/blackjack.bas" },
};

} // namespace

int main()
{
    for (const Prog& p : kProgs) {
        int n = 0;
        std::string src = detokProgram(p.apl, n);
        if (src.empty()) { std::printf("SKIP %s (load failed)\n", p.apl); continue; }
        std::ofstream(p.bas, std::ios::binary).write(src.data(), static_cast<std::streamsize>(src.size()));
        std::printf("%-48s -> %-42s  %d lines, %zu bytes\n", p.apl, p.bas, n, src.size());
    }
    return 0;
}
