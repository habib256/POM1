// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Pom1BenchCc65.cpp — the DevBench's cc65 project layer: parsing a sketch's
// sibling Makefile / linker cfg into an AsmProjectCtx, the JSON "C build spec"
// (which modules, which include paths, which runtime archive) and the ar65
// runtime-library cache, plus the cl65 / ld65 command builders.
//
// Split out of Pom1BenchHost.cpp (3957 lines) — see Pom1BenchTargets.cpp for
// the other half of that split. What lives here is deliberately the PURE part:
// functions over strings and paths, with no MainWindow, no EmulationController
// and no ImGui. The stateful build driver (build(), pollBuild(),
// compileBasicNative()) stayed behind and calls into this. Pure code motion —
// no behaviour changed.
//
// Link model reminder (doc/DEVBENCH.md): C targets link the runtime as an ar65
// ARCHIVE so ld65 dead-strips unused families; `userAsm` objects are direct and
// must survive unreferenced, so they are NEVER archived.

#include "Pom1BenchCc65.h"
#include "ResourceLocator.h"
#include "Pom1BenchTargets.h"   // benchScratchDir
#include "POM1Build.h"
#include "Logger.h"
#include "ProcessUtil.h"
#include "bench/CodeBench.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace pom1::benchhost {

// ---------------------------------------------------------------------------
// Project-context build. When the bench editor has a real sketchs/ project file
// open, compile it the way `make` would instead of as a bare sketch: its
// sibling Makefile's LOAD_CFG, the EXTRA_ASM siblings, the project dir on the
// ca65 include path (so sibling `.inc` data like tileset_rogue.inc resolve),
// and (dual-bank cfgs) lo/hi halves loaded separately.
// ---------------------------------------------------------------------------


std::string benchTrim(const std::string& s)
{
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Parse "NAME := value" / "NAME ?= value" (the Makefile.common convention). On a
// match, strips any inline `# comment` and returns the trimmed RHS in `out`.
bool benchMakeVar(const std::string& line, const char* name, std::string& out)
{
    const std::string t = benchTrim(line);
    const std::string nm(name);
    if (t.compare(0, nm.size(), nm) != 0) return false;
    size_t p = nm.size();
    while (p < t.size() && (t[p] == ' ' || t[p] == '\t')) ++p;
    if (p + 1 >= t.size() || !((t[p] == ':' || t[p] == '?') && t[p + 1] == '=')) return false;
    std::string rhs = t.substr(p + 2);
    const size_t h = rhs.find('#');
    if (h != std::string::npos) rhs = rhs.substr(0, h);
    out = benchTrim(rhs);
    return true;
}

// Minimal .sketch.json field extraction (sidecars are tiny, hand-written JSON).
std::string sketchJsonString(const std::string& json, const char* key)
{
    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";
    const size_t end = json.find('"', pos + 1);
    if (end == std::string::npos) return "";
    return json.substr(pos + 1, end - pos - 1);
}

std::vector<std::string> sketchJsonStringArray(const std::string& json, const char* key)
{
    std::vector<std::string> out;
    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return out;
    pos = json.find('[', pos);
    if (pos == std::string::npos) return out;
    const size_t end = json.find(']', pos);
    if (end == std::string::npos) return out;
    const std::string slice = json.substr(pos + 1, end - pos - 1);
    for (size_t i = 0; i < slice.size(); ) {
        const size_t q1 = slice.find('"', i);
        if (q1 == std::string::npos) break;
        const size_t q2 = slice.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        out.push_back(slice.substr(q1 + 1, q2 - q1 - 1));
        i = q2 + 1;
    }
    return out;
}

std::filesystem::path resolveRepoRelativePath(const std::filesystem::path& baseDir, const std::string& rel)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p(rel);
    if (p.is_absolute() && fs::exists(p, ec)) return fs::weakly_canonical(p, ec);
    // Local sidecar first (e.g. "apple1_sok_hgr.cfg" next to the .asm).
    {
        const fs::path cand = baseDir / p;
        if (fs::exists(cand, ec)) return fs::weakly_canonical(cand, ec);
    }
    // Walk up toward the repo root (sidecars often use repo-relative paths
    // like "sketchs/gen2/foo/apple1_sok_hgr.cfg" or "dev/lib/.../foo.cfg").
    for (fs::path root = baseDir; !root.empty(); root = root.parent_path()) {
        const fs::path cand = root / p;
        if (fs::exists(cand, ec)) return fs::weakly_canonical(cand, ec);
        if (root == root.parent_path()) break;
    }
    return {};
}

