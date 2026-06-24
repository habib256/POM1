// BasicCompiler.h -- Applesoft Lite "BASIC compiler" for POM1.
//
// Turns an Applesoft listing (.apf / .bas, the GEN2 or TMS9918 graphics dialect)
// into a ready-to-run 6502 MEMORY IMAGE, so a program can be COMPILED AHEAD OF
// TIME and LOADED + RUN directly -- with no slow keyboard "injection" of the
// listing into the live interpreter.
//
// How it works (threaded-code model, not a native code generator):
//   1. TOKENIZE the listing into Applesoft's on-disk tokenized layout, byte-for-
//      byte the same bytes the interpreter's own PARSE routine would produce when
//      you type the line (same reserved-word table, same prefix-greedy match,
//      same REM/DATA/string literal handling). See applesoft-tms9918.s / the
//      restored applesoft-gen2.s TOKEN_NAME_TABLE -- the layout is identical for
//      both cards, so one tokenizer serves both.
//   2. LAY OUT the tokenized program at $0801 (the interpreter's TXTTAB), with a
//      $00 guard at $0800 and correct absolute forward links per line, terminated
//      by a $00 $00 end-of-program link -- exactly the image the ROM builds in RAM
//      when a listing is entered.
//   3. Emit a tiny LAUNCHER stub that sets VARTAB just past the program and enters
//      the interpreter's run loop (JSR SETPTRS ; JMP NEWSTT). The interpreter ROM
//      itself supplies every runtime: floating point, SIN/COS/SQR/INT, FOR/NEXT,
//      GOSUB, and the HGR/HPLOT/HCOLOR graphics primitives for that card.
//
// The interpreter ROM must already be resident and cold-started (its zero-page
// CHRGET, HIMEM/FRETOP, output vector and FP scratch set up) BEFORE the launcher
// runs -- which is the normal state right after `4000R` (TMS CodeTank cart) or
// `9800R` (GEN2 card), i.e. the `]` prompt. Booting the interpreter is not
// "injection"; only the per-line typing of the listing is, and that is what this
// replaces.
//
// Pure: depends on <string>/<vector>/<cstdint> only, so it links into the bench
// (desktop + WASM), the CLI, and the headless tests alike.

#ifndef POM1_BASIC_COMPILER_H
#define POM1_BASIC_COMPILER_H

#include <cstdint>
#include <string>
#include <vector>

namespace basic {

// One contiguous run of bytes destined for `addr`. A compiled program is two
// zones: the program image at $0800 and the launcher stub at kLauncherAddr.
struct Zone {
    uint16_t addr = 0;
    std::vector<uint8_t> bytes;
};

// Per-interpreter constants the launcher needs. The two ROM entry points are the
// only card-specific addresses; everything else (zero-page TXTTAB/VARTAB, the
// $0801 program origin) is shared Applesoft layout. Addresses were extracted from
// the assembled interpreters (ld65 -Ln) and pinned against the shipped ROMs.
struct Target {
    const char* name    = "";
    uint16_t    setptrs = 0;  // SETPTRS: TXTPTR=TXTTAB-1, clear vars, reset stack
    uint16_t    newstt  = 0;  // NEWSTT: main statement loop entry (runs from TXTTAB)
    uint16_t    himem   = 0;  // MEMSIZ the cold ROM pins (informational / sanity)
};

// GEN2 Applesoft (roms/applesoft-gen2.rom @ $9800, cold start 9800R).
Target targetGen2();
// Applesoft TMS9918 (CodeTank cart upper bank @ $4000, cold start 4000R).
Target targetTms();

struct Result {
    bool                 ok = false;
    std::string          error;        // non-empty on failure
    std::vector<Zone>    zones;         // program image + launcher
    uint16_t             entry = 0;     // launcher address -> jump here to run
    uint16_t             progEnd = 0;   // VARTAB the launcher installs
    int                  lineCount = 0; // tokenized BASIC lines
    // Wozmon-style hex dump (loadable via Memory::loadHexDump), run address = entry.
    std::string          hex;
};

// Compile `source` (a full Applesoft listing) for `tgt`. Never throws; on a
// structural problem (e.g. a bad line number) returns ok=false with `error`.
Result compile(const std::string& source, const Target& tgt);

// Address the launcher stub is assembled at (page 2 scratch, free at the `]`
// prompt and overwritten only by variables once the program is already running).
constexpr uint16_t kLauncherAddr = 0x0280;
// Program text origin -- the interpreter's TXTTAB (a $00 guard sits at $0800).
constexpr uint16_t kProgramOrigin = 0x0801;

} // namespace basic

#endif // POM1_BASIC_COMPILER_H
