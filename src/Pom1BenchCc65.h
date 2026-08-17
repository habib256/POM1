// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Pom1BenchCc65.h — cc65 project + C-build-spec layer of the DevBench.
// Implementation in Pom1BenchCc65.cpp; consumed by Pom1BenchHost.cpp.
//
// PRIVATE to the POM1 bench host. The portable bench module (src/bench/) must
// stay ignorant of POM1 — its seam is IBenchHost — so never include this there.
//
// Everything here is a pure function over strings and paths: no MainWindow, no
// EmulationController, no ImGui. Keep it that way; it is what makes this layer
// straightforward to test on its own if the DevBench ever grows a unit test.

#ifndef POM1_BENCH_CC65_H
#define POM1_BENCH_CC65_H

#include <cstdint>
#include <filesystem>
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
struct AsmProjectCtx {
    bool ok = false;
    std::filesystem::path dir;
    std::string cfg;                       // absolute linker .cfg
    std::vector<std::string> extraAsm;     // absolute EXTRA_ASM siblings
    std::vector<std::string> incDirs;      // extra ca65 -I dirs beyond the
                                           // project dir (sidecar "incDirs" or
                                           // the Makefile LIB's -I tokens) —
                                           // cross-sketch includes like
                                           // game_rogue_x2 pulling the x1
                                           // asset pack from ../game_rogue
    std::vector<std::string> defines;      // ca65 -D symbols (e.g. CODETANK_BUILD)
    bool dualBank = false;
    uint16_t loAddr = 0x0280, hiAddr = 0xE000, entryAddr = 0x0280;
};

struct BenchCSpec {
    bool ok = false;
    bool fromFile = false;                  // loaded from dev/bench/*.json (vs embedded)
    std::string rawJson;                    // exact spec text (WASM forwards this to buildC)
    std::string cfg;                        // linker cfg, "/dev/..." form
    std::vector<std::string> defines;       // -D symbols
    std::vector<std::string> incDirs;       // -I dirs, "/dev/..." form
    struct Src { std::string path, name; };
    std::vector<Src> cSources;              // runtime .c modules (archived, dead-stripped)
    std::vector<Src> asmSources;            // hand-written runtime .s modules (archived too)
    std::vector<Src> userAsm;               // user-side .s modules (a sketch's EXTRA_ASM):
                                            // linked as DIRECT objects, never archived
};

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
struct BenchRtLib {
    bool ok = false;
    bool hardError = false;   // a module failed to COMPILE — surface it, don't fall back
    std::string libPath;      // absolute archive path (valid when ok)
    std::string console;      // accumulated tool output (compile errors land here)
};

// Embedded fallback C build specs, used when dev/bench/<name>.json is absent
// (a packaged build without the dev/ tree, or a stale checkout).
extern const char* kBenchCSpecApple1c;
extern const char* kBenchCSpecGen2c;
extern const char* kBenchCSpecTms9918c;

std::string benchTrim(const std::string& s);
bool benchMakeVar(const std::string& line, const char* name, std::string& out);
std::string sketchJsonString(const std::string& json, const char* key);
std::vector<std::string> sketchJsonStringArray(const std::string& json, const char* key);
std::filesystem::path resolveRepoRelativePath(const std::filesystem::path& baseDir, const std::string& rel);
std::string cfgCodePortion(const std::string& line);
bool cfgDeclaresOutputFile(const std::string& line, const char* token);
std::string cfgNameBeforeColon(const std::string& line);
std::string cfgSegmentLoadName(const std::string& line, const char* segment);
void probeDualBankFromCfg(const std::string& cfgPath, AsmProjectCtx& p);
std::string resolveAssetPath(const std::string& rel, const std::string& sourcePath,
                                    const std::string& devRoot);
AsmProjectCtx probeSketchProject(const std::string& sourcePath);
std::string benchAbsToWasmDev(const std::string& absPath);
std::string jsonQuoted(const std::string& s);
std::vector<BenchCSpec::Src> benchJsonObjArray(const std::string& json, const char* key);
BenchCSpec benchCSpecParse(const std::string& text);
BenchCSpec loadBenchCSpec(const std::string& devRoot, const char* name, const char* embedded);
std::string benchCSpecSerialize(const BenchCSpec& s);
std::string benchDevAbs(const std::string& devRoot, const std::string& devPath);
std::string benchCSpecCl65Cmd(const BenchCSpec& spec, const std::string& devRoot,
                                     const std::string& cl65, const std::string& cfgAbs,
                                     const std::string& srcC, const std::string& extraObjs,
                                     const std::string& outBin);
BenchRtLib benchEnsureRtLib(const BenchCSpec& spec, const std::string& devRoot,
                                   const std::string& cl65, const std::string& ar65,
                                   const std::string& target);
std::string benchCSpecLinkCmd(const BenchCSpec& spec, const std::string& devRoot,
                                     const std::string& cl65, const std::string& cfgAbs,
                                     const std::string& srcC, const std::string& extraObjs,
                                     const std::string& rtLib, const std::string& outBin);
void applySketchAssets(const std::string& sourcePath, std::string& asset, uint16_t& addr);
AsmProjectCtx probeAsmProject(const std::string& sourcePath);

} // namespace pom1::benchhost

#endif // POM1_BENCH_CC65_H
