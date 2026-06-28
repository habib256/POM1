// BasicTokeniserApplesoft.cpp -- see BasicTokeniserApplesoft.h for the design.
//
// The tokenizer reproduces applesoft-{tms9918,gen2}.s : PARSE_INPUT_LINE exactly:
//   * the input line is upper-cased (the Apple-1 keyboard the ROM reads is
//     upper-case only, so the live "type the listing" path sees upper-case too);
//   * blanks are ignored except inside a string literal or a DATA statement;
//   * '?' is shorthand for PRINT;
//   * bytes '0'..';' (0x30..0x3B: digits, ':', ';') are copied verbatim, never
//     tokenized -- this is what keeps GOTO/GOSUB line numbers as ASCII digits;
//   * otherwise the reserved-word table is scanned IN ORDER and the first keyword
//     that matches as a prefix (blanks in the input skipped mid-match) wins --
//     prefix-greedy, which is why HGR2 precedes HGR and COLOR= carries its '=';
//   * REM copies the rest of the line literally; DATA copies literally up to ':'.
// The reserved-word table and token values are identical for the GEN2 and TMS9918
// interpreters, so one table drives both.

#include "BasicTokeniserApplesoft.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace basic {

namespace {

// Applesoft token bytes referenced by the tokenizer's control flow.
constexpr uint8_t TOK_DATA  = 0x83;
constexpr uint8_t TOK_REM   = 0x94;
constexpr uint8_t TOK_PRINT = 0x98;

// The reserved-word table, in the EXACT order the ROM scans it. Index i maps to
// token byte 0x80 + i. Order is load-bearing: longer keywords that share a prefix
// (HGR2/HGR, GR2/GR, COLOR=/...) MUST come first so the prefix-greedy match picks
// them. Mirrors TOKEN_NAME_TABLE in applesoft-tms9918.s / applesoft-gen2.s.
const char* const kKeywords[] = {
    "END", "FOR", "NEXT", "DATA", "INPUT", "DIM", "READ", "CALL", "POP",
    "HIMEM:", "LOMEM:", "ONERR", "RESUME", "LET", "GOTO", "RUN", "IF",
    "RESTORE", "GOSUB", "RETURN", "REM", "STOP", "ON", "POKE", "PRINT",
    "CONT", "LIST", "CLEAR", "GET", "NEW",
    "HGR2", "HGR", "GR2", "GR", "TEXT", "CLS", "COLOR=", "HCOLOR=", "PLOT",
    "HLIN", "VLIN", "HPLOT", "SHOW", "VBL", "HOME", "HTAB", "VTAB", "APRINT",
    "MIX", "NOMIX", "NORMAL", "INVERSE", "FLASH", "DEF",
    "TO", "SPC(", "TAB(", "THEN", "NOT", "STEP",
    "+", "-", "*", "/", "^", "AND", "OR", ">", "=", "<", "FN",
    "SGN", "INT", "ABS", "FRE", "SQR", "RND", "LOG", "EXP",
    "SIN", "COS", "TAN", "ATN", "PEEK", "LEN", "STR$", "VAL", "ASC", "CHR$",
    "LEFT$", "RIGHT$", "MID$", "SCRN",
};
constexpr int kKeywordCount = static_cast<int>(sizeof(kKeywords) / sizeof(kKeywords[0]));

// Applesoft Lite reserved words (microSD / CFFA1), token byte 0x80 + index, in the
// exact order of TOKEN_NAME_TABLE in sketchs/apple1/applesoft_lite/applesoft-lite.s.
// Shares the $80-$98 prefix with the graphics table but then diverges: no graphics
// keywords, adds MENU/SAVE/LOAD/CLS, and a reduced function set (no TAB(/FN/DEF/
// COS/TAN/ATN/SCRN). `ONERR` precedes `ON` so the prefix-greedy scan is correct.
const char* const kKeywordsLite[] = {
    "END", "FOR", "NEXT", "DATA", "INPUT", "DIM", "READ", "CALL", "POP",
    "HIMEM:", "LOMEM:", "ONERR", "RESUME", "LET", "GOTO", "RUN", "IF",
    "RESTORE", "GOSUB", "RETURN", "REM", "STOP", "ON", "POKE", "PRINT",
    "CONT", "LIST", "CLEAR", "GET", "NEW", "MENU", "SAVE", "LOAD", "CLS",
    "TO", "SPC(", "THEN", "NOT", "STEP",
    "+", "-", "*", "/", "^", "AND", "OR", ">", "=", "<",
    "SGN", "INT", "ABS", "FRE", "SQR", "RND", "LOG", "EXP",
    "PEEK", "LEN", "STR$", "VAL", "ASC", "CHR$", "LEFT$", "RIGHT$", "MID$",
};
constexpr int kKeywordLiteCount = static_cast<int>(sizeof(kKeywordsLite) / sizeof(kKeywordsLite[0]));

// Try to match a reserved word at s[i]. On success returns the token byte and
// sets `consumed` to the number of input chars eaten (blanks included). Scans the
// table in order and takes the first full prefix match, exactly like the ROM.
bool matchKeyword(const std::string& s, size_t i, uint8_t& token, size_t& consumed,
                  const char* const* keywords, int keywordCount)
{
    for (int k = 0; k < keywordCount; ++k) {
        const char* kw = keywords[k];
        size_t j = i;
        bool ok = true;
        for (const char* p = kw; *p; ++p) {
            while (j < s.size() && s[j] == ' ') ++j;   // blanks in input are skipped
            if (j >= s.size() || s[j] != *p) { ok = false; break; }
            ++j;
        }
        if (ok) {
            token = static_cast<uint8_t>(0x80 + k);
            consumed = j - i;
            return true;
        }
    }
    return false;
}

// Tokenize one statement body (the text AFTER the line number) into `out`, against
// the given reserved-word table (graphics or Lite — see Dialect).
void tokenizeBody(const std::string& s, std::vector<uint8_t>& out,
                  const char* const* keywords, int keywordCount)
{
    bool dataMode = false;
    size_t i = 0;
    while (i < s.size()) {
        char c = s[i];

        if (!dataMode && c == ' ') { ++i; continue; }   // ignore blanks outside DATA

        if (c == '"') {                                  // string literal -> copy verbatim
            out.push_back('"'); ++i;
            while (i < s.size() && s[i] != '"') { out.push_back(static_cast<uint8_t>(s[i])); ++i; }
            if (i < s.size()) { out.push_back('"'); ++i; }
            continue;
        }

        if (dataMode) {                                  // inside DATA: literal until ':'
            out.push_back(static_cast<uint8_t>(c));
            if (c == ':') dataMode = false;
            ++i;
            continue;
        }

        if (c == '?') { out.push_back(TOK_PRINT); ++i; continue; }   // ? == PRINT

        if (c >= '0' && c <= ';') {                       // digits / ':' / ';' : never tokens
            out.push_back(static_cast<uint8_t>(c));
            ++i;
            continue;
        }

        uint8_t tok = 0;
        size_t consumed = 0;
        if (matchKeyword(s, i, tok, consumed, keywords, keywordCount)) {
            out.push_back(tok);
            i += consumed;
            if (tok == TOK_DATA) dataMode = true;
            if (tok == TOK_REM) {                         // REM: copy rest of line literally
                while (i < s.size()) { out.push_back(static_cast<uint8_t>(s[i])); ++i; }
            }
            continue;
        }

        out.push_back(static_cast<uint8_t>(c));           // not a keyword: copy as-is
        ++i;
    }
}

struct Line {
    int                  number = 0;
    std::vector<uint8_t> tokens;
};

void appendHexZone(std::string& hex, uint16_t addr, const std::vector<uint8_t>& bytes)
{
    auto hx = [](int v) -> char { return static_cast<char>(v < 10 ? '0' + v : 'A' + v - 10); };
    size_t i = 0;
    while (i < bytes.size()) {
        uint16_t a = static_cast<uint16_t>(addr + i);
        hex += hx((a >> 12) & 0xF); hex += hx((a >> 8) & 0xF);
        hex += hx((a >> 4) & 0xF);  hex += hx(a & 0xF); hex += ':';
        for (int n = 0; n < 16 && i < bytes.size(); ++n, ++i) {
            hex += ' ';
            hex += hx((bytes[i] >> 4) & 0xF);
            hex += hx(bytes[i] & 0xF);
        }
        hex += '\n';
    }
}

} // namespace

