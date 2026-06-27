// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "Disassembler6502.h"
#include "Symbols.h"

#include <cstdio>

namespace pom1 {
namespace {

// 6502 addressing modes for disassembly formatting
enum AddrMode {
    AM_IMP, AM_IMM, AM_ZP, AM_ZPX, AM_ZPY,
    AM_ABS, AM_ABX, AM_ABY, AM_IND,
    AM_IZX, AM_IZY, AM_REL
};

struct OpcodeInfo {
    const char* mnemonic;
    AddrMode mode;
};

// Complete 6502 opcode table (256 entries). Unofficial / illegal opcodes
// are rendered as "???" so a decoded byte is always traceable.
constexpr OpcodeInfo opcodeInfo[256] = {
    {"BRK",AM_IMP}, {"ORA",AM_IZX}, {"???",AM_IMP}, {"???",AM_IZX},  // 00-03
    {"???",AM_ZP},  {"ORA",AM_ZP},  {"ASL",AM_ZP},  {"???",AM_ZP},  // 04-07
    {"PHP",AM_IMP}, {"ORA",AM_IMM}, {"ASL",AM_IMP}, {"???",AM_IMM},  // 08-0B
    {"???",AM_ABS}, {"ORA",AM_ABS}, {"ASL",AM_ABS}, {"???",AM_ABS},  // 0C-0F
    {"BPL",AM_REL}, {"ORA",AM_IZY}, {"???",AM_IMP}, {"???",AM_IZY},  // 10-13
    {"???",AM_ZPX}, {"ORA",AM_ZPX}, {"ASL",AM_ZPX}, {"???",AM_ZPX},  // 14-17
    {"CLC",AM_IMP}, {"ORA",AM_ABY}, {"???",AM_IMP}, {"???",AM_ABY},  // 18-1B
    {"???",AM_ABX}, {"ORA",AM_ABX}, {"ASL",AM_ABX}, {"???",AM_ABX},  // 1C-1F
    {"JSR",AM_ABS}, {"AND",AM_IZX}, {"???",AM_IMP}, {"???",AM_IZX},  // 20-23
    {"BIT",AM_ZP},  {"AND",AM_ZP},  {"ROL",AM_ZP},  {"???",AM_ZP},  // 24-27
    {"PLP",AM_IMP}, {"AND",AM_IMM}, {"ROL",AM_IMP}, {"???",AM_IMM},  // 28-2B
    {"BIT",AM_ABS}, {"AND",AM_ABS}, {"ROL",AM_ABS}, {"???",AM_ABS},  // 2C-2F
    {"BMI",AM_REL}, {"AND",AM_IZY}, {"???",AM_IMP}, {"???",AM_IZY},  // 30-33
    {"???",AM_ZPX}, {"AND",AM_ZPX}, {"ROL",AM_ZPX}, {"???",AM_ZPX},  // 34-37
    {"SEC",AM_IMP}, {"AND",AM_ABY}, {"???",AM_IMP}, {"???",AM_ABY},  // 38-3B
    {"???",AM_ABX}, {"AND",AM_ABX}, {"ROL",AM_ABX}, {"???",AM_ABX},  // 3C-3F
    {"RTI",AM_IMP}, {"EOR",AM_IZX}, {"???",AM_IMP}, {"???",AM_IZX},  // 40-43
    {"???",AM_ZP},  {"EOR",AM_ZP},  {"LSR",AM_ZP},  {"???",AM_ZP},  // 44-47
    {"PHA",AM_IMP}, {"EOR",AM_IMM}, {"LSR",AM_IMP}, {"???",AM_IMM},  // 48-4B
    {"JMP",AM_ABS}, {"EOR",AM_ABS}, {"LSR",AM_ABS}, {"???",AM_ABS},  // 4C-4F
    {"BVC",AM_REL}, {"EOR",AM_IZY}, {"???",AM_IMP}, {"???",AM_IZY},  // 50-53
    {"???",AM_ZPX}, {"EOR",AM_ZPX}, {"LSR",AM_ZPX}, {"???",AM_ZPX},  // 54-57
    {"CLI",AM_IMP}, {"EOR",AM_ABY}, {"???",AM_IMP}, {"???",AM_ABY},  // 58-5B
    {"???",AM_ABX}, {"EOR",AM_ABX}, {"LSR",AM_ABX}, {"???",AM_ABX},  // 5C-5F
    {"RTS",AM_IMP}, {"ADC",AM_IZX}, {"???",AM_IMP}, {"???",AM_IZX},  // 60-63
    {"???",AM_ZP},  {"ADC",AM_ZP},  {"ROR",AM_ZP},  {"???",AM_ZP},  // 64-67
    {"PLA",AM_IMP}, {"ADC",AM_IMM}, {"ROR",AM_IMP}, {"???",AM_IMM},  // 68-6B
    {"JMP",AM_IND}, {"ADC",AM_ABS}, {"ROR",AM_ABS}, {"???",AM_ABS},  // 6C-6F
    {"BVS",AM_REL}, {"ADC",AM_IZY}, {"???",AM_IMP}, {"???",AM_IZY},  // 70-73
    {"???",AM_ZPX}, {"ADC",AM_ZPX}, {"ROR",AM_ZPX}, {"???",AM_ZPX},  // 74-77
    {"SEI",AM_IMP}, {"ADC",AM_ABY}, {"???",AM_IMP}, {"???",AM_ABY},  // 78-7B
    {"???",AM_ABX}, {"ADC",AM_ABX}, {"ROR",AM_ABX}, {"???",AM_ABX},  // 7C-7F
    {"???",AM_IMM}, {"STA",AM_IZX}, {"???",AM_IMM}, {"???",AM_IZX},  // 80-83
    {"STY",AM_ZP},  {"STA",AM_ZP},  {"STX",AM_ZP},  {"???",AM_ZP},  // 84-87
    {"DEY",AM_IMP}, {"???",AM_IMM}, {"TXA",AM_IMP}, {"???",AM_IMM},  // 88-8B
    {"STY",AM_ABS}, {"STA",AM_ABS}, {"STX",AM_ABS}, {"???",AM_ABS},  // 8C-8F
    {"BCC",AM_REL}, {"STA",AM_IZY}, {"???",AM_IMP}, {"???",AM_IZY},  // 90-93
    {"STY",AM_ZPX}, {"STA",AM_ZPX}, {"STX",AM_ZPY}, {"???",AM_ZPY},  // 94-97
    {"TYA",AM_IMP}, {"STA",AM_ABY}, {"TXS",AM_IMP}, {"???",AM_ABY},  // 98-9B
    {"???",AM_ABX}, {"STA",AM_ABX}, {"???",AM_ABY}, {"???",AM_ABY},  // 9C-9F
    {"LDY",AM_IMM}, {"LDA",AM_IZX}, {"LDX",AM_IMM}, {"???",AM_IZX},  // A0-A3
    {"LDY",AM_ZP},  {"LDA",AM_ZP},  {"LDX",AM_ZP},  {"???",AM_ZP},  // A4-A7
    {"TAY",AM_IMP}, {"LDA",AM_IMM}, {"TAX",AM_IMP}, {"???",AM_IMM},  // A8-AB
    {"LDY",AM_ABS}, {"LDA",AM_ABS}, {"LDX",AM_ABS}, {"???",AM_ABS},  // AC-AF
    {"BCS",AM_REL}, {"LDA",AM_IZY}, {"???",AM_IMP}, {"???",AM_IZY},  // B0-B3
    {"LDY",AM_ZPX}, {"LDA",AM_ZPX}, {"LDX",AM_ZPY}, {"???",AM_ZPY},  // B4-B7
    {"CLV",AM_IMP}, {"LDA",AM_ABY}, {"TSX",AM_IMP}, {"???",AM_ABY},  // B8-BB
    {"LDY",AM_ABX}, {"LDA",AM_ABX}, {"LDX",AM_ABY}, {"???",AM_ABY},  // BC-BF
    {"CPY",AM_IMM}, {"CMP",AM_IZX}, {"???",AM_IMM}, {"???",AM_IZX},  // C0-C3
    {"CPY",AM_ZP},  {"CMP",AM_ZP},  {"DEC",AM_ZP},  {"???",AM_ZP},  // C4-C7
    {"INY",AM_IMP}, {"CMP",AM_IMM}, {"DEX",AM_IMP}, {"???",AM_IMM},  // C8-CB
    {"CPY",AM_ABS}, {"CMP",AM_ABS}, {"DEC",AM_ABS}, {"???",AM_ABS},  // CC-CF
    {"BNE",AM_REL}, {"CMP",AM_IZY}, {"???",AM_IMP}, {"???",AM_IZY},  // D0-D3
    {"???",AM_ZPX}, {"CMP",AM_ZPX}, {"DEC",AM_ZPX}, {"???",AM_ZPX},  // D4-D7
    {"CLD",AM_IMP}, {"CMP",AM_ABY}, {"???",AM_IMP}, {"???",AM_ABY},  // D8-DB
    {"???",AM_ABX}, {"CMP",AM_ABX}, {"DEC",AM_ABX}, {"???",AM_ABX},  // DC-DF
    {"CPX",AM_IMM}, {"SBC",AM_IZX}, {"???",AM_IMM}, {"???",AM_IZX},  // E0-E3
    {"CPX",AM_ZP},  {"SBC",AM_ZP},  {"INC",AM_ZP},  {"???",AM_ZP},  // E4-E7
    {"INX",AM_IMP}, {"SBC",AM_IMM}, {"NOP",AM_IMP}, {"???",AM_IMM},  // E8-EB
    {"CPX",AM_ABS}, {"SBC",AM_ABS}, {"INC",AM_ABS}, {"???",AM_ABS},  // EC-EF
    {"BEQ",AM_REL}, {"SBC",AM_IZY}, {"???",AM_IMP}, {"???",AM_IZY},  // F0-F3
    {"???",AM_ZPX}, {"SBC",AM_ZPX}, {"INC",AM_ZPX}, {"???",AM_ZPX},  // F4-F7
    {"SED",AM_IMP}, {"SBC",AM_ABY}, {"???",AM_IMP}, {"???",AM_ABY},  // F8-FB
    {"???",AM_ABX}, {"SBC",AM_ABX}, {"INC",AM_ABX}, {"???",AM_ABX},  // FC-FF
};

} // namespace

std::string disassemble6502(const uint8_t* mem, uint16_t pc, int& instrLen,
                            const SymbolTable* symbols)
{
    uint8_t opcode = mem[pc];
    const OpcodeInfo& info = opcodeInfo[opcode];
    uint8_t lo = mem[(pc + 1) & 0xFFFF];
    uint8_t hi = mem[(pc + 2) & 0xFFFF];

    // Operand address → symbol name, else raw hex ($XX for zero page, $XXXX
    // otherwise). Immediate operands never come through here.
    auto operand = [symbols](uint16_t addr, bool zeroPage) -> std::string {
        if (symbols) {
            if (const std::string* name = symbols->find(addr))
                return *name;
        }
        char t[8];
        std::snprintf(t, sizeof(t), zeroPage ? "$%02X" : "$%04X", addr);
        return t;
    };

    const std::string m = info.mnemonic;
    const uint16_t abs16 = static_cast<uint16_t>(lo | (hi << 8));

    switch (info.mode) {
    case AM_IMP:
        instrLen = 1;
        return m;
    case AM_IMM: {
        instrLen = 2;
        char t[8];
        std::snprintf(t, sizeof(t), " #$%02X", lo);
        return m + t;
    }
    case AM_ZP:
        instrLen = 2;
        return m + " " + operand(lo, true);
    case AM_ZPX:
        instrLen = 2;
        return m + " " + operand(lo, true) + ",X";
    case AM_ZPY:
        instrLen = 2;
        return m + " " + operand(lo, true) + ",Y";
    case AM_ABS:
        instrLen = 3;
        return m + " " + operand(abs16, false);
    case AM_ABX:
        instrLen = 3;
        return m + " " + operand(abs16, false) + ",X";
    case AM_ABY:
        instrLen = 3;
        return m + " " + operand(abs16, false) + ",Y";
    case AM_IND:
        instrLen = 3;
        return m + " (" + operand(abs16, false) + ")";
    case AM_IZX:
        instrLen = 2;
        return m + " (" + operand(lo, true) + ",X)";
    case AM_IZY:
        instrLen = 2;
        return m + " (" + operand(lo, true) + "),Y";
    case AM_REL: {
        instrLen = 2;
        uint16_t target = static_cast<uint16_t>(pc + 2 + static_cast<int8_t>(lo));
        return m + " " + operand(target, false);
    }
    }
    instrLen = 1;
    return m;
}

std::string annotateOperand6502(const uint8_t* mem, uint16_t pc,
                                uint8_t x, uint8_t y, uint8_t status,
                                bool evalBranch)
{
    const uint8_t opcode = mem[pc];
    const OpcodeInfo& info = opcodeInfo[opcode];
    const uint8_t lo = mem[(pc + 1) & 0xFFFF];
    const uint8_t hi = mem[(pc + 2) & 0xFFFF];
    const uint16_t abs16 = static_cast<uint16_t>(lo | (hi << 8));

    auto rdword = [mem](uint16_t a) -> uint16_t {
        return static_cast<uint16_t>(mem[a] |
                                     (mem[(a + 1) & 0xFFFF] << 8));
    };
    // Zero-page word read wraps inside page 0 (6502 quirk shared by (zp,X)/(zp),Y).
    auto rdzpword = [mem](uint8_t a) -> uint16_t {
        return static_cast<uint16_t>(mem[a] |
                                     (mem[static_cast<uint8_t>(a + 1)] << 8));
    };
    // Printable Apple char (high bit stripped), as a trailing " 'c'" or "".
    auto chr = [](uint8_t v) -> std::string {
        uint8_t c = v & 0x7F;
        if (c >= 0x20 && c < 0x7F) {
            char t[8];
            std::snprintf(t, sizeof(t), " '%c'", c);
            return t;
        }
        return "";
    };
    // "$EAEA=$VV 'c'" for a data access at effective address ea.
    auto dataAt = [&](uint16_t ea) -> std::string {
        uint8_t v = mem[ea];
        char t[16];
        std::snprintf(t, sizeof(t), "$%04X=$%02X", ea, v);
        return t + chr(v);
    };

    switch (info.mode) {
    case AM_IMP:
        return "";
    case AM_IMM: {
        char t[16];
        std::snprintf(t, sizeof(t), "#%d", static_cast<int8_t>(lo));
        return t + chr(lo);
    }
    case AM_ZP:  return dataAt(lo);
    case AM_ZPX: return dataAt(static_cast<uint8_t>(lo + x));
    case AM_ZPY: return dataAt(static_cast<uint8_t>(lo + y));
    case AM_ABS:
        // JSR / JMP target the address itself; the mnemonic (with symbol)
        // already names it — annotating mem[target] would just be noise.
        if (opcode == 0x20 /*JSR*/ || opcode == 0x4C /*JMP*/) return "";
        return dataAt(abs16);
    case AM_ABX: return dataAt(static_cast<uint16_t>(abs16 + x));
    case AM_ABY: return dataAt(static_cast<uint16_t>(abs16 + y));
    case AM_IND: {                        // JMP ($abs16)
        char t[24];
        std::snprintf(t, sizeof(t), "-> $%04X", rdword(abs16));
        return t;
    }
    case AM_IZX: return dataAt(rdzpword(static_cast<uint8_t>(lo + x)));
    case AM_IZY: return dataAt(static_cast<uint16_t>(rdzpword(lo) + y));
    case AM_REL: {
        const uint16_t target = static_cast<uint16_t>(pc + 2 + static_cast<int8_t>(lo));
        const char* arrow = (target <= pc) ? " ^" : " v";   // back-branch / forward
        char t[24];
        std::snprintf(t, sizeof(t), "-> $%04X%s", target, arrow);
        std::string out = t;
        if (evalBranch) {
            bool taken = false;
            switch (opcode) {
            case 0x10: taken = !(status & 0x80); break;  // BPL N=0
            case 0x30: taken =  (status & 0x80); break;  // BMI N=1
            case 0x50: taken = !(status & 0x40); break;  // BVC V=0
            case 0x70: taken =  (status & 0x40); break;  // BVS V=1
            case 0x90: taken = !(status & 0x01); break;  // BCC C=0
            case 0xB0: taken =  (status & 0x01); break;  // BCS C=1
            case 0xD0: taken = !(status & 0x02); break;  // BNE Z=0
            case 0xF0: taken =  (status & 0x02); break;  // BEQ Z=1
            }
            out += taken ? "  (taken)" : "  (skip)";
        }
        return out;
    }
    }
    return "";
}

} // namespace pom1
