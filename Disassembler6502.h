// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Disassembler6502 — single-instruction MOS 6502 disassembler used by the
// debug console. Stateless free function: feed it a pointer to a 64 KB
// memory snapshot and a program counter, get back a printable mnemonic
// (e.g. "LDA #$42", "JMP $FF00", "BNE $0312") plus the instruction length.

#ifndef DISASSEMBLER6502_H
#define DISASSEMBLER6502_H

#include <cstdint>
#include <string>

namespace pom1 {

/// Disassemble the 6502 instruction at `mem[pc]`. `mem` must point to a
/// contiguous 64 KB region (uses `(pc + N) & 0xFFFF` for operand fetches).
/// Writes the instruction byte length (1/2/3) into `instrLen` and returns
/// the formatted mnemonic.
std::string disassemble6502(const uint8_t* mem, uint16_t pc, int& instrLen);

} // namespace pom1

#endif // DISASSEMBLER6502_H
