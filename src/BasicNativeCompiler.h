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
#include <vector>

namespace basicnative {

enum class Card { Gen2, Tms };

// Numeric precision the compiler targets. Auto picks the smallest sufficient:
// integer (16-bit) unless the program needs fractions (a decimal literal or a
// `/` division), in which case binary32 float -- so a program that doesn't use
// floats never links the (large) float runtime.
enum class FpMode { Auto, Int, Float };

struct Result {
    bool        ok = false;
    std::string error;     // non-empty on failure (Applesoft-line-numbered)
    std::string asmText;   // ca65 source for the program (links against basicrt_*)
    int         lineCount = 0;
    int         varCount  = 0;
    bool        usesFloat = false;                 // chosen precision (float vs int)
    std::vector<std::string> runtimeFeatures;      // RT_* the program needs (for the
                                                   // build to assemble only those)
};

// Compile an Applesoft listing to ca65 assembly for `card`. `mode` selects the
// numeric precision (Auto by default). The generated code imports ONLY the runtime
// symbols it actually uses, and `Result.runtimeFeatures` lists them so the build
// links a minimal runtime. Never throws; structural errors return ok=false with a
// message naming the offending Applesoft line.
Result compile(const std::string& source, Card card, FpMode mode = FpMode::Auto);

} // namespace basicnative

#endif // POM1_BASIC_NATIVE_COMPILER_H
