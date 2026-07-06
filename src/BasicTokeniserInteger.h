// BasicTokeniserInteger.h -- Apple-1 Integer BASIC (Woz) tokeniser for POM1.
//
// Turns an Integer BASIC listing (.bas) into the exact in-memory program image the
// $E000 ROM's syntax-table parser would build, so a program can be LOADED + RUN
// directly with no slow keyboard injection -- the Integer counterpart of
// BasicTokeniserApplesoft. The $E000 ROM (roms/basic.rom == sketchs/apple1/
// integer_basic/integer-basic.s, byte-identical) is the REFERENCE; this port is
// verified against it by the ROM oracle (tests/integer_oracle_dump.cpp).
//
// Integer BASIC tokenisation is CONTEXT-SENSITIVE (unlike Applesoft's flat keyword
// scan): the same character emits different tokens by grammatical context --
// "(" has 7 tokens, "," ~10, "=" 4 ($16 num-cmp / $39 str-cmp / $56 FOR / $71 LET),
// PRINT 3 ($61 str / $62 num / $63 none). So this is a small recursive parser, not
// a table lookup.
//
// Image format (verified via the ROM oracle):
//   * Program lives high: stored from HIMEM ($1000 default) DOWNWARD; `pp` ($CA)
//     points at the lowest (first) line. The image is [pp, HIMEM), lines ascending.
//   * Each line: [length][line# lo][line# hi][tokens...][$01]
//       length = total bytes in the line INCLUDING the length byte and the $01.
//   * Keyword/operator tokens are single bytes < $80 (context-specific).
//   * Numeric constant: [first-digit-char | $80][value lo][value hi]  (16-bit binary)
//       e.g. 42 -> B4 2A 00 ; 100 -> B1 64 00
//   * Variable: each char | $80 (letter, then letters/digits): A -> C1, I -> C9.
//   * String literal: $28 [chars | $80] $29.
//   * $01 terminates each line.
//
// To run an injected image: cold-start the ROM (init zero page), write [pp, HIMEM),
// set pp ($CA/$CB), then JMP run_warm ($E836). cold $E2B0, warm $E2B3.
//
// Pure: depends on <string>/<vector>/<cstdint> only (links into bench + tests).

#ifndef POM1_BASIC_TOKENISER_INTEGER_H
#define POM1_BASIC_TOKENISER_INTEGER_H

#include <cstdint>
#include <string>
#include <vector>

namespace ibasic {

// Apple-1 Integer BASIC ROM constants ($E000 / roms/basic.rom).
constexpr uint16_t kHimemDefault = 0x1000;  // cold-start HIMEM (program ceiling)
constexpr uint16_t kLomemDefault = 0x0800;  // cold-start LOMEM (variables floor)
constexpr uint16_t kRunWarm      = 0xE836;  // run entry (reads pp, runs the program)
constexpr uint16_t kColdStart    = 0xE000;  // JMP cold ($E2B0)
constexpr uint16_t kWarmStart    = 0xE2B3;  // warm re-entry: `>` prompt, program kept
constexpr uint16_t kPpZp         = 0x00CA;  // pp: low end (text start) of program
constexpr uint16_t kEol          = 0x01;    // end-of-line token

struct Result {
    bool                 ok = false;
    std::string          error;        // non-empty on failure (line-numbered)
    std::vector<uint8_t> image;         // the tokenised program: bytes for [pp, HIMEM)
    uint16_t             pp = 0;         // load address of `image` (= HIMEM - image.size())
    uint16_t             himem = kHimemDefault;
    int                  lineCount = 0;
    // Wozmon-style hex dump (image @ pp). Run address is NOT the image (you must
    // set pp + JMP run_warm); provided for inspection / SAVE.
    std::string          hex;
};

// Tokenise an Integer BASIC listing for HIMEM `himem` (default $1000). Never throws;
// returns ok=false + error on a structural problem.
Result compile(const std::string& source, uint16_t himem = kHimemDefault);

} // namespace ibasic

#endif // POM1_BASIC_TOKENISER_INTEGER_H
