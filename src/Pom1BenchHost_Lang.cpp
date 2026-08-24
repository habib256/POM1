// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Pom1BenchHost_Lang.cpp — the DevBench's high-level language paths:
// injectBasic (mode 4, both the Integer and the Applesoft interpreter) and
// injectLogo (mode 6). Split out of Pom1BenchHost.cpp, which had reached 3957
// lines. Pure code motion — no behaviour changed.
//
// The split line is INTERPRETER INJECTION vs TOOLCHAIN BUILD, not "language
// stuff": compileBasicNative (mode 5) is a BASIC path too, but it shells out
// to ca65/ld65 and shares the cc65 plumbing (TempFileSweeper, humanizeCc65,
// parseErrorMarkers, kCc65InstallHint) with build(), so it stays next to that
// pipeline. What lives here needs no toolchain at all — it tokenises in
// process and pokes RAM.
//
// Machine-relax note (unchanged, and easy to break): injectBasic RELAXES the
// machine before loading an interpreter, injectLogo deliberately does NOT —
// the TMS CodeTank machine keeps its natural config there. See the comments
// at each method.

#include "Pom1BenchHost.h"
#include "Pom1BenchTargets.h"
#include "LogoProgramLoader.h"        // logo:: — LOGO listing loader
#include "BasicTokeniserInteger.h"     // ibasic::compile — Integer BASIC ($E000)
#include "BasicTokeniserApplesoft.h"   // basic::compile — Applesoft Lite
#include "MainWindow_ImGui.h"
#include "MainWindow_Internal.h"
#include "POM1Build.h"
#include "EmulationController.h"
#include "Logger.h"
#include "ProcessUtil.h"
#include "bench/CodeBench.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace md = pom1::mainwindow::detail;
using namespace pom1::benchhost;   // kP1Targets, the sketches, the path helpers

