// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "sfxbeep/SfxAsmExport.h"

#include <cctype>
#include <sstream>

namespace sfxbeep {

std::string sanitizeName(const std::string& raw) {
    std::string out;
    for (char c : raw) {
        unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u))      out += static_cast<char>(std::tolower(u));
        else if (c == '_' || c == '-' || c == ' ') out += '_';
        // anything else is dropped
    }
    // strip leading underscores/digits so the label is a valid ca65 identifier
    std::size_t b = 0;
    while (b < out.size() && (out[b] == '_' || std::isdigit(static_cast<unsigned char>(out[b]))))
        ++b;
    out.erase(0, b);
    if (out.empty()) out = "sfx";
    return out;
}

static std::string hex2(uint8_t v) {
    static const char* d = "0123456789ABCDEF";
    std::string s = "$";
    s += d[(v >> 4) & 0xF];
    s += d[v & 0xF];
    return s;
}

std::string formatSfxAsm(const SfxModel& model) {
    const std::string label = "sfx_" + sanitizeName(model.name());
    std::ostringstream os;
    os << "; " << label
       << " -- 1-bit beeper SFX (period,length steps; length 0 ends).\n"
       << "; Generated for dev/lib/beep/beep_sfx.asm -- `.include` then "
          "LDX #<" << label << " / LDY #>" << label << " / JSR sfx_play.\n";
    os << label << ":\n";
    for (std::size_t i = 0; i < model.size(); ++i) {
        const Step& s = model.at(i);
        os << "        .byte " << hex2(s.period) << ", " << hex2(s.length)
           << "          ; step " << i << "\n";
    }
    os << "        .byte $00, $00          ; end\n";
    return os.str();
}

// --- parsing ----------------------------------------------------------------

// Strip a trailing `;` comment and surrounding whitespace.
static std::string strip(const std::string& line) {
    std::string s = line;
    std::size_t c = s.find(';');
    if (c != std::string::npos) s.erase(c);
    std::size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    std::size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Parse a byte token: "$1A" (hex) or "26" (decimal). Returns false on garbage.
static bool parseByte(std::string t, int& out) {
    // trim
    std::size_t b = t.find_first_not_of(" \t");
    if (b == std::string::npos) return false;
    t = t.substr(b);
    std::size_t e = t.find_last_not_of(" \t");
    t = t.substr(0, e + 1);
    if (t.empty()) return false;
    int base = 10;
    if (t[0] == '$') { base = 16; t.erase(0, 1); }
    if (t.empty()) return false;
    try {
        std::size_t pos = 0;
        long v = std::stol(t, &pos, base);
        if (pos != t.size() || v < 0 || v > 255) return false;
        out = static_cast<int>(v);
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<ParsedSfx> parseSfxAsm(const std::string& text) {
    std::vector<ParsedSfx> out;
    std::istringstream in(text);
    std::string raw;
    ParsedSfx* cur = nullptr;

    while (std::getline(in, raw)) {
        std::string line = strip(raw);
        if (line.empty()) continue;

        // Label line: "sfx_foo:"
        if (line.back() == ':') {
            std::string lbl = line.substr(0, line.size() - 1);
            out.push_back({});
            cur = &out.back();
            cur->name = (lbl.rfind("sfx_", 0) == 0) ? lbl.substr(4) : lbl;
            continue;
        }

        // Data line: ".byte $p, $l"
        if (cur && line.rfind(".byte", 0) == 0) {
            std::string rest = line.substr(5);
            std::size_t comma = rest.find(',');
            if (comma == std::string::npos) continue;
            int p = 0, l = 0;
            if (!parseByte(rest.substr(0, comma), p)) continue;
            if (!parseByte(rest.substr(comma + 1), l)) continue;
            if (l == 0) { cur = nullptr; continue; }   // terminator ends this table
            cur->steps.push_back(Step{static_cast<uint8_t>(p),
                                      static_cast<uint8_t>(l)});
        }
    }
    return out;
}

}  // namespace sfxbeep
