// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// HGR sprite ↔ ca65 assembly — see HgrSpriteAsmExport.h. Mirror of
// tmssprite/TmsSpriteAsmExport.cpp; keep the two parsers byte-identical.

#include "HgrSpriteAsmExport.h"

#include <cctype>
#include <cstdio>
#include <sstream>

namespace {

// Append every "$xx" hex byte on a `.byte` line to `out`.
void parseByteLine(const std::string& line, std::vector<uint8_t>& out)
{
    for (size_t i = 0; i + 1 < line.size(); ++i) {
        if (line[i] != '$') continue;
        auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        const int hi = hex(line[i + 1]);
        if (hi < 0) continue;
        const int lo = (i + 2 < line.size()) ? hex(line[i + 2]) : -1;
        out.push_back(static_cast<uint8_t>(lo < 0 ? hi : (hi * 16 + lo)));
    }
}

std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// True + fills `label` when the trimmed, comment-stripped line is a bare ca65
// label "ident:".
bool isLabel(const std::string& code, std::string& label)
{
    if (code.size() < 2 || code.back() != ':') return false;
    for (size_t i = 0; i + 1 < code.size(); ++i) {
        const char c = code[i];
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    }
    const char c0 = code[0];
    if (!(std::isalpha(static_cast<unsigned char>(c0)) || c0 == '_')) return false;
    label = code.substr(0, code.size() - 1);
    return true;
}

} // namespace

namespace hgrsprite {

std::string sanitizeAsmName(const std::string& raw)
{
    std::string out;
    for (char ch : raw) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
        else if (!out.empty() && out.back() != '_') out += '_';   // collapse runs
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.empty()) return "sprite";
    if (std::isdigit(static_cast<unsigned char>(out[0]))) out.insert(out.begin(), '_');
    return out;
}

std::string formatSpriteAsm(const std::string& name, int wBytes, int hRows,
                            const uint8_t* bytes, const std::string& note)
{
    char buf[32];
    std::string out;
    out += "; Exported by the POM1 HGR Sprite editor. GEN2 HGR format:\n";
    out += "; " + std::to_string(hRows) + " rows x " + std::to_string(wBytes) +
           " bytes/row, bit 0 = leftmost pixel within byte.\n";
    if (!note.empty()) out += "; " + note + "\n";
    out += "; slot 01/01 -- " + name + "\n";
    out += name + "_pat:\n";
    for (int r = 0; r < hRows; ++r) {
        out += "        .byte ";
        for (int b = 0; b < wBytes; ++b) {
            std::snprintf(buf, sizeof(buf), "$%02X%s", bytes[r * wBytes + b],
                          (b + 1 < wBytes) ? ", " : "");
            out += buf;
        }
        std::snprintf(buf, sizeof(buf), "   ; row %02d\n", r);
        out += buf;
    }
    return out;
}

std::vector<AsmSprite> parseSpritesAsm(const std::string& text, size_t minBytes)
{
    std::vector<AsmSprite> out;
    AsmSprite cur;
    bool haveCur = false;
    std::string pendingName;        // from the most recent "; slot ... -- name" comment
    auto flush = [&]() {
        if (haveCur && cur.bytes.size() >= minBytes) {
            cur.bytes.resize(minBytes);
            out.push_back(std::move(cur));
        }
        cur = AsmSprite{};
        haveCur = false;
    };
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty()) continue;
        if (t[0] == ';') {                                  // comment: catch slot name
            const size_t dash = t.find("--");
            if (t.find("slot") != std::string::npos && dash != std::string::npos) {
                pendingName = trim(t.substr(dash + 2));
                const size_t paren = pendingName.find('(');   // drop trailing "(renamed …)"
                if (paren != std::string::npos) pendingName = trim(pendingName.substr(0, paren));
            }
            continue;
        }
        // Strip any inline comment before inspecting the code.
        const size_t sc = t.find(';');
        std::string code = trim(sc == std::string::npos ? t : t.substr(0, sc));
        std::string label;
        if (isLabel(code, label)) {
            flush();
            if (!pendingName.empty()) { cur.name = pendingName; pendingName.clear(); }
            else {
                if (label.size() > 4 && label.compare(label.size() - 4, 4, "_pat") == 0)
                    label.resize(label.size() - 4);
                cur.name = label;
            }
            haveCur = true;
            continue;
        }
        if (haveCur && code.find(".byte") != std::string::npos)
            parseByteLine(code, cur.bytes);
    }
    flush();
    return out;
}

} // namespace hgrsprite