// cc65 dual-bank cfgs declare `file = "%O.lo"` / `file = "%O.hi"` on MEMORY lines.
// apple1_gen2.cfg mentions those tokens only in comments — ignore comment text.
std::string cfgCodePortion(const std::string& line)
{
    const size_t h = line.find('#');
    return (h == std::string::npos) ? line : line.substr(0, h);
}

bool cfgDeclaresOutputFile(const std::string& line, const char* token)
{
    const std::string code = cfgCodePortion(line);
    if (code.find("file") == std::string::npos) return false;
    return code.find(token) != std::string::npos;
}

std::string cfgNameBeforeColon(const std::string& line)
{
    const std::string code = cfgCodePortion(line);
    const size_t colon = code.find(':');
    if (colon == std::string::npos) return "";
    return benchTrim(code.substr(0, colon));
}

std::string cfgSegmentLoadName(const std::string& line, const char* segment)
{
    const std::string code = cfgCodePortion(line);
    const std::string prefix = std::string(segment) + ":";
    const std::string trimmed = benchTrim(code);
    if (trimmed.compare(0, prefix.size(), prefix) != 0) return "";
    const size_t load = trimmed.find("load");
    if (load == std::string::npos) return "";
    const size_t eq = trimmed.find('=', load);
    if (eq == std::string::npos) return "";
    size_t pos = eq + 1;
    while (pos < trimmed.size() && (trimmed[pos] == ' ' || trimmed[pos] == '\t')) ++pos;
    const size_t end = trimmed.find_first_of(" \t,;", pos);
    return benchTrim(trimmed.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
}

void probeDualBankFromCfg(const std::string& cfgPath, AsmProjectCtx& p)
{
    std::ifstream cf(cfgPath);
    std::string cl;
    bool lo = false, hi = false;
    std::string loMem, hiMem, codeMem;
    auto startAddr = [](const std::string& s, uint16_t& dst) {
        const size_t sp = s.find("start");
        if (sp == std::string::npos) return;
        const size_t d = s.find('$', sp);
        if (d == std::string::npos) return;
        try { dst = static_cast<uint16_t>(std::stoul(s.substr(d + 1, 4), nullptr, 16)); } catch (...) {}
    };
    while (std::getline(cf, cl)) {
        if (cfgDeclaresOutputFile(cl, "%O.lo")) { lo = true; loMem = cfgNameBeforeColon(cl); startAddr(cl, p.loAddr); }
        if (cfgDeclaresOutputFile(cl, "%O.hi")) { hi = true; hiMem = cfgNameBeforeColon(cl); startAddr(cl, p.hiAddr); }
        const std::string loadName = cfgSegmentLoadName(cl, "CODE");
        if (!loadName.empty()) codeMem = loadName;
    }
    p.dualBank = lo && hi;
    if (p.dualBank) {
        if (!codeMem.empty() && codeMem == hiMem) p.entryAddr = p.hiAddr;
        else p.entryAddr = p.loAddr;
    }
}

std::string resolveAssetPath(const std::string& rel, const std::string& sourcePath,
                                    const std::string& devRoot)
{
    namespace fs = std::filesystem;
    if (rel.empty()) return {};
    std::error_code ec;
    fs::path p(rel);
    if (p.is_absolute() && fs::exists(p, ec))
        return fs::weakly_canonical(p, ec).string();
    if (!sourcePath.empty()) {
        const fs::path base = fs::absolute(fs::path(sourcePath), ec).parent_path();
        if (!ec) {
            const fs::path hit = resolveRepoRelativePath(base, rel);
            if (!hit.empty()) return hit.string();
        }
    }
    if (!devRoot.empty()) {
        const fs::path cand = fs::path(devRoot).parent_path() / rel;
        if (fs::exists(cand, ec)) return fs::weakly_canonical(cand, ec).string();
    }
    // Last resort: POM1's single search order. It replaces the four "", "../",
    // "../../", "../../../" spellings this used to carry, and adds the
    // exe-relative packaged roots the hand-rolled list never had.
    const fs::path hit = pom1::ResourceLocator::defaultLocator().find(rel);
    return hit.empty() ? std::string{} : fs::absolute(hit, ec).string();
}

AsmProjectCtx probeSketchProject(const std::string& sourcePath)
{
    namespace fs = std::filesystem;
    AsmProjectCtx p;
    if (sourcePath.empty()) return p;
    std::error_code ec;
    const fs::path src = fs::absolute(fs::path(sourcePath), ec);
    p.dir = src.parent_path();
    const fs::path sidecar = p.dir / ".sketch.json";
    if (!fs::exists(sidecar, ec)) return p;

    std::ifstream in(sidecar);
    if (!in) return p;
    const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    const std::string cfgRel = sketchJsonString(json, "cfg");
    if (cfgRel.empty()) return p;
    const fs::path cfgPath = resolveRepoRelativePath(p.dir, cfgRel);
    if (cfgPath.empty()) return p;
    p.cfg = cfgPath.string();

    for (const std::string& ea : sketchJsonStringArray(json, "extraAsm")) {
        const fs::path ep = resolveRepoRelativePath(p.dir, ea);
        if (!ep.empty()) p.extraAsm.push_back(ep.string());
    }

    // Optional extra ca65 include dirs — for cross-sketch includes the project
    // dir alone can't resolve (game_rogue_x2 .include's the x1 asset pack
    // from ../game_rogue, the Makefile's `-I ../game_rogue`). Relative
    // entries resolve against the sketch dir, repo-relative ones walk up.
    for (const std::string& d : sketchJsonStringArray(json, "incDirs")) {
        const fs::path ip = resolveRepoRelativePath(p.dir, d);
        if (!ip.empty()) p.incDirs.push_back(ip.string());
    }

    // Optional ca65 -D symbols (e.g. CODETANK_BUILD for the full TMS LOGO /
    // CodeTank cartridge feature set). Applied to the main source AND every
    // extraAsm module so gated lib code (.ifdef) compiles consistently.
    for (const std::string& d : sketchJsonStringArray(json, "defines"))
        if (!d.empty()) p.defines.push_back(d);

    // Dual-bank is defined by the linker cfg (MEMORY lines with file=%O.lo/hi).
    probeDualBankFromCfg(p.cfg, p);

    p.ok = true;
    return p;
}

std::string benchAbsToWasmDev(const std::string& absPath)
{
    const std::string needle = "/dev/";
    const size_t p = absPath.find(needle);
    return (p != std::string::npos) ? absPath.substr(p) : std::string{};
}

std::string jsonQuoted(const std::string& s)
{
    std::string o = "\"";
    for (char c : s) {
        if (c == '\\' || c == '"') o += '\\';
        o += c;
    }
    o += '"';
    return o;
}

// ─────────────────────────────────────────────────────────────
// Bench C-target build specs (tms9918c / gen2c / apple1c)
//
// One JSON spec per C target: linker cfg + -D defines + include dirs + the
// runtime .c/.s file lists, all as MEMFS-style "/dev/..." paths. The EDITABLE
// source of truth is dev/bench/<target>.json — loaded at build time, so a
// runtime-lib change needs no emulator rebuild. The kBenchCSpec* literals
// below are byte-identical compiled-in fallbacks for bundles that predate
// dev/bench/. Desktop derives the build commands from the parsed spec;
// WASM forwards the raw JSON text to window.POM1cc65.buildC.
//
// LINK MODEL (archive, not force-link). ld65 dead-strips ONLY modules pulled
// from a .lib archive — objects named directly on the link line are always
// linked whole (the old single-cl65-command path force-linked the entire
// ~20 KB gen2c runtime into every Bench binary; that's why gen2_sprengine /
// gen2_sprmask used to be left OUT of this spec). Both builders therefore
// compile the spec's cSources+asmSources to per-module .o, archive them with
// ar65 into a per-target runtime .lib, and link  user.o + <rt>.lib  so a
// sketch pays only for the families it calls. Desktop caches the .o/.lib in
// benchScratchDir()/rtlib_<target> keyed on source/header mtimes, so live
// edits to lib sources still apply on the next Run; if ar65 is missing it
// falls back to the historical force-link command. The optional "userAsm"
// list is the opposite contract: user-side modules (a sketch's EXTRA_ASM)
// linked as DIRECT objects, never archived — they must survive even when no
// symbol references them.
//   - gen2c is split into per-family modules (init/pixel/rect/text/sprites/
//     hgr_blit_x2/preshift/sprmask/sprengine/geom/lores) + the hot-path asm
//     gen2_blit.s and the hand-asm gen2_hgr_x2.s (x2 inflate; its .c
//     miscompiles under -Oirs).
//   - the shared apple1c text base rides along with GEN2 so C programs can
//     also print to the WOZ terminal / read the keyboard.
//   - the card-neutral gfx layer (dev/lib/gfx) is compiled FROM SOURCE so
//     edits apply live; the backend files bind gfx_* to the card.
//   - POM1_GFX_GEN2 / POM1_GFX_TMS let a portable sketch pick its bring-up
//     with #if defined(...) without the author passing -D by hand.
//   - telemetry is header-only: include dir, nothing to compile.
// See doc/DEVBENCH.md ("Per-target C build specs").
// ─────────────────────────────────────────────────────────────

const char* kBenchCSpecApple1c = R"json({
  "cfg": "/dev/cc65/apple1_c.cfg",
  "incDirs": ["/dev/lib/apple1c", "/dev/lib/telemetry"],
  "cSources": [
    { "path": "/dev/lib/apple1c/apple1io.c", "name": "apple1io.c" }
  ],
  "asmSources": [
    { "path": "/dev/lib/apple1c/apple1io_asm.s", "name": "apple1io_asm.s" }
  ]
}
)json";

