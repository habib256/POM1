// basic_compiler_tokenize_test.cpp -- byte-exact unit pin for the Applesoft
// "BASIC compiler" tokenizer + image layout (src/BasicTokeniserApplesoft.cpp). Pure: links
// only BasicTokeniserApplesoft.cpp (no emulator core, no ROM), so it always runs and is
// fast. The sibling basic_compiler_smoke test proves end-to-end EXECUTION on the
// GEN2/TMS interpreters; this one pins the exact tokenized bytes so a tokenizer
// regression is caught precisely, independent of any ROM.
//
// Expected bytes are the Applesoft on-disk layout: a $00 guard sits at $0800;
// each line is [linkLo][linkHi][numLo][numHi][tokens...][$00] where the forward
// link is the absolute address of the next line (= start + 4 + #tokens + 1); the
// program ends with a $00 $00 link. Single-line programs start at $0801, so the
// first (only) line's link is the end-marker address. Token values mirror
// TOKEN_NAME_TABLE in applesoft-{tms9918,gen2}.s.

#include "BasicTokeniserApplesoft.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

const std::vector<uint8_t>* zoneAt(const basic::Result& r, uint16_t addr)
{
    for (const basic::Zone& z : r.zones) if (z.addr == addr) return &z.bytes;
    return nullptr;
}

void expectBytes(const char* what, const std::vector<uint8_t>& got,
                 const std::vector<uint8_t>& want)
{
    if (got == want) { std::printf("ok: %s\n", what); return; }
    ++failures;
    std::printf("FAIL: %s\n  got :", what);
    for (uint8_t b : got)  std::printf(" %02X", b);
    std::printf("\n  want:");
    for (uint8_t b : want) std::printf(" %02X", b);
    std::printf("\n");
}

// Compile a single source line for TMS and return the $0800 program zone.
std::vector<uint8_t> prog(const std::string& src)
{
    basic::Result r = basic::compile(src, basic::targetTms());
    if (!r.ok) { ++failures; std::printf("FAIL compile: %s\n", r.error.c_str()); return {}; }
    const std::vector<uint8_t>* z = zoneAt(r, 0x0800);
    return z ? *z : std::vector<uint8_t>{};
}

} // namespace

