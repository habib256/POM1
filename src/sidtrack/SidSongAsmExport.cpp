// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "sidtrack/SidSongAsmExport.h"

#include <cctype>
#include <sstream>

namespace sidtrack {

std::string sanitizeName(const std::string& raw) {
    std::string out;
    for (char c : raw) {
        unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u))      out += static_cast<char>(std::tolower(u));
        else if (c == '_' || c == '-' || c == ' ') out += '_';
    }
    std::size_t b = 0;
    while (b < out.size() && (out[b] == '_' || std::isdigit(static_cast<unsigned char>(out[b]))))
        ++b;
    out.erase(0, b);
    if (out.empty()) out = "song";
    return out;
}

static std::string hex2(uint8_t v) {
    static const char* d = "0123456789ABCDEF";
    std::string s = "$";
    s += d[(v >> 4) & 0xF];
    s += d[v & 0xF];
    return s;
}

std::string formatSongAsm(const SongModel& model) {
    const std::string label = "song_" + sanitizeName(model.name());
    std::ostringstream os;
    os << "; " << label
       << " -- SID song for dev/lib/sid/sid_player.asm (note,ctrl,frames rows;\n"
       << "; frames 0 ends). LDX #<" << label << " / LDY #>" << label
       << " / JSR sid_play_start, then JSR sid_play_tick per frame.\n";
    os << label << ":\n";
    for (std::size_t i = 0; i < model.size(); ++i) {
        const Row& r = model.at(i);
        os << "        .byte " << hex2(r.note) << ", " << hex2(r.ctrl) << ", "
           << hex2(r.frames) << "   ; " << noteName(r.note) << "\n";
    }
    os << "        .byte $00, $00, $00   ; end\n";
    return os.str();
}

// --- parsing (shared shape with SfxAsmExport) -------------------------------

static std::string strip(const std::string& line) {
    std::string s = line;
    std::size_t c = s.find(';');
    if (c != std::string::npos) s.erase(c);
    std::size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    std::size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static bool parseByte(std::string t, int& out) {
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

std::vector<ParsedSong> parseSongAsm(const std::string& text) {
    std::vector<ParsedSong> out;
    std::istringstream in(text);
    std::string raw;
    ParsedSong* cur = nullptr;

    while (std::getline(in, raw)) {
        std::string line = strip(raw);
        if (line.empty()) continue;

        if (line.back() == ':') {
            std::string lbl = line.substr(0, line.size() - 1);
            out.push_back({});
            cur = &out.back();
            cur->name = (lbl.rfind("song_", 0) == 0) ? lbl.substr(5) : lbl;
            continue;
        }

        if (cur && line.rfind(".byte", 0) == 0) {
            std::string rest = line.substr(5);
            std::size_t c1 = rest.find(',');
            if (c1 == std::string::npos) continue;
            std::size_t c2 = rest.find(',', c1 + 1);
            if (c2 == std::string::npos) continue;
            int n = 0, ct = 0, fr = 0;
            if (!parseByte(rest.substr(0, c1), n)) continue;
            if (!parseByte(rest.substr(c1 + 1, c2 - c1 - 1), ct)) continue;
            if (!parseByte(rest.substr(c2 + 1), fr)) continue;
            if (fr == 0) { cur = nullptr; continue; }   // terminator
            cur->rows.push_back(Row{static_cast<uint8_t>(n),
                                    static_cast<uint8_t>(ct),
                                    static_cast<uint8_t>(fr)});
        }
    }
    return out;
}

}  // namespace sidtrack