bench::BuildResult Pom1BenchHost::injectBasic(int target, const std::string& src, bool run)
{
    bench::BuildResult r; r.showConsole = true;
    const P1T& t = kP1Targets[p1(target)];
    auto* emu = mw_ ? mw_->emulation.get() : nullptr;
    if (!emu) { r.status = "no emulator"; r.ok = false; return r; }

    // The machine is being reprogrammed (ROM load + hard reset): the debug
    // line table described a program that is about to disappear, and its line
    // breakpoint dies with the reset. Wiped HERE and not only in build(),
    // because selectTargetExplicit's interpreter prep calls this directly —
    // possibly on the SAME preset (the Applesoft-GEN2 target shares the GEN2
    // bench's), so neither build()'s wipe nor applyTargetPreset's fires.
    dbg_.invalidate();

    // BASIC variant by target index (set in kP1Targets):
    //   7 Integer  8 Applesoft+microSD  9 Applesoft GEN2  10 Applesoft (Apple-1/CFFA1)
    //   11 Applesoft TMS9918.  The cold-start command is t.cfg (E000R/6000R/4000R).
    const int idx = p1(target);
    const bool tms = (idx == 11);
    const char* coldStart = (t.cfg && t.cfg[0]) ? t.cfg : "E000R";
    const char* interp =
        idx == 8  ? "Applesoft Lite (microSD)" :
        idx == 9  ? "Applesoft GEN2" :
        idx == 10 ? "Applesoft Lite (Apple-1)" :
        idx == 11 ? "Applesoft TMS9918" : "Integer BASIC";

    // Cold vs warm bring-up. COLD (default) reinitialises the interpreter from
    // scratch (E000R/6000R/…) — a hard reset + ROM reload, wiping any program
    // typed at the REPL. WARM (the CodeBench "Warm" toggle) re-enters the resident
    // interpreter without the reset (Integer $E2B3, Applesoft cold+3 = 6003/9803/…),
    // so a program already in memory survives. Warm assumes the interpreter was
    // already cold-started once (its zero page + ROM image are resident); the actual
    // warm-vs-cold decision is taken below, AFTER onTargetSelected settles the preset
    // (a preset switch hard-resets the machine and clears benchBasicResidentIdx_, so
    // we never warm-enter a just-wiped interpreter). The bring-up entry walks the ROM
    // to its prompt before we poke/launch a compiled image.
    const uint16_t coldEntry =
        idx == 8  ? 0x6000 : idx == 9  ? 0x9800 :
        idx == 10 ? 0xE000 : idx == 11 ? 0x4000 : ibasic::kColdStart;   // idx 7
    const uint16_t warmEntry =
        idx == 7 ? ibasic::kWarmStart : static_cast<uint16_t>(coldEntry + 3);

    namespace fs = std::filesystem;
    std::error_code ec;
    auto findRom = [&](std::initializer_list<const char*> cands) -> std::string {
        for (const char* c : cands) if (fs::exists(c, ec)) return c;
        return {};
    };

    // 1) For the TMS9918 Applesoft, the interpreter lives in the UPPER bank of the
    //    stabilised language cartridge (roms/codetank/Codetank_BASIC_LOGO.rom) —
    //    CODETANKDEV is now a pure two-slot flash cart. Resolve + validate the
    //    image UP FRONT so a missing ROM aborts with the machine completely
    //    unchanged (no preset switch, no half-plugged card, no default ARCADE
    //    cartridge).
    std::string tmsCartPath;
    if (tms) {
        tmsCartPath = codeTankBasicLogoRomReadPath();
        if (tmsCartPath.empty()) {
            r.console = std::string("[bench] ") + interp +
                ": Codetank_BASIC_LOGO.rom not found — build it with "
                "tools/build_codetank_rom.py --rom basiclogo\n"
                "[bench] aborting injection; machine left unchanged.\n";
            r.status = std::string(interp) + ": ROM not found";
            r.ok = false;
            return r;
        }
    }

    // 2) Plug the interpreter's machine. For TMS, pre-load the cartridge from memory
    //    BEFORE draining the deferred plugs so enabling CodeTank doesn't auto-probe
    //    the default GAME1 image (hasRom() is already true).
    onTargetSelected(target);
    if (tms) {
        std::string err;
        if (!emu->loadCodeTankRom(tmsCartPath, err)) {
            r.console = std::string("[bench] ") + interp + ": Codetank_BASIC_LOGO.rom load FAILED — " + err + "\n";
            r.status = std::string(interp) + ": ROM load failed";
            r.ok = false;
            return r;
        }
        // When the switch to preset 1 was applied fresh (e.g. the Mode selector, or a
        // Run from another preset), applyMachineConfig queued the preset's DEFAULT
        // CodeTank ROM (Codetank_ARCADE.rom) for the still-pending plug. Clear that
        // path now so finalizePendingCardPlugs() does NOT reload ARCADE over the
        // Applesoft cartridge we just inserted.
        mw_->pendingCodeTankRomPath.clear();
    }
    mw_->finalizePendingCardPlugs();

    // Warm vs cold, decided now that the preset is settled: honour the toggle only
    // when this interpreter is still resident (onTargetSelected clears the residency
    // flag if it had to switch presets, i.e. hard-reset the machine). Otherwise cold.
    const bool warm = benchWarmStart_ && (benchBasicResidentIdx_ == idx);
    const uint16_t bringUp = warm ? warmEntry : coldEntry;

    // 2b) Give BASIC enough backed RAM for its interpreter + workspace. Two cases:
    //
    //   * TMS9918 + CodeTank (idx 11): a REAL, buildable machine — 16 KB low RAM
    //     ($0000-$3FFF) under the CodeTank ROM cart ($4000-$7FFF). HIMEM is pinned
    //     at $4000, so BASIC ($0801-$3FFF) is wholly in range with OOR strict left
    //     ON (no non-physical 64 KB view). The TMS VRAM is external ($CC00/$CC01),
    //     so no framebuffer RAM is reserved; the cart window is served by the
    //     PeripheralBus ahead of the OOR check, so strict mode never starves it.
    //   * microSD ($6000 ROM) / CFFA1 Apple-1 ($E000-$FEFF needs $F000+ RAM): the
    //     interpreter sits in an OOR window on the strict preset, so relax to a flat
    //     64 KB view. GEN2 (preset 2, 48 KB, interpreter high at $9800) needs none.
    //
    // The original RAM/OOR is saved here and restored on abort / when the next
    // non-BASIC target runs (build() calls restoreRelaxedMachine), so it never leaks.
    if (idx == 8 || idx == 10 || idx == 11) {
        if (!injectRelaxed_) {
            injectSavedRamKB_     = mw_->presetRamKB;
            injectSavedOorStrict_ = mw_->oorStrictModeEnabled;
        }
        injectRelaxed_       = true;
        injectRelaxedPreset_ = mw_->activePresetIndex;
        if (idx == 11) {
            emu->setOutOfRangeStrictMode(true);
            emu->setPresetRamKB(16);
            mw_->oorStrictModeEnabled = true;
            mw_->presetRamKB = 16;
        } else {
            emu->setOutOfRangeStrictMode(false);
            emu->setPresetRamKB(64);
            mw_->oorStrictModeEnabled = false;
            mw_->presetRamKB = 64;
        }
    }

    // 3) Place the interpreter, then hard-reset so a clean WOZ Monitor processes the
    //    cold-start. RAM-resident ROMs (Integer/microSD/CFFA1/GEN2) load AFTER the
    //    reset (which zero-fills RAM); the TMS9918 cartridge is already flashed (step
    //    1/2) — just jumper + enable, THEN reset so 4000R sees it.
    //    WARM start skips the whole reset+reload: the interpreter (and any program
    //    typed at the REPL) is already resident, and warm re-entry (bringUp) walks
    //    it back to its prompt without wiping RAM.
    std::string romErr;
    bool romOk = true;
    if (!warm) {
        if (tms) {
            mw_->codeTankJumper = CodeTank::Jumper::Upper16;   // Applesoft lives in the upper bank
            emu->setCodeTankJumper(mw_->codeTankJumper);
            if (!mw_->tms9918Enabled)  { mw_->tms9918Enabled = true; mw_->showTMS9918 = true; emu->setTMS9918Enabled(true); }
            if (!mw_->codeTankEnabled) { mw_->codeTankEnabled = true; emu->setCodeTankEnabled(true); }
            emu->hardReset(/*animateBoot=*/false);
        } else {
            emu->hardReset(/*animateBoot=*/false);
            if (idx == 9) {
                const std::string rom = findRom({"roms/applesoft-gen2.rom",
                                                 "../roms/applesoft-gen2.rom",
                                                 "../../roms/applesoft-gen2.rom"});
                if (rom.empty()) { romErr = "roms/applesoft-gen2.rom not found"; romOk = false; }
                else romOk = emu->loadInterpreterRom(rom, 0x9800, romErr);
            } else if (idx == 8) {
                romOk = emu->reloadApplesoftLiteSDCard(romErr);
            } else if (idx == 10) {
                romOk = emu->reloadApplesoftLiteCFFA1(romErr);
            } else {
                romOk = emu->reloadBasic(romErr);   // Integer BASIC
            }
        }
    }
    if (!romOk) {
        restoreRelaxedMachine();              // undo the OOR/RAM relax on abort
        emu->copySnapshot(mw_->uiSnapshot);
        r.console = std::string("[bench] ") + interp + ": ROM (re)load FAILED — "
                    + (romErr.empty() ? "unknown error" : romErr) + "\n"
                    "[bench] cold-start " + coldStart + " would jump into unmapped "
                    "memory and drop back to the WOZ Monitor; aborting injection.\n";
        r.status = std::string(interp) + ": ROM load failed";
        r.ok = false;
        return r;
    }
    // The interpreter for this idx is now established (freshly cold-reloaded, or warm
    // and already resident) → a subsequent Warm re-entry into it is safe.
    benchBasicResidentIdx_ = idx;

    // Prep-only call: selectTargetExplicit passes an empty listing (run=false) just to
    // ready the interpreter prompt when the user picks a BASIC target. Compiling an
    // empty listing would fail with "no BASIC lines to compile" and surface as a scary
    // selection error, so bring the ROM up to its prompt and report success instead.
    // Warm re-entry keeps whatever program is already resident.
    if (src.empty() && !run) {
        constexpr uint64_t kColdStartCycles = 12'000'000;
        emu->runFromSync(bringUp, kColdStartCycles);
        emu->copySnapshot(mw_->uiSnapshot);
        r.status = std::string(interp) + (warm ? " — ready (warm, program kept)" : " — ready");
        r.ok = true;
        return r;
    }

    // 2b) Integer BASIC ($E000, idx 7) — tokenise host-side, then load + run via the
    //     ROM's RUN handler. Unlike Applesoft there is no $0801 launcher: the program
    //     lives HIGH (down from HIMEM $1000), pp ($CA) points at it, and execution
    //     enters at $EFEC (clr + run_warm). The interpreter ROM (reloaded above) is
    //     cold-started first so its zero page (lomem/himem/pp) is set up.
    if (idx == 7) {
        // Lower LOMEM from the ROM's $0800 cold default to $0300 — the program is
        // stored DOWN from HIMEM ($1000) and variables UP from LOMEM, so this widens
        // the BASIC area to $0300-$1000 (~3.25 KB, "a bit under 4 KB"). It matches how
        // these programs were actually saved (their .apl images set LOMEM=$0300,
        // HIMEM=$1000) and fits the big ones (mini-startrek 3024 B → pp $0430 > $0300);
        // the $0800 default leaves only 2 KB and a >2 KB listing MEM-ERRORs. $0300 sits
        // just above the WOZ input buffer ($0200); no RAM relax — all within the 8 KB.
        constexpr uint16_t kIntLomem = 0x0300;
        std::string norm;  // ibasic::compile splits on '\n' — fold CRLF/CR to LF
        norm.reserve(src.size());
        for (size_t i = 0; i < src.size(); ++i) {
            if (src[i] == '\r') { norm += '\n'; if (i + 1 < src.size() && src[i + 1] == '\n') ++i; }
            else norm += src[i];
        }
        ibasic::Result prog = ibasic::compile(norm);   // HIMEM $1000 (matches the .apl images)
        if (!prog.ok) {
            emu->copySnapshot(mw_->uiSnapshot);
            r.console = std::string("[bench] Integer BASIC: tokenise FAILED — ") + prog.error + "\n";
            r.status  = "Integer BASIC: tokenise error";
            r.ok = false;
            return r;
        }
        char buf[192];
        // Both Verify and Run LOAD the tokenised image into the live interpreter;
        // they differ only in the final entry. Bring Integer BASIC to its `>` prompt
        // (cold inits lomem=$0800, himem=$1000, zero page; warm re-enters keeping the
        // resident state), then poke the program image at pp and set pp ($CA/$CB).
        constexpr uint64_t kColdCycles = 12'000'000;
        emu->runFromSync(bringUp, kColdCycles);
        // Lower LOMEM ($4A/$4B) below the cold default so variables + program fit
        // (HIMEM stays at the cold $1000). $EFEC's CLR re-reads LOMEM for the var base.
        emu->writeMemory(0x004A, static_cast<uint8_t>(kIntLomem & 0xFF));
        emu->writeMemory(0x004B, static_cast<uint8_t>((kIntLomem >> 8) & 0xFF));
        for (size_t i = 0; i < prog.image.size(); ++i)
            emu->writeMemory(static_cast<uint16_t>(prog.pp + i), prog.image[i]);
        emu->writeMemory(ibasic::kPpZp,     static_cast<uint8_t>(prog.pp & 0xFF));
        emu->writeMemory(ibasic::kPpZp + 1, static_cast<uint8_t>((prog.pp >> 8) & 0xFF));
        // Run → the ROM's RUN handler ($EFEC = clr + run_warm). Verify → warm start
        // ($E2B3), which drops to the `>` prompt with the program intact + LISTable.
        emu->runFromAsync(run ? 0xEFEC : ibasic::kWarmStart);
        emu->copySnapshot(mw_->uiSnapshot);
        std::snprintf(buf, sizeof(buf),
            "[bench] Integer BASIC: tokenised %d lines → loaded %zu B @ $%04X, %s "
            "(tokeniser — no keyboard injection)\n", prog.lineCount, prog.image.size(),
            prog.pp, run ? "running" : "ready to LIST/RUN");
        r.console = buf;
        r.status  = run ? "Integer BASIC: running (tokenised)"
                        : "Integer BASIC: loaded — ready to LIST";
        r.ok = true;
        return r;
    }

    // 2c) Tokenizer path (every Applesoft target) — COMPILE the listing instead of
    //     injecting it. BasicTokeniserApplesoft tokenizes the program ahead of time into a
    //     $0801 memory image + a $0280 launcher, byte-for-byte what the interpreter's
    //     PARSE would build; the resident ROM (loaded above) supplies the runtime.
    //     We cold-start the interpreter so its zero page is set up, then load the
    //     image and jump to the launcher — no per-character keyboard typing, no
    //     127-char line cap, instant, and identical on WASM (the compiler is pure
    //     C++). idx: 8 microSD ($6000), 9 GEN2 ($9800), 10 CFFA1 ($E000), 11 TMS
    //     ($4000). Integer BASIC (idx 7) has a different token set and is handled by
    //     the ibasic::compile path above (also compiled + loaded, never keyboard-typed).
    if (idx == 8 || idx == 9 || idx == 10 || idx == 11) {
        basic::Target tgt;
        switch (idx) {
            case 8:  tgt = basic::targetMicrosd(); break;   // $6000 (bringUp from top)
            case 9:  tgt = basic::targetGen2();    break;   // $9800
            case 10: tgt = basic::targetCffa1();   break;   // $E000
            default: tgt = basic::targetTms();     break;   // idx 11, $4000
        }
        // Cold-start to the `]` prompt is well under 1M cycles; this cap is a safe
        // ceiling — extra cycles just spin harmlessly in the interpreter's GETLN.
        constexpr uint64_t  kColdStartCycles = 12'000'000;

        // basic::compile splits on '\n' (and skips a '\r' inside a line, so CRLF is
        // fine) but treats a CR-only file as a single line. The keyboard-injection
        // path normalised every newline flavour, so do the same here: fold CRLF and
        // lone CR to LF before tokenizing.
        std::string norm; norm.reserve(src.size());
        for (size_t i = 0; i < src.size(); ++i) {
            if (src[i] == '\r') { norm += '\n'; if (i + 1 < src.size() && src[i + 1] == '\n') ++i; }
            else norm += src[i];
        }
        basic::Result prog = basic::compile(norm, tgt);
        if (!prog.ok) {
            restoreRelaxedMachine();
            emu->copySnapshot(mw_->uiSnapshot);
            r.console = std::string("[bench] ") + interp + ": BASIC tokenise FAILED — "
                        + prog.error + "\n";
            r.status  = std::string(interp) + ": tokenise error";
            r.ok = false;
            return r;
        }

        char buf[192];

        // Bring the interpreter ROM to its `]` prompt (cold inits CHRGET, HIMEM/
        // FRETOP, output vector, FP scratch; warm re-enters keeping resident state)
        // before we load the compiled image.
        emu->runFromSync(bringUp, kColdStartCycles);

        // Verify = LOAD ready to LIST (no run). Same program image, but the launcher's
        // trailing `JMP NEWSTT` (run) is rewritten to `JMP <warm>` so it drops to the
        // `]` prompt after JSR SETPTRS installs the pointers — the program is present
        // and LISTable, nothing executes. Poke the zones directly (no temp file) and
        // enter the launcher live.
        if (!run) {
            for (const basic::Zone& z : prog.zones) {
                std::vector<uint8_t> bytes = z.bytes;
                if (z.addr == basic::kLauncherAddr && bytes.size() >= 3) {
                    bytes[bytes.size() - 3] = 0x4C;                                  // JMP
                    bytes[bytes.size() - 2] = static_cast<uint8_t>(warmEntry & 0xFF);
                    bytes[bytes.size() - 1] = static_cast<uint8_t>((warmEntry >> 8) & 0xFF);
                }
                for (size_t i = 0; i < bytes.size(); ++i)
                    emu->writeMemory(static_cast<uint16_t>(z.addr + i), bytes[i]);
            }
            emu->runFromAsync(basic::kLauncherAddr);
            emu->copySnapshot(mw_->uiSnapshot);
            std::snprintf(buf, sizeof(buf),
                "[bench] %s: tokenised %d lines → loaded $0801-$%04X, ready to LIST/RUN\n",
                interp, prog.lineCount, prog.progEnd ? prog.progEnd - 1 : 0x0800);
            r.console = buf;
            std::snprintf(buf, sizeof(buf), "%s: loaded — ready to LIST", interp);
            r.status  = buf; r.ok = true;
            return r;
        }

        // Run: load the compiled image + run-launcher and jump to it. loadHexDump
        // preserves the just-set-up zero page (hardReset clears only the stack, never
        // RAM) and starts the async CPU at the launcher's run address (JMP NEWSTT).
        std::string err; uint16_t loadedEntry = 0; int loaded = 0;
        const fs::path tmp = benchScratchDir(ec) / "pom1_basic_tokenized.txt";
        { std::ofstream o(tmp, std::ios::binary);
          o.write(prog.hex.data(), static_cast<std::streamsize>(prog.hex.size())); }
        const bool ok = emu->loadHexDump(tmp.string(), loadedEntry, err, &loaded);
        fs::remove(tmp, ec);
        if (!ok) {
            restoreRelaxedMachine();
            emu->copySnapshot(mw_->uiSnapshot);
            r.console = std::string("[bench] ") + interp + ": tokenised image load FAILED — "
                        + err + "\n";
            r.status  = std::string(interp) + ": load failed";
            r.ok = false;
            return r;
        }
        emu->copySnapshot(mw_->uiSnapshot);
        std::snprintf(buf, sizeof(buf),
            "[bench] %s: tokenised %d lines → loaded %d B, launched @ $%04X\n",
            interp, prog.lineCount, loaded, prog.entry);
        r.console = buf;
        std::snprintf(buf, sizeof(buf), "%s: running (tokenised)", interp);
        r.status  = buf; r.ok = true;
        return r;
    }

    // Every BASIC target (mode 4 = indices 7-11) is fully handled by the Integer
    // (idx 7) and Applesoft tokenizer (idx 8-11) paths above, each of which returns.
    // Reaching here means an unexpected target index — fail defensively rather than
    // fall through. (The old per-character keyboard-injection fallback and its
    // pollBuild RUN handler were removed once tokenisation replaced it for all cards.)
    emu->copySnapshot(mw_->uiSnapshot);
    r.status = std::string(interp) + ": unsupported BASIC target";
    r.ok = false;
    return r;
}

