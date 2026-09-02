// POM1 host for the portable bench. See Pom1BenchHost.h.
#include "Pom1BenchHost.h"
#include "ResourceLocator.h"
#include "Pom1BenchTargets.h"
#include "Pom1BenchCc65.h"

#include "BasicTokeniserApplesoft.h"        // basic::compile — Applesoft tokeniser (GEN2/TMS)
#include "LogoProgramLoader.h"              // logo::compile — LOGO proc-table injector
#include "BasicTokeniserInteger.h"          // ibasic::compile — Integer BASIC tokeniser ($E000)
#include "BasicCompilerApplesoft.h"         // basicnative::compile — native standalone 6502
#include "HexDumpFile.h"          // pom1::isHexDumpExtension — .txt/.hex/.apl/.mon
#include "MainWindow_ImGui.h"     // mw_ members (friend) + EmulationController
#include "MainWindow_Internal.h"  // kMachinePresets / BasicType (ACI program-output presets)
#include "NativeFileDialog.h"     // OS-native file picker for Open/Save source
#include "ProcessUtil.h"          // bench::shellQuote / runCapture / whichExe
#include "imgui.h"                // ImGui::GetTime for the CodeTank cold-boot

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#include <process.h>              // _getpid — per-process bench scratch dir
#else
#include <unistd.h>              // getpid  — per-process bench scratch dir
#endif

#include "POM1Build.h"             // POM1_IS_WASM
#if POM1_IS_WASM
#include <emscripten.h>            // EM_ASM / EM_ASM_INT for the in-browser cc65 path
#endif

// ─────────────────────────────────────────────────────────────
// Static data: starter sketches, the embedded asm cfg, target + example tables
// ─────────────────────────────────────────────────────────────


#if !POM1_IS_WASM   // the fallback linker cfg is written to a scratch dir for the DESKTOP ca65
static const char* kBenchEmbeddedCfg =
    "MEMORY {\n"
    "    ZP:  start = $0000, size = $0030, type = rw, define = yes;\n"
    "    RAM: start = $0300, size = $7C00, type = rw, define = yes, file = %O;\n"
    "}\n"
    "SEGMENTS {\n"
    "    ZEROPAGE: load = ZP,  type = zp;\n"
    "    CODE:     load = RAM, type = ro;\n"
    "    RODATA:   load = RAM, type = ro,  optional = yes;\n"
    "    DATA:     load = RAM, type = rw,  optional = yes;\n"
    "    BSS:      load = RAM, type = bss, optional = yes, define = yes;\n"
    "}\n";
#endif

namespace {

// The target table, the starter sketches and the two path helpers live in
// Pom1BenchTargets.cpp; pull them in unqualified so the code below is unchanged.
using namespace pom1::benchhost;

// CODETANKDEV.rom (the TMS9918 DevBench cartridge: a pure TWO-SLOT flash cart —
// the asm/C build lands in whichever bank the "Upper" toggle selects; the resident
// Applesoft/LOGO interpreters come from Codetank_BASIC_LOGO.rom, not from here)
// normally lives under roms/codetank/.
// On a dev checkout that tree is writable, so asm/C uploads reflash the lower bank
// in place. In a packaged AppImage, roms/ is a read-only squashfs symlink — writes
// there fail silently and the DevBench reboots a stale cartridge. AppRun exports
// POM1_CODETANK_DEV_DIR pointing at a writable, pre-seeded copy; we prefer it for
// both reads and writes, falling back to the cwd/exe-relative roms/codetank/ tree.

// Best existing CODETANKDEV.rom to read (writable copy first, then the bundled
// read-only one). Returns "" if none is found anywhere.
std::string codeTankDevRomReadPath() {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (const char* env = std::getenv("POM1_CODETANK_DEV_DIR"); env && *env) {
        std::string p = (fs::path(env) / "CODETANKDEV.rom").string();
        if (fs::exists(p, ec)) return p;
    }
    return pom1::ResourceLocator::defaultLocator()
        .find("roms/codetank/CODETANKDEV.rom").string();
}

// Writable CODETANKDEV.rom target for the asm/C DevBench flash. Prefers the
// explicit writable dir (AppImage), else the roms/codetank/ tree on a dev
// checkout. Returns "" if neither is available (caller picks a temp fallback).
std::string codeTankDevRomWritePath() {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (const char* env = std::getenv("POM1_CODETANK_DEV_DIR"); env && *env) {
        fs::create_directories(env, ec);
        return (fs::path(env) / "CODETANKDEV.rom").string();
    }
    const fs::path dir =
        pom1::ResourceLocator::defaultLocator().findDirectory("roms/codetank");
    return dir.empty() ? std::string{} : (dir / "CODETANKDEV.rom").string();
}

// Flash the asm/C build at `binPath` into the chosen 16 KB bank of
// CODETANKDEV.rom while preserving the OTHER bank (the user's previous flash)
// seeded from the best existing copy, write it to `outPath`, and report
// whether the write actually landed (the old code ignored ofstream failures,
// so a read-only roms/ booted a stale cartridge). Both banks are flash slots
// — the cartridge is created from blank $FF on first use, so the generated
// rom needs no committed seed (works identically on WASM's MEMFS).
bool flashCodeTankDevRom(const std::string& binPath, const std::string& outPath,
                         bool upperBank, std::string& err) {
    namespace fs = std::filesystem;
    std::vector<unsigned char> rom(0x8000, 0xFF);
    // Seed the whole 32 KB from the best existing cartridge so the other
    // bank survives, then clear + overwrite only the chosen 16 KB half.
    if (std::string seed = codeTankDevRomReadPath(); !seed.empty()) {
        std::ifstream prev(seed, std::ios::binary);
        if (prev) prev.read(reinterpret_cast<char*>(rom.data()), 0x8000);
    }
    const size_t off = upperBank ? 0x4000 : 0x0000;
    std::fill_n(rom.begin() + off, 0x4000, static_cast<unsigned char>(0xFF));
    // Verify the build actually produced bytes: a missing/empty .bin reads 0
    // bytes and would otherwise flash a blank-but-"successful" bank. A short
    // read (< 16 KB) is legitimate — small programs don't fill the half.
    { std::ifstream in(binPath, std::ios::binary);
      if (!in) { err = "cannot read build output " + binPath; return false; }
      in.read(reinterpret_cast<char*>(rom.data() + off), 0x4000);
      if (in.gcount() == 0) { err = "build output " + binPath + " is empty"; return false; }
    }
    std::error_code ec;
    fs::create_directories(fs::path(outPath).parent_path(), ec);
    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(rom.data()),
              static_cast<std::streamsize>(rom.size()));
    out.flush();
    if (!out) { err = "cannot write " + outPath + " (read-only location?)"; return false; }
    return true;
}

// RAII sweeper for Bench's temp staging files: collect every scratch path we
// write, delete them all in the destructor so cleanup runs on every return /
// early-exit path (build() has many) without threading remove() calls through
// each one. Errors are intentionally ignored (best-effort hygiene).
struct TempFileSweeper {
    std::vector<std::filesystem::path> paths;
    void add(const std::filesystem::path& p) { paths.push_back(p); }
    ~TempFileSweeper() {
        std::error_code ec;
        for (const auto& p : paths) std::filesystem::remove(p, ec);
    }
};


// Presets with ACI but no Integer-BASIC program tape (GEN2 dev bench, GEN2 HGR
// Color, …): live speaker output uses $C0xx TAPE OUT toggles. A loaded
// WOZ_talk.mp3 sits in audio-stream mode and the deck never plays the pulse
// queue — eject before Run so CrazyCycle-style chiptunes are audible.
namespace md = pom1::mainwindow::detail;

static bool presetUsesAciProgramOutput(int presetIndex)
{
    const md::MachineConfig* cfgPtr = md::machinePresetAt(presetIndex);
    if (!cfgPtr) return false;
    const md::MachineConfig& cfg = *cfgPtr;
    return cfg.hasCard(pom1::CardId::Aci) &&
           cfg.basicType != md::BasicType::IntegerCassette;
}

static void ejectTapeForAciProgramOutput(EmulationController* emu, bench::BuildResult& r, int preset)
{
    if (!emu || !presetUsesAciProgramOutput(preset)) return;
    emu->ejectTape();
    r.console += "[ok] cassette ejected (ACI program output / $C030 speaker)\n";
}


// New-dialog axes (language x machine -> target index, resolved by targetFor).
// kP1*Hints are parallel to the labels and surface as combo-entry tooltips.
const char* const kP1Languages[] = { "Assembly  —  ca65 / ld65", "C  —  cc65 / cl65",
                                     "BASIC  —  injected listing",
                                     "LOGO  —  injected listing" };
const char* const kP1LanguageHints[] = {
    "MOS 6502 assembler (cc65's ca65 + ld65). Links against the apple1 / tms9918 /\n"
    "gen2 equate libraries under dev/lib via the per-target linker .cfg.",
    "C cross-compiler (cc65's cl65). Pulls in the apple1.c runtime, or the\n"
    "tms9918c (TMS9918) / gen2 C runtime depending on the target.",
    "POM1 cold-starts the in-ROM interpreter, then tokenises your listing ahead of\n"
    "time and loads it directly (instant, no per-character typing, no 127-char line\n"
    "cap). Pure C++, so it works in the web (WASM) build too. Integer BASIC (Apple-1\n"
    "dual-rom) + Applesoft on microSD / GEN2 HGR / TMS9918.",
    "APPLE-1 LOGO V2.6 turtle graphics. POM1 cold-starts the resident interpreter,\n"
    "pokes your TO … END procedures straight into its procedure table and feeds one\n"
    "entry line — no per-character typing. Pure C++ (works on WASM). Runs on the\n"
    "TMS9918 (CodeTank) card or Uncle Bernie's GEN2 HGR card.",
};
// The "Target" combo is per-language: asm/C show the three graphics machines,
// BASIC shows its four machines (CodeBench filters by targetFor()). Each is its
// own entry so New > BASIC reads as the machine choice, not a graphics card.
// Bare-Apple-1 BASIC is Integer (the dual-ROM $E000 bank) — Applesoft needs a
// card (microSD / GEN2 / TMS), so there is no "Applesoft on bare Apple-1".
// Inject-vs-native-compile is NOT a machine here: it's an "Inject | Compile"
// toggle the New dialog / Mode switcher surface via nativeSiblingOf() for the
// two Applesoft machines (GEN2, TMS) that have a native compiler.
const char* const kP1Machines[]  = {
    "Apple-1 dual 4K/8K  (text) - start here",   // 0  asm/C
    "P-LAB Graphic Card  (TMS9918)",             // 1  asm/C
    "Uncle Bernie GEN2 HGR  (colour)",           // 2  asm/C
    "Applesoft Lite + microSD",                  // 3  BASIC -> target 8  (inject only)
    "Applesoft GEN2 HGR",                        // 4  BASIC -> target 9  (+ native 12)
    "Applesoft TMS9918",                         // 5  BASIC -> target 11 (+ native 13)
    "Integer BASIC (Apple-1 dual-rom)",          // 6  BASIC -> target 7  (inject only)
    "LOGO TMS9918  (interpreter)",               // 7  LOGO  -> target 14
    "LOGO GEN2 HGR  (interpreter)",              // 8  LOGO  -> target 15
};
static_assert(sizeof(kP1Machines) / sizeof(kP1Machines[0]) ==
                  kP1MachineLogoGen2 + 1,
              "kP1Machines[] and the kP1Machine* index constants in "
              "Pom1BenchHost.h are out of sync — update both together "
              "(targetFor() and the boot profile chooser consume them)");
const char* const kP1MachineHints[] = {
    "Stock Apple-1: 40x24 text printed through the WozMon ECHO routine ($FFEF).\n"
    "Easiest place to start - no graphics card needed.",
    "P-LAB Graphic Card by Claudio Parmigiani — TMS9918 VDP, Graphics I mode,\n"
    "256x192, data port $CC00 / control $CC01. Upload flashes the build into the\n"
    "CodeTank dev cartridge and boots 4000R (all TMS9918 code runs from CodeTank).",
    "Uncle Bernie's GEN2 colour card — Apple II-style HIRES (280x192) driven by\n"
    "the soft switches $C250-$C257. Hello world uses the BBFont.",
    "Applesoft Lite on the P-LAB microSD machine — ROM at $6000-$7FFF, cold start\n"
    "6000R, SD-OS LOAD/SAVE at $8000. The Bench relaxes the 8 KB preset to 64 KB\n"
    "for the run ($6000 is inside its out-of-range window).",
    "Applesoft GEN2 — Applesoft with the GEN2 colour graphics commands (TEXT/GR/\n"
    "HGR/COLOR=/HCOLOR=/PLOT/HLIN/VLIN/HPLOT, PRINT->GEN2 screen). Interpreter at\n"
    "$9800 (top of RAM) on the GEN2 card (preset 2). Demos: sketchs/basic_applesoft.",
    "Applesoft TMS9918 — Applesoft with the same graphics commands driving the\n"
    "P-LAB TMS9918 VDP ($CC00/$CC01). The interpreter is a CodeTank ROM cartridge\n"
    "($4000-$7FFF), cold start 4000R. sketchs/tms9918/applesoft_tms9918.",
    "Integer BASIC — Wozniak's 6502 Integer BASIC in the Apple-1 dual-ROM second\n"
    "bank ($E000-$EFFF), cold start E000R. No graphics, no floating point — the\n"
    "classic Apple-1 BASIC. Listing tokenised + loaded directly (no keyboard typing).",
    "APPLE-1 LOGO V2.6 on the P-LAB TMS9918 card — the turtle interpreter in the\n"
    "LOWER bank of Codetank_BASIC_LOGO.rom ($4000, jumper Lower), cold start 4000R, 16 KB.\n"
    "Procedures poked into the proc table; entry line fed to the REPL. 256x192 bitmap.",
    "APPLE-1 LOGO V2.6 on Uncle Bernie's GEN2 HGR card — interpreter loaded at $6000\n"
    "(roms/logo-gen2.rom), cold start 6000R, 48 KB (preset 2). Same injection path,\n"
    "280x192 HIRES turtle with the GEN2 artifact palette. Manual: sketchs/.../tool_logo.",
};

// Graduated learning examples (inline sources) on the Apple-1 text target. They
// build on each other: print a char -> a string -> a loop -> read the keyboard,
// then the same I/O in C. Larger demos (CrazyCycle, Telemetry) follow.
static const char* kEx_char =
    "; Example 1 - print one character. The Apple-1 display uses bit 7 as a\n"
    "; \"data valid\" flag, so ORA #$80 before printing. Return with JMP WOZMON.\n"
    ".include \"apple1.inc\"\n"
    ".segment \"CODE\"\n"
    "start:\n"
    "    lda #'H'\n"
    "    ora #$80\n"
    "    jsr ECHO\n"
    "    jmp WOZMON\n";
static const char* kEx_string =
    "; Example 2 - print a NUL-terminated string in a loop.\n"
    ".include \"apple1.inc\"\n"
    ".segment \"CODE\"\n"
    "start:\n"
    "    ldx #0\n"
    "loop:\n"
    "    lda msg,x\n"
    "    beq done\n"
    "    ora #$80\n"
    "    jsr ECHO\n"
    "    inx\n"
    "    bne loop\n"
    "done:\n"
    "    jmp WOZMON\n"
    "msg:\n"
    "    .byte \"HELLO, APPLE 1!\", $0D, $00\n";
static const char* kEx_loop =
    "; Example 3 - count 0 to 9 by incrementing a character.\n"
    ".include \"apple1.inc\"\n"
    ".segment \"CODE\"\n"
    "start:\n"
    "    lda #'0'\n"
    "loop:\n"
    "    ora #$80          ; set bit 7, print the digit\n"
    "    jsr ECHO\n"
    "    and #$7F          ; strip it back off before maths\n"
    "    clc\n"
    "    adc #1\n"
    "    cmp #'9'+1\n"
    "    bne loop\n"
    "    lda #$8D          ; carriage return\n"
    "    jsr ECHO\n"
    "    jmp WOZMON\n";
static const char* kEx_keyboard =
    "; Example 4 - echo the keyboard until Return ($0D). KBDCR bit 7 = key ready;\n"
    "; reading KBD returns the key with bit 7 set, so AND #$7F to get the ASCII.\n"
    ".include \"apple1.inc\"\n"
    ".segment \"CODE\"\n"
    "start:\n"
    "wait:\n"
    "    lda KBDCR\n"
    "    bpl wait\n"
    "    lda KBD\n"
    "    and #$7F\n"
    "    cmp #$0D\n"
    "    beq done\n"
    "    ora #$80\n"
    "    jsr ECHO\n"
    "    jmp wait\n"
    "done:\n"
    "    jmp WOZMON\n";
static const char* kEx_c_hello =
    "/* Example 5 - hello in C on the plain text Apple-1 (shared apple1c base). */\n"
    "#include \"apple1io.h\"\n"
    "void main(void) {\n"
    "    woz_puts((const unsigned char *)\"\\rHELLO FROM C\\r\");\n"
    "    woz_mon();\n"
    "}\n";
