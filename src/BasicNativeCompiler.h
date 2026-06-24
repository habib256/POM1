// BasicNativeCompiler.h -- a REAL native-code Applesoft compiler for POM1.
//
// Unlike BasicCompiler (which only tokenizes a listing for the interpreter to
// run), this generates standalone 6502 assembly: native control flow, native
// variable storage at fixed addresses, expressions compiled once into straight-
// line code. The output binary runs with NO Applesoft interpreter -- it only
// needs the graphics card (GEN2 framebuffer / TMS9918 VDP). All the interpreter
// overhead disappears: token dispatch, per-iteration expression re-parsing,
// variable-name string search, GOTO line search.
//
// Progressive scope. PHASE 1 (this module): the INTEGER subset -- 16-bit signed
// variables and arithmetic (+ - * / , comparisons, AND/OR/NOT), FOR/NEXT,
// IF/THEN, GOTO, GOSUB/RETURN, END, REM, and the integer graphics statements
// HGR/HGR2, HCOLOR=, HPLOT (point and TO-chains). This is where a compiler wins
// 10-50x: control- and integer-bound code carries almost no interpreter tax once
// native. PHASE 2 (future): a standalone floating-point runtime (FADD/FMUL/.../
// SIN/SQR) so float programs like 3DHat compile with no ROM either.
//
// The generated program calls a tiny fixed runtime ABI (rt_hgr/rt_hcolor/rt_plot/
// rt_line/rt_mul/rt_div) implemented per card in runtime/basicrt_{gen2,tms}.s,
// which wrap the project's existing graphics asm libs (dev/lib/gen2, dev/lib/
// tms9918) -- those are leaf graphics routines, not the interpreter.
//
// compile() returns ca65 assembly text; assemble with ca65 + ld65 against the
// matching runtime + linker config to get a standalone .bin.

#ifndef POM1_BASIC_NATIVE_COMPILER_H
#define POM1_BASIC_NATIVE_COMPILER_H

#include <string>

namespace basicnative {

enum class Card { Gen2, Tms };

struct Result {
    bool        ok = false;
    std::string error;   // non-empty on failure (line-numbered)
    std::string asmText; // ca65 source for the program (links against basicrt_*)
    int         lineCount = 0;
    int         varCount  = 0;
};

// Compile an Applesoft (integer-subset) listing to ca65 assembly for `card`.
// Never throws; structural errors return ok=false with a line-numbered message.
Result compile(const std::string& source, Card card);

} // namespace basicnative

#endif // POM1_BASIC_NATIVE_COMPILER_H