Target targetGen2()
{
    // Extracted via ld65 -Ln from the restored applesoft-gen2.s built at $9800;
    // matches roms/applesoft-gen2.rom (SETPTRS/NEWSTT well below the only drift).
    Target t;
    t.name = "Applesoft GEN2 (9800R)";
    t.setptrs = 0x9D96;
    t.newstt  = 0x9EF2;
    t.himem   = 0x2000;       // page-1 hi-res floor (default ceiling)
    t.page2Floor = 0x4000;    // page-2 hi-res floor (HGR2-only programs may reach it)
    return t;
}

Target targetTms()
{
    // Extracted via ld65 -Ln from applesoft-tms9918.s built at $4000; byte-for-byte
    // identical to the upper bank of roms/codetank/CODETANKDEV.rom.
    Target t;
    t.name = "Applesoft TMS9918 (4000R)";
    t.setptrs = 0x4596;
    t.newstt  = 0x46F2;
    t.himem   = 0x4000;
    return t;
}

Target targetMicrosd()
{
    // Extracted via ld65 -Ln from applesoft-lite.s relinked at $6000 (the microSD
    // flavour = the Apple-1 Applesoft Lite relocated). Matches the regenerated
    // roms/applesoft-lite-microsd.rom byte-for-byte (see applesoft_lite_microsd.cfg).
    Target t;
    t.name = "Applesoft Lite microSD (6000R)";
    t.setptrs = 0x64EC;
    t.newstt  = 0x6648;
    t.himem   = 0x6000;   // probed at cold start; the $6000 ROM floor (informational)
    t.dialect = Dialect::Lite;
    return t;
}