const char* kBenchCSpecGen2c = R"json({
  "cfg": "/dev/cc65/apple1_gen2_c.cfg",
  "defines": ["POM1_GFX_GEN2"],
  "incDirs": ["/dev/lib/gen2c", "/dev/lib/gen2", "/dev/lib/apple1c", "/dev/lib/gfx", "/dev/lib/telemetry"],
  "cSources": [
    { "path": "/dev/lib/gen2c/gen2_init.c", "name": "gen2_init.c" },
    { "path": "/dev/lib/gen2c/gen2_pixel.c", "name": "gen2_pixel.c" },
    { "path": "/dev/lib/gen2c/gen2_rect.c", "name": "gen2_rect.c" },
    { "path": "/dev/lib/gen2c/gen2_text.c", "name": "gen2_text.c" },
    { "path": "/dev/lib/gen2c/gen2_text_num.c", "name": "gen2_text_num.c" },
    { "path": "/dev/lib/gen2c/gen2_sprites.c", "name": "gen2_sprites.c" },
    { "path": "/dev/lib/gen2c/gen2_hgr_blit_x2.c", "name": "gen2_hgr_blit_x2.c" },
    { "path": "/dev/lib/gen2c/gen2_preshift.c", "name": "gen2_preshift.c" },
    { "path": "/dev/lib/gen2c/gen2_sprengine.c", "name": "gen2_sprengine.c" },
    { "path": "/dev/lib/gen2c/gen2_geom.c", "name": "gen2_geom.c" },
    { "path": "/dev/lib/gen2c/gen2_lores.c", "name": "gen2_lores.c" },
    { "path": "/dev/lib/apple1c/apple1io.c", "name": "apple1io.c" },
    { "path": "/dev/lib/gfx/gfx_line.c", "name": "gfx_line.c" },
    { "path": "/dev/lib/gfx/gfx_rect.c", "name": "gfx_rect.c" },
    { "path": "/dev/lib/gfx/gfx_circle.c", "name": "gfx_circle.c" },
    { "path": "/dev/lib/gfx/gfx_ellipse.c", "name": "gfx_ellipse.c" },
    { "path": "/dev/lib/gfx/gfx_num_dec.c", "name": "gfx_num_dec.c" },
    { "path": "/dev/lib/gfx/gfx_num_hex.c", "name": "gfx_num_hex.c" },
    { "path": "/dev/lib/gfx/gfx_text.c", "name": "gfx_text.c" },
    { "path": "/dev/lib/gfx/gfx_backend_gen2.c", "name": "gfx_backend_gen2.c" },
    { "path": "/dev/lib/gfx/gfx_backend_gen2_rect.c", "name": "gfx_backend_gen2_rect.c" },
    { "path": "/dev/lib/gfx/gfx_text_backend_gen2.c", "name": "gfx_text_backend_gen2.c" }
  ],
  "asmSources": [
    { "path": "/dev/lib/gen2c/gen2_blit.s", "name": "gen2_blit.s" },
    { "path": "/dev/lib/gen2c/gen2_hgr_x2.s", "name": "gen2_hgr_x2.s" },
    { "path": "/dev/lib/gen2c/gen2_sprmask.s", "name": "gen2_sprmask.s" },
    { "path": "/dev/lib/apple1c/apple1io_asm.s", "name": "apple1io_asm.s" }
  ]
}
)json";

