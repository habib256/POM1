// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "Symbols.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

namespace pom1 {

void SymbolTable::add(uint16_t address, std::string name)
{
    if (name.empty()) return;
    map_[address] = std::move(name);
}

const std::string* SymbolTable::find(uint16_t address) const
{
    auto it = map_.find(address);
    return it == map_.end() ? nullptr : &it->second;
}

void SymbolTable::loadApple1Defaults()
{
    // WOZ Monitor zero-page scratch (Wozniak's published listing).
    add(0x0024, "XAML");  add(0x0025, "XAMH");
    add(0x0026, "STL");   add(0x0027, "STH");
    add(0x0028, "L");     add(0x0029, "H");
    add(0x002A, "YSAV");  add(0x002B, "MODE");
    add(0x0200, "IN");    // WOZ Monitor input buffer

    // PIA 6821 — keyboard + display I/O.
    add(0xD010, "KBD");   add(0xD011, "KBDCR");
    add(0xD012, "DSP");   add(0xD013, "DSPCR");

    // Integer BASIC cold start (`E000R`).
    add(0xE000, "BASIC");

    // WOZ Monitor entry + the three print helpers.
    add(0xFF00, "WOZMON");
    add(0xFFDC, "PRBYTE");
    add(0xFFE5, "PRHEX");
    add(0xFFEF, "ECHO");

    // 6502 hardware vectors.
    add(0xFFFA, "NMIVEC");
    add(0xFFFC, "RESVEC");
    add(0xFFFE, "IRQVEC");
}

namespace {

// Parse a hex address token, tolerating `$`, `0x`/`0X` and a VICE `C:` bank
// prefix. Returns false unless the whole token (after the prefix) is hex.
bool parseHexAddr(const std::string& tok, uint32_t& out)
{
    const char* s = tok.c_str();
    if ((s[0] == 'C' || s[0] == 'c') && s[1] == ':') s += 2;   // VICE bank prefix
    if (s[0] == '$') s += 1;
    else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    if (*s == '\0') return false;
    char* end = nullptr;
    unsigned long v = std::strtoul(s, &end, 16);
    if (end == s || *end != '\0') return false;
    out = static_cast<uint32_t>(v);
    return true;
}

// True if the token carries an explicit address prefix ($, 0x/0X, or VICE C:).
bool hasAddrPrefix(const std::string& t)
{
    return (!t.empty() && t[0] == '$') ||
           (t.size() >= 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) ||
           (t.size() >= 2 && (t[0] == 'C' || t[0] == 'c') && t[1] == ':');
}

bool isKeyword(const std::string& t)
{
    // Assignment keywords that sit between name and value in some formats.
    return t == "equ" || t == "EQU" || t == ".equ" || t == "=";
}

} // namespace

int SymbolTable::loadFile(const std::string& path, std::string& err)
{
    std::ifstream in(path);
    if (!in) {
        err = "cannot open '" + path + "'";
        return 0;
    }

    int added = 0;
    std::string raw;
    while (std::getline(in, raw)) {
        // Strip comments (; # //) and normalise '=' into a token separator.
        for (std::size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] == ';' || raw[i] == '#' ||
                (raw[i] == '/' && i + 1 < raw.size() && raw[i + 1] == '/')) {
                raw.resize(i);
                break;
            }
            if (raw[i] == '=') raw[i] = ' ';
        }

        std::istringstream ls(raw);
        std::vector<std::string> tok;
        for (std::string t; ls >> t; )
            if (!isKeyword(t)) tok.push_back(t);
        if (tok.empty()) continue;

        std::string name;
        uint32_t addr = 0;
        bool ok = false;

        if (tok[0] == "al" && tok.size() >= 3) {
            // VICE / ca65: `al 00FFEF .ECHO`
            ok = parseHexAddr(tok[1], addr);
            name = tok[2];
        } else if (tok.size() >= 2) {
            // `name <addr>` or `<addr> name`. Prefer a token carrying an explicit
            // address prefix ($/0x/C:) so an all-hex-digit LABEL (e.g. "FACE")
            // isn't mistaken for the address in `LABEL $1000` form. Fall back to
            // bare-hex detection only when neither token is prefixed.
            uint32_t a = 0;
            if (hasAddrPrefix(tok[1]) && parseHexAddr(tok[1], a)) { addr = a; name = tok[0]; ok = true; }
            else if (hasAddrPrefix(tok[0]) && parseHexAddr(tok[0], a)) { addr = a; name = tok[1]; ok = true; }
            else if (parseHexAddr(tok[0], a)) { addr = a; name = tok[1]; ok = true; }
            else if (parseHexAddr(tok[1], a)) { addr = a; name = tok[0]; ok = true; }
        }

        if (!ok || name.empty()) continue;
        name.erase(0, name.find_first_not_of('.'));   // strip VICE `.`/`..label` dots
        if (name.empty()) continue;

        add(static_cast<uint16_t>(addr & 0xFFFF), name);
        ++added;
    }
    return added;
}

} // namespace pom1