// LOGO deploy (mode 6). Unlike BASIC there is no tokenised memory image: an APPLE-1
// LOGO procedure is stored as its RAW ASCII source in the interpreter's proc_table
// (the interpreter re-parses each body line at run time). LogoProgramLoader turns
// the listing into proc_table writes + ONE entry line; we cold-start the resident
// interpreter, poke the table while the CPU is parked, queue the entry line, and
// resume the REPL live (startCpu). Feeding only one short line means the REPEAT
// break-poll can never eat queued type-ahead (procedure bodies never touch the
// keyboard). Pure C++ loader → identical on desktop + WASM.
//
// RAM: NO relax (contrast injectBasic). The TMS CodeTank machine keeps its natural
// 8 KB Parmigiani DUAL-BANK ($0000-$0FFF + $E000-$EFFF) because PROCBSS lives at
// $E431 in the HIGH bank — forcing a linear 16 KB view (as the Applesoft path does)
// would unback $E000 and silently drop every proc_table write. GEN2 (preset 2,
// 48 KB) already covers $6000 (code) + $B431 (PROCBSS).
bench::BuildResult Pom1BenchHost::injectLogo(int target, const std::string& src, bool run)
{
    bench::BuildResult r; r.showConsole = true;
    auto* emu = mw_ ? mw_->emulation.get() : nullptr;
    if (!emu) { r.status = "no emulator"; r.ok = false; return r; }

    // Same wipe as injectBasic, same reason: the interpreter injection
    // reprograms the machine, and selectTargetExplicit reaches here directly
    // (no build(), possibly no preset change).
    dbg_.invalidate();

    const int idx = p1(target);
    const bool tms = (idx == 14);   // 14 = LOGO TMS9918 (CodeTank), 15 = LOGO GEN2 HGR
    const logo::Target ltgt = tms ? logo::targetTms() : logo::targetGen2();
    const std::string interp = tms ? "APPLE-1 LOGO (TMS9918)" : "APPLE-1 LOGO (GEN2 HGR)";
    const uint16_t coldEntry = ltgt.coldEntry;   // 0x4000 (TMS) / 0x6000 (GEN2)
    const bool haveSrc = !src.empty();

    namespace fs = std::filesystem;
    std::error_code ec;

    // 0) Parse the listing host-side FIRST (pure — touches no machine state). A parse
    //    error aborts before any preset switch, exactly like a BASIC tokenise error.
    logo::Result prog;
    if (haveSrc) {
        prog = logo::compile(src, ltgt);
        if (!prog.ok) {
            emu->copySnapshot(mw_->uiSnapshot);
            r.console = "[bench] " + interp + ": LOGO parse FAILED — " + prog.error + "\n";
            r.status  = interp + ": parse error";
            r.ok = false;
            return r;
        }
        // Verify = parse-check only; leave the machine completely alone.
        if (!run) {
            char buf[224];
            std::snprintf(buf, sizeof(buf),
                "[bench] %s: parsed OK — %d procedure(s), %d immediate line(s), entry '%s' "
                "(Run to poke the proc table + launch)\n",
                interp.c_str(), prog.procCount, prog.immediateCount,
                prog.entry.empty() ? "(none)" : prog.entry.c_str());
            r.console = buf;
            if (!prog.warning.empty()) r.console += "[bench] note: " + prog.warning + "\n";
            std::snprintf(buf, sizeof(buf), "%s: parsed (%d procs)", interp.c_str(), prog.procCount);
            r.status = buf; r.ok = true;
            return r;
        }
    }

    // From here we (re)program the machine, so any prior LOGO REPL is void until a
    // fresh cold-start succeeds below.
    logoReplActive_ = false;

    // 1) Resolve + validate the interpreter ROM UP FRONT so a missing ROM aborts with
    //    the machine untouched (no preset switch, no half-plugged card).
    std::string tmsCartPath, gen2RomPath;
    if (tms) {
        tmsCartPath = codeTankBasicLogoRomReadPath();
        if (tmsCartPath.empty()) {
            r.console = "[bench] " + interp +
                ": Codetank_BASIC_LOGO.rom not found — build it with "
                "tools/build_codetank_rom.py --rom basiclogo\n"
                "[bench] aborting injection; machine left unchanged.\n";
            r.status = interp + ": ROM not found"; r.ok = false; return r;
        }
    } else {
        for (const char* c : {"roms/logo-gen2.rom", "../roms/logo-gen2.rom",
                              "../../roms/logo-gen2.rom"})
            if (fs::exists(c, ec)) { gen2RomPath = c; break; }
        if (gen2RomPath.empty()) {
            r.console = "[bench] " + interp + ": roms/logo-gen2.rom not found.\n"
                        "[bench] aborting injection; machine left unchanged.\n";
            r.status = interp + ": ROM not found"; r.ok = false; return r;
        }
    }

    // 2) Plug the interpreter's machine. For TMS, pre-load the BASIC_LOGO cartridge
    //    BEFORE draining the deferred plugs so enabling CodeTank doesn't
    //    auto-probe the default ARCADE image, then pin the jumper to LOWER (4000R ->
    //    LOGO). GEN2 loads the interpreter into RAM at $6000 after the reset.
    onTargetSelected(target);
    if (tms) {
        std::string err;
        if (!emu->loadCodeTankRom(tmsCartPath, err)) {
            r.console = "[bench] " + interp + ": Codetank_BASIC_LOGO.rom load FAILED — " + err + "\n";
            r.status = interp + ": ROM load failed"; r.ok = false; return r;
        }
        mw_->pendingCodeTankRomPath.clear();
    }
    mw_->finalizePendingCardPlugs();

    std::string romErr; bool romOk = true;
    if (tms) {
        mw_->codeTankJumper = CodeTank::Jumper::Lower16;   // LOGO lives in the LOWER bank
        emu->setCodeTankJumper(mw_->codeTankJumper);
        if (!mw_->tms9918Enabled)  { mw_->tms9918Enabled = true; mw_->showTMS9918 = true; emu->setTMS9918Enabled(true); }
        if (!mw_->codeTankEnabled) { mw_->codeTankEnabled = true; emu->setCodeTankEnabled(true); }
        emu->hardReset(/*animateBoot=*/false);
    } else {
        mw_->showGraphicsCard = true;
        emu->hardReset(/*animateBoot=*/false);
        romOk = emu->loadInterpreterRom(gen2RomPath, 0x6000, romErr);
    }
    if (!romOk) {
        emu->copySnapshot(mw_->uiSnapshot);
        r.console = "[bench] " + interp + ": ROM (re)load FAILED — "
                    + (romErr.empty() ? "unknown error" : romErr) + "\n";
        r.status = interp + ": ROM load failed"; r.ok = false; return r;
    }

    // 3) Cold-start the interpreter to its `?` prompt: `main` initialises zero page,
    //    the turtle, the LFSR seed and zeroes n_procs, then parks in the REPL's
    //    keyboard-poll. RAM (incl. PROCBSS) is left as `main` set it.
    constexpr uint64_t kColdStartCycles = 12'000'000;
    emu->runFromSync(coldEntry, kColdStartCycles);

    // Prep-only call (selectTargetExplicit passes an empty listing, run=false): the
    // interpreter is at its prompt — report ready without poking anything.
    if (!haveSrc) {
        emu->copySnapshot(mw_->uiSnapshot);
        logoReplActive_ = true;   // interpreter live at its prompt → REPL input usable
        r.status = interp + " — ready"; r.ok = true; return r;
    }

    // 4) Poke the procedure table + n_procs while the CPU is parked (n_procs was just
    //    zeroed by cold-start, so the table is ours to fill), then queue the single
    //    entry line and resume the REPL live. The async loop drains the queued keys
    //    into $D010 as it runs; the REPL reads the entry line and executes it.
    std::vector<std::pair<uint16_t, uint8_t>> writes;
    writes.reserve(prog.writes.size());
    for (const logo::Write& w : prog.writes) writes.emplace_back(w.addr, w.value);
    emu->writeMemoryBatch(writes);

    for (char c : prog.entry) emu->queueKey(c);
    if (!prog.entry.empty()) emu->queueKey('\r');
    emu->startCpu();
    emu->copySnapshot(mw_->uiSnapshot);

    char buf[256];
    if (prog.entry.empty()) {
        std::snprintf(buf, sizeof(buf),
            "[bench] %s: poked %d procedure(s) into the proc table — no entry point, "
            "nothing launched (type a call at the LOGO prompt in the Apple-1 screen "
            "window)\n", interp.c_str(), prog.procCount);
    } else {
        std::snprintf(buf, sizeof(buf),
            "[bench] %s: poked %d procedure(s) → running '%s' (proc table injected, "
            "no keyboard typing)\n", interp.c_str(), prog.procCount, prog.entry.c_str());
    }
    r.console = buf;
    if (!prog.warning.empty()) r.console += "[bench] note: " + prog.warning + "\n";
    r.console += "[bench] LOGO is live at its prompt — type commands in the Apple-1 "
                 "screen window (e.g. a proc call, FD 50, or TO … END) to drive the "
                 "turtle interactively.\n";
    logoReplActive_ = true;   // interpreter resident + running → REPL input usable
    r.status = interp + (prog.entry.empty() ? ": procedures loaded" : ": running (injected)");
    r.ok = true;
    return r;
}