const char* kBenchCSpecTms9918c = R"json({
  "cfg": "/dev/lib/tms9918c/cc65/codetank_c.cfg",
  "defines": ["POM1_GFX_TMS"],
  "incDirs": ["/dev/lib/tms9918c", "/dev/lib/gfx", "/dev/lib/telemetry"],
  "cSources": [
    { "path": "/dev/lib/tms9918c/apple1.c", "name": "apple1.c" },
    { "path": "/dev/lib/tms9918c/tms9918.c", "name": "tms9918.c" },
    { "path": "/dev/lib/tms9918c/screen1.c", "name": "screen1.c" },
    { "path": "/dev/lib/tms9918c/c64font.c", "name": "c64font.c" },
    { "path": "/dev/lib/tms9918c/screen1_input.c", "name": "screen1_input.c" },
    { "path": "/dev/lib/tms9918c/screen2_init.c", "name": "screen2_init.c" },
    { "path": "/dev/lib/tms9918c/screen2_text.c", "name": "screen2_text.c" },
    { "path": "/dev/lib/tms9918c/screen2_pixel.c", "name": "screen2_pixel.c" },
    { "path": "/dev/lib/tms9918c/screen2_geom.c", "name": "screen2_geom.c" },
    { "path": "/dev/lib/tms9918c/screen1_ext.c", "name": "screen1_ext.c" },
    { "path": "/dev/lib/tms9918c/screen2_ext.c", "name": "screen2_ext.c" },
    { "path": "/dev/lib/tms9918c/sprites.c", "name": "sprites.c" },
    { "path": "/dev/lib/tms9918c/sprite_shadow.c", "name": "sprite_shadow.c" },
    { "path": "/dev/lib/tms9918c/vsync.c", "name": "vsync.c" },
    { "path": "/dev/lib/tms9918c/printlib.c", "name": "printlib.c" },
    { "path": "/dev/lib/tms9918c/random.c", "name": "random.c" },
    { "path": "/dev/lib/gfx/gfx_line.c", "name": "gfx_line.c" },
    { "path": "/dev/lib/gfx/gfx_rect.c", "name": "gfx_rect.c" },
    { "path": "/dev/lib/gfx/gfx_circle.c", "name": "gfx_circle.c" },
    { "path": "/dev/lib/gfx/gfx_ellipse.c", "name": "gfx_ellipse.c" },
    { "path": "/dev/lib/gfx/gfx_num_dec.c", "name": "gfx_num_dec.c" },
    { "path": "/dev/lib/gfx/gfx_num_hex.c", "name": "gfx_num_hex.c" },
    { "path": "/dev/lib/gfx/gfx_text.c", "name": "gfx_text.c" },
    { "path": "/dev/lib/gfx/gfx_backend_tms.c", "name": "gfx_backend_tms.c" },
    { "path": "/dev/lib/gfx/gfx_backend_tms_rect.c", "name": "gfx_backend_tms_rect.c" },
    { "path": "/dev/lib/gfx/gfx_text_backend_tms.c", "name": "gfx_text_backend_tms.c" }
  ],
  "asmSources": [
    { "path": "/dev/lib/tms9918c/apple1_asm.s", "name": "apple1_asm.s" },
    { "path": "/dev/lib/tms9918c/tms_fast.s", "name": "tms_fast.s" }
  ]
}
)json";



