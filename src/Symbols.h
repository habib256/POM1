// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// SymbolTable — address → name map for the disassembler. Lets the debug UI
// render "JSR ECHO" instead of "JSR $FFEF". Ships a small, accurate set of
// canonical Apple-1 / WOZ Monitor symbols and can additionally load user
// labels from a file (ca65 `.lbl` VICE format, or simple `name = $addr`).

#ifndef SYMBOLS_H
#define SYMBOLS_H

#include <cstdint>
#include <string>
#include <unordered_map>

namespace pom1 {

class SymbolTable {
public:
    /// Add / overwrite a symbol. Empty names are ignored.
    void add(uint16_t address, std::string name);

    /// Exact-address lookup. Returns nullptr when no symbol is defined there.
    const std::string* find(uint16_t address) const;

    void clear() { map_.clear(); }
    std::size_t size() const { return map_.size(); }
    bool empty() const { return map_.empty(); }

    /// Populate the built-in Apple-1 / WOZ Monitor symbols (zero-page scratch,
    /// PIA I/O, the three print routines, RESET, CPU vectors, BASIC cold start).
    /// Only rock-solid, widely-documented addresses are included so the
    /// disassembler never shows a misleading label.
    void loadApple1Defaults();

    /// Merge symbols from a file. Tolerant of three line formats:
    ///   * VICE / ca65 `-Ln` label:   `al 00FFEF .ECHO`   (optional `C:` prefix)
    ///   * assignment:                `ECHO = $FFEF`  /  `ECHO equ $FFEF`
    ///   * plain dump:                `FFEF ECHO`
    /// Blank lines and `;` / `#` / `//` comments are skipped. Returns the number
    /// of symbols added; sets `err` and returns 0 when the file can't be opened.
    int loadFile(const std::string& path, std::string& err);

private:
    std::unordered_map<uint16_t, std::string> map_;
};

} // namespace pom1

#endif // SYMBOLS_H
