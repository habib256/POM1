// bench_cc65_smoke — first test on Pom1BenchCc65.cpp, the DevBench's pure
// string/path layer. CLAUDE.md described the TU as "straightforward to test
// on its own if the DevBench ever grows a unit test"; this is that test, and
// like cli_dispatcher_smoke half its assertion is that it LINKS with no
// MainWindow, no EmulationController and no ImGui.
//
// Part A (always): pins the Makefile-variable / .sketch.json / linker-cfg
// micro-parsers and the embedded C build specs.
//
// Part B (cc65-gated, POSIX): assembles a fixture with `ca65 -g`, links with
// `ld65 --dbgfile`, and feeds the REAL output through pom1::parseDbgFile —
// the format-compatibility half of dbgfile_smoke, which pins the parser on a
// hand-written miniature. Skips (77) when cc65 is not on PATH.

#include "Pom1BenchCc65.h"
#include "DbgFile.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include <unistd.h>  // mkdtemp (POSIX; macOS needs the explicit include)

using namespace pom1::benchhost;

static constexpr int kSkip = 77;

static bool haveCc65()
{
    return std::system("ca65 --version >/dev/null 2>&1") == 0 &&
           std::system("ld65 --version >/dev/null 2>&1") == 0;
}

static void partA()
{
    // benchTrim
    assert(benchTrim("  \t x y \r\n") == "x y");
    assert(benchTrim("   \t\n") == "");

    // benchMakeVar — ":=" and "?=" only, inline comments stripped
    std::string v;
    assert(benchMakeVar("LOAD_CFG := ../apple1_8k.cfg  # dual", "LOAD_CFG", v));
    assert(v == "../apple1_8k.cfg");
    assert(benchMakeVar("EXTRA_ASM ?= a.s b.s", "EXTRA_ASM", v) && v == "a.s b.s");
    assert(!benchMakeVar("LOAD_CFG = plain-equals.cfg", "LOAD_CFG", v));
    assert(!benchMakeVar("OTHER := x", "LOAD_CFG", v));

    // .sketch.json micro-parsers
    const std::string json =
        R"({"cfg":"apple1_sok.cfg","defines":["CODETANK_BUILD","X2"],"extraAsm":[]})";
    assert(sketchJsonString(json, "cfg") == "apple1_sok.cfg");
    assert(sketchJsonString(json, "missing").empty());
    const auto defs = sketchJsonStringArray(json, "defines");
    assert(defs.size() == 2 && defs[0] == "CODETANK_BUILD" && defs[1] == "X2");
    assert(sketchJsonStringArray(json, "extraAsm").empty());

    // linker-cfg line parsers (comment text must never count as code)
    assert(cfgCodePortion("CODE: load = MAIN; # file = \"%O.lo\"") ==
           "CODE: load = MAIN; ");
    assert(cfgDeclaresOutputFile("    LO: file = \"%O.lo\", start=$0280;", "%O.lo"));
    assert(!cfgDeclaresOutputFile("# file = \"%O.lo\" (comment only)", "%O.lo"));
    assert(cfgNameBeforeColon("  CODE: load = MAIN, type = rw;") == "CODE");
    assert(cfgNameBeforeColon("no colon here").empty());
    assert(cfgSegmentLoadName("CODE: load = MAIN, type = rw;", "CODE") == "MAIN");
    assert(cfgSegmentLoadName("DATA: load = MAIN;", "CODE").empty());

    // Embedded C build specs stay parseable (the packaged-build fallback path)
    for (const char* spec : { kBenchCSpecApple1c, kBenchCSpecGen2c,
                              kBenchCSpecTms9918c }) {
        const BenchCSpec s = benchCSpecParse(spec);
        assert(s.ok && !s.cfg.empty() && !s.cSources.empty());
    }
    std::printf("bench_cc65_smoke: part A (pure parsers) OK\n");
}