// [{"path":...,"name":...},...] extraction — same tolerant hand-rolled style as
// the .sketch.json helpers above (fixed schema, no nested arrays/objects).
std::vector<BenchCSpec::Src> benchJsonObjArray(const std::string& json, const char* key)
{
    std::vector<BenchCSpec::Src> out;
    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return out;
    pos = json.find('[', pos + needle.size());
    if (pos == std::string::npos) return out;
    for (size_t i = pos + 1; i < json.size(); ) {
        const size_t ob = json.find_first_of("{]", i);
        if (ob == std::string::npos || json[ob] == ']') break;
        const size_t cb = json.find('}', ob + 1);
        if (cb == std::string::npos) break;
        const std::string obj = json.substr(ob, cb - ob + 1);
        BenchCSpec::Src s;
        s.path = sketchJsonString(obj, "path");
        s.name = sketchJsonString(obj, "name");
        if (s.name.empty() && !s.path.empty())
            s.name = std::filesystem::path(s.path).filename().string();
        if (!s.path.empty()) out.push_back(std::move(s));
        i = cb + 1;
    }
    return out;
}

BenchCSpec benchCSpecParse(const std::string& text)
{
    BenchCSpec s;
    s.rawJson    = text;
    s.cfg        = sketchJsonString(text, "cfg");
    s.defines    = sketchJsonStringArray(text, "defines");
    s.incDirs    = sketchJsonStringArray(text, "incDirs");
    s.cSources   = benchJsonObjArray(text, "cSources");
    s.asmSources = benchJsonObjArray(text, "asmSources");
    s.userAsm    = benchJsonObjArray(text, "userAsm");
    s.ok = !s.cfg.empty() && !(s.cSources.empty() && s.asmSources.empty());
    return s;
}

