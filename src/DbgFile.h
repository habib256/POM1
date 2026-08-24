// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// DbgFile — parser for ld65's `--dbgfile` output (cc65 debug info format v2).
// The DevBench builds with `ca65 -g` + `ld65 --dbgfile`, and this module turns
// the resulting text into the two maps source-level debugging needs:
//
//   address -> 1-based source line   (highlight the line the PC sits on)
//   line    -> first address         (arm a CPU breakpoint from a source line)
//
// plus the `sym ... type=lab` label records, so a debug build can feed the
// disassembler's SymbolTable without the user hand-loading a VICE .lbl file.
//
// PURE: strings in, structs out. No filesystem, no UI, no EmulationController —
// the same discipline as MachinePresets / Pom1BenchCc65, and what makes
// `dbgfile_smoke` a plain link-and-assert test.
//
// Format reference (cc65 doc/dbgfmt): one record per line,
//   `keyword<TAB>key=value,key=value,...` — ids are decimal, seg starts and
//   sym values are 0x-hex, span lists join with `+`, strings are "quoted".
// Only file/line/span/seg/sym records matter here; everything else is skipped.

#ifndef POM1_DBGFILE_H
#define POM1_DBGFILE_H

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pom1 {

struct DbgLineInfo {
    bool ok = false;               // a version record parsed and line data found
    std::string error;             // human-readable reason when !ok

    // Line table for ONE source file (the editor buffer the Bench staged).
    // addrToLine maps every byte covered by a line's spans; lineToAddr keeps
    // the LOWEST address of each line, ordered so callers can snap a click on
    // a blank/comment line to the next line that generated code.
    //
    // CODE ONLY, as far as the file allows — a breakpoint must only land
    // where the PC can actually go, and snapping skips everything else. Two
    // independent rules, both verified against real ld65 output:
    //   * spans carrying a `type=` data descriptor (.byte in every form,
    //     .word, .addr — never an instruction);
    //   * segments with no `oname`, written to no output file and therefore
    //     holding no code (BSS, ZEROPAGE, anything unloaded) — this is what
    //     makes a click on a `.res` variable declaration a no-op instead of
    //     a breakpoint that never fires.
    // Together they cover 14 282 of the 14 296 data directives in this
    // repo's 6502 sources. KNOWN RESIDUE, measured rather than guessed:
    // `.asciiz` (14 occurrences) and `.dword` (0) get no type descriptor, and
    // a `.res` inside a LOADED segment (an inline scratch buffer among
    // instructions) is indistinguishable from code. Clicking one of those
    // lines arms an address the PC never reaches.
    std::unordered_map<uint16_t, int> addrToLine;
    std::map<int, uint16_t> lineToAddr;

    // `sym` records with type=lab: (value, name), file order preserved.
    std::vector<std::pair<uint16_t, std::string>> labels;

    /// Line whose first address is `addr`, else the line covering `addr`
    /// through its spans; -1 when the address maps to no source line.
    int lineForAddr(uint16_t addr) const;

    /// First address at or after source line `line` (1-based) — a click on a
    /// comment/blank line arms the next code-bearing line. Returns true and
    /// fills `addrOut`/`lineOut` (the snapped line) when one exists.
    bool addrForLine(int line, uint16_t& addrOut, int& lineOut) const;
};

/// Parse a full dbgfile text. `primarySource` selects which file's line table
/// is extracted — matched against the record's name by exact string OR by
/// basename (ca65 records the path exactly as it appeared on the command
/// line, which the Bench passes absolute). Labels are collected regardless.
DbgLineInfo parseDbgFile(const std::string& text, const std::string& primarySource);

} // namespace pom1

#endif // POM1_DBGFILE_H