Target targetCffa1()
{
    // Extracted via ld65 -Ln from applesoft-lite.s + cffa1.s built at $E000;
    // byte-for-byte identical to roms/applesoft-lite-cffa1.rom.
    Target t;
    t.name = "Applesoft Lite CFFA1 (E000R)";
    t.setptrs = 0xE4EC;
    t.newstt  = 0xE648;
    t.himem   = 0xE000;   // probed at cold start; the $E000 ROM floor (informational)
    t.dialect = Dialect::Lite;
    return t;
}

Result compile(const std::string& source, const Target& tgt)
{
    Result r;

    // Pick the reserved-word table for this target's interpreter dialect.
    const char* const* keywords = (tgt.dialect == Dialect::Lite) ? kKeywordsLite : kKeywords;
    const int keywordCount       = (tgt.dialect == Dialect::Lite) ? kKeywordLiteCount : kKeywordCount;

    // 1) Split into lines, parse the leading line number, tokenize the body.
    std::vector<Line> lines;
    size_t pos = 0;
    int physical = 0;
    while (pos <= source.size()) {
        size_t nl = source.find('\n', pos);
        std::string raw = (nl == std::string::npos) ? source.substr(pos)
                                                     : source.substr(pos, nl - pos);
        pos = (nl == std::string::npos) ? source.size() + 1 : nl + 1;
        ++physical;

        // Strip CR and upper-case (the ROM only ever sees upper-case input).
        std::string line;
        line.reserve(raw.size());
        for (char ch : raw) {
            if (ch == '\r') continue;
            line += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }

        // Skip leading blanks, then the line number.
        size_t j = 0;
        while (j < line.size() && line[j] == ' ') ++j;
        if (j >= line.size()) continue;                  // blank line

        if (!(line[j] >= '0' && line[j] <= '9')) {
            r.error = "line " + std::to_string(physical) +
                      ": Applesoft program lines must start with a line number";
            return r;
        }
        long num = 0;
        while (j < line.size() && line[j] >= '0' && line[j] <= '9') {
            num = num * 10 + (line[j] - '0');
            ++j;
            // Bound inside the loop: a long unbounded digit run would overflow
            // signed long (UB) and wrap to a value that passes the post-loop
            // check. Mirrors the Integer tokeniser's guard.
            if (num > 63999) {
                r.error = "line " + std::to_string(physical) + ": line number > 63999";
                return r;
            }
        }

        Line L;
        L.number = static_cast<int>(num);
        tokenizeBody(line.substr(j), L.tokens, keywords, keywordCount);
        lines.push_back(std::move(L));
    }

    if (lines.empty()) { r.error = "no BASIC lines to compile"; return r; }

    // Applesoft stores lines in ascending order; FNDLIN/GOTO rely on it. Stable
    // sort so an accidental duplicate keeps source order.
    std::stable_sort(lines.begin(), lines.end(),
                     [](const Line& a, const Line& b) { return a.number < b.number; });

    // 2) Lay out the tokenized image at $0801 with absolute forward links.
    std::vector<uint8_t> prog;                            // bytes starting at $0800
    prog.push_back(0x00);                                 // $0800 guard

    // Pick the image ceiling. Default = himem (the page-1 hi-res floor on GEN2).
    // If the target has a page-2 framebuffer AND the program draws ONLY to page 2
    // (uses HGR2, never HGR), the idle page-1 region is fair game for code/data, so
    // the image may extend up to page2Floor ($4000) instead of $2000. Tokens are
    // 0x80+index into the active keyword table; resolve HGR/HGR2 there rather than
    // hard-coding bytes (the Lite table has no HGR2, so this stays page-1-only).
    size_t ceiling = tgt.himem ? tgt.himem : 0x10000u;
    if (tgt.page2Floor) {
        uint8_t tokHGR = 0, tokHGR2 = 0;
        for (int k = 0; k < keywordCount; ++k) {
            if (std::strcmp(keywords[k], "HGR")  == 0) tokHGR  = static_cast<uint8_t>(0x80 + k);
            if (std::strcmp(keywords[k], "HGR2") == 0) tokHGR2 = static_cast<uint8_t>(0x80 + k);
        }
        bool usesHGR = false, usesHGR2 = false;
        for (const Line& L : lines)
            for (uint8_t b : L.tokens) {
                if (tokHGR  && b == tokHGR)  usesHGR  = true;
                if (tokHGR2 && b == tokHGR2) usesHGR2 = true;
            }
        if (usesHGR2 && !usesHGR) ceiling = tgt.page2Floor;
    }

    uint16_t cur = kProgramOrigin;                        // $0801
    for (const Line& L : lines) {
        // Compute in a wide type so a single >64 KB line can't wrap lineLen/next
        // past the check. The program is followed by a 2-byte end-of-program marker
        // ($00 $00) at `next`/`next+1` and VARTAB at next+2, so every emitted byte
        // must stay below the ceiling (HIMEM, e.g. the GEN2 framebuffer floor; the
        // 16-bit space otherwise) -- guard next+2, not next, or the trailing marker
        // can still land in the framebuffer. Otherwise garbage links emit with ok=true.
        const size_t lineLen = 4 + L.tokens.size() + 1;
        const size_t next = static_cast<size_t>(cur) + lineLen;
        if (next + 2 > ceiling) {
            r.error = "program too large: line " + std::to_string(L.number) +
                      " overflows available memory";
            return r;
        }
        const uint16_t next16 = static_cast<uint16_t>(next);
        prog.push_back(next16 & 0xFF);                    // forward link -> next line
        prog.push_back((next16 >> 8) & 0xFF);
        prog.push_back(L.number & 0xFF);                  // line number
        prog.push_back((L.number >> 8) & 0xFF);
        for (uint8_t b : L.tokens) prog.push_back(b);     // tokens
        prog.push_back(0x00);                             // end-of-line
        cur = next16;
    }
    prog.push_back(0x00);                                 // end-of-program link ($0000)
    prog.push_back(0x00);
    const uint16_t progEnd = static_cast<uint16_t>(cur + 2);   // VARTAB

    // 3) Launcher: install VARTAB, then enter the interpreter's run loop.
    //    LDA #<end ; STA $69 ; LDA #>end ; STA $6A ; JSR SETPTRS ; JMP NEWSTT
    std::vector<uint8_t> launcher = {
        0xA9, static_cast<uint8_t>(progEnd & 0xFF),
        0x85, 0x69,
        0xA9, static_cast<uint8_t>((progEnd >> 8) & 0xFF),
        0x85, 0x6A,
        0x20, static_cast<uint8_t>(tgt.setptrs & 0xFF), static_cast<uint8_t>((tgt.setptrs >> 8) & 0xFF),
        0x4C, static_cast<uint8_t>(tgt.newstt & 0xFF),  static_cast<uint8_t>((tgt.newstt >> 8) & 0xFF),
    };

    r.zones.push_back({0x0800, std::move(prog)});
    r.zones.push_back({kLauncherAddr, launcher});
    r.entry = kLauncherAddr;
    r.progEnd = progEnd;
    r.lineCount = static_cast<int>(lines.size());
    r.ok = true;

    // Wozmon-style hex (loadable via Memory::loadHexDump); last token sets run addr.
    for (const Zone& z : r.zones) appendHexZone(r.hex, z.addr, z.bytes);
    auto hx = [](int v) -> char { return static_cast<char>(v < 10 ? '0' + v : 'A' + v - 10); };
    r.hex += hx((r.entry >> 12) & 0xF); r.hex += hx((r.entry >> 8) & 0xF);
    r.hex += hx((r.entry >> 4) & 0xF);  r.hex += hx(r.entry & 0xF);
    r.hex += "R\n";
    return r;
}

} // namespace basic