// dev/bench/<name>.json when present (the editable source of truth), else the
// compiled-in default. devRoot: the resolved dev/ tree on desktop, "/dev" on
// WASM (MEMFS). A missing or unparsable file falls back with a one-line notice
// so packaged builds with an old bundle layout keep working.
BenchCSpec loadBenchCSpec(const std::string& devRoot, const char* name, const char* embedded)
{
    std::string text;
    if (!devRoot.empty()) {
        std::ifstream in(devRoot + "/bench/" + name + ".json", std::ios::binary);
        if (in) text.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    if (!text.empty()) {
        BenchCSpec s = benchCSpecParse(text);
        if (s.ok) { s.fromFile = true; return s; }
    }
    std::fprintf(stderr, "[bench] dev/bench/%s.json %s - using built-in spec\n",
                 name, text.empty() ? "not found" : "unparsable");
    return benchCSpecParse(embedded);
}

// Compact re-serialisation — used when per-sketch EXTRA_ASM modules must be
// folded into a spec before handing it to the WASM builder.
std::string benchCSpecSerialize(const BenchCSpec& s)
{
    std::ostringstream o;
    o << "{\"cfg\":" << jsonQuoted(s.cfg);
    auto strArray = [&o](const char* key, const std::vector<std::string>& v) {
        o << ",\"" << key << "\":[";
        for (size_t i = 0; i < v.size(); ++i) o << (i ? "," : "") << jsonQuoted(v[i]);
        o << "]";
    };
    auto srcArray = [&o](const char* key, const std::vector<BenchCSpec::Src>& v) {
        o << ",\"" << key << "\":[";
        for (size_t i = 0; i < v.size(); ++i)
            o << (i ? "," : "") << "{\"path\":" << jsonQuoted(v[i].path)
              << ",\"name\":" << jsonQuoted(v[i].name) << "}";
        o << "]";
    };
    if (!s.defines.empty()) strArray("defines", s.defines);
    strArray("incDirs", s.incDirs);
    srcArray("cSources", s.cSources);
    srcArray("asmSources", s.asmSources);
    if (!s.userAsm.empty()) srcArray("userAsm", s.userAsm);
    o << "}";
    return o.str();
}

#if !POM1_IS_WASM
// Map a spec's "/dev/..." path onto the resolved desktop dev/ tree — the same
// fs::absolute(devRoot / rel) computation probe()'s members used, so the
// derived command line stays byte-identical to the historical hardcoded one.
std::string benchDevAbs(const std::string& devRoot, const std::string& devPath)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    std::string rel = devPath;
    if (rel.rfind("/dev/", 0) == 0) rel = rel.substr(5);
    else if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);
    return fs::absolute(fs::path(devRoot) / rel, ec).string();
}

// The full desktop cl65 command for a C target, derived from the parsed spec
// (ONE source of truth with the WASM path). Sources are emitted zip-interleaved
// (c[0], asm[0], c[1], asm[1], then the longer list's tail), which preserves
// the historical argument order byte-for-byte; cl65 itself is order-agnostic
// (options are global, objects reach ld65 whatever their position).
std::string benchCSpecCl65Cmd(const BenchCSpec& spec, const std::string& devRoot,
                                     const std::string& cl65, const std::string& cfgAbs,
                                     const std::string& srcC, const std::string& extraObjs,
                                     const std::string& outBin)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    std::string cmd = bench::shellQuote(cl65) + " -t none -Oirs";
    for (const std::string& d : spec.defines) cmd += " -D" + d;
    cmd += " -C " + bench::shellQuote(cfgAbs);
    for (const std::string& inc : spec.incDirs) {
        const std::string dir = benchDevAbs(devRoot, inc);
        if (fs::exists(dir, ec)) cmd += " -I " + bench::shellQuote(dir);
    }
    cmd += " " + bench::shellQuote(srcC);
    const size_t n = std::max(spec.cSources.size(), spec.asmSources.size());
    for (size_t i = 0; i < n; ++i) {
        if (i < spec.cSources.size())
            cmd += " " + bench::shellQuote(benchDevAbs(devRoot, spec.cSources[i].path));
        if (i < spec.asmSources.size())
            cmd += " " + bench::shellQuote(benchDevAbs(devRoot, spec.asmSources[i].path));
    }
    cmd += extraObjs + " -o " + bench::shellQuote(outBin);
    return cmd;
}

// ── Runtime archive (the dead-strip link path) ─────────────────────────────
// Compile the spec's runtime modules to per-module .o and archive them with
// ar65 into <scratch>/rtlib_<target>/<target>_rt.lib. ld65 pulls only the
// members a program references out of a .lib, so the final link pays per
// family actually called instead of force-linking the whole runtime. The
// cache is mtime-keyed: a module recompiles when its source — or any
// .h/.inc in the spec's incDirs — is newer than its .o, so live edits to lib
// sources still apply on the next Run while the common case (unchanged
// runtime) costs stat() calls instead of ~25 compiles. A stamp file (tools +
// flags + module list) wipes the cache when the spec itself changes.