static const char* kEx_c_keyboard =
    "/* Example 6 - echo the keyboard in C until Return. */\n"
    "#include \"apple1io.h\"\n"
    "void main(void) {\n"
    "    unsigned char k;\n"
    "    woz_puts((const unsigned char *)\"\\rTYPE (Return quits):\\r\");\n"
    "    for (;;) {\n"
    "        k = apple1_getkey();\n"
    "        if (k == 13) break;\n"
    "        woz_putc(k);\n"
    "    }\n"
    "    woz_mon();\n"
    "}\n";

// `group` = section header shown before this entry in the Examples popup
// (nullptr → continues the current section). Showcases first (the "wow" for
// newcomers), then the inline asm + C basics.
struct P1Ex { const char* group; const char* label; bool file; const char* data; int target; const char* asset; uint16_t addr; };
const P1Ex kP1Examples[] = {
    { "Showcases",       "A-1-CrazyCycle  (Bernie GEN2 HGR)",  true,  "sketchs/gen2/demo_a1_crazycycle/A-1-CrazyCycle.asm", 2,
      "sdcard/NONO/HGR/UBERNIE#062000", 0x2000 },
    { nullptr,           "Snake telemetry  (Bernie GEN2 HGR)", true,  "sketchs/gen2/game_snake_telemetry/GEN2Snake.c", 5, "", 0 },
    { nullptr,           "Telemetry demo  (SDK harness)",      true,  "sketchs/apple1/demo_telemetry/A1_TelemetryDemo.asm", 0, "", 0 },
    { "Assembly basics", "Print a character",                  false, kEx_char,       0, "", 0 },
    { nullptr,           "Print a string",                     false, kEx_string,     0, "", 0 },
    { nullptr,           "Count 0 to 9",                       false, kEx_loop,       0, "", 0 },
    { nullptr,           "Echo the keyboard",                  false, kEx_keyboard,   0, "", 0 },
    { "C basics",        "Hello world",                        false, kEx_c_hello,    3, "", 0 },
    { nullptr,           "Keyboard echo",                      false, kEx_c_keyboard, 3, "", 0 },
    { "BASIC",           "Hello (Integer BASIC)",              false, kSketchBasicInteger,   7, "", 0 },
    { nullptr,           "Hello (Applesoft Lite)",             false, kSketchBasicApplesoft, 8, "", 0 },
    // APPLE-1 LOGO V2.6 turtle catalogue (sketchs/logo/, one .logo per row),
    // ordered so each builds on the one above: a shape, then a parameterised
    // procedure, then recursion, then RANDOM. Every listing is MACHINE-NEUTRAL
    // and runs unchanged on the GEN2 HGR card — switch Mode to "LOGO GEN2 HGR"
    // and press Run, the source needs no edit.
    //
    // They open on the TMS9918 target because that is the card the V2.6
    // interpreter shipped on (Codetank_BASIC_LOGO, lower bank), and because a
    // row can only name ONE: injectLogo picks the interpreter's RAM layout
    // (proc_table / n_procs / cold entry) from the TARGET INDEX, not from the
    // live machine, so a row that left the two disagreeing would poke TMS
    // addresses into a GEN2 machine. Catalogue + dialect notes (turns are
    // TR/TL, integers only, ≤ 6-char identifiers): sketchs/logo/README.md.
    { "LOGO turtle (TMS9918)",
                         "Hexagon  (REPEAT)",                  true,  "sketchs/logo/Hexagon.logo",  14, "", 0 },
    { nullptr,           "Star  (over-turning)",               true,  "sketchs/logo/Star.logo",     14, "", 0 },
    { nullptr,           "Star8  (star polygon {8/3})",        true,  "sketchs/logo/Star8.logo",    14, "", 0 },
    { nullptr,           "Rosette  (nested REPEAT)",           true,  "sketchs/logo/Rosette.logo",  14, "", 0 },
    { nullptr,           "Squares  (a :SIZE parameter)",       true,  "sketchs/logo/Squares.logo",  14, "", 0 },
    { nullptr,           "Flower  (a proc calling a proc)",    true,  "sketchs/logo/Flower.logo",   14, "", 0 },
    { nullptr,           "Spiral  (tail recursion + STOP)",    true,  "sketchs/logo/Spiral.logo",   14, "", 0 },
    { nullptr,           "Tree  (branching recursion)",        true,  "sketchs/logo/Tree.logo",     14, "", 0 },
    { nullptr,           "Rays  (RANDOM, SETH, PU/PD)",        true,  "sketchs/logo/Rays.logo",     14, "", 0 },
    { nullptr,           "Meadow  (SETXY, SETPC, recursion)",  true,  "sketchs/logo/Meadow.logo",   14, "", 0 },
};
const int kP1ExampleCount = static_cast<int>(sizeof(kP1Examples) / sizeof(kP1Examples[0]));

uint16_t parseCfgLoadAddr(const std::string& cfgPath)
{
    std::ifstream in(cfgPath);
    if (!in) return 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.find("%O") == std::string::npos) continue;
        size_t s = line.find("start");
        if (s == std::string::npos) continue;
        size_t dollar = line.find('$', s);
        if (dollar == std::string::npos) continue;
        try { return static_cast<uint16_t>(std::stoul(line.substr(dollar + 1), nullptr, 16)); }
        catch (...) { return 0; }
    }
    return 0;
}

struct BuildLogMeta {
    const char* action = "verify";   // verify | run
    const P1T*  target = nullptr;
    std::string sourcePath;
    std::string cfgPath;
    // Snapshotted by value from the AsmProjectCtx: the finalizer that reads
    // these runs at function-scope exit, after the block-scoped proj local has
    // already been destroyed — a stored pointer would dangle.
    bool     projValid    = false;   // proj.ok
    bool     projDualBank = false;
    uint16_t projLoAddr   = 0;
    uint16_t projHiAddr   = 0;
    const char* host = nullptr;      // desktop | wasm
    const char* toolchain = nullptr;   // ca65+ld65 | cl65 | wasm-cc65
};

static const char* buildLogSourceMode(int mode)
{
    switch (mode) {
    case 1: return "hex";
    case 3: return "c";
    case 4: return "basic";
    default: return "asm";
    }
}

static std::string formatBuildLogHeader(const BuildLogMeta& m)
{
    std::ostringstream os;
    os << "# POM1 DevBench build log (schema pom1-devbench/1)\n";
    os << "# host: " << (m.host ? m.host : "desktop") << "\n";
    os << "# action: " << (m.action ? m.action : "verify") << "\n";
    if (m.target) {
        os << "# target_label: " << (m.target->label ? m.target->label : "?") << "\n";
        os << "# preset_index: " << m.target->preset << "\n";
        os << "# source_mode: " << buildLogSourceMode(m.target->mode) << "\n";
        if (m.target->cfg && m.target->cfg[0])
            os << "# target_default_cfg: dev/cc65/" << m.target->cfg << "\n";
        if (m.target->codetankRom) os << "# deploy: codetank_rom_flash_4000R\n";
    }
    if (!m.sourcePath.empty()) os << "# source_path: " << m.sourcePath << "\n";
    else                       os << "# source_path: (untitled scratch)\n";
    if (!m.cfgPath.empty())    os << "# linker_cfg: " << m.cfgPath << "\n";
    if (m.projValid) {
        os << "# project_ctx: sketch_sidecar_or_makefile\n";
        if (m.projDualBank)
            os << "# load_map: dual_bank lo=$" << std::hex << m.projLoAddr
               << " hi=$" << m.projHiAddr << std::dec << "\n";
    }
    if (m.toolchain) os << "# toolchain: " << m.toolchain << "\n";
    os << "# interpreter: cc65/ca65/ld65 compiler output below; status bar = human summary\n";
    os << "# ---\n";
    return os.str();
}

static void prependBuildLogHeader(bench::BuildResult& r, const BuildLogMeta& m)
{
    if (!r.showConsole || r.console.empty()) return;
    const std::string hdr = formatBuildLogHeader(m);
    // Only prepend if the header marker isn't already at the front. Using rfind
    // at pos 0 is length-agnostic (the marker is 25 chars; the old compare(0,24,…)
    // mismatched the literal's length and so never matched, defeating the guard).
    if (r.console.rfind("# POM1 DevBench build log", 0) != 0)
        r.console.insert(0, hdr);
}

struct BuildLogFinalizer {
    bench::BuildResult& r;
    BuildLogMeta& m;
    ~BuildLogFinalizer() { prependBuildLogHeader(r, m); }
};

void parseErrorMarkers(const std::string& out, std::vector<std::pair<int, std::string>>& markers)
{
    size_t start = 0;
    while (start <= out.size()) {
        const size_t nl  = out.find('\n', start);
        const size_t end = (nl == std::string::npos) ? out.size() : nl;
        const std::string line = out.substr(start, end - start);
        if (line.find("rror") != std::string::npos || line.find("arning") != std::string::npos) {
            const size_t lp = line.find('(');
            int lineNo = 0;
            if (lp != std::string::npos) {
                const size_t rp = line.find(')', lp);
                if (rp != std::string::npos)
                    try { lineNo = std::stoi(line.substr(lp + 1, rp - lp - 1)); } catch (...) { lineNo = 0; }
            }
            if (lineNo > 0) {
                size_t mp = line.find("rror:");
                if (mp == std::string::npos) mp = line.find("arning:");
                markers.emplace_back(lineNo, (mp == std::string::npos) ? line : line.substr(mp));
            }
        }
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
}

// Cross-platform install nudge appended whenever the cc65 toolchain (or the dev/
// source tree it needs) is missing — turns a dead-end "not found" into a fix.
#if !POM1_IS_WASM   // nothing to install in a browser: the WASM build ships its own toolchain
const char* const kCc65InstallHint =
    "\nInstall the cc65 toolchain, then reopen the Bench:\n"
    "  Debian/Ubuntu : sudo apt install cc65\n"
    "  Fedora        : sudo dnf install cc65\n"
    "  Arch          : sudo pacman -S cc65\n"
    "  macOS         : brew install cc65\n"
    "  Windows/other : https://cc65.github.io/  (add its bin/ to PATH)\n";
#endif

// Plain-language one-liner for the most common ca65/ld65/cl65 diagnostics, so a
// newcomer gets a nudge above the raw toolchain spew. Empty if nothing matches.
std::string humanizeCc65(const std::string& out)
{
    auto has = [&](const char* s) { return out.find(s) != std::string::npos; };
    std::string tip;
    if (has("ndefined"))            // "Symbol ... undefined" / "Undefined external"
        tip = "an undefined label/symbol - check spelling, or add its definition / .include.";
    else if (has("Range error"))
        tip = "a value is out of range - over 255 for an 8-bit operand, or a branch over 127 bytes away.";
    else if (has("verflow") || has("emory configuration"))
        tip = "the program is too big for the linker config - shrink it or pick a roomier target.";
    else if (has("Unknown identifier") || has("nknown opcode") || has("yntax error"))
        tip = "a typo or unknown name on the flagged line - check the mnemonic / identifier.";
    return tip.empty() ? std::string() : "[bench] Hint: " + tip + "\n";
}

} // namespace

// ─────────────────────────────────────────────────────────────
// Pom1BenchHost
// ─────────────────────────────────────────────────────────────

void Pom1BenchHost::setActiveSourcePath(const std::string& path)
{
    activeSourcePath_ = path;
    applySketchAssets(activeSourcePath_, extraAsset_, extraAssetAddr_);
}

Pom1BenchHost::Pom1BenchHost(MainWindow_ImGui* mw) : mw_(mw)
{
    // The browser now has the full cc65 toolchain compiled to WASM
    // (build-wasm/cc65/ + the bundled C runtime, driven by window.POM1cc65 via the
    // async pollBuild path), so the web build exposes every target + the New-sketch
    // (language x machine) matrix, same as desktop.
    for (int i = 0; i < kP1TargetCount; ++i) {
        targets_.push_back({ kP1Targets[i].label, kP1Targets[i].label,
                             kP1Targets[i].lang });
        targetMap_.push_back(i);
    }
    // New-sketch matrix — works on web + desktop (the starter sketches are
    // compiled-in strings, no file access).
    for (const char* l : kP1Languages)     languages_.push_back(l);
    for (const char* m : kP1Machines)      machines_.push_back(m);
    for (const char* h : kP1LanguageHints) languageHints_.push_back(h);
    for (const char* h : kP1MachineHints)  machineHints_.push_back(h);
    // File-based examples load their source from sketchs/, which the WASM build
    // now preloads into MEMFS (see CMakeLists `--preload-file sketchs`), so the
    // Examples popup works on web too — loadExample()'s cwd-relative ifstream
    // resolves "sketchs/..." against the MEMFS root. (Inline examples 1-6 never
    // needed a file.)
    for (int i = 0; i < kP1ExampleCount; ++i)
        examples_.push_back({ kP1Examples[i].label,
                              kP1Examples[i].group ? kP1Examples[i].group : "" });
}

// Make a relocatable cc65 bundle self-locate its runtime (include/, lib/,
// target/) by pointing CC65_HOME at <cc65>/share/cc65 next to the resolved
// binary, when the user hasn't already set CC65_HOME. apt/brew cc65 binaries
// don't all derive their prefix from argv[0], so a bundled toolchain needs this.
// Desktop only: the browser toolchain locates its own runtime, and probe()'s
// caller is compiled out there — leaving this defined would be an unused static.
#if !POM1_IS_WASM
static void ensureCc65Home(const std::string& binaryPath)
{
    namespace fs = std::filesystem;
    if (binaryPath.empty()) return;
    if (const char* existing = std::getenv("CC65_HOME"); existing && *existing) return;
    std::error_code ec;
    // binaryPath = <cc65>/bin/ca65[.exe]  ->  <cc65>/share/cc65
    fs::path home = fs::path(binaryPath).parent_path().parent_path() / "share" / "cc65";
    if (!fs::is_directory(home, ec)) return;   // bare PATH hit, not a bundled tree
    const std::string h = fs::absolute(home, ec).string();
  #if defined(_WIN32)
    _putenv_s("CC65_HOME", h.c_str());
  #else
    setenv("CC65_HOME", h.c_str(), 0);   // 0 = keep any pre-existing value (already guarded)
  #endif
}
#endif

void Pom1BenchHost::probe() const
{
    if (probed_) return;
    probed_ = true;
#if !POM1_IS_WASM
    namespace fs = std::filesystem;
    std::error_code ec;

    // A bundled cc65 shipped next to the app makes a packaged build self-contained
    // (no system cc65 on PATH needed). Search exe-relative dirs + an explicit
    // POM1_CC65_DIR override FIRST, so the known-good bundle wins over PATH; a
    // dev build with no bundle simply falls through to PATH.
    const std::string exeDir = bench::executableDir();
    std::vector<std::string> cc65Dirs;
    if (const char* envDir = std::getenv("POM1_CC65_DIR"); envDir && *envDir)
        cc65Dirs.emplace_back(envDir);
    if (!exeDir.empty()) {
        const fs::path e(exeDir);
        cc65Dirs.push_back((e / "cc65" / "bin").string());                                 // Win ZIP / generic
        cc65Dirs.push_back((e.parent_path() / "Resources" / "cc65" / "bin").string());     // macOS .app
        cc65Dirs.push_back((e.parent_path() / "share" / "POM1" / "cc65" / "bin").string());// Linux AppImage
    }

    ca65_ = bench::whichExe("ca65", cc65Dirs);
    ld65_ = bench::whichExe("ld65", cc65Dirs);
    cl65_ = bench::whichExe("cl65", cc65Dirs);
    ar65_ = bench::whichExe("ar65", cc65Dirs);
    toolchainOk_ = !ca65_.empty() && !ld65_.empty();
    ensureCc65Home(!ca65_.empty() ? ca65_ : cl65_);

    // The Bench's linker cfgs + libraries live under dev/. Release bundles ship
    // a dev/ subtree next to the app, and a packaged app has chdir'd to a
    // user-data dir by now — the cwd walk AND the exe-relative layouts
    // (Win ZIP, macOS Resources/, AppImage share/POM1/) that used to be spelled
    // out here are exactly ResourceLocator's roots. `dev/cc65` rather than
    // `dev`: the linker cfgs are what makes a dev/ tree usable.
    std::string& devRoot = devRoot_;
    const fs::path devCc65 =
        pom1::ResourceLocator::defaultLocator().findDirectory("dev/cc65");
    devRoot = devCc65.empty() ? std::string{} : devCc65.parent_path().string();
    if (!devRoot.empty()) {
        std::string flags;
        // Recurse: nested lib dirs (e.g. lib/games/sokoban, lib/games/chess,
        // lib/gen2/sprites, lib/tms9918c/cc65) hold .inc/.asm includes too, so a
        // shallow one-level scan would miss them and ca65 would fail with
        // "Cannot open include file 'sokoban_common.inc'". Add every directory
        // under dev/lib as a -I search path (ca65 dedups; over-including is free).
        for (const auto& e : fs::recursive_directory_iterator(fs::path(devRoot) / "lib", ec))
            if (e.is_directory(ec))
                flags += "-I " + bench::shellQuote(fs::absolute(e.path(), ec).string()) + " ";
        libFlags_ = flags;
    }
    if (!devRoot.empty()) {
        // The TMS9918 C lib (ex-apple1-videocard-lib) now lives flat under dev/lib/tms9918c.
        const fs::path vroot = fs::path(devRoot) / "lib" / "tms9918c";
        if (fs::exists(vroot, ec)) videocardLib_ = fs::absolute(vroot, ec).string();
        const fs::path cfg = vroot / "cc65" / "codetank_c.cfg";
        if (fs::exists(cfg, ec)) codetankCfg_ = fs::absolute(cfg, ec).string();
    }

    // GEN2 HGR C: the gen2c lib + its cfg under dev/.
    if (!devRoot.empty()) {
        const fs::path glib = fs::path(devRoot) / "lib" / "gen2c";
        const fs::path gcfg = fs::path(devRoot) / "cc65" / "apple1_gen2_c.cfg";
        if (fs::exists(glib, ec)) gen2cLib_ = fs::absolute(glib, ec).string();
        if (fs::exists(gcfg, ec)) gen2Cfg_  = fs::absolute(gcfg, ec).string();
        // Plain text C uses dev/cc65/apple1_c.cfg + the shared apple1c text base.
        const fs::path pcfg = fs::path(devRoot) / "cc65" / "apple1_c.cfg";
        if (fs::exists(pcfg, ec)) plainCfg_ = fs::absolute(pcfg, ec).string();
        // Shared Apple-1 text/keyboard C base (woz_puts/apple1_getkey) — card-neutral,
        // linked by both the plain-text and GEN2 HGR C targets so either can do
        // terminal I/O. The graphics runtimes (gen2c / videocard-lib) sit on top.
        const fs::path a1c = fs::path(devRoot) / "lib" / "apple1c";
        if (fs::exists(a1c, ec)) apple1cLib_ = fs::absolute(a1c, ec).string();
        // Header-only telemetry side-channel kit (telemetry.h). No .c to link —
        // just an include dir, folded into every C build below.
        const fs::path tele = fs::path(devRoot) / "lib" / "telemetry";
        if (fs::exists(tele, ec)) telemetryLib_ = fs::absolute(tele, ec).string();
        // Card-neutral geometry/number layer (dev/lib/gfx, factoring axis 1):
        // gfx_line/rect/circle/ellipse + gfx_utoa/itoa/hexstr, with a per-card
        // backend resolved at link time. Folded into the GEN2 HGR C target below
        // so a sketch can #include "gfx.h" and draw vectors on the GEN2 card.
        const fs::path gfx = fs::path(devRoot) / "lib" / "gfx";
        if (fs::exists(gfx, ec)) gfxLib_ = fs::absolute(gfx, ec).string();
    }
    cl65Ok_ = !cl65_.empty() && !videocardLib_.empty() && !codetankCfg_.empty() && !gfxLib_.empty();
    gen2COk_  = !cl65_.empty() && !gen2cLib_.empty() && !gen2Cfg_.empty();
    plainCOk_ = !cl65_.empty() && !apple1cLib_.empty() && !plainCfg_.empty();
#endif
}

