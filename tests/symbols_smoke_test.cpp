// Pom1 Apple 1 Emulator — SymbolTable + symbolic disassembly smoke test.
//
// Pins: built-in Apple-1 defaults, operand symbolisation in the disassembler
// (abs / zero-page / relative / indirect, with immediates left raw), and the
// tolerant .sym/.lbl file parser (VICE `al`, `name = $addr`, `addr name`,
// `name equ $addr`, comments). Self-contained — links only Symbols.cpp +
// Disassembler6502.cpp.

#include "Symbols.h"
#include "Disassembler6502.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using pom1::SymbolTable;
using pom1::disassemble6502;
using pom1::annotateOperand6502;

namespace {

std::string dis(const std::vector<uint8_t>& mem, uint16_t pc, const SymbolTable* s)
{
    int len = 0;
    return disassemble6502(mem.data(), pc, len, s);
}

void put(std::vector<uint8_t>& mem, uint16_t at, std::initializer_list<uint8_t> bytes)
{
    uint16_t a = at;
    for (uint8_t b : bytes) mem[a++] = b;
}

} // namespace

int main()
{
    // ---- built-in defaults -------------------------------------------------
    SymbolTable sym;
    sym.loadApple1Defaults();
    assert(sym.find(0xFFEF) && *sym.find(0xFFEF) == "ECHO");
    assert(sym.find(0xD012) && *sym.find(0xD012) == "DSP");
    assert(sym.find(0x0024) && *sym.find(0x0024) == "XAML");
    assert(sym.find(0xFFFC) && *sym.find(0xFFFC) == "RESVEC");
    assert(sym.find(0x1234) == nullptr);   // unknown → no symbol

    // ---- symbolic disassembly ----------------------------------------------
    std::vector<uint8_t> mem(0x10000, 0);
    put(mem, 0x0300, {0x20, 0xEF, 0xFF});  // JSR $FFEF
    put(mem, 0x0310, {0xAD, 0x12, 0xD0});  // LDA $D012
    put(mem, 0x0320, {0x85, 0x24});        // STA $24   (zero page)
    put(mem, 0x0330, {0xA9, 0xEF});        // LDA #$EF  (immediate — never a symbol)
    put(mem, 0x0340, {0x6C, 0xFC, 0xFF});  // JMP ($FFFC) (indirect)
    put(mem, 0x0350, {0xD0, 0x02});        // BNE $0354 (relative)
    sym.add(0x0354, "SKIP");

    assert(dis(mem, 0x0300, &sym) == "JSR ECHO");
    assert(dis(mem, 0x0310, &sym) == "LDA DSP");
    assert(dis(mem, 0x0320, &sym) == "STA XAML");
    assert(dis(mem, 0x0330, &sym) == "LDA #$EF");
    assert(dis(mem, 0x0340, &sym) == "JMP (RESVEC)");
    assert(dis(mem, 0x0350, &sym) == "BNE SKIP");

    // Same instructions, no symbol table → raw hex (and length preserved).
    assert(dis(mem, 0x0300, nullptr) == "JSR $FFEF");
    assert(dis(mem, 0x0320, nullptr) == "STA $24");
    assert(dis(mem, 0x0350, nullptr) == "BNE $0354");
    {
        int len = 0;
        disassemble6502(mem.data(), 0x0300, len, &sym);
        assert(len == 3);
        disassemble6502(mem.data(), 0x0320, len, &sym);
        assert(len == 2);
    }

    // ---- operand annotation (AppleWin-style effective addr + value) --------
    // Reproduces the rows from the AppleWin //e debugger screenshot, with the
    // same live registers (X=$65, Y=$0C) so indexed modes resolve identically.
    {
        const uint8_t X = 0x65, Y = 0x0C, P = 0x34;  // P: nv-Bdi-- (Z=0,C=0,N=0)
        std::vector<uint8_t> m(0x10000, 0);

        // LDX $398D,Y  with Y=$0C → $3999; mem[$3999]=$00.
        put(m, 0x0526, {0xBE, 0x8D, 0x39});
        m[0x3999] = 0x00;
        assert(annotateOperand6502(m.data(), 0x0526, X, Y, P) == "$3999=$00");

        // LSR $C8E8,X  with X=$65 → $C94D; mem[$C94D]=$4D → printable 'M'.
        put(m, 0x0529, {0x5E, 0xE8, 0xC8});
        m[0xC94D] = 0x4D;
        assert(annotateOperand6502(m.data(), 0x0529, X, Y, P) == "$C94D=$4D 'M'");

        // CPX #$BA  → signed -70, char ($BA & $7F = $3A) ':'.
        put(m, 0x052C, {0xE0, 0xBA});
        assert(annotateOperand6502(m.data(), 0x052C, X, Y, P) == "#-70 ':'");

        // LDA $207D (absolute data) → value $35 → '5'.
        put(m, 0x0561, {0xAD, 0x7D, 0x20});
        m[0x207D] = 0x35;
        assert(annotateOperand6502(m.data(), 0x0561, X, Y, P) == "$207D=$35 '5'");

        // JSR/JMP abs: target is named by the mnemonic → no annotation.
        put(m, 0x0538, {0x4C, 0x42, 0xD2});  // JMP $D242
        assert(annotateOperand6502(m.data(), 0x0538, X, Y, P).empty());
        put(m, 0x0570, {0x20, 0xBD, 0xD5});  // JSR $D5BD
        assert(annotateOperand6502(m.data(), 0x0570, X, Y, P).empty());

        // Branch direction arrows + taken/skip under the PC. Z=0 here.
        // BNE forward (target > pc) → 'v', and BNE is taken (Z=0).
        put(m, 0x0560, {0xD0, 0x20});  // BNE $0582
        assert(annotateOperand6502(m.data(), 0x0560, X, Y, P, /*eval=*/true)
               == "-> $0582 v  (taken)");
        // BEQ back (target < pc) → '^', and BEQ is NOT taken (Z=0).
        put(m, 0x0560, {0xF0, 0xF0});  // BEQ $0552
        assert(annotateOperand6502(m.data(), 0x0560, X, Y, P, /*eval=*/true)
               == "-> $0552 ^  (skip)");
        // Without evalBranch: target + arrow only, no verdict.
        assert(annotateOperand6502(m.data(), 0x0560, X, Y, P, /*eval=*/false)
               == "-> $0552 ^");

        // JMP ($FFFC): indirect → resolves the pointer, not data.
        put(m, 0x0340, {0x6C, 0xFC, 0xFF});
        put(m, 0xFFFC, {0x00, 0xFF});  // vector → $FF00
        assert(annotateOperand6502(m.data(), 0x0340, X, Y, P) == "-> $FF00");

        // Implied opcode (INX) → nothing to annotate.
        m[0x055B] = 0xE8;
        assert(annotateOperand6502(m.data(), 0x055B, X, Y, P).empty());
    }

    // ---- file parser (the three accepted formats + comments) ---------------
    const char* path = "symbols_smoke_tmp.lbl";
    {
        std::ofstream f(path);
        f << "; a comment line\n";
        f << "al 00FACE .LOOP\n";       // VICE / ca65, bank-prefixed, dotted label
        f << "KBDX = $D0F0\n";          // assignment with $
        f << "C0DE  ENTRY\n";           // addr-first plain dump (bare hex)
        f << "ROUT equ $1234   # tail comment\n";
        f << "LABEL $5000\n";           // name-first, prefixed addr
        f << "BEEF $6000\n";            // all-HEX label must NOT be read as the addr
        f << "..DOT $7000\n";           // multi-dot VICE label
        f << "\n";                       // blank
        f << "// trailing comment-only line\n";
    }
    std::string err;
    SymbolTable fileSym;
    int added = fileSym.loadFile(path, err);
    std::remove(path);

    assert(err.empty());
    assert(added == 7);
    assert(fileSym.find(0xFACE) && *fileSym.find(0xFACE) == "LOOP");
    assert(fileSym.find(0xD0F0) && *fileSym.find(0xD0F0) == "KBDX");
    assert(fileSym.find(0xC0DE) && *fileSym.find(0xC0DE) == "ENTRY");
    assert(fileSym.find(0x1234) && *fileSym.find(0x1234) == "ROUT");
    assert(fileSym.find(0x5000) && *fileSym.find(0x5000) == "LABEL");
    // The prefix-preference fix: "BEEF $6000" → BEEF@$6000, not "$6000"@$BEEF.
    assert(fileSym.find(0x6000) && *fileSym.find(0x6000) == "BEEF");
    assert(fileSym.find(0xBEEF) == nullptr);
    assert(fileSym.find(0x7000) && *fileSym.find(0x7000) == "DOT");

    // Missing file → 0 added, err set, table untouched.
    std::string err2;
    SymbolTable miss;
    int n = miss.loadFile("does_not_exist_12345.lbl", err2);
    assert(n == 0 && !err2.empty() && miss.empty());

    std::printf("symbols_smoke: OK\n");
    return 0;
}
