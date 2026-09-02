// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// sprite_catalogues_smoke — the 32 SHIPPED sprite catalogues, read with the
// real parser.
//
// WHAT WAS ALREADY COVERED, AND WHAT WAS NOT
//     `sprite_asm_export_smoke` pins the GRAMMAR: the editors' exporter and
//     `parseSpritesAsm` round-trip, and the parser is held against a
//     hand-written catalogue-style fixture (base label, slot comments, inline
//     comments, `_pat` fallback, short blocks dropped). That is the format.
//
//     Nothing read the FILES. `dev/lib/gen2/sprites/sprites_*_hgr.asm` and
//     `dev/lib/tms9918/sprites_*.asm` are a data format with THREE parties —
//     the assembler `.include`s them, the emulator parses them into both
//     sprite editors' Dev library, and `tools/build_hgr_sprites.py` GENERATES
//     the HGR half from the TMS half. The two failure modes differ in the worst
//     possible way: ca65 stops on a malformed file and says so, while the C++
//     parser DROPS what it does not recognise. A sprite lost to a mistyped
//     `.byte` row is a catalogue that quietly shows 22 entries where its own
//     header says 23, and nothing anywhere complains.
//
// Four sections:
//   1. every catalogue parses, and yields exactly the count its header claims.
//   2. every sprite is exactly the geometry its family fixes (HGR 48 B, TMS 32 B).
//   3. names are non-empty and unique within a catalogue — the editor lists them.
//   4. the generator's contract: each HGR catalogue names the same sprites, in
//      the same order, as the TMS master it was generated from.

#include "HgrSpriteAsmExport.h"
#include "TmsSpriteAsmExport.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr size_t kHgrSpriteBytes = 48;   // 16 rows x 3 bytes
constexpr size_t kTmsSpriteBytes = 32;   // native 16x16 quadrant stream

std::string readFile(const fs::path& p)
{
    std::ifstream in(p);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", p.string().c_str());
        std::exit(2);
    }
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

/// The count a catalogue claims about ITSELF, from its header line
/// ("-- 23 sprites, GEN2 HGR x1 mono master"). Asserting against the file's own
/// declaration rather than a number in this test is what makes the assertion
/// survive someone legitimately adding a sprite.
int declaredCount(const std::string& text)
{
    std::smatch m;
    const std::regex re(R"(--\s+(\d+)\s+sprites)");
    if (std::regex_search(text, m, re)) return std::stoi(m[1].str());
    return -1;
}