int Pom1BenchHost::defaultTargetIndex() const
{
#if POM1_IS_WASM
    return 0;   // asm dual-4K (WASM bundles the full cc65 toolchain; all targets exposed)
#else
    probe();
    return toolchainOk_ ? 0 : 6;   // asm dual-4k if cc65 present, else Wozmon hex
#endif
}

std::string Pom1BenchHost::starterSketch(int target) const
{
    if (target < 0 || target >= kP1TargetCount) return "";
    return kP1Targets[p1(target)].sketch ? kP1Targets[p1(target)].sketch : "";
}

const std::vector<std::string>& Pom1BenchHost::languages()     const { return languages_; }
const std::vector<std::string>& Pom1BenchHost::machines()      const { return machines_; }
const std::vector<std::string>& Pom1BenchHost::languageHints() const { return languageHints_; }
const std::vector<std::string>& Pom1BenchHost::machineHints()  const { return machineHints_; }

int Pom1BenchHost::targetFor(int language, int machine) const
{
    // languages: 0=asm, 1=C, 2=BASIC, 3=LOGO. machines: 0=Apple-1 text, 1=TMS9918,
    // 2=GEN2 HGR (asm/C use these three); 3=Applesoft Lite + microSD, 4=Applesoft
    // GEN2 HGR, 5=Applesoft TMS9918, 6=Integer BASIC (Apple-1 dual-rom) (BASIC uses
    // these four); 7=LOGO TMS9918, 8=LOGO GEN2 HGR (LOGO uses these two). CodeBench's
    // New dialog shows only the machines valid for the language. Inject-vs-native
    // compile is a per-target toggle (nativeSiblingOf), NOT a machine row.
    if (language == 0) return (machine >= 0 && machine <= 2) ? machine     : -1;  // asm 0..2
    if (language == 1) return (machine >= 0 && machine <= 2) ? 3 + machine : -1;  // C   3..5
    if (language == 2) {                                                          // BASIC
        switch (machine) {
            case kP1MachineApplesoftMicroSD: return 8;   // Applesoft Lite + microSD ($6000)
            case kP1MachineApplesoftGen2:    return 9;   // Applesoft GEN2 HGR ($9800)
            case kP1MachineApplesoftTms:     return 11;  // Applesoft TMS9918 (CodeTank $4000)
            case kP1MachineIntegerBasic:     return 7;   // Integer BASIC (Apple-1 dual-rom, $E000)
        }
        return -1;
    }
    if (language == 3) {                                                          // LOGO
        switch (machine) {
            case kP1MachineLogoTms:  return 14;  // LOGO TMS9918 (CodeTank $4000)
            case kP1MachineLogoGen2: return 15;  // LOGO GEN2 HGR ($6000)
        }
        return -1;
    }
    return -1;
}

// The native-compile sibling of a BASIC "inject" target. Only the two Applesoft
// machines with a native compiler have one (GEN2 inject 9 -> native 12, TMS
// inject 11 -> native 13). Native compile needs the desktop cc65 toolchain +
// dev/ tree, so on WASM there is no sibling (the toggle collapses to Inject).
int Pom1BenchHost::nativeSiblingOf(int target) const
{
#if POM1_IS_WASM
    (void)target;
    return -1;
#else
    switch (target) {
        case 9:  return 12;  // Applesoft GEN2 HGR  : interpreter -> native ($0300)
        case 11: return 13;  // Applesoft TMS9918   : interpreter -> native ($0300)
        default: return -1;
    }
#endif
}

// The Warm/Cold toggle only makes sense for the resident-interpreter BASIC
// targets (mode 4) — those are the ones injectBasic can re-enter warm to keep a
// typed-in program. Compiled targets (asm/C/native BASIC/hex) have nothing to
// preserve, so CodeBench hides the toggle for them.
bool Pom1BenchHost::warmStartApplies(int target) const
{
    const int i = p1(target);
    return i >= 0 && i < kP1TargetCount && kP1Targets[i].mode == 4;
}

bool Pom1BenchHost::flashBankApplies(int target) const
{
    const int i = p1(target);
    if (i < 0 || i >= kP1TargetCount) return false;
    const P1T& t = kP1Targets[i];
    // Only the asm/C build targets FLASH the CODETANKDEV cartridge; the
    // Applesoft/LOGO interpreter targets (modes 4/6) also carry codetankRom
    // but insert the stabilised Codetank_BASIC_LOGO.rom instead.
    return t.codetankRom && (t.mode == 0 || t.mode == 3);
}

