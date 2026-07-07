// LogoProgramLoader.h -- APPLE-1 LOGO V2.6 "program loader" for the POM1 Bench.
//
// Turns a LOGO listing (turtle commands + TO/END procedures) into a set of RAM
// writes that populate the resident interpreter's procedure table directly, plus
// a single ENTRY line to hand to the REPL -- so a LOGO program is INJECTED WITHOUT
// per-line keyboard typing, the LOGO analogue of the Applesoft tokeniser
// (BasicTokeniserApplesoft).
//
// Why not just type the listing? The LOGO REPL polls the keyboard for the ESC/
// Ctrl-G break inside REPEAT, which eats queued type-ahead -- so a pasted multi-
// line program drops every line after the first REPEAT. Poking the proc table
// sidesteps that entirely: procedure BODIES never travel through the keyboard,
// and only ONE short line (the entry call) is ever queued.
//
// The in-memory format is NOT tokenised. A LOGO procedure is stored as its raw
// ASCII source: the interpreter re-parses each body line at run time (see
// TMS_Logo_16k.asm, `cmd_to` / `proc_collect_line`). One PROC_SLOT (244 bytes):
//
//     off  0  : 6-byte name        (uppercase A-Z/0-9, SPACE-padded to NAME_LEN)
//     off  6  : n_params           (0..2)
//     off  7  : param 0 name       (6 bytes, space-padded)
//     off 13  : param 1 name       (6 bytes, space-padded)
//     off 19  : body_len           (0..224)
//     off 20  : body               (raw CR-separated source lines, <= 224 bytes)
//
// `n_procs` (a single byte) is the live procedure count the interpreter iterates.
// The proc_table base and n_procs address are fixed by the LOGO RAM linker cfg
// (PROC + LINEBUF segments) and are STABLE across CODE placement -- both cartridge
// (Codetank_BASIC_LOGO) and run-in-place builds share PROC=$E000 / LBUF=$0200 (TMS) or
// $B000 / $0280 (GEN2). Pinned against the shipped ROMs by bench_logo_inject_smoke.
//
// Pure: depends on <string>/<vector>/<cstdint> only, so it links into the bench
// (desktop + WASM), the CLI, and the headless tests alike.

#ifndef POM1_LOGO_PROGRAM_LOADER_H
#define POM1_LOGO_PROGRAM_LOADER_H

#include <cstdint>
#include <string>
#include <vector>

namespace logo {

// Interpreter equates -- mirror TMS_Logo_16k.asm. Pinned by bench_logo_inject_smoke.
constexpr int kMaxProcs      = 10;   // MAX_PROCS
constexpr int kProcSlot      = 244;  // PROC_SLOT bytes per procedure
constexpr int kNameLen       = 6;    // NAME_LEN identifier field width
constexpr int kMaxParams     = 2;    // MAX_PARAMS named parameters per proc
constexpr int kProcNparamsOff = 6;   // PROC_NPARAMS_OFF
constexpr int kProcParamsOff  = 7;   // PROC_PARAMS_OFF (2 x NAME_LEN)
constexpr int kProcBodyLenOff = 19;  // PROC_BODYLEN_OFF
constexpr int kProcBodyOff    = 20;  // PROC_BODY_OFF (first body char)
constexpr int kProcBodyMax    = 224; // PROC_BODY_MAX
constexpr int kLineMax        = 60;  // LINE_MAX -- REPL / body line-buffer width

// Per-interpreter RAM layout. `procTable` + `nProcs` are the only addresses the
// injector pokes; `coldEntry` is where the WOZ Monitor cold-starts the ROM.
struct Target {
    const char* name      = "";
    uint16_t    coldEntry = 0;  // cold-start address (TMS 4000R / GEN2 6000R)
    uint16_t    procTable = 0;  // proc_table base (TMS $E431 / GEN2 $B431)
    uint16_t    nProcs    = 0;  // n_procs counter (TMS $0260 / GEN2 $02E3)
};

// LOGO TMS9918 (Codetank_BASIC_LOGO.rom lower bank @ $4000, cold start 4000R).
Target targetTms();
// LOGO GEN2 HGR (roms/logo-gen2.rom @ $6000, cold start 6000R).
Target targetGen2();

// One RAM write the injector applies before resuming the REPL.
struct Write { uint16_t addr = 0; uint8_t value = 0; };

struct Result {
    bool               ok = false;
    std::string        error;    // non-empty on failure (nothing should be poked)
    std::vector<Write> writes;   // proc_table slot bytes + the n_procs byte
    std::string        entry;    // line to feed the REPL (empty => nothing runs)
    int                procCount = 0;      // user + synthesized procedures poked
    int                immediateCount = 0; // top-level immediate command lines
    std::string        warning;  // non-fatal note surfaced to the console
};

// Compile `source` (a full LOGO listing) into memory writes + an entry line for
// `tgt`. Never throws; on a structural problem returns ok=false with `error` and
// no writes. On success `writes` is ready for EmulationController::writeMemoryBatch
// and `entry` (if non-empty) for queueKey() one char at a time.
Result compile(const std::string& source, const Target& tgt);

} // namespace logo

#endif // POM1_LOGO_PROGRAM_LOADER_H