BenchRtLib benchEnsureRtLib(const BenchCSpec& spec, const std::string& devRoot,
                                   const std::string& cl65, const std::string& ar65,
                                   const std::string& target)
{
    namespace fs = std::filesystem;
    BenchRtLib r;
    std::error_code ec;
    const fs::path cacheDir = benchScratchDir(ec) / ("rtlib_" + target);
    std::error_code mkec;
    fs::create_directories(cacheDir, mkec);
    if (mkec) { r.console = "cannot create " + cacheDir.string() + "\n"; return r; }

    // Per-module compile flags (no -C: compile only; -Oirs is inert for .s).
    std::string flags = " -t none -Oirs -c";
    for (const std::string& d : spec.defines) flags += " -D" + d;
    std::string incFlags;
    for (const std::string& inc : spec.incDirs) {
        const std::string dir = benchDevAbs(devRoot, inc);
        if (fs::exists(dir, ec)) incFlags += " -I " + bench::shellQuote(dir);
    }

    struct Mod { std::string src; fs::path obj; };
    std::vector<Mod> mods;
    auto addMods = [&](const std::vector<BenchCSpec::Src>& list) {
        for (const auto& s : list) {
            Mod m;
            m.src = benchDevAbs(devRoot, s.path);
            std::string base = fs::path(s.name.empty() ? s.path : s.name).filename().string();
            const size_t dot = base.find_last_of('.');
            if (dot != std::string::npos) base.resize(dot);
            m.obj = cacheDir / (base + ".o");
            mods.push_back(std::move(m));
        }
    };
    addMods(spec.cSources);
    addMods(spec.asmSources);

    // Stamp: tools + flags + the module list. Any change wipes the cache.
    std::string stamp = cl65 + "|" + ar65 + "|" + flags + "|" + incFlags + "|";
    for (const auto& m : mods) stamp += m.src + ";";
    const fs::path stampFile = cacheDir / "flags.stamp";
    {
        std::string old;
        std::ifstream in(stampFile, std::ios::binary);
        if (in) old.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (old != stamp) {
            for (const auto& e : fs::directory_iterator(cacheDir, ec)) fs::remove(e.path(), ec);
            std::ofstream(stampFile, std::ios::binary) << stamp;
        }
    }

    // Newest header/.inc mtime across the incDirs (flat scan — headers sit flat
    // in each lib dir): an edited gen2.h must recompile every module.
    fs::file_time_type newestHdr = fs::file_time_type::min();
    for (const std::string& inc : spec.incDirs) {
        const std::string dir = benchDevAbs(devRoot, inc);
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            const std::string ext = e.path().extension().string();
            if (ext != ".h" && ext != ".inc") continue;
            std::error_code tec;
            const auto t = fs::last_write_time(e.path(), tec);
            if (!tec && t > newestHdr) newestHdr = t;
        }
    }

    const fs::path lib = cacheDir / (target + "_rt.lib");
    bool rearchive = !fs::exists(lib, ec);
    for (const auto& m : mods) {
        std::error_code sec;
        const auto srcT = fs::last_write_time(m.src, sec);
        if (sec) { r.console = "runtime source missing: " + m.src + "\n"; return r; }
        std::error_code oec;
        const auto objT = fs::last_write_time(m.obj, oec);
        if (!oec && objT >= srcT && objT >= newestHdr) continue;    // up to date
        const std::string cmd = bench::shellQuote(cl65) + flags + incFlags +
            " -o " + bench::shellQuote(m.obj.string()) + " " + bench::shellQuote(m.src);
        std::string out;
        if (bench::runCapture(cmd, out) != 0) {
            fs::remove(m.obj, ec);
            r.hardError = true;
            r.console += "[cl65 -c " + fs::path(m.src).filename().string() + "]\n" + out;
            return r;
        }
        if (!out.empty())
            r.console += "[cl65 -c " + fs::path(m.src).filename().string() + "]\n" + out;
        rearchive = true;
    }

    if (rearchive) {
        fs::remove(lib, ec);   // ar65 a appends; never accumulate stale members
        std::string cmd = bench::shellQuote(ar65) + " a " + bench::shellQuote(lib.string());
        for (const auto& m : mods) cmd += " " + bench::shellQuote(m.obj.string());
        std::string out;
        if (bench::runCapture(cmd, out) != 0) {
            fs::remove(lib, ec);
            r.console += "[ar65]\n" + out;
            return r;
        }
    }
    r.ok = true;
    r.libPath = lib.string();
    return r;
}

// Final link for the archive path: user source + direct user objects + the
// runtime archive (AFTER the objects, so ld65 resolves their imports from it).
std::string benchCSpecLinkCmd(const BenchCSpec& spec, const std::string& devRoot,
                                     const std::string& cl65, const std::string& cfgAbs,
                                     const std::string& srcC, const std::string& extraObjs,
                                     const std::string& rtLib, const std::string& outBin)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    std::string cmd = bench::shellQuote(cl65) + " -t none -Oirs";
    for (const std::string& d : spec.defines) cmd += " -D" + d;
    cmd += " -C " + bench::shellQuote(cfgAbs);
    for (const std::string& inc : spec.incDirs) {
        const std::string dir = benchDevAbs(devRoot, inc);
        if (fs::exists(dir, ec)) cmd += " -I " + bench::shellQuote(dir);
    }
    cmd += " " + bench::shellQuote(srcC) + extraObjs + " " + bench::shellQuote(rtLib) +
           " -o " + bench::shellQuote(outBin);
    return cmd;
}
#endif