static bool sourcePathLooksGT6144(const std::string& path)
{
    std::string p = path;
    std::replace(p.begin(), p.end(), '\\', '/');
    std::transform(p.begin(), p.end(), p.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return p.find("/gt6144") != std::string::npos ||
           p.find("graphic gt-6144") != std::string::npos;
}

// A "portable" sketch lives under sketchs/portable/ — it draws through the
// card-neutral gfx façade and builds for either graphics card. Opening one must
// NOT yank the user off their current preset: it follows whatever card is live
// (TMS by default on a bare Apple-1). See targetForPath / onTargetSelected.
static bool sourcePathLooksPortable(const std::string& path)
{
    std::string p = path;
    std::replace(p.begin(), p.end(), '\\', '/');
    std::transform(p.begin(), p.end(), p.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return p.find("/sketchs/portable/") != std::string::npos ||
           p.find("/portable/") != std::string::npos;
}

// A generic Applesoft sketch lives under sketchs/basic_applesoft/ — Applesoft BASIC
// that runs on ANY Applesoft-capable machine. Opening one must NOT switch the user's
// profile: it follows whatever Applesoft machine is already live (see targetForPath /
// onTargetSelected). The user chooses the machine via the Mode switcher, not by
// opening a file.
static bool sourcePathLooksApplesoftSketch(const std::string& path)
{
    std::string p = path;
    std::replace(p.begin(), p.end(), '\\', '/');
    std::transform(p.begin(), p.end(), p.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return p.find("/basic_applesoft/") != std::string::npos;
}

// An Integer BASIC sketch lives under sketchs/basic_integer/. Integer BASIC has a
// single machine (the Apple-1 $E000 ROM, no graphics variants), so "machine-neutral"
// here means the same as for Applesoft: opening one keeps the user's current profile
// rather than forcing the Integer DevBench preset.
static bool sourcePathLooksIntegerSketch(const std::string& path)
{
    std::string p = path;
    std::replace(p.begin(), p.end(), '\\', '/');
    std::transform(p.begin(), p.end(), p.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return p.find("/basic_integer/") != std::string::npos;
}

int Pom1BenchHost::targetForPath(const std::string& path) const
{
    namespace fs = std::filesystem;
    std::string p = path;
    std::replace(p.begin(), p.end(), '\\', '/');
    std::transform(p.begin(), p.end(), p.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const std::string ext = fs::path(p).extension().string();
    // .txt/.hex/.apl/.mon — HexDumpFile.h owns the list.
    if (pom1::isHexDumpExtension(ext)) return 6;       // Wozmon hex quick-load

    // BASIC source. ".apf" = Applesoft (tokenised). The interpreter follows the
    // path: a TMS9918 path -> Applesoft TMS9918 (11), a GEN2/HGR path -> Applesoft
    // GEN2 (9), otherwise the stock microSD Applesoft (8). ".bas"/".ibas" = Integer
    // BASIC (idx 7, tokenised) -- see below.
    if (ext == ".apf") {
        // Generic Applesoft sketches (sketchs/basic_applesoft/) are machine-neutral:
        // follow whatever Applesoft-capable machine is already live so opening one
        // never switches the user's profile (see onTargetSelected). Path-tagged files
        // (under /gen2, /tms9918, …) still pin their own card.
        if (sourcePathLooksApplesoftSketch(p)) {
            if (mw_ && mw_->cardPlugged(pom1::CardId::Gen2)) return 9;    // Applesoft GEN2 ($9800)
            if (mw_ && mw_->cardPlugged(pom1::CardId::Tms9918)) return 11; // Applesoft TMS9918 ($4000)
            if (mw_ && mw_->cardPlugged(pom1::CardId::MicroSD)) return 8;  // Applesoft Lite + microSD
            return 10;                                        // Applesoft Lite (Apple-1, $E000)
        }
        const bool tmspath  = p.find("/tms9918") != std::string::npos ||
                              p.find("applesoft_tms9918") != std::string::npos ||
                              p.find("/codetank") != std::string::npos;
        const bool gen2path = p.find("/gen2") != std::string::npos ||
                              p.find("applesoft_gen2") != std::string::npos ||
                              p.find("/hgr") != std::string::npos ||
                              p.find("graphic hgr") != std::string::npos;
        return tmspath ? 11 : gen2path ? 9 : 8;
    }
    if (ext == ".bas" || ext == ".ibas") return 7;     // Integer BASIC (tokenised)

    // LOGO source (".logo"). Both LOGO targets are interpreter injections; follow the
    // live/path-tagged card: GEN2/HGR -> LOGO GEN2 (15), otherwise LOGO TMS9918 (14,
    // the primary CodeTank platform and the bare-Apple-1 default).
    if (ext == ".logo") {
        const bool gen2path = p.find("/gen2") != std::string::npos ||
                              p.find("/hgr") != std::string::npos ||
                              p.find("graphic hgr") != std::string::npos;
        const bool tmspath  = p.find("/tms9918") != std::string::npos ||
                              p.find("/codetank") != std::string::npos ||
                              p.find("/tool_logo") != std::string::npos;
        if (gen2path) return 15;
        if (tmspath)  return 14;
        if (mw_ && mw_->cardPlugged(pom1::CardId::Gen2)) return 15;   // LOGO GEN2 ($6000)
        return 14;                                        // LOGO TMS9918 ($4000)
    }

    const bool cMode   = (ext == ".c");
    const bool asmMode = (ext == ".s" || ext == ".asm");
    if (!cMode && !asmMode) return -1;                 // unknown type -> "do nothing"

    const bool tms  = p.find("/sketchs/tms9918") != std::string::npos ||
                      p.find("/tms9918") != std::string::npos ||
                      p.find("/tms9918c") != std::string::npos ||
                      p.find("/codetank") != std::string::npos ||
                      p.find("graphic tms9918") != std::string::npos;
    const bool gen2 = p.find("/sketchs/gen2") != std::string::npos ||
                      p.find("/gen2") != std::string::npos ||
                      p.find("/gen2c") != std::string::npos ||
                      p.find("/hgr") != std::string::npos ||
                      p.find("graphic hgr") != std::string::npos;

    // Portable (card-agnostic) sketch: don't impose a card — follow the one
    // that's already live so the build links the matching backend and the
    // current preset is left alone. A bare Apple-1 (no graphics card) gets TMS.
    // sourcePathLooksPortable wins over the tms/gen2 path hints below.
    int machine;
    if (sourcePathLooksPortable(p))
        machine = (mw_ && mw_->cardPlugged(pom1::CardId::Gen2)) ? 2 : 1;   // GEN2 if live, else TMS
    else
        machine = tms ? 1 : gen2 ? 2 : 0;                      // default = Apple-1
    return (cMode ? 3 : 0) + machine;                  // kP1Targets language-major order
}

void Pom1BenchHost::enableSketchSidecarCards(EmulationController* emu)
{
    if (!mw_ || !emu) return;
    if (sourcePathLooksGT6144(activeSourcePath_)) {
        mw_->showGT6144 = true;
        emu->setCardEnabled(pom1::CardId::Gt6144, true);
    }
}

// File-open / Run targeting: machine-neutral sketches keep the user's profile.
void Pom1BenchHost::onTargetSelected(int target) { applyTargetPreset(target, /*force=*/false); }

// Mode selector: an explicit user choice — always switch to the target's profile,
// then PREPARE its runtime so it's immediately usable: cold-start the matching
// BASIC interpreter (ROM loaded + prompt up, no program typed — that happens on
// Run), or probe the cc65 toolchain for asm/C. Returns a concise status (and, on a
// BASIC ROM failure, the console) for the bench to surface.
bench::BuildResult Pom1BenchHost::selectTargetExplicit(int target)
{
    bench::BuildResult r; r.showConsole = false;
    restoreRelaxedMachine();                       // clear any prior BASIC OOR/RAM relax
    applyTargetPreset(target, /*force=*/true);     // switch the profile (preset)
    if (target < 0 || target >= kP1TargetCount) { r.status = "bad target"; return r; }
    const P1T& t = kP1Targets[p1(target)];
    if (t.mode != 6) logoReplActive_ = false;      // leaving LOGO → REPL gone
    // An explicit Mode switch re-establishes the machine (applyTargetPreset force),
    // so any resident BASIC interpreter is void → the prep below cold-starts fresh.
    benchBasicResidentIdx_ = -1;

    if (t.mode == 4) {
        // BASIC: cold-start the matching interpreter (empty listing, no RUN) so its
        // prompt is ready. injectBasic loads the ROM, hard-resets, then cold-starts
        // the interpreter to its prompt (the empty-listing prep path).
        bench::BuildResult ib = injectBasic(target, std::string(), /*run=*/false);
        r.ok = ib.ok;
        r.status = ib.ok ? (std::string(t.label) + " — ready") : ib.status;
        if (!ib.ok) { r.console = ib.console; r.showConsole = true; }   // surface ROM errors
    } else if (t.mode == 6) {
        // LOGO: cold-start the resident interpreter to its `?` prompt (empty listing,
        // no RUN — the same prep path as BASIC), so it's ready for Run to poke procs.
        bench::BuildResult il = injectLogo(target, std::string(), /*run=*/false);
        r.ok = il.ok;
        r.status = il.ok ? (std::string(t.label) + " — ready") : il.status;
        if (!il.ok) { r.console = il.console; r.showConsole = true; }   // surface ROM errors
    } else if (t.mode == 0 || t.mode == 3 || t.mode == 5) {
        // asm / C / native BASIC: make the cc65 toolchain ready so Verify/Run work
        // immediately (mode 5 drives ca65/ld65 directly, same toolchain as asm).
        probe();
        r.ok = toolchainReady(target);
        const std::string hint = toolchainHint(target);
        r.status = std::string(t.label) + (hint.empty() ? "" : (" — " + hint));
    } else {
        r.status = t.label; r.ok = true;           // hex / other: nothing to prepare
    }
    return r;
}

void Pom1BenchHost::applyTargetPreset(int target, bool force)
{
    if (target < 0 || target >= kP1TargetCount) return;
    const P1T& t = kP1Targets[p1(target)];
    if (t.preset < 0 || t.preset == mw_->activePresetIndex) return;

    // Portable (card-agnostic) and machine-neutral BASIC sketches (Applesoft /
    // Integer) keep the user's CURRENT preset whenever it already provides the
    // target's machine (t.preset 2 = GEN2 HGR, 1 = TMS9918, 8 = microSD; preset 0 /
    // bare Apple-1 always has it) — so OPENING such a file never yanks the user off
    // their chosen profile. The Mode selector passes force=true to bypass this.
    if (!force && (sourcePathLooksPortable(activeSourcePath_)        ||
                   sourcePathLooksApplesoftSketch(activeSourcePath_) ||
                   sourcePathLooksIntegerSketch(activeSourcePath_))) {
        const bool haveCard = (t.preset == md::kPresetGen2Bench)    ? mw_->cardPlugged(pom1::CardId::Gen2)
                            : (t.preset == md::kPresetTMS9918Bench) ? mw_->cardPlugged(pom1::CardId::Tms9918)
                            : (t.preset == md::kPresetMicroSD)      ? mw_->cardPlugged(pom1::CardId::MicroSD)
                            : true;
        if (haveCard) return;
    }

    // The machine is about to be reprogrammed (cards swapped, hard reset): the
    // debug line table describes a program that will no longer be there, and
    // the line breakpoint armed through it dies with the reset — arming a new
    // one from the stale table would poke a breakpoint into whatever the new
    // machine runs. Drop both. Reached from the Mode selector AND from
    // opening a file that auto-targets a different machine; a Run rebuild
    // whose own onTargetSelected lands here re-adopts the fresh table right
    // after (build()'s run paths parse AFTER machine prep for this reason).
    dropDebugSession();

    // The bench is driving the preset change here — the user already picked a
    // target (which sets the bench's own sketch). Do not let the DevBench preset
    // auto-load overwrite that with the asm starter.
    mw_->suppressDevBenchAutoload = true;
    benchBasicResidentIdx_ = -1;   // a preset switch hard-resets → no resident BASIC
    mw_->applyMachineConfig(t.preset);
    mw_->suppressDevBenchAutoload = false;
}

// The bench echoes its status line (Opened/Saved/Build…) into the app's main
// status bar at the bottom of the window — full width, so long file paths that
// would overflow the narrow Bench window read cleanly. (Errors linger a touch.)
void Pom1BenchHost::onStatus(const std::string& msg, bool ok)
{
    if (msg.empty()) return;
    mw_->setStatusMessage(msg, ok ? 4.0f : 6.0f);
}

bench::ExampleLoad Pom1BenchHost::loadExample(int i)
{
    bench::ExampleLoad r;
    if (i < 0 || i >= kP1ExampleCount) { r.status = "bad example"; return r; }
    const P1Ex& e = kP1Examples[i];

    if (e.file) {
        const std::string found =
            pom1::ResourceLocator::defaultLocator().find(e.data).string();
        if (found.empty()) { r.status = std::string("Example not found (needs dev/): ") + e.data; return r; }
        std::ifstream in(found, std::ios::binary);
        r.source.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    } else {
        r.source = e.data;
    }
    extraAsset_     = e.asset ? e.asset : "";
    extraAssetAddr_ = e.addr;
    onTargetSelected(e.target);          // apply the example's machine
    // The snake example is a self-describing telemetry showcase — pop the
    // Telemetry Side Channel window so its schema-driven "Decoded state" table
    // is visible the moment the user loads it. Keyed on the source path so other
    // examples are untouched (a friend of MainWindow_ImGui reaches showTelemetry).
    if (e.data && std::strstr(e.data, "game_snake_telemetry")) {
        mw_->showTelemetry = true;
        mw_->emulation->setTelemetryEnabled(true);   // open the port so the
        // "Decoded state" table updates live without the user ticking Enabled
        // (a preset switch on Run does not disable telemetry, so this persists).
    }
    r.targetIndex = e.target;
    r.status = std::string("Example: ") + e.label;
    r.ok = true;
    return r;
}

bench::BuildResult Pom1BenchHost::verify(int target, const std::string& src, const std::string& addrHex)
{
    return build(target, src, addrHex, false);
}

bench::BuildResult Pom1BenchHost::upload(int target, const std::string& src, const std::string& addrHex)
{
    return build(target, src, addrHex, true);
}

bench::BuildResult Pom1BenchHost::directLoad(int target, const std::string& src, const std::string& /*addrHex*/)
{
    namespace fs = std::filesystem;
    bench::BuildResult r; r.showConsole = false;
    std::error_code ec;
    const fs::path dir = benchScratchDir(ec);
    if (ec || dir.empty()) { r.status = "no temp directory available"; r.ok = false; return r; }
    TempFileSweeper sweep;
    auto* emu = mw_->emulation.get();
    // Same deferred-plug fix as build() / pollBuild(): drain pending plugs so
    // the new preset's cards are on the bus before the CPU runs the freshly
    // loaded binary. Otherwise a `New` + `directLoad` (Wozmon hex mode)
    // immediately after a preset switch races the card-enable countdown and
    // the first frame of execution writes into RAM instead of the card.
    mw_->applyPendingCardConfiguration();
    std::string error; int bytesLoaded = 0; bool ok = false; uint16_t entry = 0;
    if (kP1Targets[p1(target)].mode == 1) {   // Wozmon hex
        const fs::path tmp = dir / "pom1_bench_sketch.txt";
        sweep.add(tmp);
        std::ofstream(tmp, std::ios::binary).write(src.data(), static_cast<std::streamsize>(src.size()));
        std::vector<std::pair<uint16_t, uint16_t>> zones;
        ok = emu->loadHexDump(tmp.string(), entry, error, &bytesLoaded, &zones);
    }
    if (ok) {
        emu->copySnapshot(mw_->uiSnapshot);
        char m[128]; std::snprintf(m, sizeof(m), "Uploaded %d B run @ $%04X", bytesLoaded, entry);
        r.status = m; r.ok = true;
    } else { r.status = "Upload failed: " + error; r.ok = false; }
    return r;
}

// BASIC deploy (mode 4): no toolchain. Bring up the interpreter's machine and
// cold-start the in-ROM interpreter so its zero page / vectors are set up, then
// COMPILE the listing ahead of time (Integer via ibasic::compile, Applesoft via
// basic::compile) into a memory image and load+launch it — no per-character
// keyboard typing. Pure C++, so byte-for-byte identical on desktop and WASM (no
// cc65, no async compile). Integer BASIC lives at $E000 (loaded by initMemory on
// reset); Applesoft Lite at $6000 (zeroed by the reset, so reloaded before 6000R).

// Undo the OOR/RAM relax a BASIC run applied (idx 8/10/11). No-op if nothing was
// relaxed, or if the preset has since changed (applyMachineConfig already reset
// RAM/OOR for the new preset — we'd otherwise restore a stale value).
void Pom1BenchHost::restoreRelaxedMachine()
{
    if (!injectRelaxed_) return;
    auto* emu = mw_ ? mw_->emulation.get() : nullptr;
    if (emu && mw_->activePresetIndex == injectRelaxedPreset_) {
        emu->setPresetRamKB(injectSavedRamKB_);
        emu->setOutOfRangeStrictMode(injectSavedOorStrict_);
        mw_->presetRamKB = injectSavedRamKB_;
        mw_->oorStrictModeEnabled = injectSavedOorStrict_;
    }
    injectRelaxed_ = false;
}

#if !POM1_IS_WASM
// BASIC native compile (mode 5, DESKTOP). basicnative::compile turns the Applesoft
// listing into ca65 assembly for a STANDALONE 6502 program (no interpreter, ~20x
// faster than the tokeniser). This mirrors tools/basicc_native.sh exactly:
//   1. write asmText to prog.s
//   2. ca65 -I dev/lib/<gen2|tms9918> -I dev/lib/apple1 -I dev/lib/basicrt  prog.s
//   3. derive -D RT_xxx from Result.runtimeFeatures (uppercased rt_* symbols) and
//      assemble the card runtime basicrt_<gen2|tms>.s with those defines
//   4. if usesFloat: assemble basicrt_float.s with -D FP_INT/FP_SQRT/FP_SIN for the
//      transcendentals the program imports (grep asmText)
//   5. TMS + draws (RT_HGR/RT_PLOT/RT_LINE/RT_HCOLOR): also assemble the VDP lib
//      (tms9918m2.asm + tms9918_pad.asm)
//   6. ld65 -C basicc_native.cfg  prog.o rt.o [fp.o] [vdp objs]
// The binary loads + runs at $0300 on BOTH cards (TMS draws to the VDP at $CC00/
// $CC01 but the CODE runs from $0300 RAM — it is NOT a CodeTank cartridge).
bench::BuildResult Pom1BenchHost::compileBasicNative(int target, const std::string& src, bool run)
{
    namespace fs = std::filesystem;
    bench::BuildResult r; r.showConsole = true;
    const P1T& t = kP1Targets[p1(target)];
    const bool gen2 = (t.preset == md::kPresetGen2Bench);   // GEN2 HGR card; else TMS9918 bench

    probe();
    if (!toolchainOk_) {
        r.console = std::string("cc65 (ca65/ld65) not found.\n") + kCc65InstallHint;
        r.status = "cc65 missing"; return r;
    }
    if (devRoot_.empty()) {
        r.console = "native BASIC compile needs the dev/ tree (dev/lib/basicrt + the "
                    "card runtime). Run from the cloned repo or a release bundle.\n";
        r.status = "dev/ tree missing"; return r;
    }
    const fs::path rtDir = fs::path(devRoot_) / "lib" / "basicrt";
    const fs::path cfg   = rtDir / "basicc_native.cfg";
    std::error_code ec;
    if (!fs::exists(cfg, ec)) {
        r.console = "linker cfg not found: " + cfg.string() + " (needs dev/lib/basicrt)\n";
        r.status = "basicc_native.cfg missing"; return r;
    }

    // 1) Compile the listing to ca65 asm via the native compiler.
    basicnative::Result nr = basicnative::compile(
        src, gen2 ? basicnative::Card::Gen2 : basicnative::Card::Tms);
    if (!nr.ok) {
        r.console = "[native compiler] " + nr.error + "\n";
        // Surface the offending line in the editor gutter. The native error names the
        // BASIC line number (e.g. 90); the gutter marker wants the PHYSICAL editor row,
        // so map it to the source row whose leading number == that BASIC line.
        if (const size_t lp = nr.error.find("line "); lp != std::string::npos) {
            int ln = 0;
            try { ln = std::stoi(nr.error.substr(lp + 5)); } catch (...) { ln = 0; }
            if (ln > 0) {
                int row = 0, phys = 0;
                std::istringstream ls(src); std::string line;
                while (std::getline(ls, line)) {
                    ++phys;
                    size_t k = 0; while (k < line.size() && (line[k] == ' ' || line[k] == '\t')) ++k;
                    int n = 0; bool dig = false;
                    while (k < line.size() && line[k] >= '0' && line[k] <= '9') { n = n * 10 + (line[k] - '0'); ++k; dig = true; }
                    if (dig && n == ln) { row = phys; break; }
                }
                r.errors.emplace_back(row > 0 ? row : ln, nr.error);
            }
        }
        r.status = "native compile failed (" + nr.error + ")"; r.ok = false; return r;
    }
    r.console = std::string("$ basicnative::compile (") + (gen2 ? "GEN2" : "TMS9918") + ")\n"
              + "[ok] " + std::to_string(nr.lineCount) + " lines -> ca65 asm"
              + (nr.usesFloat ? " (binary32 float)" : " (16-bit integer)") + "\n";

    const fs::path dir = benchScratchDir(ec);
    if (ec || dir.empty()) { r.console += "no temp directory available\n"; r.status = "no temp directory"; return r; }
    TempFileSweeper sweep;
    const fs::path progS = dir / "pom1_bench_native.s";
    const fs::path progO = dir / "pom1_bench_native_prog.o";
    const fs::path rtO   = dir / "pom1_bench_native_rt.o";
    const fs::path fpO   = dir / "pom1_bench_native_fp.o";
    const fs::path m2O   = dir / "pom1_bench_native_m2.o";
    const fs::path padO  = dir / "pom1_bench_native_pad.o";
    const fs::path binB  = dir / "pom1_bench_native.bin";
    sweep.add(progS); sweep.add(progO); sweep.add(rtO); sweep.add(fpO);
    sweep.add(m2O); sweep.add(padO); sweep.add(binB);
    std::ofstream(progS, std::ios::binary).write(nr.asmText.data(),
                  static_cast<std::streamsize>(nr.asmText.size()));

    // Shared -I flags: the card's equate lib + apple1 + basicrt (matches basicc_native.sh).
    const fs::path cardLib = fs::path(devRoot_) / "lib" / (gen2 ? "gen2" : "tms9918");
    const fs::path a1Lib   = fs::path(devRoot_) / "lib" / "apple1";
    const std::string I = " -I " + bench::shellQuote(cardLib.string()) +
                          " -I " + bench::shellQuote(a1Lib.string()) +
                          " -I " + bench::shellQuote(rtDir.string()) + " ";

    // 3) Derive -D RT_xxx from the rt_* runtime features the program imports, so the
    // card runtime assembles ONLY those routines (unused routines + tables drop).
    std::string rtDefs;
    for (std::string f : nr.runtimeFeatures) {
        if (f.rfind("rt_", 0) != 0) continue;          // skip fp_* (handled below)
        for (char& c : f) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        rtDefs += " -D " + f;
    }
    // VDP graphics lib is linked (TMS only) when the program actually draws.
    // Hi-res needs tms9918m2 (init_vdp_g2 / plot_set / line_xy); lo-res
    // (Multicolor) needs only tms9918_pad (tms9918_pad12). Both gate on pad.
    const bool drawsHires = rtDefs.find("RT_HGR")   != std::string::npos ||
                            rtDefs.find("RT_PLOT")  != std::string::npos ||
                            rtDefs.find("RT_LINE")  != std::string::npos ||
                            rtDefs.find("RT_HCOLOR")!= std::string::npos;
    const bool drawsLores = rtDefs.find("RT_GR")        != std::string::npos ||
                            rtDefs.find("RT_COLOR")     != std::string::npos ||
                            rtDefs.find("RT_LORESPLOT") != std::string::npos ||
                            rtDefs.find("RT_HLIN")      != std::string::npos ||
                            rtDefs.find("RT_VLIN")      != std::string::npos ||
                            rtDefs.find("RT_TEXT")      != std::string::npos ||
                            rtDefs.find("RT_HOME")      != std::string::npos;
    const bool draws = drawsHires || drawsLores;

    // GEN2 lo-res uses the Apple-II page-2 framebuffer ($0800-$0BFF), so a program
    // loaded at $0300 would overwrite itself once GR/PLOT paints there. Such programs
    // link + load at $0C00 (above both lo-res pages) via a dedicated cfg. HGR (frame-
    // buffer at $2000) and TMS (pixels in VRAM) keep the full $0300-$1FFF window.
    const bool gen2Lores = gen2 && drawsLores;
    const fs::path cfgSel = gen2Lores ? (rtDir / "basicc_native_gen2_lores.cfg") : cfg;
    const uint16_t loadAddr = gen2Lores ? 0x0C00 : 0x0300;
    if (gen2Lores && !fs::exists(cfgSel, ec)) {
        r.console += "linker cfg not found: " + cfgSel.string() + "\n";
        r.status = "basicc_native_gen2_lores.cfg missing"; return r;
    }

    auto step = [&](const std::string& cmd, const char* tag) -> bool {
        std::string out;
        const int rc = bench::runCapture(cmd, out);
        r.console += std::string("$ ") + tag + "\n" + out;
        if (rc != 0) {
            parseErrorMarkers(out, r.errors);
            r.console += humanizeCc65(out);
            r.status = std::string(tag) + " failed (see Build output)";
            return false;
        }
        return true;
    };

    // 2) Assemble the program.
    if (!step(bench::shellQuote(ca65_) + I + "-o " + bench::shellQuote(progO.string()) +
              " " + bench::shellQuote(progS.string()), "ca65 [program]"))
        return r;
    // 3) Assemble the minimal card runtime with the RT_xxx defines.
    const fs::path rtSrc = rtDir / (gen2 ? "basicrt_gen2.s" : "basicrt_tms.s");
    if (!step(bench::shellQuote(ca65_) + rtDefs + I + "-o " + bench::shellQuote(rtO.string()) +
              " " + bench::shellQuote(rtSrc.string()), "ca65 [runtime]"))
        return r;

    std::string linkObjs = bench::shellQuote(progO.string()) + " " + bench::shellQuote(rtO.string());

    // 4) Float runtime, gated transcendentals (-D FP_INT/FP_SQRT/FP_SIN) by what the
    // program imports (grep the generated asm, same as basicc_native.sh).
    if (nr.usesFloat) {
        std::string fpDefs;
        if (nr.asmText.find("fp_int")  != std::string::npos) fpDefs += " -D FP_INT";
        if (nr.asmText.find("fp_sqrt") != std::string::npos) fpDefs += " -D FP_SQRT";
        if (nr.asmText.find("fp_sin")  != std::string::npos) fpDefs += " -D FP_SIN";
        if (nr.asmText.find("fp_cos")  != std::string::npos) fpDefs += " -D FP_COS";
        if (nr.asmText.find("fp_atn")  != std::string::npos) fpDefs += " -D FP_ATN";
        if (nr.asmText.find("fp_rand") != std::string::npos) fpDefs += " -D FP_RAND";
        const fs::path fpSrc = rtDir / "basicrt_float.s";
        if (!step(bench::shellQuote(ca65_) + fpDefs + " -o " + bench::shellQuote(fpO.string()) +
                  " " + bench::shellQuote(fpSrc.string()), "ca65 [float runtime]"))
            return r;
        linkObjs += " " + bench::shellQuote(fpO.string());
    }

    // 5) TMS only, and only if the program draws: the VDP graphics lib. Lo-res
    // (Multicolor) needs only tms9918_pad (tms9918_pad12); hi-res also links
    // tms9918m2 (init_vdp_g2 / plot_set / line_xy).
    if (!gen2 && draws) {
        const std::string tI = " -I " + bench::shellQuote(cardLib.string()) +
                               " -I " + bench::shellQuote(a1Lib.string()) + " ";
        const fs::path padSrc = cardLib / "tms9918_pad.asm";
        if (!step(bench::shellQuote(ca65_) + tI + "-o " + bench::shellQuote(padO.string()) +
                  " " + bench::shellQuote(padSrc.string()), "ca65 [tms9918_pad]"))
            return r;
        if (drawsHires) {
            const fs::path m2Src = cardLib / "tms9918m2.asm";
            if (!step(bench::shellQuote(ca65_) + tI + "-o " + bench::shellQuote(m2O.string()) +
                      " " + bench::shellQuote(m2Src.string()), "ca65 [tms9918m2]"))
                return r;
            linkObjs += " " + bench::shellQuote(m2O.string());
        }
        linkObjs += " " + bench::shellQuote(padO.string());
    }

    // 6) Link the standalone binary (loads + runs at $0300, or $0C00 for GEN2 lo-res).
    char addrTag[8]; std::snprintf(addrTag, sizeof(addrTag), "$%04X", loadAddr);
    if (!step(bench::shellQuote(ld65_) + " -C " + bench::shellQuote(cfgSel.string()) +
              " -o " + bench::shellQuote(binB.string()) + " " + linkObjs, "ld65"))
        return r;
    r.console += std::string("[ok] assembled + linked (native, run @ ") + addrTag + ")\n";

    if (!run) { r.status = "Verify OK"; r.ok = true; return r; }

    // Deploy: switch to the target's card profile, drain the deferred card plug so
    // the GEN2/TMS card is on the bus before the CPU runs, then loadBinary resets +
    // runs at $0300 (NOT a CodeTank flash — the code runs from $0300 RAM).
    if (t.preset >= 0) onTargetSelected(target);
    mw_->applyPendingCardConfiguration();
    auto* emu = mw_->emulation.get();
    if (!emu) { r.status = "no emulator"; r.ok = false; return r; }

    // The standalone native image owns contiguous low RAM ($0300-$1FFF per the cfg),
    // but the TMS9918 (CodeTank) preset is the 8 KB Parmigiani dual-bank — RAM only at
    // $0000-$0FFF — so any program past $0FFF reads $FF under strict OOR and crashes.
    // Relax to 16 KB low RAM ($0000-$3FFF, strict left ON) so the binary's full window
    // is backed (mirrors the BASIC tokeniser path for this preset). Done unconditionally
    // for the TMS native target (preset 1 is always the 8 KB dual-bank) so it never
    // depends on the UI RAM mirror being current. GEN2 (48 KB) is already contiguous;
    // restoreRelaxedMachine() (called by the next build()) reverts.
    if (!gen2) {
        if (!injectRelaxed_) {
            injectSavedRamKB_     = mw_->presetRamKB;
            injectSavedOorStrict_ = mw_->oorStrictModeEnabled;
        }
        injectRelaxed_       = true;
        injectRelaxedPreset_ = mw_->activePresetIndex;
        emu->setOutOfRangeStrictMode(true);
        emu->setPresetRamKB(16);
        mw_->oorStrictModeEnabled = true;
        mw_->presetRamKB = 16;
    }

    if (gen2) mw_->showGraphicsCard = true;
    else if (!mw_->showTMS9918) mw_->showTMS9918 = true;
    std::string error; int bytesLoaded = 0;
    if (emu->loadBinary(binB.string(), loadAddr, error, &bytesLoaded)) {
        emu->copySnapshot(mw_->uiSnapshot);
        char msg[160]; std::snprintf(msg, sizeof(msg),
            "Built %d B (native %s) run @ %s", bytesLoaded, gen2 ? "GEN2" : "TMS9918", addrTag);
        r.status = msg; r.ok = true;
    } else { r.status = "load failed: " + error; r.ok = false; }
    return r;
}
#endif // !POM1_IS_WASM

bench::BuildResult Pom1BenchHost::build(int target, const std::string& src, const std::string& addrHex, bool run)
{
    bench::BuildResult r;
    if (target < 0 || target >= kP1TargetCount) { r.status = "bad target"; return r; }
    const P1T& t = kP1Targets[p1(target)];

    // Undo any OOR/RAM relax a previous BASIC run left on this preset before doing
    // anything else, so an asm/hex/C build never inherits the loosened 64 KB view.
    // (A BASIC target re-applies the relax inside injectBasic.)
    restoreRelaxedMachine();

    // Any build invalidates the previous build's line table — the addresses a
    // source line mapped to are about to move. Drop it AND the line breakpoint
    // armed through it (only ours: the Debug window's own breakpoint, armed at
    // a different address, is left alone — breakpointLine() already reports
    // none when the CPU breakpoint isn't the one we set). The armed LINE is
    // remembered so a successful build can re-arm it against the fresh table:
    // without that, the natural debug loop — Verify, arm a breakpoint, Run —
    // silently disarmed the breakpoint during Run's rebuild and the program
    // sailed past it (and even a plain re-Verify lost it).
    // beginRebuild drops the table AND remembers the armed line inside the
    // session, so the intent survives the dozen ways this build can fail
    // before it links; only a machine change (invalidate) drops it.
    if (dbg_.beginRebuild(machineBp()) >= 0) {
        if (auto* emuBp = mw_->emulation.get())
            emuBp->clearCpuBreakpoint();     // ours, and its address is moving
    }
    // Re-arm the remembered line against the FRESH table. Must run after the
    // LAST machine reset of its path: loadBinary() and hardReset() both reach
    // M6502::reset(), which clears the CPU breakpoint — re-arming at parse
    // time would be undone by the load. Verify touches no machine state, so
    // its call site re-arms immediately after the parse. If the line no
    // longer resolves (code deleted), the breakpoint stays cleared — visibly,
    // since the gutter marker follows breakpointLine().
    [[maybe_unused]] auto rearmDbgBreakpoint = [&](bench::BuildResult& res) {
        auto* emuRe = mw_->emulation.get();
        if (!emuRe)
            return;
        const auto re = dbg_.rearm();
        if (!re.ok)
            return;
        emuRe->setCpuBreakpoint(re.address);
        char note[96];
        std::snprintf(note, sizeof(note),
                      "[ok] breakpoint re-armed at line %d ($%04X)\n",
                      re.line, re.address);
        res.console += note;
    };

    // Any non-LOGO build reprograms/hard-resets the machine → a resident LOGO REPL
    // is gone. (LOGO's own mode 6 manages the flag inside injectLogo.)
    if (t.mode != 6) logoReplActive_ = false;
    // Likewise a non-BASIC build voids any resident BASIC interpreter, so the next
    // Warm start can't re-enter it. (mode 4 manages benchBasicResidentIdx_ itself.)
    if (t.mode != 4) benchBasicResidentIdx_ = -1;

    BuildLogMeta logMeta;
    logMeta.action = run ? "run" : "verify";
    logMeta.target = &t;
    logMeta.sourcePath = activeSourcePath_;
#if POM1_IS_WASM
    logMeta.host = "wasm";
#else
    logMeta.host = "desktop";
#endif
    BuildLogFinalizer logFin{r, logMeta};

    if (t.mode == 1) {     // Wozmon hex: no compile
        if (!run) { r.status = "Nothing to verify (hex)"; r.showConsole = false; return r; }
        return directLoad(target, src, addrHex);
    }

    if (t.mode == 4) {     // BASIC: tokenise/compile the listing host-side and load
        return injectBasic(target, src, run);   // the image — no typing (WASM too).
    }

    if (t.mode == 6) {     // LOGO: poke the procedure table + feed one entry line
        return injectLogo(target, src, run);    // (no per-line typing — WASM too).
    }

    if (t.mode == 5) {     // BASIC native compile -> standalone 6502, no interpreter.
#if POM1_IS_WASM
        // The native compile drives the bundled ca65/ld65 binaries directly; the
        // in-browser cc65 (POM1cc65) path is a follow-up. Not exposed on WASM
        // (targetFor returns -1 there), but guard the dispatch all the same.
        r.console = "Native BASIC compile is desktop-only for now "
                    "(the in-browser cc65 native path is a follow-up).\n";
        r.status = "native compile is desktop-only"; r.ok = false; return r;
#else
        return compileBasicNative(target, src, run);
#endif
    }

#if POM1_IS_WASM
    // In-browser cc65 (build-wasm/cc65, driven by window.POM1cc65). The compile is
    // an async JS Promise, so kick it off here and return pending=true; CodeBench
    // then drives pollBuild() each frame until the .bin is ready. C targets (need
    // cc65's version-matched runtime libs) + the TMS9918 ROM-flash asm target stay
    // desktop-only — available() doesn't expose them on WASM, so only mode-0 asm
    // (dual-4k / GEN2 HGR / GEN2 TXT) reaches here.
    if (EM_ASM_INT({ return (window.POM1cc65 && window.POM1cc65.available()) ? 1 : 0; }) == 0) {
        r.console = "In-browser cc65 not available yet (build-wasm/cc65 missing, or POM1 "
                    "still loading). Reload once the page has finished loading.\n";
        r.status = "web cc65 not ready"; return r;
    }
    if (t.mode == 3) {
        // C target: a per-target file spec (cfg + runtime lib .c/.s + include dirs)
        // matching the desktop cl65 command, fed to POM1cc65.buildC. pollBuild then
        // loadBinary+runs (or ROM-flashes the TMS9918 C target) just like asm.
        const std::string cfgTag = t.cfg ? t.cfg : "";
        const bool wPlain = (cfgTag == "C-plain");
        const bool wGen2  = (cfgTag == "C-gen2");   // else: "C" = TMS9918 CodeTank ROM
        // Spec = /dev/bench/<target>.json from MEMFS (preloaded with the dev/
        // tree), the compiled-in copy as fallback. buildC gets the raw JSON text.
        BenchCSpec bspec = loadBenchCSpec("/dev",
            wPlain ? "apple1c" : wGen2 ? "gen2c" : "tms9918c",
            wPlain ? kBenchCSpecApple1c : wGen2 ? kBenchCSpecGen2c : kBenchCSpecTms9918c);
        std::string spec = bspec.rawJson;
        if (!wPlain && !wGen2) {
            // TMS9918: fold the sketch's EXTRA_ASM modules into userAsm — NOT
            // asmSources. asmSources are archived and dead-stripped; a sketch's
            // own modules must link as direct objects so they survive even when
            // no symbol references them (fixed-segment data, IRQ stubs, …).
            const AsmProjectCtx proj = probeSketchProject(activeSourcePath_);
            bool added = false;
            for (const std::string& ea : proj.extraAsm) {
                const std::string wp = benchAbsToWasmDev(ea);
                if (wp.empty()) continue;
                bspec.userAsm.push_back({wp, std::filesystem::path(ea).filename().string()});
                added = true;
            }
            if (added) spec = benchCSpecSerialize(bspec);
        }
        const std::string cfg = bspec.cfg;   // "/dev/..." path, MEMFS-visible
        uint16_t entry = parseCfgLoadAddr(cfg);
        if (entry == 0) entry = wPlain ? 0x0300 : wGen2 ? 0x6000 : 0x4000;
        wasmJobActive_ = true; wasmJobVerifyOnly_ = !run;
        wasmJobTarget_ = target; wasmJobEntry_ = entry;
        EM_ASM({
            var src = UTF8ToString($0);
            var spec = JSON.parse(UTF8ToString($1));
            var FS = Module.FS;
            Module.__benchJob = ({ state: 'running', code: -1 });
            window.POM1cc65.buildC(src, spec)
                .then(function (res) {
                    try { FS.writeFile('/tmp/pom1_bench.bin', res.bin || new Uint8Array(0)); } catch (e) {}
                    try { FS.writeFile('/tmp/pom1_bench.log', res.log || ""); } catch (e) {}
                    Module.__benchJob = ({ state: 'done', code: res.code | 0 });
                })
                .catch(function (e) {
                    try { FS.writeFile('/tmp/pom1_bench.log', 'web cc65 error: ' + (e && e.stack || e)); } catch (_) {}
                    Module.__benchJob = ({ state: 'done', code: 99 });
                });
        }, src.c_str(), spec.c_str());
        r.pending = true; r.showConsole = true;
        logMeta.toolchain = "wasm-cc65";
        logMeta.cfgPath = cfg;
        r.console = "Compiling C with in-browser cc65 (WASM)…\n";
        r.status = run ? "Building (web cc65 C)…" : "Compiling (web cc65 C)…";
        return r;
    }
    {
        // Prefer the loaded sketch's own linker cfg + extra modules + defines —
        // probeSketchProject reads /sketchs/<dir>/.sketch.json from MEMFS (now
        // preloaded), so multi-module CodeTank sketches (e.g. TMS LOGO, with
        // -D CODETANK_BUILD) build in-browser exactly like desktop. Fall back to
        // the target's default /dev/cc65 cfg for a bare editor snippet.
        const AsmProjectCtx proj = probeSketchProject(activeSourcePath_);
        auto memfsAbs = [](std::string p) {
            if (!p.empty() && p[0] != '/') p = "/" + p; return p;
        };
        std::string cfg;
        std::string specExtra;   // ,"asmSources":[...],"defines":[...],"sketchDir":"..."
        if (proj.ok && !proj.cfg.empty()) {
            cfg = memfsAbs(proj.cfg);
            std::string srcs = "[";
            for (size_t i = 0; i < proj.extraAsm.size(); ++i) {
                const std::string p = memfsAbs(proj.extraAsm[i]);
                const std::string name = std::filesystem::path(p).filename().string();
                srcs += (i ? "," : "") + std::string("{\"path\":\"") + p + "\",\"name\":\"" + name + "\"}";
            }
            srcs += "]";
            std::string defs = "[";
            for (size_t i = 0; i < proj.defines.size(); ++i)
                defs += (i ? "," : "") + std::string("\"") + proj.defines[i] + "\"";
            defs += "]";
            std::string incs = "[";
            for (size_t i = 0; i < proj.incDirs.size(); ++i)
                incs += (i ? "," : "") + jsonQuoted(memfsAbs(proj.incDirs[i]));
            incs += "]";
            specExtra = ",\"asmSources\":" + srcs + ",\"defines\":" + defs
                      + ",\"incDirs\":" + incs
                      + ",\"sketchDir\":\"" + memfsAbs(proj.dir.string()) + "\"";
        } else {
            cfg = std::string("/dev/cc65/") + (t.cfg ? t.cfg : "");
        }
        uint16_t entry = parseCfgLoadAddr(cfg);   // std::ifstream works on MEMFS
        if (entry == 0) { try { entry = static_cast<uint16_t>(std::stoul(addrHex, nullptr, 16)); } catch (...) { entry = 0x0300; } }
        wasmJobActive_ = true; wasmJobVerifyOnly_ = !run;
        wasmJobTarget_ = target; wasmJobEntry_ = entry;
        logMeta.toolchain = "wasm-cc65";
        logMeta.cfgPath = cfg;
        const std::string spec = std::string("{\"cfg\":\"") + cfg + "\"" + specExtra + "}";
        // Mirror the desktop libFlags_ (-I every dev/lib subdir) in JS + the sketch
        // dir, then drive POM1cc65.buildAsm with the sketch's cfg/asmSources/defines.
        // On resolve write .bin + .log into POM1's MEMFS where pollBuild() reads them.
        // NB: no top-level commas in this EM_ASM body — the C preprocessor would
        // split them as macro args (only () protects commas, not {} or []). So
        // multi-var decls are separate statements and bare object literals are
        // parenthesised; commas inside (...) calls are already safe.
        EM_ASM({
            var src = UTF8ToString($0);
            var spec = JSON.parse(UTF8ToString($1));
            var FS = Module.FS;
            var incDirs = [];
            // RECURSIVE walk of /dev/lib — mirror the desktop libFlags_ so nested
            // lib dirs (games/chess, games/sokoban, games/rogue, gen2/sprites,
            // tms9918c, ...) are on the ca65 -I search path. A shallow readdir
            // (first level only) made every sketch that .include's a nested
            // common/sprite file fail to assemble in the browser.
            var walk = function (dir) {
                var ents;
                try { ents = FS.readdir(dir); } catch (e) { return; }
                incDirs.push(dir);
                for (var i = 0; i < ents.length; i++) {
                    var n = ents[i];
                    if (n === '.' || n === '..') continue;
                    var p = dir + '/' + n;
                    try { if (FS.isDir(FS.stat(p).mode)) walk(p); } catch (e) {}
                }
            };
            walk('/dev/lib');
            if (spec.sketchDir) incDirs.push(spec.sketchDir);
            if (spec.incDirs) incDirs = incDirs.concat(spec.incDirs);
            Module.__benchJob = ({ state: 'running', code: -1 });
            window.POM1cc65.buildAsm(src, ({ cfg: spec.cfg, incDirs: incDirs, asmSources: spec.asmSources || [], defines: spec.defines || [] }))
                .then(function (res) {
                    try { FS.writeFile('/tmp/pom1_bench.bin', res.bin || new Uint8Array(0)); } catch (e) {}
                    try { FS.writeFile('/tmp/pom1_bench.log', res.log || ""); } catch (e) {}
                    Module.__benchJob = { state: 'done', code: res.code | 0 };
                })
                .catch(function (e) {
                    try { FS.writeFile('/tmp/pom1_bench.log', 'web cc65 error: ' + (e && e.stack || e)); } catch (_) {}
                    Module.__benchJob = { state: 'done', code: 99 };
                });
        }, src.c_str(), spec.c_str());
        r.pending = true; r.showConsole = true;
        logMeta.toolchain = "wasm-cc65";
        r.console = "Compiling with in-browser cc65 (WASM)…\n";
        r.status = run ? "Building (web cc65)…" : "Compiling (web cc65)…";
        return r;
    }
#else
    probe();
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir  = fs::temp_directory_path(ec);
    if (ec || dir.empty()) {
        r.console = "fs::temp_directory_path failed\n"; r.status = "no temp directory"; return r;
    }
    TempFileSweeper sweep;
    const fs::path binB = dir / "pom1_bench.bin";
    sweep.add(binB);
    // Debug-info staging paths + parser, at FUNCTION scope on purpose: the run
    // paths may re-parse AFTER their machine prep — onTargetSelected can apply
    // a preset (applyMachineConfig → hardReset), which rightly invalidates
    // the line table (which described a program the reset just destroyed), so
    // the freshly linked table must be re-adopted afterwards. Idempotent: a
    // second call with the table already adopted is a no-op, and a missing
    // .dbg (non-asm modes) is silently skipped.
    const fs::path dbgSrcS = dir / "pom1_bench.s";
    const fs::path dbgFileP = dir / "pom1_bench.dbg";
    // The .dbg TEXT is read once (right after the link, when the file is
    // freshly written) and kept: the late re-parse must not depend on the
    // file still being on disk and unchanged. That window is long — a preset
    // apply, card plugs, a ROM flash — and the path is a FIXED name in the
    // shared temp dir, so a second POM1 instance building at the same moment
    // could have replaced or swept it underneath us.
    std::string dbgFileText;
    // Set by the asm branch once it has actually asked ld65 for a --dbgfile.
    // Without this, a stray /tmp/pom1_bench.dbg — another POM1 instance's, or
    // one an interrupted build left behind — would be adopted by a target that
    // never requested debug info (the C paths share the run code below and call
    // adoptDbgInfo too), decorating a C program with an asm program's lines.
    bool dbgFileExpected = false;
    // Why the table could not be adopted, in the user's words. parseDbgFile
    // writes real diagnostics ("was the source assembled with ca65 -g?") and
    // they used to be dropped on the floor by three silent `return`s: when
    // source-level debugging failed, the breakpoint button simply never
    // appeared and nothing anywhere said why.
    std::string dbgUnavailable;
    auto adoptDbgInfo = [&]() {
        if (dbg_.hasLineInfo() || !dbgFileExpected)
            return;
        if (dbgFileText.empty()) {
            std::ifstream df(dbgFileP);
            if (!df) {
                dbgUnavailable = "ld65 wrote no debug file";
                return;
            }
            std::stringstream dss;
            dss << df.rdbuf();
            dbgFileText = dss.str();
        }
        if (dbgFileText.empty()) {
            dbgUnavailable = "the debug file is empty";
            return;
        }
        pom1::DbgLineInfo parsed = pom1::parseDbgFile(dbgFileText, dbgSrcS.string());
        if (!parsed.ok) {
            dbgUnavailable = parsed.error;
            return;
        }
        dbgUnavailable.clear();
        r.console += "[ok] debug info: "
            + std::to_string(parsed.lineToAddr.size())
            + " source lines mapped\n";
        if (mw_->memoryViewer) {
            mw_->memoryViewer->resetSymbolsToDefaults();
            for (const auto& l : parsed.labels)
                mw_->memoryViewer->addSymbol(l.first, l.second);
        }
        dbg_.adopt(std::move(parsed));
    };
    // Say WHY source-level debugging is unavailable, once, right after the
    // link that was supposed to produce it. Silence here reads as "this build
    // has no debugger" with no way to tell a toolchain problem from a missing
    // feature. Only reached on the asm path, which is the one that asks for a
    // --dbgfile at all.
    auto reportDbgUnavailable = [&](bench::BuildResult& res) {
        if (dbg_.hasLineInfo() || dbgUnavailable.empty())
            return;
        res.console += "[warn] source-level debugging unavailable: "
                     + dbgUnavailable + "\n";
        dbgUnavailable.clear();      // reported once per build
    };
    const std::string cfgTag = t.cfg ? t.cfg : "";
    const bool cmode  = (t.mode == 3);
    const bool gen2c  = cmode && cfgTag == "C-gen2";    // GEN2 HGR (loadBinary @ $6000)
    const bool plainc = cmode && cfgTag == "C-plain";   // plain text (loadBinary @ $0300)
    // (cmode && !gen2c && !plainc) is the TMS9918 C target; its deploy goes through
    // the shared t.codetankRom path below, same as the TMS9918 asm target.
    r.showConsole = true;
    logMeta.toolchain = cmode ? "cl65" : "ca65+ld65";
    uint16_t entry = 0;

    if (cmode) {
        const bool ready = gen2c ? gen2COk_ : plainc ? plainCOk_ : cl65Ok_;
        if (!ready) {
            r.console = gen2c ? "cl65 / gen2c lib not found (needs dev/)\n"
                      : plainc ? "cl65 / apple1c lib not found (needs dev/)\n"
                               : "cl65 / tms9918c runtime not found (needs dev/lib/tms9918c + dev/lib/gfx)\n";
            if (!cl65_.empty() && !devRoot_.empty()) {
                if (videocardLib_.empty() || codetankCfg_.empty())
                    r.console += "  missing: dev/lib/tms9918c (or cc65/codetank_c.cfg)\n";
                if (gfxLib_.empty())
                    r.console += "  missing: dev/lib/gfx\n";
            }
            r.console += "Release packages bundle cc65 + the dev/ tree, so asm AND C build "
                         "out of the box; for a source build, install cc65 and run from the "
                         "cloned repo so dev/ resolves.\n";
            r.console += kCc65InstallHint;
            r.status = "cc65 cl65 missing"; return r;
        }
        const fs::path srcC = dir / "pom1_bench.c";
        sweep.add(srcC);
        std::ofstream(srcC, std::ios::binary).write(src.data(), static_cast<std::streamsize>(src.size()));
        const char* tag = gen2c ? "GEN2 HGR" : plainc ? "Apple-1 text" : "CodeTank ROM";
        // The per-target build spec — dev/bench/<target>.json (the editable
        // source of truth, shared with the WASM path) with the compiled-in
        // kBenchCSpec* copy as fallback. The full cl65 command (defines, cfg,
        // -I dirs, runtime .c/.s lists) is derived from it by benchCSpecCl65Cmd.
        const BenchCSpec spec = loadBenchCSpec(devRoot_,
            gen2c ? "gen2c" : plainc ? "apple1c" : "tms9918c",
            gen2c ? kBenchCSpecGen2c : plainc ? kBenchCSpecApple1c : kBenchCSpecTms9918c);
        std::string cfgAbs = benchDevAbs(devRoot_, spec.cfg);
        std::string extraObjs;
        if (!gen2c && !plainc) {
            // TMS9918: a real sketch's own linker cfg (sidecar .sketch.json)
            // overrides the spec default, and its EXTRA_ASM modules are
            // pre-assembled and appended to the link.
            const AsmProjectCtx proj = probeSketchProject(activeSourcePath_);
            if (proj.ok && !proj.cfg.empty()) cfgAbs = proj.cfg;
            int xn = 0;
            for (const std::string& ea : proj.extraAsm) {
                const fs::path eo = dir / ("pom1_bench_x" + std::to_string(xn++) + ".o");
                sweep.add(eo);
                std::string eout;
                const std::string eca = bench::shellQuote(ca65_) + " " + libFlags_ +
                    bench::shellQuote(ea) + " -o " + bench::shellQuote(eo.string());
                if (bench::runCapture(eca, eout) != 0) {
                    parseErrorMarkers(eout, r.errors);
                    r.console = std::string("$ cl65 -t none [") + tag + "]\n$ ca65 [" +
                        fs::path(ea).filename().string() + "]\n" + eout + humanizeCc65(eout);
                    r.status = "ca65 failed (extraAsm, see Build output)"; return r;
                }
                extraObjs += " " + bench::shellQuote(eo.string());
            }
        }
        // Spec-declared user-side modules ("userAsm"): assembled and linked as
        // DIRECT objects like EXTRA_ASM — never archived, so they survive even
        // when no symbol references them (fixed-segment data, IRQ stubs, …).
        int un = 0;
        for (const auto& ua : spec.userAsm) {
            const fs::path uo = dir / ("pom1_bench_u" + std::to_string(un++) + ".o");
            sweep.add(uo);
            std::string uout;
            const std::string uca = bench::shellQuote(ca65_) + " " + libFlags_ +
                bench::shellQuote(benchDevAbs(devRoot_, ua.path)) + " -o " + bench::shellQuote(uo.string());
            if (bench::runCapture(uca, uout) != 0) {
                parseErrorMarkers(uout, r.errors);
                r.console = std::string("$ cl65 -t none [") + tag + "]\n$ ca65 [" +
                    fs::path(ua.path).filename().string() + "]\n" + uout + humanizeCc65(uout);
                r.status = "ca65 failed (userAsm, see Build output)"; return r;
            }
            extraObjs += " " + bench::shellQuote(uo.string());
        }
        logMeta.cfgPath = cfgAbs;
        // Archive-based link (dead-strip; see the LINK MODEL comment above the
        // specs). A runtime-module COMPILE error is surfaced like any build
        // error — the user may be live-editing a lib source. Infrastructure
        // failures (no ar65, unwritable cache) fall back to the historical
        // single-command force-link so the Bench still builds, just bigger.
        std::string cmd;
        if (!ar65_.empty()) {
            const BenchRtLib rt = benchEnsureRtLib(spec, devRoot_, cl65_, ar65_,
                gen2c ? "gen2c" : plainc ? "apple1c" : "tms9918c");
            if (rt.hardError) {
                parseErrorMarkers(rt.console, r.errors);
                r.console = std::string("$ cl65 -t none [") + tag + "]\n" + rt.console + humanizeCc65(rt.console);
                r.status = "cl65 failed (runtime lib, see Build output)"; return r;
            }
            if (rt.ok) {
                if (!rt.console.empty()) r.console += rt.console;
                cmd = benchCSpecLinkCmd(spec, devRoot_, cl65_, cfgAbs,
                                        srcC.string(), extraObjs, rt.libPath, binB.string());
            } else if (!rt.console.empty()) {
                r.console += "[rtlib] " + rt.console + "[rtlib] falling back to force-link\n";
            }
        }
        if (cmd.empty())
            cmd = benchCSpecCl65Cmd(spec, devRoot_, cl65_, cfgAbs,
                                    srcC.string(), extraObjs, binB.string());
        std::string out;
        const int rc = bench::runCapture(cmd, out);
        r.console += std::string("$ cl65 -t none [") + tag + "]\n" + out;
        if (rc != 0) { parseErrorMarkers(out, r.errors); r.console += humanizeCc65(out); r.status = "cl65 failed (see Build output)"; return r; }
        r.console += std::string("[ok] compiled + linked (") + tag + ")\n";
        entry = (gen2c || plainc) ? parseCfgLoadAddr(cfgAbs) : 0x4000;
        if (entry == 0) entry = plainc ? 0x0300 : 0x6000;
    } else {
        if (!toolchainOk_) { r.console = std::string("cc65 (ca65/ld65) not found.\n") + kCc65InstallHint; r.status = "cc65 missing"; return r; }
        const fs::path& srcS = dbgSrcS;               // staged editor buffer
        const fs::path objO = dir / "pom1_bench.o";
        const fs::path& dbgP = dbgFileP;              // ld65 --dbgfile output
        sweep.add(srcS); sweep.add(objO); sweep.add(dbgP);
        // Drop a stale .dbg from a previous build NOW: this build could fail
        // before ld65 runs, and a later adoptDbgInfo() call (run-path re-parse)
        // must never adopt the previous program's table.
        fs::remove(dbgP, ec);
        dbgFileExpected = true;   // this build asks ld65 for one (see the lambda)
        std::ofstream(srcS, std::ios::binary).write(src.data(), static_cast<std::streamsize>(src.size()));
        // If the editor's file is a real sketch or multi-file project source (sidecar
        // .sketch.json or sibling Makefile), build it in context: its own cfg,
        // and the EXTRA_ASM siblings. Empty path / no Makefile -> bare sketch path.
        const AsmProjectCtx proj = probeAsmProject(activeSourcePath_);
        // -g keeps line + symbol info in every object so ld65's --dbgfile can
        // emit the table adoptDbgInfo() parses. Costs object size only — the
        // linked binary is byte-identical.
        std::string asmFlags = "-g " + libFlags_;
        std::string extraObjs;   // " obj ..." appended to the ld65 link line
        std::string cfgPath;
        if (proj.ok) {
            cfgPath  = proj.cfg;
            logMeta.cfgPath = cfgPath;
            logMeta.projValid    = proj.ok;
            logMeta.projDualBank = proj.dualBank;
            logMeta.projLoAddr   = proj.loAddr;
            logMeta.projHiAddr   = proj.hiAddr;
            asmFlags += "-I " + bench::shellQuote(proj.dir.string()) + " ";
            for (const std::string& id : proj.incDirs)
                asmFlags += "-I " + bench::shellQuote(id) + " ";
            for (const std::string& d : proj.defines)
                asmFlags += "-D " + bench::shellQuote(d) + " ";
            int n = 0;
            for (const std::string& ea : proj.extraAsm) {
                const fs::path eo = dir / ("pom1_bench_x" + std::to_string(n++) + ".o");
                sweep.add(eo);
                std::string eout;
                const std::string eca = bench::shellQuote(ca65_) + " " + asmFlags +
                    bench::shellQuote(ea) + " -o " + bench::shellQuote(eo.string());
                if (bench::runCapture(eca, eout) != 0) {
                    parseErrorMarkers(eout, r.errors);
                    r.console += "$ ca65 [" + fs::path(ea).filename().string() + "]\n" + eout + humanizeCc65(eout);
                    r.status = "ca65 failed (EXTRA_ASM, see Build output)"; return r;
                }
                extraObjs += " " + bench::shellQuote(eo.string());
            }
        } else if (!t.cfg[0]) {
            const fs::path e2 = dir / "pom1_bench_default.cfg";
            sweep.add(e2);
            std::ofstream(e2, std::ios::binary) << kBenchEmbeddedCfg;
            cfgPath = e2.string();
        } else {
            // Prefer the dev/ tree probe() resolved (covers source + bundled
            // layouts), then fall back to a cwd-relative search.
            if (!devRoot_.empty()) {
                const fs::path p = fs::path(devRoot_) / "cc65" / t.cfg;
                if (fs::exists(p, ec)) cfgPath = fs::absolute(p, ec).string();
            }
            if (cfgPath.empty()) {
                const fs::path p = pom1::ResourceLocator::defaultLocator()
                    .find(std::string("dev/cc65/") + t.cfg);
                if (!p.empty()) cfgPath = fs::absolute(p, ec).string();
            }
            if (cfgPath.empty()) { r.console = std::string("linker cfg not found (needs dev/): ") + t.cfg + "\n"; r.status = "ld65 cfg missing"; return r; }
        }
        if (!cfgPath.empty() && logMeta.cfgPath.empty())
            logMeta.cfgPath = cfgPath;
        // Pasted-source convenience (no sketch/Makefile context — proj.ok
        // handles opened sketches via their EXTRA_ASM): auto-link the shared
        // dev/lib/tms9918 modules the source references. Every TMS sketch
        // paces its VDP accesses with `JSR tms9918_pad18/pad12` and the
        // canonical exits use vdp_display_off, so a bare paste otherwise
        // dies at ld65 with unresolved externals. A module is pulled in only
        // when one of its symbols appears WITHOUT a local definition
        // (`sym:`) — a self-contained sketch defining its own pad keeps
        // linking alone, no duplicate-symbol trap.
        if (!proj.ok && std::string(t.label).find("TMS9918") != std::string::npos &&
            !devRoot_.empty()) {
            struct LibSym { const char* sym; const char* file; };
            static const LibSym kTmsLibSyms[] = {
                { "tms9918_pad12",    "tms9918_pad.asm"       },
                { "tms9918_pad18",    "tms9918_pad.asm"       },
                { "tms9918_pad24",    "tms9918_pad.asm"       },
                { "tms9918_pad40",    "tms9918_pad.asm"       },
                { "vdp_display_off",  "tms9918_pad.asm"       },
                { "vdp_display_on",   "tms9918_pad.asm"       },
                { "init_vdp_g1",      "tms9918m1.asm"         },
                { "init_vdp_g2",      "tms9918m2.asm"         },
                { "arm_5s_trigger",   "tms9918_5strigger.asm" },
                { "wait_5s_trigger",  "tms9918_5strigger.asm" },
                { "vdp_write_a",      "tms9918_helpers.asm"   },
                { "vdp_set_write_xy", "tms9918_helpers.asm"   },
                // The VDP access primitives themselves. They were missing, so
                // a program that points the chip at VRAM without calling one
                // of the two init routines linked nothing at all.
                { "vdp_set_write",    "tms9918m1.asm"         },
                { "vdp_set_read",     "tms9918m1.asm"         },
                { "vdp_hi",           "tms9918m1.asm"         },
                { "vdp_lo",           "tms9918m1.asm"         },
                { "clear_name_table", "tms9918m1.asm"         },
                { "wipe_all_vram",    "tms9918m1.asm"         },
                { "disable_sprites",  "tms9918m1.asm"         },
            };
            // Both tests run on a COMMENT-STRIPPED copy. Searching the raw
            // text made a comment decide the link: TMS_RogueDiag.asm says
            // "; ... same init path as Rogue (lib init_vdp_g1: 8 Mode-1 ...",
            // the `sym + ":"` probe found that colon, concluded the symbol was
            // defined locally, and dropped tms9918m1.asm — taking init_vdp_g1,
            // vdp_set_write, vdp_hi and vdp_lo down with it. And a local
            // definition is a LABEL, which starts a line; matching `sym:`
            // anywhere would also fire on `JSR sym` followed by a colon in a
            // trailing comment.
            std::string code;
            code.reserve(src.size());
            for (size_t i = 0, n = src.size(); i < n; ) {
                const size_t eol = src.find('\n', i);
                const size_t end = (eol == std::string::npos) ? n : eol;
                const size_t semi = src.find(';', i);
                const size_t cut = (semi != std::string::npos && semi < end) ? semi : end;
                code.append(src, i, cut - i);
                code.push_back('\n');
                if (eol == std::string::npos) break;
                i = eol + 1;
            }
            auto definedLocally = [&code](const std::string& sym) {
                const std::string needle = sym + ":";
                for (size_t at = code.find(needle); at != std::string::npos;
                     at = code.find(needle, at + 1)) {
                    size_t b = at;                       // only whitespace before it?
                    while (b > 0 && (code[b - 1] == ' ' || code[b - 1] == '\t')) --b;
                    if (b == 0 || code[b - 1] == '\n') return true;
                }
                return false;
            };

            std::vector<std::string> mods;
            auto addMod = [&](const std::string& f) {
                for (const auto& m : mods) if (m == f) return;
                mods.push_back(f);
            };
            for (const auto& ls : kTmsLibSyms) {
                if (code.find(ls.sym) == std::string::npos) continue;
                if (definedLocally(ls.sym)) continue;   // a real label — skip
                addMod(ls.file);
            }
            // m1/m2/5strigger/helpers all JSR the pad themselves.
            if (!mods.empty()) addMod("tms9918_pad.asm");
            const fs::path tmsLib = fs::path(devRoot_) / "lib" / "tms9918";
            int nMod = 0;
            for (const auto& mfile : mods) {
                const fs::path msrc = tmsLib / mfile;
                if (!fs::exists(msrc, ec)) continue;
                const fs::path mo = dir / ("pom1_bench_lib" +
                                           std::to_string(nMod++) + ".o");
                sweep.add(mo);
                std::string mout;
                const std::string mca = bench::shellQuote(ca65_) + " " + asmFlags +
                    "-I " + bench::shellQuote(tmsLib.string()) + " " +
                    bench::shellQuote(msrc.string()) + " -o " +
                    bench::shellQuote(mo.string());
                if (bench::runCapture(mca, mout) != 0) {
                    parseErrorMarkers(mout, r.errors);
                    r.console += "$ ca65 [auto-link " + mfile + "]\n" + mout
                               + humanizeCc65(mout);
                    r.status = "ca65 failed (lib module, see Build output)";
                    return r;
                }
                r.console += "$ ca65 [auto-link " + mfile + "]\n";
                extraObjs += " " + bench::shellQuote(mo.string());
            }
        }
        std::string out;
        const std::string ca = bench::shellQuote(ca65_) + " " + asmFlags +
            bench::shellQuote(srcS.string()) + " -o " + bench::shellQuote(objO.string());
        int rc = bench::runCapture(ca, out);
        r.console += "$ ca65 [" + std::string(t.label) + (proj.ok ? " + project" : "") + "]\n" + out;
        if (rc != 0) { parseErrorMarkers(out, r.errors); r.console += humanizeCc65(out); r.status = "ca65 failed (see Build output)"; return r; }

        if (proj.ok && proj.dualBank) {
            // Dual-bank: ld65 writes <base>.lo + <base>.hi. Stage the high bank
            // first, then load+run the low bank, whose start address is the real
            // entry point for text Chess and other dual-bank Apple-1 sketches.
            const fs::path base = dir / "pom1_bench_db.bin";
            const std::string loP = base.string() + ".lo", hiP = base.string() + ".hi";
            sweep.add(base); sweep.add(fs::path(loP)); sweep.add(fs::path(hiP));
            const std::string ld = bench::shellQuote(ld65_) + " -C " + bench::shellQuote(cfgPath) +
                " --dbgfile " + bench::shellQuote(dbgP.string()) + " " +
                bench::shellQuote(objO.string()) + extraObjs + " -o " + bench::shellQuote(base.string());
            rc = bench::runCapture(ld, out);
            r.console += "$ ld65 -C " + cfgPath + " (dual-bank)\n" + out;
            if (rc != 0) { r.console += humanizeCc65(out); r.status = "ld65 failed (see Build output)"; return r; }
            r.console += "[ok] assembled + linked (dual-bank)\n";
            adoptDbgInfo();
            reportDbgUnavailable(r);
            // Verify touches no machine state — re-arm right away.
            if (!run) { rearmDbgBreakpoint(r); r.status = "Verify OK"; r.ok = true; return r; }
            if (t.preset >= 0) onTargetSelected(target);
            mw_->applyPendingCardConfiguration();
            auto* emu = mw_->emulation.get();
            std::error_code ec2;
            if (!fs::exists(loP, ec2) || !fs::exists(hiP, ec2)) {
                r.console += "[error] dual-bank outputs missing after ld65 (expected "
                    + loP + " + " + hiP + ")\n";
                r.status = "dual-bank .lo/.hi missing after ld65"; r.ok = false; return r;
            }
            std::string error; int loaded = 0;
            const bool runHigh = (proj.entryAddr == proj.hiAddr);
            const auto& stagePath = runHigh ? loP : hiP;
            const auto& runPath = runHigh ? hiP : loP;
            const uint16_t stageAddr = runHigh ? proj.loAddr : proj.hiAddr;
            const uint16_t runAddr = runHigh ? proj.hiAddr : proj.loAddr;
            if (!emu->loadBinaryToRam(stagePath, stageAddr, error)) { r.status = "dual-bank stage load failed: " + error; r.ok = false; return r; }
            if (emu->loadBinary(runPath, runAddr, error, &loaded)) {
                // After loadBinary — its reset cleared any earlier breakpoint,
                // and onTargetSelected above may have applied a preset, which
                // wipes the line table: re-adopt the fresh table first. (The CPU is
                // already running: a breakpoint within the very first
                // instructions can miss its first pass; loops catch the next.)
                adoptDbgInfo();
                dbg_.markProgramLoaded(emu->programGeneration());
                rearmDbgBreakpoint(r);
                emu->copySnapshot(mw_->uiSnapshot);
                if (t.preset == md::kPresetGen2Bench) mw_->showGraphicsCard = true;
                char msg[176]; std::snprintf(msg, sizeof(msg), "Built dual-bank ($%04X+$%04X) run @ $%04X",
                                             proj.loAddr, proj.hiAddr, runAddr);
                r.status = msg; r.ok = true;
            } else { r.status = "dual-bank run load failed: " + error; r.ok = false; }
            return r;
        }

        const std::string ld = bench::shellQuote(ld65_) + " -C " + bench::shellQuote(cfgPath) +
            " --dbgfile " + bench::shellQuote(dbgP.string()) + " " +
            bench::shellQuote(objO.string()) + extraObjs + " -o " + bench::shellQuote(binB.string());
        rc = bench::runCapture(ld, out);
        r.console += "$ ld65 -C " + cfgPath + "\n" + out;
        if (rc != 0) { r.console += humanizeCc65(out); r.status = "ld65 failed (see Build output)"; return r; }
        r.console += "[ok] assembled + linked\n";
        adoptDbgInfo();
        reportDbgUnavailable(r);
        entry = parseCfgLoadAddr(cfgPath);
        if (entry == 0) { try { entry = static_cast<uint16_t>(std::stoul(addrHex, nullptr, 16)); } catch (...) { entry = 0x0300; } }
    }

    // Verify touches no machine state — re-arm the line breakpoint right away
    // (no-op for the modes without a line table).
    if (!run) { rearmDbgBreakpoint(r); r.status = "Verify OK"; r.ok = true; return r; }

    // Keep the live machine aligned with the DevBench target (GEN2 + ACI for
    // CrazyCycle). A Presets-menu switch after picking a target leaves the
    // cards unplugged — beam sync then hangs and the ACI speaker stays silent.
    if (t.preset >= 0) onTargetSelected(target);

    applySketchAssets(activeSourcePath_, extraAsset_, extraAssetAddr_);

    auto* emu = mw_->emulation.get();
    // Close the deferred-card-plug race: applyMachineConfig() queues a 15-frame
    // delay before the new preset's cards actually plug onto the bus, so a
    // synchronous cl65 build + loadBinary that runs immediately after a New
    // (which switches preset) would start the CPU BEFORE the GEN2 / TMS9918
    // card is on the bus — early writes to $2000-$3FFF or $CC00/$CC01 vanish
    // into RAM. File > Load drains the same queue here; we do the same.
    mw_->applyPendingCardConfiguration();
    enableSketchSidecarCards(emu);
    ejectTapeForAciProgramOutput(emu, r, t.preset);
    if (gen2c || plainc) {
        // GEN2 HGR / plain text C: load + run (the target's preset already plugged
        // the right card). loadBinary resets + runs at the cfg's entry.
        std::string error; int bytesLoaded = 0;
        if (emu->loadBinary(binB.string(), entry, error, &bytesLoaded)) {
            emu->copySnapshot(mw_->uiSnapshot);
            if (gen2c && t.preset == md::kPresetGen2Bench) mw_->showGraphicsCard = true;
            char msg[160]; std::snprintf(msg, sizeof(msg), "Built %d B run @ $%04X", bytesLoaded, entry);
            r.status = msg; r.ok = true;
        } else { r.status = "load failed: " + error; r.ok = false; }
        return r;
    }
    if (t.codetankRom) {
        // Unified CODETANKDEV path (TMS9918 asm + C targets): wrap the build into a
        // persistent CodeTank dev ROM (CODETANKDEV.rom), flash it, jumper to the
        // lower 16K bank, reset and boot 4000R. The write target is a WRITABLE copy
        // (POM1_CODETANK_DEV_DIR in an AppImage, else the roms/codetank/ tree), so a
        // read-only packaged roms/ no longer makes the flash fail silently.
        fs::path romPath = codeTankDevRomWritePath();
        if (romPath.empty()) romPath = dir / "CODETANKDEV.rom";   // fallback: temp build dir
        // Flash the bank picked in the bench toolbar (default Lower), preserving
        // the other flash slot; abort loudly if the write didn't land instead of
        // booting a stale cartridge.
        const bool upper = benchFlashUpper_;
        std::string error;
        if (!flashCodeTankDevRom(binB.string(), romPath.string(), upper, error)) {
            r.status = "CODETANKDEV.rom flash failed: " + error; r.ok = false; return r;
        }
        if (!emu->loadCodeTankRom(romPath.string(), error)) { r.status = "CODETANKDEV.rom load failed: " + error; r.ok = false; return r; }
        mw_->codeTankJumper = upper ? CodeTank::Jumper::Upper16
                                    : CodeTank::Jumper::Lower16;
        emu->setCodeTankJumper(mw_->codeTankJumper);
        if (!mw_->cardPlugged(pom1::CardId::Tms9918)) { mw_->showTMS9918 = true; mw_->setCardPlugged(pom1::CardId::Tms9918, true); }
        if (!mw_->cardPlugged(pom1::CardId::CodeTank)) mw_->setCardPlugged(pom1::CardId::CodeTank, true);
        emu->hardReset(/*animateBoot=*/false); // DevBench: no ~3 s power-on scenario
        // After the reset (which cleared any CPU breakpoint) and BEFORE the
        // deferred 4000R types — the ideal re-arm window: the program has not
        // started, so even a first-instruction breakpoint trips. Re-adopt
        // first: the preset apply above may have wiped the line table.
        adoptDbgInfo();
        dbg_.markProgramLoaded(emu->programGeneration());
        rearmDbgBreakpoint(r);
        mw_->codeTankPendingWozRunAt = ImGui::GetTime() + 1.0;
        emu->copySnapshot(mw_->uiSnapshot);
        r.console += std::string("[ok] flashed CODETANKDEV.rom (")
                     + (upper ? "upper" : "lower") + " bank) - 4000R\n";
        r.status = "CODETANKDEV.rom flashed - boot 4000R"; r.ok = true;
        return r;
    }

    // asm: stage companion assets before load+run. loadBinaryToRam() pauses the
    // CPU and does not resume it, while loadBinary() resets + starts it; therefore
    // the program load must be the final memory load in this Run path.
    std::string error; int bytesLoaded = 0;
    if (!extraAsset_.empty()) {
        const std::string ap = resolveAssetPath(extraAsset_, activeSourcePath_, devRoot_);
        std::string aerr;
        if (!ap.empty() && emu->loadBinaryToRam(ap, extraAssetAddr_, aerr)) {
            std::error_code ec3;
            const auto sz = fs::file_size(ap, ec3);
            char addr[8]; std::snprintf(addr, sizeof(addr), "$%04X", extraAssetAddr_);
            r.console += std::string("[ok] asset -> ") + addr + "  "
                + std::to_string(ec3 ? 0 : sz) + " B\n    " + ap + "\n";
        } else {
            r.console += "[warn] asset not loaded: " + extraAsset_;
            if (!aerr.empty()) r.console += " (" + aerr + ")";
            r.console += "\n";
        }
    }
    if (emu->loadBinary(binB.string(), entry, error, &bytesLoaded)) {
        // After loadBinary — its reset cleared any earlier breakpoint, and
        // onTargetSelected above may have applied a preset, which wipes
        // dbgInfo_: re-adopt the fresh table first. (The CPU is already
        // running: a breakpoint within the very first instructions can miss
        // its first pass; loops catch the next one.)
        adoptDbgInfo();
        dbg_.markProgramLoaded(emu->programGeneration());
        rearmDbgBreakpoint(r);
        emu->copySnapshot(mw_->uiSnapshot);
        if (t.preset == md::kPresetGen2Bench) mw_->showGraphicsCard = true;
        char msg[160]; std::snprintf(msg, sizeof(msg), "Built %d B run @ $%04X", bytesLoaded, entry);
        r.status = msg; r.ok = true;
    } else { r.status = "load failed: " + error; r.ok = false; }
    return r;
#endif
}

// Drive the WASM async cc65 build started by build() (no-op on desktop). Called
// every frame by CodeBench while pending: returns pending=true until the JS
// Promise resolves, then reads the .bin/.log out of MEMFS and loads+runs it.
bench::BuildResult Pom1BenchHost::pollBuild()
{
    bench::BuildResult r;
#if POM1_IS_WASM
    if (!wasmJobActive_) return r;                 // nothing in flight (pending stays false)
    const int state = EM_ASM_INT({
        return (Module.__benchJob && Module.__benchJob.state === 'done') ? 1
             : (Module.__benchJob ? 0 : -1);
    });
    if (state != 1) { r.pending = true; r.showConsole = true; r.status = "Building (web cc65)…"; return r; }

    wasmJobActive_ = false;
    const int code = EM_ASM_INT({ return Module.__benchJob.code | 0; });
    std::string log;
    { std::ifstream f("/tmp/pom1_bench.log", std::ios::binary);
      std::ostringstream ss; ss << f.rdbuf(); log = ss.str(); }
    r.showConsole = true;
    r.console = "$ cc65 (in-browser WASM)\n" + log + "\n";

    const P1T& t = kP1Targets[p1(wasmJobTarget_)];
    BuildLogMeta logMeta;
    logMeta.action = wasmJobVerifyOnly_ ? "verify" : "run";
    logMeta.target = &t;
    logMeta.sourcePath = activeSourcePath_;
    logMeta.host = "wasm";
    logMeta.toolchain = "wasm-cc65";
    if (t.mode == 0 && t.cfg && t.cfg[0])
        logMeta.cfgPath = std::string("/dev/cc65/") + t.cfg;
    else if (t.mode == 3) {
        const std::string cfgTag = t.cfg ? t.cfg : "";
        if (cfgTag == "C-plain") logMeta.cfgPath = "/dev/cc65/apple1_c.cfg";
        else if (cfgTag == "C-gen2") logMeta.cfgPath = "/dev/cc65/apple1_gen2_c.cfg";
        else logMeta.cfgPath = "/dev/lib/tms9918c/cc65/codetank_c.cfg";
    }
    BuildLogFinalizer logFin{r, logMeta};

    if (code != 0) {
        parseErrorMarkers(log, r.errors);
        r.console += humanizeCc65(log);
        r.status = "cc65 failed (see Build output)"; r.ok = false; return r;
    }
    r.console += "[ok] assembled + linked (web cc65)\n";
    if (wasmJobVerifyOnly_) { r.status = "Verify OK"; r.ok = true; return r; }

    auto* emu = mw_->emulation.get();
    namespace fs = std::filesystem;
    std::error_code ec;

    // Same deferred-plug fix as the desktop path in build() — drain any pending
    // card plugs that applyMachineConfig() queued so the new preset's GEN2 /
    // TMS9918 card is on the bus before the CPU starts the freshly loaded
    // binary. Otherwise the program's early writes can land before the card.
    mw_->applyPendingCardConfiguration();
    enableSketchSidecarCards(emu);
    ejectTapeForAciProgramOutput(emu, r, t.preset);

    if (t.codetankRom) {
        // TMS9918 asm: wrap the .bin into a CodeTank dev ROM, flash the bank
        // picked in the bench toolbar, jumper to it, reset + boot 4000R
        // (mirrors the desktop path). Write target is a writable copy
        // (POM1_CODETANK_DEV_DIR in an AppImage; MEMFS on WASM), so a
        // read-only packaged roms/ no longer makes the flash fail silently.
        // CODETANKDEV.rom is generated (never committed): the flash composes
        // it from blank $FF when absent.
        fs::path romPath = codeTankDevRomWritePath();
        if (romPath.empty()) romPath = "/tmp/CODETANKDEV.rom";
        const bool upper = benchFlashUpper_;
        std::string error;
        if (!flashCodeTankDevRom("/tmp/pom1_bench.bin", romPath.string(), upper, error)) {
            r.status = "CODETANKDEV.rom flash failed: " + error; r.ok = false; return r;
        }
        if (!emu->loadCodeTankRom(romPath.string(), error)) { r.status = "CODETANKDEV.rom load failed: " + error; r.ok = false; return r; }
        mw_->codeTankJumper = upper ? CodeTank::Jumper::Upper16
                                    : CodeTank::Jumper::Lower16;
        emu->setCodeTankJumper(mw_->codeTankJumper);
        if (!mw_->cardPlugged(pom1::CardId::Tms9918)) { mw_->showTMS9918 = true; mw_->setCardPlugged(pom1::CardId::Tms9918, true); }
        if (!mw_->cardPlugged(pom1::CardId::CodeTank)) mw_->setCardPlugged(pom1::CardId::CodeTank, true);
        emu->hardReset(/*animateBoot=*/false); // DevBench: no ~3 s power-on scenario
        mw_->codeTankPendingWozRunAt = ImGui::GetTime() + 1.0;
        emu->copySnapshot(mw_->uiSnapshot);
        r.console += std::string("[ok] flashed CODETANKDEV.rom (")
                     + (upper ? "upper" : "lower") + " bank) - 4000R\n";
        r.status = "CODETANKDEV.rom flashed - boot 4000R"; r.ok = true;
        return r;
    }

    // other asm targets: loadBinary at the cfg entry + run
    std::string error; int bytesLoaded = 0;
    if (emu->loadBinary("/tmp/pom1_bench.bin", wasmJobEntry_, error, &bytesLoaded)) {
        emu->copySnapshot(mw_->uiSnapshot);
        if (t.preset == md::kPresetGen2Bench) mw_->showGraphicsCard = true;
        char msg[160]; std::snprintf(msg, sizeof(msg), "Built %d B run @ $%04X", bytesLoaded, wasmJobEntry_);
        r.status = msg; r.ok = true;
    } else { r.status = "load failed: " + error; r.ok = false; }
#endif
    return r;
}

bool Pom1BenchHost::toolchainReady(int target) const
{
    if (target < 0 || target >= kP1TargetCount) return false;
    const P1T& t = kP1Targets[p1(target)];
    if (t.mode == 1 || t.mode == 4 || t.mode == 6) return true;   // hex + BASIC + LOGO need no toolchain
#if POM1_IS_WASM
    return t.mode == 0 || t.mode == 3;   // asm + C compile via the bundled WASM cc65
#else
    probe();
    if (t.mode == 5) return toolchainOk_;   // native BASIC compile: ca65/ld65
    if (!t.needsCl65) return toolchainOk_;
    const std::string cfg = t.cfg ? t.cfg : "";
    if (cfg == "C-gen2")  return gen2COk_;
    if (cfg == "C-plain") return plainCOk_;
    return cl65Ok_;
#endif
}

std::string Pom1BenchHost::toolchainHint(int target) const
{
    if (target < 0 || target >= kP1TargetCount) return "";
    const P1T& t = kP1Targets[p1(target)];
    if (t.mode == 1) return "";
    if (t.mode == 4) return "BASIC — no compiler (injected)";   // desktop + WASM
    if (t.mode == 6) return "LOGO — no compiler (injected)";    // desktop + WASM
#if POM1_IS_WASM
    if (t.mode == 5) return "native compile is desktop-only";
    return "cc65 (WASM) ready";
#else
    probe();
    if (t.mode == 5) return toolchainOk_ ? "native compile (ca65/ld65) ready"
                                         : "needs cc65 (ca65/ld65)";
    if (!t.needsCl65) return toolchainOk_ ? "ca65/ld65 ready" : "needs cc65 (ca65/ld65)";
    return toolchainReady(target) ? "cl65 ready" : "needs cl65 + dev/";
#endif
}

std::string Pom1BenchHost::modeLabel(int target) const
{
    if (target < 0 || target >= kP1TargetCount) return "";
    const int idx = p1(target);
    const P1T& t = kP1Targets[idx];
    if (t.mode == 1) return "Mode: HEX + Apple-1";
    if (t.mode == 4) {   // BASIC: interpreter named by the target index
        switch (idx) {
            case 8:  return "Mode: Applesoft Lite + microSD";
            case 9:  return "Mode: Applesoft GEN2 + GEN2 HGR";
            case 10: return "Mode: Applesoft Lite + Apple-1";
            case 11: return "Mode: Applesoft TMS9918 + CodeTank";
            default: return "Mode: Integer BASIC + Apple-1";
        }
    }
    if (t.mode == 5)     // BASIC native compile: standalone 6502 by card
        return (idx == 13) ? "Mode: Applesoft TMS9918 (native)"
                           : "Mode: Applesoft GEN2 (native)";
    if (t.mode == 6)     // LOGO: interpreter by card
        return (idx == 14) ? "Mode: LOGO + CodeTank (TMS9918)"
                           : "Mode: LOGO + GEN2 HGR";

    const char* language = (t.mode == 3) ? "C" : "ASM";
    const char* machine = "Apple-1";
    if (idx == 1 || idx == 4) machine = "TMS9918";
    else if (idx == 2 || idx == 5) machine = "GEN2 HGR";
    return std::string("Mode: ") + language + " + " + machine;
}

std::string Pom1BenchHost::toolchainReport() const
{
    probe();
#if POM1_IS_WASM
    return "DevBench is desktop-only - the web build has no cc65 toolchain.\n";
#else
    auto line = [](const char* name, const std::string& path) {
        return std::string(name) + (path.empty() ? " : not found" : (" : " + path)) + "\n";
    };
    auto yn = [](bool b) { return b ? "ready" : "MISSING"; };
    std::string s = "cc65 toolchain (DevBench probe)\n-------------------------------\n";
    s += line("ca65 (assembler)", ca65_);
    s += line("ld65 (linker)   ", ld65_);
    s += line("cl65 (C driver) ", cl65_);
    s += line("tms9918c lib     ", videocardLib_);
    s += line("gfx lib          ", gfxLib_);
    s += line("codetank_c.cfg   ", codetankCfg_);
    if (const char* home = std::getenv("CC65_HOME"); home && *home)
        s += std::string("CC65_HOME       : ") + home + "\n";
    s += std::string("dev/ tree        : ") +
         (devRoot_.empty() ? "NOT found (run from the cloned repo; release packages bundle dev/)"
                           : (devRoot_ + "  (resolved)")) + "\n";
    s += "\nPer-target runtime:\n";
    s += std::string("  asm (any machine)   : ") + yn(toolchainOk_) + "\n";
    s += std::string("  C  Apple-1 text     : ") + yn(plainCOk_) + "\n";
    s += std::string("  C  GEN2 HGR (gen2c) : ") + yn(gen2COk_) + "\n";
    s += std::string("  C  TMS9918 (vcard)  : ") + yn(cl65Ok_) + "\n";
    if (!toolchainOk_)
        s += "\nInstall cc65:  apt install cc65  /  brew install cc65  /  pacman -S cc65\n"
             "or https://cc65.github.io/ (add its bin/ to PATH), then reopen the Bench.\n";
    return s;
#endif
}

std::string Pom1BenchHost::headerNote() const
{
#if POM1_IS_WASM
    // The web build bundles the full cc65 toolchain compiled to WASM (compiler,
    // assembler, linker + the -t none C runtime), so both 6502 asm and C compile
    // + run entirely in-browser — no desktop app needed.
    return "";
#else
    return "";
#endif
}

void Pom1BenchHost::stop()
{
    mw_->stopCpu();   // halt the emulated CPU (same as the CPU menu's Stop)
}

std::string Pom1BenchHost::cpuStep()
{
    // single-step one instruction (same as the CPU menu / F7); return the
    // post-step PC so the toolbar can show numeric confirmation.
    return "Stepped - " + mw_->stepCpu();
}

void Pom1BenchHost::cpuRun()
{
    // Resuming while parked ON the armed breakpoint would re-trip before a
    // single instruction executes (M6502::run checks the breakpoint at the
    // TOP of its loop), so the Bench's ▶ looked dead after a hit. Same recipe
    // as the Debug window's Continue: step past the address, then free-run —
    // the breakpoint stays armed for the next pass.
    //
    // Deliberately NOT conditioned on isCpuBreakpointTripped(): a rebuild
    // while parked at the breakpoint re-arms it (which resets the trip
    // latch), and resuming from PC == armed address insta-retrips whatever
    // the latch says. "Stopped, parked exactly on the armed address" is the
    // whole condition.
    auto* emu = mw_->emulation.get();
    if (emu && !mw_->cpuRunning && emu->hasCpuBreakpoint() &&
        mw_->uiSnapshot.programCounter == emu->getCpuBreakpoint())
        mw_->stepCpu();
    mw_->startCpu();  // resume free-running (same as the CPU menu's Run)
}

bool Pom1BenchHost::cpuIsRunning() const
{
    return mw_->cpuRunning;  // UI-thread mirror of the run state (friend access)
}

// ── Source-level debugging (asm builds: ca65 -g + ld65 --dbgfile) ───────────

// Snapshot the machine's single CPU breakpoint for the pure session. Null
// emulator reads as "not armed", which makes every session answer degrade to
// "nothing of ours is live" instead of needing its own null checks.
Pom1BenchHost::MachineBpOf Pom1BenchHost::machineBp() const
{
    pom1::BenchDebugSession::MachineBp m;
    if (auto* emu = mw_->emulation.get(); emu && emu->hasCpuBreakpoint()) {
        m.armed = true;
        m.address = emu->getCpuBreakpoint();
    }
    return m;
}

void Pom1BenchHost::dropDebugSession()
{
    // The machine is being reprogrammed, so the line table is void — but the
    // CPU breakpoint we armed through it is REAL and outlives the table. Most
    // paths here also hard-reset (which disarms it in M6502::reset), yet not
    // all: injectBasic's WARM start deliberately skips the whole reset+reload
    // so a program typed at the REPL survives. Without this clear, a
    // breakpoint left at some address of the previous asm program stays armed
    // under the interpreter; the moment the PC wanders onto it the machine
    // parks itself, and nothing in the Bench explains why — the session has
    // forgotten it, so no marker and no banner mention it. Clear it here,
    // once, for every reprogramming path; a breakpoint the Debug window owns
    // is left strictly alone (markerLine answers -1 for it).
    if (dbg_.markerLine(machineBp()) >= 0) {
        if (auto* emu = mw_->emulation.get())
            emu->clearCpuBreakpoint();
    }
    dbg_.invalidate();
}

bool Pom1BenchHost::debugLineInfo() const
{
    return dbg_.hasLineInfo();
}

int Pom1BenchHost::sourceLineForPc() const
{
    if (mw_->cpuRunning)
        return -1;
    // uiSnapshot is refreshed every frame by MainWindow::render(), including
    // while the CPU is parked on a breakpoint/step — no extra lock needed.
    auto* emu = mw_->emulation.get();
    if (!emu)
        return -1;
    // The stamp guards against the table describing a program the machine no
    // longer runs (Verify without Run, File > Load, a rewind scrub).
    return dbg_.lineForPc(mw_->uiSnapshot.programCounter, emu->programGeneration());
}

int Pom1BenchHost::toggleLineBreakpoint(int line)
{
    auto* emu = mw_->emulation.get();
    if (!emu)
        return -1;
    const auto res = dbg_.toggle(line, machineBp());
    switch (res.kind) {
    case pom1::BenchDebugSession::Toggle::Armed:
        emu->setCpuBreakpoint(res.address);   // the machine's single breakpoint
        return res.line;
    case pom1::BenchDebugSession::Toggle::Cleared:
        emu->clearCpuBreakpoint();
        return -1;
    case pom1::BenchDebugSession::Toggle::NoCode:
        break;
    }
    return -1;
}

int Pom1BenchHost::breakpointLine() const
{
    // POM1 has ONE CPU breakpoint. If the Debug window cleared or re-armed it
    // since our toggle, the session answers -1 rather than showing a marker
    // that no longer describes the machine.
    return dbg_.markerLine(machineBp());
}

std::string Pom1BenchHost::browseDir() const
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const pom1::ResourceLocator res = pom1::ResourceLocator::defaultLocator();
    for (const char* rel : {"sketchs", "dev/sketchs", "dev"})
        if (const fs::path p = res.findDirectory(rel); !p.empty())
            return fs::absolute(p, ec).string();
    return ".";
}

bool Pom1BenchHost::pickFilePath(bool forSave, const std::string& title,
                                 const std::string& filterDesc,
                                 const std::string& extCsv,
                                 const std::string& defaultDir,
                                 const std::string& defaultName,
                                 std::string& outPath)
{
    // mw_->window is set after window creation; reading it here (at pick time) is
    // always current. Friend access to the private member.
    GLFWwindow* parent = mw_ ? mw_->window : nullptr;
    return pom1::NativeFileDialog::pickFiltered(parent, forSave, title, filterDesc,
                                                extCsv, defaultDir, defaultName,
                                                outPath);
}

bool Pom1BenchHost::nativeFilePickerAvailable() const
{
    return pom1::NativeFileDialog::isAvailable();
}

void Pom1BenchHost::openSerial()
{
    mw_->showTelemetry = true;
}

// ---- Interactive LOGO REPL --------------------------------------------------

bool Pom1BenchHost::replActive() const
{
    return logoReplActive_ && mw_ && mw_->emulation;
}

void Pom1BenchHost::replSend(const std::string& line)
{
    if (!replActive()) return;
    auto* emu = mw_->emulation.get();
    // Feed the line one character at a time over the keyboard FIFO, then a CR, so
    // the resident REPL reads + executes it exactly as if typed (setKeyPressed
    // forces uppercase like the real Apple-1 keyboard). Only ONE line is ever
    // queued per submit, so the interpreter's REPEAT break-poll can't eat any
    // type-ahead. Strip control bytes; the interpreter's line buffer caps at 60.
    for (char c : line) {
        if (static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) < 0x7F)
            emu->queueKey(c);
    }
    emu->queueKey('\r');
    emu->startCpu();   // ensure the emulation thread is running to drain the keys
}

void Pom1BenchHost::replBreak()
{
    if (!replActive()) return;
    // Ctrl-G ($07): APPLE-1 LOGO's poll_break sets break_flag on ESC ($1B) or
    // Ctrl-G, aborting a REPEAT (incl. REPEAT FOREVER) at its next iteration —
    // without halting the CPU (unlike the toolbar Stop). Ctrl-G is preferred over
    // ESC as it survives the TerminalCard's ESC-sequence handling.
    mw_->emulation->queueKey('\x07');
    mw_->emulation->startCpu();
}