int main()
{
    // 4 tokens (9E 3A A5 33) -> line = 4+4+1 = 9 bytes, link = $0801+9 = $080A.
    expectBytes("HGR2 : HCOLOR=3",
        prog("10 HGR2 : HCOLOR=3"),
        {0x00, 0x0A,0x08, 0x0A,0x00, 0x9E,0x3A,0xA5,0x33, 0x00, 0x00,0x00});

    // ?-shorthand becomes PRINT ($98); the string is copied verbatim with quotes.
    // 5 tokens -> link = $0801+10 = $080B.
    expectBytes("?\"HI\" -> PRINT",
        prog("20 ?\"HI\""),
        {0x00, 0x0B,0x08, 0x14,0x00, 0x98,0x22,0x48,0x49,0x22, 0x00, 0x00,0x00});

    // GOTO=$8E then the target line number stays ASCII "150" (never tokenized).
    // 4 tokens -> link = $080A.
    expectBytes("GOTO keeps ASCII line number",
        prog("70 GOTO 150"),
        {0x00, 0x0A,0x08, 0x46,0x00, 0x8E,0x31,0x35,0x30, 0x00, 0x00,0x00});

    // REM=$94 copies the rest of the line literally (upper-cased), spaces kept.
    // 6 tokens (94 20 48 49 20 58) -> link = $0801+11 = $080C.
    expectBytes("REM literal tail",
        prog("5 REM hi x"),
        {0x00, 0x0C,0x08, 0x05,0x00, 0x94,0x20,0x48,0x49,0x20,0x58, 0x00, 0x00,0x00});

    // DATA=$83 copies literally (spaces kept) up to ':'; PRINT=$98 after the colon.
    // 8 tokens -> link = $0801+13 = $080E.
    expectBytes("DATA literal until colon",
        prog("30 DATA 1, 2:PRINT"),
        {0x00, 0x0E,0x08, 0x1E,0x00, 0x83,0x20,0x31,0x2C,0x20,0x32,0x3A,0x98, 0x00, 0x00,0x00});

    // Prefix-greedy + operators: FOR=$81, '='=$C4, TO=$B6, STEP=$BB.
    // 8 tokens -> link = $080E.
    expectBytes("FOR I=1 TO 9 STEP 2",
        prog("40 FOR I=1 TO 9 STEP 2"),
        {0x00, 0x0E,0x08, 0x28,0x00,
         0x81,0x49,0xC4,0x31,0xB6,0x39,0xBB,0x32, 0x00, 0x00,0x00});

    // Regression: `AND` inside parens + a relational `=` right after `)`. This
    // was suspected of freezing the interpreter; it does NOT — the tokeniser emits
    // bytes byte-identical to the ROM's own CRUNCH (AND=$C1, ')'=$29, '='=$C4,
    // THEN=$B9, GOTO=$8E, "100" kept ASCII). The apparent "hang" is genuine
    // Applesoft semantics: AND/OR are LOGICAL (nonzero->1), so `(X AND 7)=0` is
    // true only when X=0 — a program written for BITWISE AND loops forever under
    // the interpreter (the native compiler is bitwise; see doc/BASIC_COMPILER.md).
    // 13 tokens -> line = 4+13+1 = 18 ($12) bytes, link = $0801+$12 = $0813.
    expectBytes("IF (X AND 7)=0 THEN GOTO 100  (logical-AND, not a tokeniser bug)",
        prog("10 IF (X AND 7)=0 THEN GOTO 100"),
        {0x00, 0x13,0x08, 0x0A,0x00,
         0x90,0x28,0x58,0xC1,0x37,0x29,0xC4,0x30,0xB9,0x8E,0x31,0x30,0x30, 0x00, 0x00,0x00});

    // Lines are emitted ascending regardless of source order; links chain across
    // two lines. line10 HGR(9F): 6 bytes @ $0801, link=$0807. line20 END(80): 6
    // bytes @ $0807, link=$080D. End marker at $080D.
    {
        basic::Result r = basic::compile("20 END\n10 HGR", basic::targetTms());
        const std::vector<uint8_t>* z = zoneAt(r, 0x0800);
        expectBytes("ascending sort + links",
            z ? *z : std::vector<uint8_t>{},
            {0x00,
             0x07,0x08, 0x0A,0x00, 0x9F, 0x00,
             0x0D,0x08, 0x14,0x00, 0x80, 0x00,
             0x00,0x00});
    }

    // Launcher: LDA #<end;STA $69;LDA #>end;STA $6A;JSR SETPTRS;JMP NEWSTT.
    {
        basic::Result r = basic::compile("10 HGR", basic::targetTms());
        const std::vector<uint8_t>* L = zoneAt(r, basic::kLauncherAddr);
        const uint16_t e = r.progEnd, s = basic::targetTms().setptrs, n = basic::targetTms().newstt;
        expectBytes("launcher stub (TMS targets)",
            L ? *L : std::vector<uint8_t>{},
            {0xA9, (uint8_t)(e & 0xFF), 0x85, 0x69,
             0xA9, (uint8_t)(e >> 8),   0x85, 0x6A,
             0x20, (uint8_t)(s & 0xFF), (uint8_t)(s >> 8),
             0x4C, (uint8_t)(n & 0xFF), (uint8_t)(n >> 8)});
    }

    // page2Floor (GEN2): an HGR2-only program may extend its tokenised image into
    // the idle page-1 region (ceiling $4000); an HGR (page-1) program of the SAME
    // size stays capped at $2000 (it would otherwise overwrite the framebuffer it
    // draws to). Build a listing big enough to cross $2000 but stay below $4000
    // (~11 KB: 300 REM lines of ~37 bytes). Pins the SteveJobs.apf fix.
    {
        std::string pad;
        for (int n = 10; n < 10 + 300; ++n)
            pad += std::to_string(n) + " REM 123456789012345678901234567890\r";
        basic::Result hgr2 = basic::compile("5 HGR2\r" + pad, basic::targetGen2());
        if (!hgr2.ok) {
            ++failures;
            std::printf("FAIL: HGR2-only program should fit below $4000 (%s)\n",
                        hgr2.error.c_str());
        } else {
            std::printf("ok: HGR2-only program extends past $2000 to the $4000 ceiling\n");
        }
        basic::Result hgr = basic::compile("5 HGR\r" + pad, basic::targetGen2());
        if (hgr.ok) {
            ++failures;
            std::printf("FAIL: HGR (page-1) program of same size must be capped at $2000\n");
        } else {
            std::printf("ok: HGR (page-1) program of same size stays capped at $2000\n");
        }
    }

    if (failures) { std::printf("basic_compiler_tokenize: %d FAILED\n", failures); return 1; }
    std::printf("basic_compiler_tokenize: OK\n");
    return 0;
}