void applySketchAssets(const std::string& sourcePath, std::string& asset, uint16_t& addr)
{
    asset.clear();
    addr = 0;
    namespace fs = std::filesystem;
    if (sourcePath.empty()) return;
    std::error_code ec;
    const fs::path sidecar = fs::absolute(fs::path(sourcePath), ec).parent_path() / ".sketch.json";
    if (!fs::exists(sidecar, ec)) return;
    std::ifstream in(sidecar);
    if (!in) return;
    const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string a = sketchJsonString(json, "asset");
    if (!a.empty()) asset = a;
    const std::string addrStr = sketchJsonString(json, "assetAddr");
    if (!addrStr.empty()) {
        try { addr = static_cast<uint16_t>(std::stoul(addrStr, nullptr, 16)); } catch (...) {}
    }
}

AsmProjectCtx probeAsmProject(const std::string& sourcePath)
{
    namespace fs = std::filesystem;
    AsmProjectCtx p;
    if (sourcePath.empty()) return p;
    p = probeSketchProject(sourcePath);
    if (p.ok) return p;

    std::error_code ec;
    const fs::path src = fs::absolute(fs::path(sourcePath), ec);
    p.dir = src.parent_path();
    const fs::path mk = p.dir / "Makefile";
    if (!fs::exists(mk, ec)) return p;

    std::ifstream f(mk);
    std::string line, loadCfg, cfgDefault, extra, lib, v;
    while (std::getline(f, line)) {
        // Fold backslash-continued lines (the `LIB := -I a \` convention).
        while (true) {
            const std::string t = benchTrim(line);
            if (t.empty() || t.back() != '\\') break;
            line = t.substr(0, t.size() - 1);
            std::string cont;
            if (!std::getline(f, cont)) break;
            line += " " + cont;
        }
        if      (benchMakeVar(line, "LOAD_CFG",  v)) loadCfg    = v;
        else if (benchMakeVar(line, "EXTRA_ASM", v)) extra      = v;
        else if (benchMakeVar(line, "CFG",       v)) cfgDefault = v;
        else if (benchMakeVar(line, "LIB",       v)) lib        = v;
    }
    if (loadCfg == "$(CFG)") loadCfg = cfgDefault;     // CFG ?= default + LOAD_CFG := $(CFG)
    if (loadCfg.empty()) return p;

    fs::path cfgPath(loadCfg);
    if (cfgPath.is_relative()) cfgPath = p.dir / cfgPath;
    cfgPath = fs::weakly_canonical(cfgPath, ec);
    if (ec || !fs::exists(cfgPath, ec)) return p;
    p.cfg = cfgPath.string();

    // EXTRA_ASM tokens (whitespace-separated) -> existing absolute paths.
    for (size_t i = 0; i < extra.size(); ) {
        while (i < extra.size() && (extra[i] == ' ' || extra[i] == '\t')) ++i;
        const size_t start = i;
        while (i < extra.size() && extra[i] != ' ' && extra[i] != '\t') ++i;
        if (i == start) break;
        fs::path ep(extra.substr(start, i - start));
        if (ep.is_relative()) ep = p.dir / ep;
        ep = fs::weakly_canonical(ep, ec);
        if (!ec && fs::exists(ep, ec)) p.extraAsm.push_back(ep.string());
    }

    // LIB's `-I <dir>` tokens -> extra include dirs. dev/lib entries duplicate
    // libFlags_ (harmless, ca65 dedups); the load-bearing ones are the
    // cross-sketch dirs like game_rogue_x2's `-I ../game_rogue`.
    {
        std::istringstream ls(lib);
        std::string tok;
        while (ls >> tok) {
            std::string d;
            if (tok == "-I") { if (!(ls >> d)) break; }
            else if (tok.rfind("-I", 0) == 0) d = tok.substr(2);
            else continue;
            fs::path ip(d);
            if (ip.is_relative()) ip = p.dir / ip;
            ip = fs::weakly_canonical(ip, ec);
            if (!ec && fs::exists(ip, ec)) p.incDirs.push_back(ip.string());
        }
    }

    probeDualBankFromCfg(p.cfg, p);

    p.ok = true;
    return p;
}

// Machine-readable header prepended to DevBench "Build output" so humans and
// agents (IDE assistants, CI triage) can interpret the log without UI context.

} // namespace pom1::benchhost