std::vector<fs::path> catalogues(const fs::path& dir, const std::string& suffix)
{
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string fn = e.path().filename().string();
        if (e.is_regular_file(ec) && fn.rfind("sprites_", 0) == 0 &&
            e.path().extension() == ".asm" &&
            (suffix.empty() || (fn.size() > suffix.size() &&
             fn.compare(fn.size() - suffix.size(), suffix.size(), suffix) == 0)))
            out.push_back(e.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

int failures = 0;

void check(bool ok, const std::string& what)
{
    if (ok) return;
    std::fprintf(stderr, "  [FAIL] %s\n", what.c_str());
    ++failures;
}

} // namespace

int main(int argc, char** argv)
{
    const fs::path root = (argc > 1) ? fs::path(argv[1]) : fs::path("..");
    const fs::path hgrDir = root / "dev" / "lib" / "gen2" / "sprites";
    const fs::path tmsDir = root / "dev" / "lib" / "tms9918";

    const auto hgrFiles = catalogues(hgrDir, "_hgr.asm");
    const auto tmsFiles = catalogues(tmsDir, "");
    if (hgrFiles.empty() || tmsFiles.empty()) {
        std::fprintf(stderr, "no catalogues under %s / %s — wrong root?\n",
                     hgrDir.string().c_str(), tmsDir.string().c_str());
        return 2;
    }

    // ── 1-3. HGR catalogues ──────────────────────────────────────────────
    for (const fs::path& f : hgrFiles) {
        const std::string name = f.filename().string();
        const std::string text = readFile(f);
        const auto sprites = hgrsprite::parseSpritesAsm(text, kHgrSpriteBytes);

        const int declared = declaredCount(text);
        check(declared > 0, name + ": header declares no sprite count");
        check(static_cast<int>(sprites.size()) == declared,
              name + ": header says " + std::to_string(declared) +
              " sprites, the parser finds " + std::to_string(sprites.size()));

        std::set<std::string> seen;
        for (const auto& s : sprites) {
            check(s.bytes.size() == kHgrSpriteBytes,
                  name + "/" + s.name + ": " + std::to_string(s.bytes.size()) +
                  " bytes, expected " + std::to_string(kHgrSpriteBytes));
            check(!s.name.empty(), name + ": a sprite has an empty name");
            check(seen.insert(s.name).second,
                  name + ": duplicate sprite name '" + s.name + "'");
        }
    }

    // ── 1-3. TMS catalogues ──────────────────────────────────────────────
    for (const fs::path& f : tmsFiles) {
        const std::string name = f.filename().string();
        const std::string text = readFile(f);
        const auto sprites = tmssprite::parseSpritesAsm(text, kTmsSpriteBytes);

        const int declared = declaredCount(text);
        check(declared > 0, name + ": header declares no sprite count");
        check(static_cast<int>(sprites.size()) == declared,
              name + ": header says " + std::to_string(declared) +
              " sprites, the parser finds " + std::to_string(sprites.size()));

        std::set<std::string> seen;
        for (const auto& s : sprites) {
            check(s.bytes.size() == kTmsSpriteBytes,
                  name + "/" + s.name + ": " + std::to_string(s.bytes.size()) +
                  " bytes, expected " + std::to_string(kTmsSpriteBytes));
            check(!s.name.empty(), name + ": a sprite has an empty name");
            check(seen.insert(s.name).second,
                  name + ": duplicate sprite name '" + s.name + "'");
        }
    }

    // ── 4. the generator's contract ──────────────────────────────────────
    // tools/build_hgr_sprites.py derives each HGR catalogue from the TMS master
    // of the same family. Nothing re-runs it here — what is asserted is that
    // the two files still describe the SAME sprites, in the same order. Editing
    // a TMS master and forgetting to regenerate is the drift this catches, and
    // it is invisible otherwise: both files parse fine on their own.
    int pairs = 0;
    for (const fs::path& hgr : hgrFiles) {
        std::string stem = hgr.filename().string();      // sprites_building_hgr.asm
        stem = stem.substr(0, stem.size() - std::string("_hgr.asm").size());
        const fs::path tms = tmsDir / (stem + ".asm");
        if (!fs::exists(tms)) {
            check(false, hgr.filename().string() +
                         ": no TMS master at " + tms.filename().string());
            continue;
        }
        ++pairs;
        const auto a = hgrsprite::parseSpritesAsm(readFile(hgr), kHgrSpriteBytes);
        const auto b = tmssprite::parseSpritesAsm(readFile(tms), kTmsSpriteBytes);
        if (a.size() != b.size()) {
            check(false, stem + ": " + std::to_string(a.size()) +
                         " HGR sprites vs " + std::to_string(b.size()) + " TMS");
            continue;
        }
        for (size_t i = 0; i < a.size(); ++i)
            check(a[i].name == b[i].name,
                  stem + " #" + std::to_string(i) + ": HGR names '" + a[i].name +
                  "' where the TMS master names '" + b[i].name +
                  "' — rerun tools/build_hgr_sprites.py --only " + stem);
    }

    std::printf("sprite_catalogues_smoke: %zu HGR + %zu TMS catalogues, "
                "%d generated pairs cross-checked — %s\n",
                hgrFiles.size(), tmsFiles.size(), pairs,
                failures ? "FAILURES" : "all consistent");
    return failures ? 1 : 0;
}
