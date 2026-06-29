// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// HgrSpriteBank — pre-shifted (Buzzard-Bait) sprite baking + emitters. See the
// header for the ABI; it is byte-identical to dev/lib/gen2c gen2_sprite_t and to
// tools/build_preshift_sprites.py, so the editor's exports load straight into
// gen2_hgr_sprite() and the python-baked banks interchange with the editor's.

#include "HgrSpriteBank.h"

#include "HgrPaintModel.h"   // pixelOn, kHiresWidth/Height

#include <cstdio>

namespace hgrpaint {

static constexpr int kPhases = 7;

int spriteStride(int w)
{
    if (w < 1) return 0;
    return (w + 6 + 6) / 7;              // == ceil((w + 6) / 7)
}

std::vector<std::vector<uint8_t>> extractMask(const uint8_t* page8k,
                                              const SpriteRegion& r)
{
    std::vector<std::vector<uint8_t>> mask;
    if (!page8k || r.w < 1 || r.h < 1) return mask;
    mask.assign(r.h, std::vector<uint8_t>(r.w, 0));
    for (int row = 0; row < r.h; ++row) {
        const int py = r.y + row;
        for (int col = 0; col < r.w; ++col) {
            const int px = r.x + col;
            if (px >= 0 && px < kHiresWidth && py >= 0 && py < kHiresHeight)
                mask[row][col] = pixelOn(page8k, px, py) ? 1u : 0u;
        }
    }
    return mask;
}

BakedSprite bakeMask(const std::string& name,
                     const std::vector<std::vector<uint8_t>>& mask)
{
    BakedSprite s;
    s.name = name;
    s.h = static_cast<int>(mask.size());
    s.w = s.h ? static_cast<int>(mask[0].size()) : 0;
    s.stride = spriteStride(s.w);
    if (s.w < 1 || s.h < 1 || s.stride < 1) return s;

    s.bank.assign(static_cast<size_t>(kPhases) * s.h * s.stride, 0u);
    for (int phase = 0; phase < kPhases; ++phase) {
        for (int row = 0; row < s.h; ++row) {
            const size_t rowBase =
                (static_cast<size_t>(phase) * s.h + row) * s.stride;
            const std::vector<uint8_t>& src = mask[row];
            for (int col = 0; col < s.w; ++col) {
                if (!src[col]) continue;
                const int bitpos = col + phase;         // sub-byte pre-shift
                s.bank[rowBase + bitpos / 7] |=
                    static_cast<uint8_t>(1u << (bitpos % 7));   // bit 7 never set
            }
        }
    }
    return s;
}

BakedSprite bakeRegion(const uint8_t* page8k, const SpriteRegion& r)
{
    return bakeMask(r.name, extractMask(page8k, r));
}

// ── Emitters ────────────────────────────────────────────────────────────────
static std::string upper(const std::string& s)
{
    std::string o = s;
    for (char& c : o) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    return o;
}

// Sanitise to a C / ca65 identifier (matches what the editor lets users type,
// but defensive for imported sheets).
static std::string ident(const std::string& s, const char* fallback)
{
    std::string o;
    for (char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_';
        o += ok ? c : '_';
    }
    if (o.empty() || (o[0] >= '0' && o[0] <= '9')) o = std::string(fallback) + o;
    return o;
}

static std::string hexByteRows(const std::vector<uint8_t>& data, const char* indent)
{
    std::string out;
    char buf[8];
    for (size_t i = 0; i < data.size(); i += 12) {
        out += indent;
        for (size_t j = i; j < i + 12 && j < data.size(); ++j) {
            std::snprintf(buf, sizeof buf, "0x%02X", data[j]);
            out += buf;
            if (j + 1 < data.size()) out += ", ";
        }
        out += "\n";
    }
    return out;
}

// The per-sprite C data (arrays + gen2_sprite_t), shared by the header export
// (emitC, wrapped in an include guard) and the inline/clipboard export
// (emitInline, no guard — pasted into a buffer that already has gen2.h).
static std::string emitBodyC(const std::vector<BakedSprite>& sprites)
{
    std::string o;
    char hdr[160];
    for (const BakedSprite& s : sprites) {
        const std::string nm = ident(s.name, "spr");
        std::snprintf(hdr, sizeof hdr,
                      "/* %s: %dx%d px, stride=%d B/row, 7 phases, %zu B total */\n",
                      nm.c_str(), s.w, s.h, s.stride, s.bank.size());
        o += hdr;
        std::snprintf(hdr, sizeof hdr,
                      "static const unsigned char %s_ps[%zu] = {\n",
                      nm.c_str(), s.bank.size());
        o += hdr;
        o += hexByteRows(s.bank, "    ");
        o += "};\n";
        std::snprintf(hdr, sizeof hdr,
                      "static const gen2_sprite_t %s = { %s_ps, %d, %d };\n\n",
                      nm.c_str(), nm.c_str(), s.stride, s.h);
        o += hdr;
    }
    return o;
}

std::string emitC(const std::vector<BakedSprite>& sprites, const std::string& title)
{
    const std::string guard = "PRESHIFT_" + upper(ident(title, "S")) + "_H";
    std::string o;
    o += "/* " + title + " -- 7-phase pre-shifted GEN2 HGR sprite bank.\n";
    o += " * Auto-generated by the HGR Paint Sprite Bank editor -- DO NOT EDIT.\n";
    o += " * Use with gen2_hgr_sprite() (dev/lib/gen2c, link GEN2C_PRESHIFT_SRCS). */\n";
    o += "#ifndef " + guard + "\n#define " + guard + "\n\n";
    o += "#include \"gen2.h\"\n\n";
    o += emitBodyC(sprites);
    o += "#endif /* " + guard + " */\n";
    return o;
}

std::string emitInline(const std::vector<BakedSprite>& sprites)
{
    std::string o;
    o += "/* Pre-shifted sprite bank -- paste into a DevBench GEN2 HGR buffer that\n";
    o += " * #includes \"gen2.h\". Draw: gen2_hgr_sprite(x, y, &NAME, GEN2_XOR). */\n";
    o += emitBodyC(sprites);
    return o;
}

std::string emitAsm(const std::vector<BakedSprite>& sprites, const std::string& title,
                    std::string& incOut)
{
    std::string o, inc;
    o += "; " + title + " -- 7-phase pre-shifted GEN2 HGR sprite bank.\n";
    o += "; Auto-generated by the HGR Paint Sprite Bank editor -- DO NOT EDIT.\n";
    o += "; Per sprite: 7 phase blocks, each H rows x STRIDE bytes, 7px/byte.\n\n";
    inc += "; " + title + " -- pre-shifted sprite bank constants (immediate-mode).\n";
    inc += "; .include this; .import the *_ps data labels from the sister .s.\n\n";
    for (const BakedSprite& s : sprites) {
        const std::string nm = ident(s.name, "spr");
        o += "        .export _" + nm + "_ps, " + nm + "_ps\n";
        o += "_" + nm + "_ps:\n";
        o += nm + "_ps:\n";
        // .byte rows
        char buf[8];
        for (size_t i = 0; i < s.bank.size(); i += 12) {
            o += "        .byte ";
            for (size_t j = i; j < i + 12 && j < s.bank.size(); ++j) {
                std::snprintf(buf, sizeof buf, "$%02X", s.bank[j]);
                o += buf;
                if (j + 1 < i + 12 && j + 1 < s.bank.size()) o += ",";
            }
            o += "\n";
        }
        o += "\n";
        const std::string up = upper(nm);
        char line[96];
        std::snprintf(line, sizeof line, "%s_W      = %d\n", up.c_str(), s.w);  inc += line;
        std::snprintf(line, sizeof line, "%s_H      = %d\n", up.c_str(), s.h);  inc += line;
        std::snprintf(line, sizeof line, "%s_STRIDE = %d\n", up.c_str(), s.stride); inc += line;
        std::snprintf(line, sizeof line, "%s_PHASES = %d\n\n", up.c_str(), kPhases); inc += line;
    }
    incOut = inc;
    return o;
}

std::string emitSheet(const uint8_t* page8k, const std::vector<SpriteRegion>& regions)
{
    std::string o;
    o += "; Sprite sheet exported by the HGR Paint Sprite Bank editor.\n";
    o += "; Re-bake with: python3 tools/build_preshift_sprites.py THIS.txt -o bank.h\n\n";
    char hdr[96];
    for (const SpriteRegion& r : regions) {
        const std::string nm = ident(r.name, "spr");
        std::snprintf(hdr, sizeof hdr, "sprite %s %dx%d\n", nm.c_str(), r.w, r.h);
        o += hdr;
        const std::vector<std::vector<uint8_t>> mask = extractMask(page8k, r);
        for (const std::vector<uint8_t>& row : mask) {
            std::string line;
            for (uint8_t bit : row) line += bit ? '#' : '.';
            o += line;
            o += "\n";
        }
        o += "\n";
    }
    return o;
}

} // namespace hgrpaint
