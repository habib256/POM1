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

class SymbolTable;

/// Disassemble the 6502 instruction at `mem[pc]`. `mem` must point to a
/// contiguous 64 KB region (uses `(pc + N) & 0xFFFF` for operand fetches).
/// Writes the instruction byte length (1/2/3) into `instrLen` and returns
/// the formatted mnemonic. When `symbols` is non-null, address operands that
/// have a matching symbol render as the name (e.g. "JSR ECHO") instead of the
/// raw hex; immediate operands (`#$xx`) are never symbolised.
std::string disassemble6502(const uint8_t* mem, uint16_t pc, int& instrLen,
                            const SymbolTable* symbols = nullptr);

/// Resolve the operand of the instruction at `mem[pc]` into a human-readable
/// annotation — the AppleWin-style "what does this actually touch right now?"
/// hint shown to the right of the disassembly. Uses the live X/Y registers so
/// indexed/indirect modes report their *effective* address+value (e.g.
/// "LDA $398D,Y" with Y=$0C → "$3999=$00"); data modes append the byte's
/// printable Apple char. Control-flow modes (branches, JMP/JMP()/—JSR is left
/// to the symbol) report the target with a direction arrow, and when
/// `evalBranch` is set the conditional branch is evaluated against `status`
/// ("taken"/"skip"). Returns "" when there is nothing useful to annotate
/// (implied/accumulator, or JSR/JMP whose target the mnemonic already names).
std::string annotateOperand6502(const uint8_t* mem, uint16_t pc,
                                uint8_t x, uint8_t y, uint8_t status,
                                bool evalBranch = false);

} // namespace pom1

#endif // DISASSEMBLER6502_H
