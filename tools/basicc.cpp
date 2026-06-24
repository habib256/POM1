// basicc -- standalone Applesoft Lite "BASIC compiler" for POM1.
//
// Compiles an Applesoft listing (.apf / .bas, the GEN2 or TMS9918 graphics
// dialect) AHEAD OF TIME into a 6502 memory image -- a tokenized program at $0801
// plus a launcher stub -- so the program can be LOADED + RUN directly instead of
// having its listing typed into the live interpreter one keystroke at a time.
//
// Build (host tool, no emulator deps):
//     g++ -std=c++17 -I src tools/basicc.cpp src/BasicCompiler.cpp -o basicc
//
// Usage:
//     basicc --target {gen2|tms} INPUT.apf [-o OUTPUT.hex]
//
// The output is a Wozmon-style hex dump (Memory::loadHexDump format) whose run
// address is the launcher. To run it in POM1, boot the matching interpreter to
// its `]` prompt first (so its zero page / vectors are set up), then load the hex
// and jump to the launcher:
//   * TMS9918:  preset "Apple-1 TMS9918 Development Bench", cold start 4000R
//   * GEN2 HGR: preset "GEN2 HGR Color",                    cold start 9800R
// See doc/BASIC_COMPILER.md for the full pipeline and the in-app DevBench hook.

#include "BasicCompiler.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

static std::string readAll(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv)
{
    std::string target, input, output;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--target" && i + 1 < argc)      target = argv[++i];
        else if (a == "-o" && i + 1 < argc)       output = argv[++i];
        else if (!a.empty() && a[0] != '-')       input = a;
        else { std::fprintf(stderr, "unknown argument: %s\n", a.c_str()); return 2; }
    }
    if (input.empty() || (target != "gen2" && target != "tms")) {
        std::fprintf(stderr,
            "usage: basicc --target {gen2|tms} INPUT.apf [-o OUTPUT.hex]\n");
        return 2;
    }

    const std::string src = readAll(input);
    if (src.empty()) { std::fprintf(stderr, "cannot read %s\n", input.c_str()); return 1; }

    const basic::Target tgt = (target == "gen2") ? basic::targetGen2() : basic::targetTms();
    const basic::Result r = basic::compile(src, tgt);
    if (!r.ok) { std::fprintf(stderr, "compile error: %s\n", r.error.c_str()); return 1; }

    if (output.empty()) {
        std::fputs(r.hex.c_str(), stdout);
    } else {
        std::ofstream o(output, std::ios::binary);
        if (!o) { std::fprintf(stderr, "cannot write %s\n", output.c_str()); return 1; }
        o << r.hex;
    }

    std::fprintf(stderr,
        "compiled %s for %s: %d lines, run @ $%04X, VARTAB $%04X (boot %s first)\n",
        input.c_str(), tgt.name, r.lineCount, r.entry, r.progEnd,
        (target == "gen2") ? "9800R" : "4000R");
    return 0;
}