// The fixture: line numbers are load-bearing — part B asserts against them.
static const char kProgS[] =
    "        .setcpu \"6502\"\n"     // line 1 (no code)
    "; boot: print an A\n"           // line 2 (comment)
    "start:  lda #$41\n"             // line 3 -> $0300..0301
    "        jsr echo\n"             // line 4 -> $0302..0304
    "loop:   jmp loop\n"             // line 5 -> $0305..0307
    "; helper\n"                     // line 6 (comment)
    "echo:   sta $D012\n"            // line 7 -> $0308..030A
    "        rts\n";                 // line 8 -> $030B

static const char kCfg[] =
    "MEMORY { MAIN: start=$0300, size=$1000, file=%O; }\n"
    "SEGMENTS { CODE: load=MAIN, type=rw; }\n";

static bool hasLabel(const pom1::DbgLineInfo& d, uint16_t addr, const char* name)
{
    for (const auto& l : d.labels)
        if (l.first == addr && l.second == name)
            return true;
    return false;
}

static int partB()
{
    if (!haveCc65()) {
        std::fprintf(stderr, "SKIP: cc65 (ca65/ld65) not on PATH — "
                             "part B (real --dbgfile) not run\n");
        return kSkip;
    }
    char tmpl[] = "/tmp/pom1_dbg_XXXXXX";
    if (!mkdtemp(tmpl)) {
        std::fprintf(stderr, "SKIP: no temp dir\n");
        return kSkip;
    }
    const std::string dir(tmpl);
    const std::string srcS = dir + "/prog.s", cfg = dir + "/prog.cfg";
    const std::string obj = dir + "/prog.o", bin = dir + "/prog.bin";
    const std::string dbg = dir + "/prog.dbg";
    std::ofstream(srcS) << kProgS;
    std::ofstream(cfg) << kCfg;

    if (std::system(("ca65 -g -o " + obj + " " + srcS).c_str()) != 0) {
        std::fprintf(stderr, "FAIL: ca65 -g\n");
        return 1;
    }
    if (std::system(("ld65 -C " + cfg + " --dbgfile " + dbg + " -o " + bin +
                     " " + obj).c_str()) != 0) {
        std::fprintf(stderr, "FAIL: ld65 --dbgfile\n");
        return 1;
    }

    std::ifstream in(dbg);
    std::stringstream ss;
    ss << in.rdbuf();
    const pom1::DbgLineInfo d = pom1::parseDbgFile(ss.str(), srcS);
    if (!d.ok) {
        std::fprintf(stderr, "FAIL: parseDbgFile on real ld65 output: %s\n",
                     d.error.c_str());
        return 1;
    }

    // Address -> line, every instruction
    assert(d.lineForAddr(0x0300) == 3);
    assert(d.lineForAddr(0x0302) == 4);
    assert(d.lineForAddr(0x0304) == 4);   // middle byte of the jsr operand
    assert(d.lineForAddr(0x0305) == 5);
    assert(d.lineForAddr(0x0308) == 7);
    assert(d.lineForAddr(0x030B) == 8);

    // Line -> address, with comment-line snap-forward
    uint16_t addr = 0;
    int snapped = 0;
    assert(d.addrForLine(3, addr, snapped) && addr == 0x0300 && snapped == 3);
    assert(d.addrForLine(2, addr, snapped) && addr == 0x0300 && snapped == 3);
    assert(d.addrForLine(6, addr, snapped) && addr == 0x0308 && snapped == 7);
    assert(!d.addrForLine(9, addr, snapped));

    // Labels harvested from the real sym records
    assert(hasLabel(d, 0x0300, "start"));
    assert(hasLabel(d, 0x0305, "loop"));
    assert(hasLabel(d, 0x0308, "echo"));

    std::printf("bench_cc65_smoke: part B (real ca65 -g / ld65 --dbgfile) OK\n");
    return 0;
}

int main()
{
    partA();
    return partB();
}
