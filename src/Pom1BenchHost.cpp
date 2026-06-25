// POM1 host for the portable bench. See Pom1BenchHost.h.
#include "Pom1BenchHost.h"

#include "BasicTokeniserApplesoft.h"        // basic::compile — Applesoft tokeniser (GEN2/TMS)
#include "BasicTokeniserInteger.h"          // ibasic::compile — Integer BASIC tokeniser ($E000)
#include "BasicCompilerApplesoft.h"         // basicnative::compile — native standalone 6502
#include "MainWindow_ImGui.h"     // mw_ members (friend) + EmulationController
#include "MainWindow_Internal.h"  // kMachinePresets / BasicType (ACI program-output presets)
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

#include "POM1Build.h"             // POM1_IS_WASM
#if POM1_IS_WASM
#include <emscripten.h>            // EM_ASM / EM_ASM_INT for the in-browser cc65 path
#endif

// ─────────────────────────────────────────────────────────────
// Static data: starter sketches, the embedded asm cfg, target + example tables
// ─────────────────────────────────────────────────────────────

// HELLO-WORLD starters, one per (language x machine) the New dialog offers.
static const char* kSketchAsm =          // asm x Apple dual-4k/8k (text)
    "; HELLO WORLD - Apple-1 text via WozMon ECHO ($FFEF).\n"
    "ECHO = $FFEF\n"
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
    "    jmp done\n"
    "msg:\n"
    "    .byte $0D, \"HELLO WORLD\", $0D, $00\n";
static const char* kSketchAsmTms =       // asm x TMS9918 (Graphics I text, loads a font)
    "; HELLO WORLD on the TMS9918 (Graphics I). Proper init: regs loaded with the\n"
    "; display BLANKED, all 16 KB VRAM cleared, sprites parked, then font/colour/\n"
    "; message loaded and the screen turned on LAST. VDP data=$CC00, ctrl=$CC01.\n"
    "; Upload flashes this into CODETANKDEV.rom (CodeTank dev cartridge) and boots\n"
    "; 4000R - it runs in place from the ROM window, like every TMS9918 program.\n"
    "VDAT = $CC00\n"
    "VCTL = $CC01\n"
    ".segment \"CODE\"\n"
    "start:\n"
    "    ldx #0          ; --- load 8 VDP registers (display OFF: R1 bit6=0) ---\n"
    "ireg:\n"
    "    lda regs,x\n"
    "    sta VCTL\n"
    "    txa\n"
    "    ora #$80\n"
    "    sta VCTL\n"
    "    inx\n"
    "    cpx #8\n"
    "    bne ireg\n"
    "    lda #$00         ; --- clear all 16 KB VRAM to a known state ---\n"
    "    sta VCTL\n"
    "    lda #$40         ; $0000 | write\n"
    "    sta VCTL\n"
    "    lda #$00\n"
    "    ldy #64          ; 64 * 256 = 16384\n"
    "clro:\n"
    "    ldx #0\n"
    "clri:\n"
    "    sta VDAT\n"
    "    inx\n"
    "    bne clri\n"
    "    dey\n"
    "    bne clro\n"
    "    lda #$00         ; --- park sprites: sprite 0 Y=$D0 stops the scan ---\n"
    "    sta VCTL\n"
    "    lda #($1B|$40)   ; sprite attribute table $1B00\n"
    "    sta VCTL\n"
    "    lda #$D0\n"
    "    sta VDAT\n"
    "    lda #$00         ; --- pattern table $0000: blank + 7 glyphs (64 B) ---\n"
    "    sta VCTL\n"
    "    lda #$40         ; $0000 | write\n"
    "    sta VCTL\n"
    "    ldx #0\n"
    "pat:\n"
    "    lda font,x\n"
    "    sta VDAT\n"
    "    inx\n"
    "    cpx #64\n"
    "    bne pat\n"
    "    lda #$00         ; --- colour table $2000: 32 x $F4 (white on blue) ---\n"
    "    sta VCTL\n"
    "    lda #($20|$40)\n"
    "    sta VCTL\n"
    "    lda #$F4\n"
    "    ldx #32\n"
    "col:\n"
    "    sta VDAT\n"
    "    dex\n"
    "    bne col\n"
    "    lda #$00         ; --- name table $1800: write the message ---\n"
    "    sta VCTL\n"
    "    lda #($18|$40)   ; (rest of the table is already blank from the clear)\n"
    "    sta VCTL\n"
    "    ldx #0\n"
    "msgl:\n"
    "    lda message,x\n"
    "    cmp #$FF\n"
    "    beq enable\n"
    "    sta VDAT\n"
    "    inx\n"
    "    bne msgl\n"
    "enable:\n"
    "    lda #$C0         ; --- screen ON last: R1 bit6 (BLANK) = 1 ---\n"
    "    sta VCTL\n"
    "    lda #($80|1)     ; register 1\n"
    "    sta VCTL\n"
    "done:\n"
    "    jmp done\n"
    "; reg0..7: graphics I, name $1800, colour $2000, pattern $0000, backdrop blue\n"
    "; R1=$80 here -> display BLANKED during setup; turned on at 'enable'\n"
    "regs:\n"
    "    .byte $00,$80,$06,$80,$00,$36,$07,$04\n"
    "; pattern 0 = blank, then glyphs 1..7 = H E L O W R D (8x8 each)\n"
    "font:\n"
    "    .byte $00,$00,$00,$00,$00,$00,$00,$00\n"
    "    .byte $00,$44,$44,$7C,$44,$44,$44,$00   ; H\n"
    "    .byte $00,$7C,$40,$78,$40,$40,$7C,$00   ; E\n"
    "    .byte $00,$40,$40,$40,$40,$40,$7C,$00   ; L\n"
    "    .byte $00,$38,$44,$44,$44,$44,$38,$00   ; O\n"
    "    .byte $00,$44,$44,$44,$54,$54,$28,$00   ; W\n"
    "    .byte $00,$78,$44,$44,$78,$48,$44,$00   ; R\n"
    "    .byte $00,$78,$44,$44,$44,$44,$78,$00   ; D\n"
    "; \"HELLO WORLD\" -> glyph indices (space = blank 0), $FF terminates\n"
    "message:\n"
    "    .byte 1,2,3,3,4,0,5,4,6,3,7,$FF\n";
static const char* kSketchAsmGen2 =      // asm x GEN2 HGR (BBFont text)
    "; HELLO WORLD - GEN2 HIRES with the Beautiful Boot 8x8 font (bbfont_cp437).\n"
    "; Renders white, artifact-free text by PIXEL-DOUBLING every glyph: each set\n"
    "; pixel becomes two adjacent HGR pixels (a >=2px run shows as white, never an\n"
    "; NTSC colour fringe), and each row is drawn on two scanlines -> 16x16 cells.\n"
    "; plot_pixel handles the 7px/byte packing, so glyphs may straddle byte\n"
    "; boundaries freely. Pulls in dev/lib/gen2.\n"
    ".include \"gen2.inc\"\n"
    "\n"
    "TOP_ROW = 88            ; top scanline of the text band (0..191)\n"
    "START_X = 42            ; left pixel of the first cell (centres 11 cells)\n"
    "STRIDE  = 18            ; cell pitch: 16px doubled glyph + 2px gap\n"
    "\n"
    ".zeropage\n"
    "cur_x:   .res 1         ; plot_pixel inputs\n"
    "cur_y:   .res 1\n"
    "ptr_lo:  .res 1\n"
    "ptr_hi:  .res 1\n"
    "src_lo:  .res 1         ; glyph data pointer (HGR_BBFont + ch*8)\n"
    "src_hi:  .res 1\n"
    "gx:      .res 1         ; left pixel of the current cell\n"
    "px:      .res 1         ; running doubled-pixel X within a row\n"
    "line:    .res 1         ; glyph row 0..7\n"
    "chidx:   .res 1         ; index into the message\n"
    "rowbits: .res 1         ; current glyph row, shifted out bit by bit\n"
    "tmp:     .res 1\n"
    "\n"
    ".code\n"
    "start:\n"
    "    bit GEN2_TEXTOFF        ; graphics (TEXT off)\n"
    "    bit GEN2_HIRES          ; HIRES\n"
    "    bit GEN2_PAGE1          ; page 1 ($2000)\n"
    "    bit GEN2_MIXOFF         ; full screen\n"
    "    jsr clear_hgr           ; zero $2000-$3FFF (black)\n"
    "    lda #START_X\n"
    "    sta gx\n"
    "    lda #$00\n"
    "    sta chidx\n"
    "next_ch:\n"
    "    ldx chidx\n"
    "    lda message,x\n"
    "    beq done                ; 0 terminates\n"
    "    and #$7F                ; CP437 lower 128 == ASCII -> glyph index\n"
    "    sta tmp                 ; src = HGR_BBFont + index*8\n"
    "    lda #$00\n"
    "    sta src_hi\n"
    "    lda tmp\n"
    "    asl a\n"
    "    rol src_hi\n"
    "    asl a\n"
    "    rol src_hi\n"
    "    asl a\n"
    "    rol src_hi\n"
    "    clc\n"
    "    adc #<HGR_BBFont\n"
    "    sta src_lo\n"
    "    lda src_hi\n"
    "    adc #>HGR_BBFont\n"
    "    sta src_hi\n"
    "    lda #$00\n"
    "    sta line\n"
    "rowloop:\n"
    "    lda line                ; cur_y = TOP_ROW + line*2 (vertical doubling)\n"
    "    asl a\n"
    "    clc\n"
    "    adc #TOP_ROW\n"
    "    sta cur_y\n"
    "    jsr draw_row            ; top scanline of the doubled row\n"
    "    inc cur_y\n"
    "    jsr draw_row            ; bottom scanline\n"
    "    inc line\n"
    "    lda line\n"
    "    cmp #$08\n"
    "    bne rowloop\n"
    "    lda gx                  ; advance to next cell\n"
    "    clc\n"
    "    adc #STRIDE\n"
    "    sta gx\n"
    "    inc chidx\n"
    "    jmp next_ch\n"
    "done:\n"
    "    jmp *\n"
    "\n"
    "; --- Draw one glyph row at cur_y, doubled horizontally. gx = cell left px ---\n"
    "draw_row:\n"
    "    ldy line\n"
    "    lda (src_lo),y\n"
    "    sta rowbits\n"
    "    lda gx\n"
    "    sta px                  ; px = cell left pixel\n"
    "    ldx #$08                ; 8 source columns\n"
    "@b: lsr rowbits            ; bit 0 (leftmost) -> carry\n"
    "    bcc @skip\n"
    "    lda px                  ; pixel pair: px and px+1 -> white run\n"
    "    sta cur_x\n"
    "    jsr plot_pixel\n"
    "    inc cur_x               ; plot_pixel preserves cur_x\n"
    "    jsr plot_pixel\n"
    "@skip: lda px\n"
    "    clc\n"
    "    adc #$02                ; doubled pixels are 2 apart\n"
    "    sta px\n"
    "    dex\n"
    "    bne @b\n"
    "    rts\n"
    "\n"
    "message:\n"
    "    .byte \"HELLO WORLD\", 0\n"
    "\n"
    ".include \"bbfont_cp437.inc\"\n"
    ".include \"hgr_tables.inc\"\n";
static const char* kSketchCText =        // C x Apple dual-4k/8k (WozMon I/O)
    "/* HELLO WORLD in C on a plain text Apple-1, using the shared apple1c text\n"
    "   base (woz_puts/apple1_getkey). The same apple1c.h works on the GEN2 card. */\n"
    "#include \"apple1c.h\"\n"
    "\n"
    "void main(void) {\n"
    "    woz_puts((const unsigned char *)\"\\rHELLO WORLD (C / Apple-1)\\r\");\n"
    "    woz_mon();\n"
    "}\n";
static const char* kSketchHex =
    "; POM1 Bench - Wozmon hex. Upload loads + runs (addresses are in the text).\n"
    "0300: A9 A1 20 EF FF 4C 00 03\n"
    "0300R\n";
static const char* kSketchC =
    "/* P-LAB TMS9918 (Apple-1) — cc65 C program for POM1 CodeTank ($4000, 4000R)\n"
    " * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).\n"
    " * Software: dev/lib/tms9918c — cc65 port of Antonino \"Nino\" Porcino's\n"
    " *   apple1-videocard-lib (https://github.com/nippur72/apple1-videocard-lib).\n"
    " *\n"
    " * HELLO WORLD — minimal Screen 1 text demo.\n"
    " * DevBench builds a CodeTank ROM with cl65, flashes it and boots 4000R. */\n"
    "#include \"tms9918.h\"\n"
    "#include \"screen1.h\"\n"
    "\n"
    "void main(void) {\n"
    "    tms_init_regs(SCREEN1_TABLE);\n"
    "    tms_set_color(COLOR_CYAN);\n"
    "    screen1_prepare();\n"
    "    screen1_load_font();\n"
    "    screen1_puts((const unsigned char *)\"HELLO WORLD (C / TMS9918)\\nPOM1 Bench\");\n"
    "    for (;;) { /* idle */ }\n"
    "}\n";
static const char* kSketchGen2C =
    "/* HELLO WORLD on Uncle Bernie's GEN2 HIRES, drawn with the Beautiful Boot\n"
    "   font. gen2_hgr_puts pixel-doubles every glyph so the text is solid white\n"
    "   (no NTSC colour artifacts). Soft switches $C250-$C257 — see gen2.h.\n"
    "   Upload builds + runs @ $6000. */\n"
    "#include \"gen2.h\"\n"
    "\n"
    "void main(void) {\n"
    "    gen2_hgr_init();                    /* graphics + hires + page1 + full */\n"
    "    gen2_hgr_clear(0);                  /* black */\n"
    "    gen2_hgr_puts(42, 80, \"HELLO WORLD\");\n"
    "    for (;;) { /* idle */ }\n"
    "}\n";

// BASIC starters. The Bench cold-starts the in-ROM interpreter, then tokenises/
// compiles the listing ahead of time and loads it directly (see injectBasic) — no
// keyboard typing. Pure C++, so both run in the web (WASM) build too.
static const char* kSketchBasicInteger =     // Integer BASIC ($E000, Apple-1 text)
    "10 PRINT \"HELLO FROM INTEGER BASIC\"\n"
    "20 FOR I=1 TO 5\n"
    "30 PRINT \"  LINE \";I\n"
    "40 NEXT I\n"
    "50 END\n";
static const char* kSketchBasicApplesoft =   // Applesoft Lite ($6000, microSD)
    "10 PRINT \"HELLO FROM APPLESOFT LITE\"\n"
    "20 FOR I = 1 TO 5\n"
    "30 PRINT \"  1/\"; I; \" = \"; 1 / I\n"
    "40 NEXT I\n"
    "50 END\n";
// Applesoft GEN2 ($9800 on the GEN2 card): the applesoft-gen2 interpreter —
// Applesoft with the GEN2 graphics command set, shipped prebuilt as
// roms/applesoft-gen2.rom (graphics-BASIC demos under sketchs/basic_applesoft).
// PRINT goes to the GEN2 screen, APRINT to the Apple-1.
static const char* kSketchBasicApplesoftGen2 =
    "10 HGR : HCOLOR=3\n"
    "20 HPLOT 0,0 TO 279,191\n"
    "30 HPLOT 0,191 TO 279,0\n"
    "40 GR : COLOR=13 : PLOT 20,20\n"
    "50 TEXT : HOME : VTAB 12 : HTAB 12 : PRINT \"HELLO GEN2\"\n";

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

namespace {

// CODETANKDEV.rom (the unified TMS9918 DevBench cartridge: blank flash slot in the
// lower bank, Applesoft TMS9918 in the upper) normally lives under roms/codetank/.
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
    for (const char* c : {"roms/codetank/CODETANKDEV.rom",
                          "../roms/codetank/CODETANKDEV.rom",
                          "../../roms/codetank/CODETANKDEV.rom"})
        if (fs::exists(c, ec)) return c;
    return {};
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
    for (const char* pre : {"roms/codetank", "../roms/codetank", "../../roms/codetank"})
        if (fs::exists(fs::path(pre), ec)) return (fs::path(pre) / "CODETANKDEV.rom").string();
    return {};
}

// Flash the asm/C build at `binPath` into the LOWER 16 KB of CODETANKDEV.rom while
// preserving the upper bank (Applesoft TMS9918) seeded from the best existing copy,
// write it to `outPath`, and report whether the write actually landed (the old
// code ignored ofstream failures, so a read-only roms/ booted a stale cartridge).
bool flashCodeTankDevRom(const std::string& binPath, const std::string& outPath,
                         std::string& err) {
    namespace fs = std::filesystem;
    std::vector<unsigned char> rom(0x8000, 0xFF);
    // Seed the whole 32 KB from the best existing cartridge so the Applesoft upper
    // bank survives, then clear + overwrite only the lower 16 KB with the build.
    if (std::string seed = codeTankDevRomReadPath(); !seed.empty()) {
        std::ifstream prev(seed, std::ios::binary);
        if (prev) prev.read(reinterpret_cast<char*>(rom.data()), 0x8000);
    }
    std::fill_n(rom.begin(), 0x4000, 0xFF);
    { std::ifstream in(binPath, std::ios::binary);
      in.read(reinterpret_cast<char*>(rom.data()), 0x4000); }
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
    if (presetIndex < 0 || presetIndex >= md::kMachinePresetCount) return false;
    const md::MachineConfig& cfg = md::kMachinePresets[presetIndex];
    return cfg.aci && cfg.basicType != md::BasicType::IntegerCassette;
}

static void ejectTapeForAciProgramOutput(EmulationController* emu, bench::BuildResult& r, int preset)
{
    if (!emu || !presetUsesAciProgramOutput(preset)) return;
    emu->ejectTape();
    r.console += "[ok] cassette ejected (ACI program output / $C030 speaker)\n";
}

// POM1 target: machine preset + linker cfg + source mode
//   (0 asm / 1 hex / 3 C / 4 BASIC injected) + the HELLO-WORLD starter for it.
// The New dialog picks a (language x machine) pair (see targetFor):
//   0..2 = asm x {dual-4k, TMS9918, GEN2 HGR},
//   3..5 = C   x {dual-4k, TMS9918, GEN2 HGR},
//   6    = Wozmon hex (any machine),
//   7..8 = BASIC x {Integer ($E000, Apple-1 CC65 DevBench), Applesoft Lite ($6000, microSD)}.
// (preset indices = the development benches: CC65 bench = 0, TMS9918 bench = 1,
//  GEN2 HGR bench = 2 — the profiles the DevBench loads per machine target.)
// For mode-4 BASIC targets `cfg` holds the WOZ-Monitor cold-start command
// (E000R / 6000R) the host types to bring the interpreter up — no compiler.
//
// Applesoft Lite runs on the **microSD + Applesoft Lite preset (8)** — the P-LAB
// machine that owns the $6000 Applesoft ROM + the $8000 SD-OS. That preset is 8 KB
// with silicon/OOR-strict armed, which makes $6000-$7FFF (inside the $1000..$7FFF
// out-of-range window) read back $FF, so a bare 6000R would jump into $FF garbage
// and fall back to WozMon. injectBasic() therefore relaxes the machine to a
// permissive 64 KB view for the BASIC run (OOR off → $6000 and Applesoft's RAM
// workspace live), keeping the microSD card. Integer BASIC ($E000) is OOR-exempt
// (>= $8000) so it stays on the authentic CC65 DevBench (preset 0).
struct P1T { const char* label; int preset; const char* cfg; const char* lang; int mode;
             bool needsCl65; bool codetankRom; const char* sketch; };
const P1T kP1Targets[] = {
    { "Apple-1 dual 4K/8K (asm)",         0, "apple1_4k.cfg",   "6502",  0, false, false, kSketchAsm            },
    { "P-LAB TMS9918 Graphic Card (asm)", 1, "codetank.cfg",    "6502",  0, false, true,  kSketchAsmTms         },
    { "Uncle Bernie GEN2 HGR (asm)",      2, "apple1_gen2.cfg", "6502",  0, false, false, kSketchAsmGen2        },
    { "Apple-1 dual 4K/8K (C)",           0, "C-plain",         "C",     3, true,  false, kSketchCText          },
    { "P-LAB TMS9918 CodeTank ROM (C)",   1, "C",               "C",     3, true,  true,  kSketchC              },
    { "Uncle Bernie GEN2 HGR (C)",        2, "C-gen2",          "C",     3, true,  false, kSketchGen2C          },
    { "Wozmon hex (any machine)",        -1, "",                "hex",   1, false, false, kSketchHex            },
    { "Integer BASIC (interpreter, E000R)",      0, "E000R",    "BASIC", 4, false, false, kSketchBasicInteger   },
    { "Applesoft Lite (interpreter, microSD 6000R)", 8, "6000R","BASIC", 4, false, false, kSketchBasicApplesoft },
    // Target 9: Applesoft GEN2 — BASIC injection on the GEN2 card (preset 2),
    // applesoft-gen2 interpreter ROM loaded HIGH in RAM at $9800 (HIMEM just
    // below it) so BASIC owns ~37 KB of $0801-$97FF — real-silicon faithful.
    { "Applesoft GEN2 (interpreter, 9800R)",     2, "9800R",    "BASIC", 4, false, false, kSketchBasicApplesoftGen2 },
    // Target 10: Applesoft Lite on a bare Apple-1 — the CFFA1 flavour ROM at
    // $E000 (roms/applesoft-lite-cffa1.rom), cold start E000R, 64 KB-relaxed.
    { "Applesoft Lite (interpreter, Apple-1 E000R)", 0, "E000R","BASIC", 4, false, false, kSketchBasicApplesoft     },
    // Target 11: Applesoft TMS9918 — the applesoft-tms9918 interpreter as a
    // CodeTank ROM cartridge ($4000-$7FFF), preset 1, cold start 4000R.
    { "Applesoft TMS9918 (interpreter, 4000R)",  1, "4000R",    "BASIC", 4, false, true,  kSketchBasicApplesoftGen2 },
    // Targets 12-13: NATIVE Applesoft compiler (basicnative::compile) — standalone
    // 6502, NO interpreter (~20x faster). Mode 5 routes to compileBasicNative(),
    // which mirrors tools/basicc_native.sh (ca65 prog + minimal runtime + optional
    // float, then ld65 against basicc_native.cfg) and loadBinary+runs the result at
    // $0300. cfg=nullptr (the native build hardcodes basicc_native.cfg). DESKTOP
    // only — guarded out of the WASM target table (see the ctor). GEN2 = preset 2
    // (HGR page 1 framebuffer); TMS = preset 1 (code runs from $0300 RAM, draws to
    // the VDP at $CC00/$CC01 — NOT a CodeTank cartridge, so codetankRom = false).
    { "Applesoft GEN2 (native, 0300R)",          2, nullptr,    "BASIC", 5, false, false, kSketchBasicApplesoftGen2 },
    { "Applesoft TMS9918 (native, 0300R)",       1, nullptr,    "BASIC", 5, false, false, kSketchBasicApplesoftGen2 },
};
const int kP1TargetCount = static_cast<int>(sizeof(kP1Targets) / sizeof(kP1Targets[0]));

// New-dialog axes (language x machine -> target index, resolved by targetFor).
// kP1*Hints are parallel to the labels and surface as combo-entry tooltips.
const char* const kP1Languages[] = { "Assembly  —  ca65 / ld65", "C  —  cc65 / cl65",
                                     "BASIC  —  injected listing" };
const char* const kP1LanguageHints[] = {
    "MOS 6502 assembler (cc65's ca65 + ld65). Links against the apple1 / tms9918 /\n"
    "gen2 equate libraries under dev/lib via the per-target linker .cfg.",
    "C cross-compiler (cc65's cl65). Pulls in the apple1.c runtime, or the\n"
    "tms9918c (TMS9918) / gen2 C runtime depending on the target.",
    "POM1 cold-starts the in-ROM interpreter, then tokenises your listing ahead of\n"
    "time and loads it directly (instant, no per-character typing, no 127-char line\n"
    "cap). Pure C++, so it works in the web (WASM) build too. Integer BASIC (Apple-1\n"
    "dual-rom) + Applesoft on microSD / GEN2 HGR / TMS9918.",
};
// The "Target" combo is per-language: asm/C show the three graphics machines,
// BASIC shows its four interpreters (CodeBench filters by targetFor()). Each is
// its own entry so New > BASIC reads as the interpreter choice, not a graphics
// machine. Bare-Apple-1 BASIC is Integer (the dual-ROM $E000 bank) — Applesoft
// needs a card (microSD / GEN2 / TMS), so there is no "Applesoft on bare Apple-1".
const char* const kP1Machines[]  = {
    "Apple-1 dual 4K/8K  (text) - start here",   // 0  asm/C
    "P-LAB Graphic Card  (TMS9918)",             // 1  asm/C
    "Uncle Bernie GEN2 HGR  (colour)",           // 2  asm/C
    "Applesoft Lite + microSD  (interpreter)",   // 3  BASIC -> target 8
    "Applesoft GEN2 HGR  (interpreter)",         // 4  BASIC -> target 9
    "Applesoft TMS9918  (interpreter)",          // 5  BASIC -> target 11
    "Integer BASIC (Apple-1 dual-rom)  (interpreter)", // 6  BASIC -> target 7
    "Applesoft GEN2 HGR  (native compile)",      // 7  BASIC -> target 12 (desktop only)
    "Applesoft TMS9918  (native compile)",       // 8  BASIC -> target 13 (desktop only)
};
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
    "Applesoft GEN2 (native compile) — COMPILES the listing to standalone 6502\n"
    "(no interpreter, ~20x faster) via the native compiler, links the minimal GEN2\n"
    "runtime, loads + runs at $0300 on the GEN2 card (preset 2). Desktop only.",
    "Applesoft TMS9918 (native compile) — COMPILES the listing to standalone 6502\n"
    "(no interpreter, ~20x faster), links the minimal TMS9918 runtime + VDP lib,\n"
    "loads + runs at $0300 on the TMS9918 card (preset 1). Desktop only.",
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

// ---------------------------------------------------------------------------
// Project-context build. When the bench editor has a real dev/projects/ file
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
    std::vector<std::string> defines;      // ca65 -D symbols (e.g. CODETANK_BUILD)
    bool dualBank = false;
    uint16_t loAddr = 0x0280, hiAddr = 0xE000, entryAddr = 0x0280;
};

static std::string benchTrim(const std::string& s)
{
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Parse "NAME := value" / "NAME ?= value" (the Makefile.common convention). On a
// match, strips any inline `# comment` and returns the trimmed RHS in `out`.
static bool benchMakeVar(const std::string& line, const char* name, std::string& out)
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
static std::string sketchJsonString(const std::string& json, const char* key)
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

static std::vector<std::string> sketchJsonStringArray(const std::string& json, const char* key)
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

static std::filesystem::path resolveRepoRelativePath(const std::filesystem::path& baseDir, const std::string& rel)
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
static std::string cfgCodePortion(const std::string& line)
{
    const size_t h = line.find('#');
    return (h == std::string::npos) ? line : line.substr(0, h);
}

static bool cfgDeclaresOutputFile(const std::string& line, const char* token)
{
    const std::string code = cfgCodePortion(line);
    if (code.find("file") == std::string::npos) return false;
    return code.find(token) != std::string::npos;
}

static std::string cfgNameBeforeColon(const std::string& line)
{
    const std::string code = cfgCodePortion(line);
    const size_t colon = code.find(':');
    if (colon == std::string::npos) return "";
    return benchTrim(code.substr(0, colon));
}

static std::string cfgSegmentLoadName(const std::string& line, const char* segment)
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

static void probeDualBankFromCfg(const std::string& cfgPath, AsmProjectCtx& p)
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

static std::string resolveAssetPath(const std::string& rel, const std::string& sourcePath,
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
    for (const char* pre : {"", "../", "../../", "../../../"}) {
        const fs::path cand = fs::path(pre) / rel;
        if (fs::exists(cand, ec)) return fs::absolute(cand, ec).string();
    }
    return {};
}

static AsmProjectCtx probeSketchProject(const std::string& sourcePath)
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

// Full tms9918c runtime for the Bench C target (ld65 dead-strips unused .o files,
// but we link every family so any sketch under sketchs/tms9918/*.c builds).
static std::string tms9918cBenchRuntimeCl65Args(const std::string& lib, const std::string& gfxLib)
{
    auto qf = [&](const std::string& base, const char* name) {
        return " " + bench::shellQuote(base + "/" + name);
    };
    std::string s =
        qf(lib, "apple1.c") + qf(lib, "apple1_asm.s") +
        qf(lib, "tms9918.c") + qf(lib, "tms_fast.s") +
        qf(lib, "screen1.c") + qf(lib, "c64font.c") + qf(lib, "screen1_input.c") +
        qf(lib, "screen2_init.c") + qf(lib, "screen2_text.c") + qf(lib, "screen2_pixel.c") +
        qf(lib, "screen2_geom.c") + qf(lib, "screen1_ext.c") + qf(lib, "screen2_ext.c") +
        qf(lib, "sprites.c") + qf(lib, "sprite_shadow.c") +
        qf(lib, "vsync.c") + qf(lib, "printlib.c") + qf(lib, "random.c");
    if (!gfxLib.empty()) {
        s += qf(gfxLib, "gfx_line.c") + qf(gfxLib, "gfx_rect.c") + qf(gfxLib, "gfx_circle.c") +
             qf(gfxLib, "gfx_ellipse.c") + qf(gfxLib, "gfx_num_dec.c") + qf(gfxLib, "gfx_num_hex.c") +
             qf(gfxLib, "gfx_text.c") +
             qf(gfxLib, "gfx_backend_tms.c") + qf(gfxLib, "gfx_backend_tms_rect.c") +
             qf(gfxLib, "gfx_text_backend_tms.c");
    }
    return s;
}

static std::string benchAbsToWasmDev(const std::string& absPath)
{
    const std::string needle = "/dev/";
    const size_t p = absPath.find(needle);
    return (p != std::string::npos) ? absPath.substr(p) : std::string{};
}

static std::string jsonQuoted(const std::string& s)
{
    std::string o = "\"";
    for (char c : s) {
        if (c == '\\' || c == '"') o += '\\';
        o += c;
    }
    o += '"';
    return o;
}

static std::string wasmTms9918cBuildSpec(const std::vector<std::string>& extraAsmAbs)
{
    std::ostringstream spec;
    spec << R"({"cfg":"/dev/lib/tms9918c/cc65/codetank_c.cfg","defines":["POM1_GFX_TMS"],"incDirs":["/dev/lib/tms9918c","/dev/lib/gfx","/dev/lib/telemetry"],)"
         << R"("cSources":[{"path":"/dev/lib/tms9918c/apple1.c","name":"apple1.c"},)"
         << R"({"path":"/dev/lib/tms9918c/tms9918.c","name":"tms9918.c"},)"
         << R"({"path":"/dev/lib/tms9918c/screen1.c","name":"screen1.c"},)"
         << R"({"path":"/dev/lib/tms9918c/c64font.c","name":"c64font.c"},)"
         << R"({"path":"/dev/lib/tms9918c/screen1_input.c","name":"screen1_input.c"},)"
         << R"({"path":"/dev/lib/tms9918c/screen2_init.c","name":"screen2_init.c"},)"
         << R"({"path":"/dev/lib/tms9918c/screen2_text.c","name":"screen2_text.c"},)"
         << R"({"path":"/dev/lib/tms9918c/screen2_pixel.c","name":"screen2_pixel.c"},)"
         << R"({"path":"/dev/lib/tms9918c/screen2_geom.c","name":"screen2_geom.c"},)"
         << R"({"path":"/dev/lib/tms9918c/screen1_ext.c","name":"screen1_ext.c"},)"
         << R"({"path":"/dev/lib/tms9918c/screen2_ext.c","name":"screen2_ext.c"},)"
         << R"({"path":"/dev/lib/tms9918c/sprites.c","name":"sprites.c"},)"
         << R"({"path":"/dev/lib/tms9918c/sprite_shadow.c","name":"sprite_shadow.c"},)"
         << R"({"path":"/dev/lib/tms9918c/vsync.c","name":"vsync.c"},)"
         << R"({"path":"/dev/lib/tms9918c/printlib.c","name":"printlib.c"},)"
         << R"({"path":"/dev/lib/gfx/gfx_line.c","name":"gfx_line.c"},)"
         << R"({"path":"/dev/lib/gfx/gfx_rect.c","name":"gfx_rect.c"},)"
         << R"({"path":"/dev/lib/gfx/gfx_circle.c","name":"gfx_circle.c"},)"
         << R"({"path":"/dev/lib/gfx/gfx_ellipse.c","name":"gfx_ellipse.c"},)"
         << R"({"path":"/dev/lib/gfx/gfx_num_dec.c","name":"gfx_num_dec.c"},)"
         << R"({"path":"/dev/lib/gfx/gfx_num_hex.c","name":"gfx_num_hex.c"},)"
         << R"({"path":"/dev/lib/gfx/gfx_text.c","name":"gfx_text.c"},)"
         << R"({"path":"/dev/lib/gfx/gfx_backend_tms.c","name":"gfx_backend_tms.c"},)"
         << R"({"path":"/dev/lib/gfx/gfx_backend_tms_rect.c","name":"gfx_backend_tms_rect.c"},)"
         << R"({"path":"/dev/lib/gfx/gfx_text_backend_tms.c","name":"gfx_text_backend_tms.c"}],)"
         << R"("asmSources":[{"path":"/dev/lib/tms9918c/apple1_asm.s","name":"apple1_asm.s"},)"
         << R"({"path":"/dev/lib/tms9918c/tms_fast.s","name":"tms_fast.s"})";
    for (const std::string& ea : extraAsmAbs) {
        const std::string wp = benchAbsToWasmDev(ea);
        if (wp.empty()) continue;
        spec << ",{\"path\":" << jsonQuoted(wp) << ",\"name\":"
             << jsonQuoted(std::filesystem::path(ea).filename().string()) << "}";
    }
    spec << "]}";
    return spec.str();
}

static void applySketchAssets(const std::string& sourcePath, std::string& asset, uint16_t& addr)
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

static AsmProjectCtx probeAsmProject(const std::string& sourcePath)
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
    std::string line, loadCfg, cfgDefault, extra, v;
    while (std::getline(f, line)) {
        if      (benchMakeVar(line, "LOAD_CFG",  v)) loadCfg    = v;
        else if (benchMakeVar(line, "EXTRA_ASM", v)) extra      = v;
        else if (benchMakeVar(line, "CFG",       v)) cfgDefault = v;
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

    probeDualBankFromCfg(p.cfg, p);

    p.ok = true;
    return p;
}

// Machine-readable header prepended to DevBench "Build output" so humans and
// agents (IDE assistants, CI triage) can interpret the log without UI context.
struct BuildLogMeta {
    const char* action = "verify";   // verify | run
    const P1T*  target = nullptr;
    std::string sourcePath;
    std::string cfgPath;
    const AsmProjectCtx* proj = nullptr;
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
    if (m.proj && m.proj->ok) {
        os << "# project_ctx: sketch_sidecar_or_makefile\n";
        if (m.proj->dualBank)
            os << "# load_map: dual_bank lo=$" << std::hex << m.proj->loAddr
               << " hi=$" << m.proj->hiAddr << std::dec << "\n";
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
const char* const kCc65InstallHint =
    "\nInstall the cc65 toolchain, then reopen the Bench:\n"
    "  Debian/Ubuntu : sudo apt install cc65\n"
    "  Fedora        : sudo dnf install cc65\n"
    "  Arch          : sudo pacman -S cc65\n"
    "  macOS         : brew install cc65\n"
    "  Windows/other : https://cc65.github.io/  (add its bin/ to PATH)\n";

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
static void ensureCc65Home(const std::string& binaryPath)
{
#if !POM1_IS_WASM
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
#else
    (void)binaryPath;
#endif
}

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
    toolchainOk_ = !ca65_.empty() && !ld65_.empty();
    ensureCc65Home(!ca65_.empty() ? ca65_ : cl65_);

    // The Bench's linker cfgs + libraries live under dev/. Release bundles can
    // ship a dev/ subtree next to the app; probe exe-relative too so it resolves
    // even though a packaged app chdir'd to a user-data dir at startup.
    std::vector<std::string> devCandidates = {"dev", "../dev", "../../dev"};
    if (!exeDir.empty()) {
        const fs::path e(exeDir);
        devCandidates.push_back((e / "dev").string());                            // Win ZIP / generic
        devCandidates.push_back((e.parent_path() / "share" / "POM1" / "dev").string()); // Linux AppImage
        devCandidates.push_back((e.parent_path() / "Resources" / "dev").string());      // macOS .app
    }
    std::string& devRoot = devRoot_;
    devRoot.clear();
    for (const auto& p : devCandidates)
        if (fs::exists(fs::path(p) / "cc65", ec)) { devRoot = p; break; }
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
    // languages: 0=asm, 1=C, 2=BASIC. machines: 0=Apple-1 text, 1=TMS9918,
    // 2=GEN2 HGR (asm/C use these three); 3=Applesoft Lite + microSD, 4=Applesoft
    // GEN2 HGR, 5=Applesoft TMS9918, 6=Integer BASIC (Apple-1 dual-rom) (BASIC uses
    // these four). CodeBench's New dialog shows only the machines valid for the lang.
    if (language == 0) return (machine >= 0 && machine <= 2) ? machine     : -1;  // asm 0..2
    if (language == 1) return (machine >= 0 && machine <= 2) ? 3 + machine : -1;  // C   3..5
    if (language == 2) {                                                          // BASIC
        switch (machine) {
            case 3: return 8;    // Applesoft Lite + microSD ($6000)
            case 4: return 9;    // Applesoft GEN2 HGR ($9800)
            case 5: return 11;   // Applesoft TMS9918 (CodeTank $4000)
            case 6: return 7;    // Integer BASIC (Apple-1 dual-rom, $E000)
#if !POM1_IS_WASM
            case 7: return 12;   // Applesoft GEN2 (native compile, $0300) — desktop only
            case 8: return 13;   // Applesoft TMS9918 (native compile, $0300) — desktop only
#endif
        }
        return -1;
    }
    return -1;
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
    if (ext == ".hex" || ext == ".txt") return 6;      // Wozmon hex quick-load

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
            if (mw_ && mw_->graphicsCardEnabled) return 9;    // Applesoft GEN2 ($9800)
            if (mw_ && mw_->tms9918Enabled)      return 11;   // Applesoft TMS9918 ($4000)
            if (mw_ && mw_->microSDEnabled)      return 8;    // Applesoft Lite + microSD
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
        machine = (mw_ && mw_->graphicsCardEnabled) ? 2 : 1;   // GEN2 if live, else TMS
    else
        machine = tms ? 1 : gen2 ? 2 : 0;                      // default = Apple-1
    return (cMode ? 3 : 0) + machine;                  // kP1Targets language-major order
}

void Pom1BenchHost::enableSketchSidecarCards(EmulationController* emu)
{
    if (!mw_ || !emu) return;
    if (sourcePathLooksGT6144(activeSourcePath_)) {
        mw_->gt6144Enabled = true;
        mw_->showGT6144 = true;
        emu->setGT6144Enabled(true);
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

    if (t.mode == 4) {
        // BASIC: cold-start the matching interpreter (empty listing, no RUN) so its
        // prompt is ready. injectBasic loads the ROM, resets and types the cold-start.
        bench::BuildResult ib = injectBasic(target, std::string(), /*run=*/false);
        r.ok = ib.ok;
        r.status = ib.ok ? (std::string(t.label) + " — ready") : ib.status;
        if (!ib.ok) { r.console = ib.console; r.showConsole = true; }   // surface ROM errors
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
        const bool haveCard = (t.preset == 2) ? mw_->graphicsCardEnabled
                            : (t.preset == 1) ? mw_->tms9918Enabled
                            : (t.preset == 8) ? mw_->microSDEnabled
                            : true;
        if (haveCard) return;
    }

    // The bench is driving the preset change here — the user already picked a
    // target (which sets the bench's own sketch). Do not let the DevBench preset
    // auto-load overwrite that with the asm starter.
    mw_->suppressDevBenchAutoload = true;
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
        namespace fs = std::filesystem;
        std::error_code ec;
        std::string found;
        for (const char* pre : {"", "../", "../../"}) {
            const fs::path p = fs::path(pre) / e.data;
            if (fs::exists(p, ec)) { found = p.string(); break; }
        }
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
    const fs::path dir = fs::temp_directory_path(ec);
    if (ec || dir.empty()) { r.status = "no temp directory available"; r.ok = false; return r; }
    TempFileSweeper sweep;
    auto* emu = mw_->emulation.get();
    // Same deferred-plug fix as build() / pollBuild(): drain pending plugs so
    // the new preset's cards are on the bus before the CPU runs the freshly
    // loaded binary. Otherwise a `New` + `directLoad` (Wozmon hex mode)
    // immediately after a preset switch races the card-enable countdown and
    // the first frame of execution writes into RAM instead of the card.
    mw_->finalizePendingCardPlugs();
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

bench::BuildResult Pom1BenchHost::injectBasic(int target, const std::string& src, bool run)
{
    bench::BuildResult r; r.showConsole = true;
    const P1T& t = kP1Targets[p1(target)];
    auto* emu = mw_ ? mw_->emulation.get() : nullptr;
    if (!emu) { r.status = "no emulator"; r.ok = false; return r; }

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

    namespace fs = std::filesystem;
    std::error_code ec;
    auto findRom = [&](std::initializer_list<const char*> cands) -> std::string {
        for (const char* c : cands) if (fs::exists(c, ec)) return c;
        return {};
    };

    // 1) For the TMS9918 Applesoft, the interpreter lives in the UPPER bank of the
    //    unified CODETANKDEV cartridge (roms/codetank/CODETANKDEV.rom) — the lower
    //    bank is the DevBench's asm/C flash slot. Resolve + validate the image UP
    //    FRONT so a missing ROM aborts with the machine completely unchanged (no
    //    preset switch, no half-plugged card, no default GAME1 cartridge).
    std::string tmsCartPath;
    if (tms) {
        tmsCartPath = codeTankDevRomReadPath();
        if (tmsCartPath.empty()) {
            r.console = std::string("[bench] ") + interp +
                ": CODETANKDEV.rom not found — build it with "
                "tools/build_codetank_rom.py --rom dev\n"
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
            r.console = std::string("[bench] ") + interp + ": CODETANKDEV.rom load FAILED — " + err + "\n";
            r.status = std::string(interp) + ": ROM load failed";
            r.ok = false;
            return r;
        }
        // When the switch to preset 1 was applied fresh (e.g. the Mode selector, or a
        // Run from another preset), applyMachineConfig queued the preset's DEFAULT
        // CodeTank ROM (Codetank_GAME1.rom) for the still-pending plug. Clear that path
        // now so finalizePendingCardPlugs() does NOT reload GAME1 over the Applesoft
        // cartridge we just flashed.
        mw_->pendingCodeTankRomPath.clear();
    }
    mw_->finalizePendingCardPlugs();

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
    std::string romErr;
    bool romOk = true;
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

    // Prep-only call: selectTargetExplicit passes an empty listing (run=false) just to
    // ready the interpreter prompt when the user picks a BASIC target. Compiling an
    // empty listing would fail with "no BASIC lines to compile" and surface as a scary
    // selection error, so cold-start the ROM to its prompt and report success instead.
    if (src.empty() && !run) {
        constexpr uint64_t kColdStartCycles = 12'000'000;
        const uint16_t coldEntry =
            idx == 8  ? 0x6000 : idx == 9  ? 0x9800 :
            idx == 10 ? 0xE000 : idx == 11 ? 0x4000 : ibasic::kColdStart;  // else idx 7
        emu->runFromSync(coldEntry, kColdStartCycles);
        emu->copySnapshot(mw_->uiSnapshot);
        r.status = std::string(interp) + " — ready";
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
        if (!run) {  // Verify = tokenise-check only
            emu->copySnapshot(mw_->uiSnapshot);
            std::snprintf(buf, sizeof(buf),
                "[bench] Integer BASIC: tokenised OK — %d lines, image $%04X-$%04X (%zu B) "
                "(Run to load + run)\n", prog.lineCount, prog.pp, prog.himem - 1, prog.image.size());
            r.console = buf;
            std::snprintf(buf, sizeof(buf), "Integer BASIC: tokenised (%d lines)", prog.lineCount);
            r.status  = buf; r.ok = true;
            return r;
        }
        // Cold-start Integer BASIC to its `>` prompt (inits lomem=$0800, himem=$1000,
        // zero page), then poke the program image at pp, set pp ($CA/$CB), and enter
        // the ROM's RUN handler ($EFEC = clr + run_warm) live.
        constexpr uint64_t kColdCycles = 12'000'000;
        emu->runFromSync(ibasic::kColdStart, kColdCycles);
        // Lower LOMEM ($4A/$4B) below the cold default so variables + program fit
        // (HIMEM stays at the cold $1000). $EFEC's CLR re-reads LOMEM for the var base.
        emu->writeMemory(0x004A, static_cast<uint8_t>(kIntLomem & 0xFF));
        emu->writeMemory(0x004B, static_cast<uint8_t>((kIntLomem >> 8) & 0xFF));
        for (size_t i = 0; i < prog.image.size(); ++i)
            emu->writeMemory(static_cast<uint16_t>(prog.pp + i), prog.image[i]);
        emu->writeMemory(ibasic::kPpZp,     static_cast<uint8_t>(prog.pp & 0xFF));
        emu->writeMemory(ibasic::kPpZp + 1, static_cast<uint8_t>((prog.pp >> 8) & 0xFF));
        emu->runFromAsync(0xEFEC);   // RUN command handler: clr + run_warm
        emu->copySnapshot(mw_->uiSnapshot);
        std::snprintf(buf, sizeof(buf),
            "[bench] Integer BASIC: tokenised %d lines → loaded %zu B @ $%04X, running "
            "(tokeniser — no keyboard injection)\n", prog.lineCount, prog.image.size(), prog.pp);
        r.console = buf;
        r.status  = "Integer BASIC: running (tokenised)";
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
        basic::Target tgt; uint16_t coldEntry;
        switch (idx) {
            case 8:  tgt = basic::targetMicrosd(); coldEntry = 0x6000; break;
            case 9:  tgt = basic::targetGen2();    coldEntry = 0x9800; break;
            case 10: tgt = basic::targetCffa1();   coldEntry = 0xE000; break;
            default: tgt = basic::targetTms();     coldEntry = 0x4000; break;  // idx 11
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
        if (!run) {  // Verify = compile-check only; don't disturb the machine further.
            restoreRelaxedMachine();
            emu->copySnapshot(mw_->uiSnapshot);
            std::snprintf(buf, sizeof(buf),
                "[bench] %s: tokenised OK — %d lines, program $0801-$%04X, launcher $%04X "
                "(Run to load + launch)\n", interp, prog.lineCount,
                prog.progEnd ? prog.progEnd - 1 : 0x0800, prog.entry);
            r.console = buf;
            std::snprintf(buf, sizeof(buf), "%s: tokenised (%d lines)", interp, prog.lineCount);
            r.status  = buf; r.ok = true;
            return r;
        }

        // Cold-start the interpreter ROM (initialises CHRGET, HIMEM/FRETOP, output
        // vector, FP scratch), then load the compiled image + launcher and jump to
        // the launcher. loadHexDump preserves the cold-started zero page
        // (cpu->hardReset clears only the stack, never RAM) and starts the async
        // CPU at the launcher's run address.
        emu->runFromSync(coldEntry, kColdStartCycles);

        std::string err; uint16_t loadedEntry = 0; int loaded = 0;
        const fs::path tmp = fs::temp_directory_path(ec) / "pom1_basic_tokenized.txt";
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
    const bool gen2 = (t.preset == 2);   // GEN2 HGR card; else TMS9918 (preset 1)

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

    const fs::path dir = fs::temp_directory_path(ec);
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
    mw_->finalizePendingCardPlugs();
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

    if (t.mode == 4) {     // BASIC: no compile — type the listing into the in-ROM
        return injectBasic(target, src, run);   // interpreter (works on WASM too).
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
        std::string cfg, spec;
        if (cfgTag == "C-plain") {
            cfg  = "/dev/cc65/apple1_c.cfg";
            spec = R"({"cfg":"/dev/cc65/apple1_c.cfg","incDirs":["/dev/lib/apple1c","/dev/lib/telemetry"],)"
                   R"("cSources":[{"path":"/dev/lib/apple1c/apple1io.c","name":"apple1io.c"}],)"
                   R"("asmSources":[{"path":"/dev/lib/apple1c/apple1io_asm.s","name":"apple1io_asm.s"}]})";
        } else if (cfgTag == "C-gen2") {
            // The gen2c runtime is split into per-family modules (init/pixel/
            // rect/text/sprites/geom/lores) so ld65 can dead-strip per family;
            // the Bench links the lot since a sketch may call anything. gen2_geom
            // forwards to the card-neutral gfx layer (Axis 1), so its sources
            // ride along — matching the desktop GEN2 C Bench command.
            cfg  = "/dev/cc65/apple1_gen2_c.cfg";
            spec = R"({"cfg":"/dev/cc65/apple1_gen2_c.cfg","defines":["POM1_GFX_GEN2"],"incDirs":["/dev/lib/gen2c","/dev/lib/apple1c","/dev/lib/gfx","/dev/lib/telemetry"],)"
                   R"("cSources":[{"path":"/dev/lib/gen2c/gen2_init.c","name":"gen2_init.c"},{"path":"/dev/lib/gen2c/gen2_pixel.c","name":"gen2_pixel.c"},)"
                   R"({"path":"/dev/lib/gen2c/gen2_rect.c","name":"gen2_rect.c"},{"path":"/dev/lib/gen2c/gen2_text.c","name":"gen2_text.c"},)"
                   R"({"path":"/dev/lib/gen2c/gen2_sprites.c","name":"gen2_sprites.c"},{"path":"/dev/lib/gen2c/gen2_geom.c","name":"gen2_geom.c"},)"
                   R"({"path":"/dev/lib/gen2c/gen2_lores.c","name":"gen2_lores.c"},{"path":"/dev/lib/apple1c/apple1io.c","name":"apple1io.c"},)"
                   R"({"path":"/dev/lib/gfx/gfx_line.c","name":"gfx_line.c"},{"path":"/dev/lib/gfx/gfx_rect.c","name":"gfx_rect.c"},)"
                   R"({"path":"/dev/lib/gfx/gfx_circle.c","name":"gfx_circle.c"},{"path":"/dev/lib/gfx/gfx_ellipse.c","name":"gfx_ellipse.c"},)"
                   R"({"path":"/dev/lib/gfx/gfx_num_dec.c","name":"gfx_num_dec.c"},{"path":"/dev/lib/gfx/gfx_num_hex.c","name":"gfx_num_hex.c"},)"
                   R"({"path":"/dev/lib/gfx/gfx_text.c","name":"gfx_text.c"},)"
                   R"({"path":"/dev/lib/gfx/gfx_backend_gen2.c","name":"gfx_backend_gen2.c"},{"path":"/dev/lib/gfx/gfx_backend_gen2_rect.c","name":"gfx_backend_gen2_rect.c"},)"
                   R"({"path":"/dev/lib/gfx/gfx_text_backend_gen2.c","name":"gfx_text_backend_gen2.c"}],)"
                   R"("asmSources":[{"path":"/dev/lib/gen2c/gen2_blit.s","name":"gen2_blit.s"},{"path":"/dev/lib/apple1c/apple1io_asm.s","name":"apple1io_asm.s"}]})";
        } else {   // "C" = TMS9918 CodeTank ROM
            cfg  = "/dev/lib/tms9918c/cc65/codetank_c.cfg";
            {
                const AsmProjectCtx proj = probeSketchProject(activeSourcePath_);
                spec = wasmTms9918cBuildSpec(proj.extraAsm);
            }
        }
        uint16_t entry = parseCfgLoadAddr(cfg);
        if (entry == 0) entry = (cfgTag == "C-plain") ? 0x0300 : (cfgTag == "C-gen2") ? 0x6000 : 0x4000;
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
            specExtra = ",\"asmSources\":" + srcs + ",\"defines\":" + defs
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
        std::string cmd; const char* tag;
        // Header-only telemetry side channel (telemetry.h) — include dir only,
        // available to every C target so any sketch can emit telemetry frames.
        const std::string tele = telemetryLib_.empty() ? std::string()
            : " -I " + bench::shellQuote(telemetryLib_);
        if (gen2c) {
            tag = "GEN2 HGR";
            logMeta.cfgPath = gen2Cfg_;
            // Optionally fold in the shared apple1c text base so GEN2 C programs
            // can also print to the WOZ terminal / read the keyboard.
            std::string a1c;
            if (!apple1cLib_.empty())
                a1c = " -I " + bench::shellQuote(apple1cLib_) +
                      " " + bench::shellQuote(apple1cLib_ + "/apple1io.c") +
                      " " + bench::shellQuote(apple1cLib_ + "/apple1io_asm.s");
            // Card-neutral gfx layer (dev/lib/gfx). Compiled FROM SOURCE here so
            // edits apply live; the GEN2 backend's gfx_plot resolves to
            // gen2_hgr_plot (in gen2_pixel.c). A sketch can #include "gfx.h"
            // and call gfx_line/circle/ellipse/utoa. (Shipped builds under
            // dev/projects use the pruning gfx-gen2.lib instead so an unused
            // layer costs 0 bytes; a Bench binary is throwaway.)
            std::string gfx;
            if (!gfxLib_.empty())
                gfx = " -I " + bench::shellQuote(gfxLib_) +
                      " " + bench::shellQuote(gfxLib_ + "/gfx_line.c") +
                      " " + bench::shellQuote(gfxLib_ + "/gfx_rect.c") +
                      " " + bench::shellQuote(gfxLib_ + "/gfx_circle.c") +
                      " " + bench::shellQuote(gfxLib_ + "/gfx_ellipse.c") +
                      " " + bench::shellQuote(gfxLib_ + "/gfx_num_dec.c") +
                      " " + bench::shellQuote(gfxLib_ + "/gfx_num_hex.c") +
                      " " + bench::shellQuote(gfxLib_ + "/gfx_text.c") +
                      " " + bench::shellQuote(gfxLib_ + "/gfx_backend_gen2.c") +
                      " " + bench::shellQuote(gfxLib_ + "/gfx_backend_gen2_rect.c") +
                      " " + bench::shellQuote(gfxLib_ + "/gfx_text_backend_gen2.c");
            // The gen2c runtime is split into per-family modules so ld65 can
            // dead-strip per family; the Bench links the lot since a sketch may
            // call anything. Order is link-order irrelevant; kept stable for
            // readability.
            // -DPOM1_GFX_GEN2: lets a card-agnostic ("portable") sketch pick the
            // GEN2 bring-up with #if defined(POM1_GFX_GEN2) (the TMS target sets
            // POM1_GFX_TMS) without the author passing -D by hand.
            cmd = bench::shellQuote(cl65_) + " -t none -Oirs -DPOM1_GFX_GEN2 -C " + bench::shellQuote(gen2Cfg_) +
                " -I " + bench::shellQuote(gen2cLib_) + a1c + gfx + tele + " " + bench::shellQuote(srcC.string()) +
                " " + bench::shellQuote(gen2cLib_ + "/gen2_init.c") +
                " " + bench::shellQuote(gen2cLib_ + "/gen2_pixel.c") +
                " " + bench::shellQuote(gen2cLib_ + "/gen2_rect.c") +
                " " + bench::shellQuote(gen2cLib_ + "/gen2_text.c") +
                " " + bench::shellQuote(gen2cLib_ + "/gen2_sprites.c") +
                " " + bench::shellQuote(gen2cLib_ + "/gen2_geom.c") +
                " " + bench::shellQuote(gen2cLib_ + "/gen2_lores.c") +
                " " + bench::shellQuote(gen2cLib_ + "/gen2_blit.s") +
                " -o " + bench::shellQuote(binB.string());
        } else if (plainc) {
            tag = "Apple-1 text";
            logMeta.cfgPath = plainCfg_;
            const std::string& lib = apple1cLib_;   // shared, card-neutral text base
            cmd = bench::shellQuote(cl65_) + " -t none -Oirs -C " + bench::shellQuote(plainCfg_) +
                " -I " + bench::shellQuote(lib) + tele + " " + bench::shellQuote(srcC.string()) +
                " " + bench::shellQuote(lib + "/apple1io.c") + " " + bench::shellQuote(lib + "/apple1io_asm.s") +
                " -o " + bench::shellQuote(binB.string());
        } else {
            tag = "CodeTank ROM";
            const AsmProjectCtx proj = probeSketchProject(activeSourcePath_);
            std::string cfgUse = codetankCfg_;
            if (proj.ok && !proj.cfg.empty()) {
                cfgUse = proj.cfg;
                logMeta.cfgPath = cfgUse;
            } else {
                logMeta.cfgPath = codetankCfg_;
            }
            const std::string& lib = videocardLib_;
            std::string gfxInc;
            if (!gfxLib_.empty()) gfxInc = " -I " + bench::shellQuote(gfxLib_);
            std::string extraObjs;
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
            // -DPOM1_GFX_TMS: the TMS twin of the GEN2 target's define above, so
            // a portable sketch selects the TMS bring-up at compile time.
            cmd = bench::shellQuote(cl65_) + " -t none -Oirs -DPOM1_GFX_TMS -C " + bench::shellQuote(cfgUse) +
                " -I " + bench::shellQuote(lib) + gfxInc + tele + " " + bench::shellQuote(srcC.string()) +
                tms9918cBenchRuntimeCl65Args(lib, gfxLib_) + extraObjs +
                " -o " + bench::shellQuote(binB.string());
        }
        std::string out;
        const int rc = bench::runCapture(cmd, out);
        r.console = std::string("$ cl65 -t none [") + tag + "]\n" + out;
        if (rc != 0) { parseErrorMarkers(out, r.errors); r.console += humanizeCc65(out); r.status = "cl65 failed (see Build output)"; return r; }
        r.console += std::string("[ok] compiled + linked (") + tag + ")\n";
        entry = gen2c ? parseCfgLoadAddr(gen2Cfg_) : plainc ? parseCfgLoadAddr(plainCfg_) : 0x4000;
        if (entry == 0) entry = plainc ? 0x0300 : 0x6000;
    } else {
        if (!toolchainOk_) { r.console = std::string("cc65 (ca65/ld65) not found.\n") + kCc65InstallHint; r.status = "cc65 missing"; return r; }
        const fs::path srcS = dir / "pom1_bench.s";
        const fs::path objO = dir / "pom1_bench.o";
        sweep.add(srcS); sweep.add(objO);
        std::ofstream(srcS, std::ios::binary).write(src.data(), static_cast<std::streamsize>(src.size()));
        // If the editor's file is a real sketch or dev/projects/ source (sidecar
        // .sketch.json or sibling Makefile), build it in context: its own cfg,
        // and the EXTRA_ASM siblings. Empty path / no Makefile -> bare sketch path.
        const AsmProjectCtx proj = probeAsmProject(activeSourcePath_);
        std::string asmFlags = libFlags_;
        std::string extraObjs;   // " obj ..." appended to the ld65 link line
        std::string cfgPath;
        if (proj.ok) {
            cfgPath  = proj.cfg;
            logMeta.cfgPath = cfgPath;
            logMeta.proj = &proj;
            asmFlags += "-I " + bench::shellQuote(proj.dir.string()) + " ";
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
            if (cfgPath.empty())
                for (const char* pre : {"dev/cc65/", "../dev/cc65/", "../../dev/cc65/"}) {
                    const fs::path p = fs::path(pre) / t.cfg;
                    if (fs::exists(p, ec)) { cfgPath = fs::absolute(p, ec).string(); break; }
                }
            if (cfgPath.empty()) { r.console = std::string("linker cfg not found (needs dev/): ") + t.cfg + "\n"; r.status = "ld65 cfg missing"; return r; }
        }
        if (!cfgPath.empty() && logMeta.cfgPath.empty())
            logMeta.cfgPath = cfgPath;
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
            const std::string ld = bench::shellQuote(ld65_) + " -C " + bench::shellQuote(cfgPath) + " " +
                bench::shellQuote(objO.string()) + extraObjs + " -o " + bench::shellQuote(base.string());
            rc = bench::runCapture(ld, out);
            r.console += "$ ld65 -C " + cfgPath + " (dual-bank)\n" + out;
            if (rc != 0) { r.console += humanizeCc65(out); r.status = "ld65 failed (see Build output)"; return r; }
            r.console += "[ok] assembled + linked (dual-bank)\n";
            if (!run) { r.status = "Verify OK"; r.ok = true; return r; }
            if (t.preset >= 0) onTargetSelected(target);
            mw_->finalizePendingCardPlugs();
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
                emu->copySnapshot(mw_->uiSnapshot);
                if (t.preset == 2) mw_->showGraphicsCard = true;
                char msg[176]; std::snprintf(msg, sizeof(msg), "Built dual-bank ($%04X+$%04X) run @ $%04X",
                                             proj.loAddr, proj.hiAddr, runAddr);
                r.status = msg; r.ok = true;
            } else { r.status = "dual-bank run load failed: " + error; r.ok = false; }
            return r;
        }

        const std::string ld = bench::shellQuote(ld65_) + " -C " + bench::shellQuote(cfgPath) + " " +
            bench::shellQuote(objO.string()) + extraObjs + " -o " + bench::shellQuote(binB.string());
        rc = bench::runCapture(ld, out);
        r.console += "$ ld65 -C " + cfgPath + "\n" + out;
        if (rc != 0) { r.console += humanizeCc65(out); r.status = "ld65 failed (see Build output)"; return r; }
        r.console += "[ok] assembled + linked\n";
        entry = parseCfgLoadAddr(cfgPath);
        if (entry == 0) { try { entry = static_cast<uint16_t>(std::stoul(addrHex, nullptr, 16)); } catch (...) { entry = 0x0300; } }
    }

    if (!run) { r.status = "Verify OK"; r.ok = true; return r; }

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
    mw_->finalizePendingCardPlugs();
    enableSketchSidecarCards(emu);
    ejectTapeForAciProgramOutput(emu, r, t.preset);
    if (gen2c || plainc) {
        // GEN2 HGR / plain text C: load + run (the target's preset already plugged
        // the right card). loadBinary resets + runs at the cfg's entry.
        std::string error; int bytesLoaded = 0;
        if (emu->loadBinary(binB.string(), entry, error, &bytesLoaded)) {
            emu->copySnapshot(mw_->uiSnapshot);
            if (gen2c && t.preset == 2) mw_->showGraphicsCard = true;
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
        // Flash the lower bank, preserving the Applesoft TMS9918 upper bank; abort
        // loudly if the write didn't land instead of booting a stale cartridge.
        std::string error;
        if (!flashCodeTankDevRom(binB.string(), romPath.string(), error)) {
            r.status = "CODETANKDEV.rom flash failed: " + error; r.ok = false; return r;
        }
        if (!emu->loadCodeTankRom(romPath.string(), error)) { r.status = "CODETANKDEV.rom load failed: " + error; r.ok = false; return r; }
        mw_->codeTankJumper = CodeTank::Jumper::Lower16;
        emu->setCodeTankJumper(mw_->codeTankJumper);
        if (!mw_->tms9918Enabled) { mw_->tms9918Enabled = true; mw_->showTMS9918 = true; emu->setTMS9918Enabled(true); }
        if (!mw_->codeTankEnabled) { mw_->codeTankEnabled = true; emu->setCodeTankEnabled(true); }
        emu->hardReset(/*animateBoot=*/false); // DevBench: no ~3 s power-on scenario
        mw_->codeTankPendingWozRunAt = ImGui::GetTime() + 1.0;
        emu->copySnapshot(mw_->uiSnapshot);
        r.console += "[ok] flashed CODETANKDEV.rom (lower bank) - 4000R\n";
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
        emu->copySnapshot(mw_->uiSnapshot);
        if (t.preset == 2) mw_->showGraphicsCard = true;
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
    mw_->finalizePendingCardPlugs();
    enableSketchSidecarCards(emu);
    ejectTapeForAciProgramOutput(emu, r, t.preset);

    if (t.codetankRom) {
        // TMS9918 asm: wrap the .bin into a CodeTank dev ROM, flash it, jumper to
        // the lower 16K bank, reset + boot 4000R (mirrors the desktop path). Write
        // target is a writable copy (POM1_CODETANK_DEV_DIR in an AppImage), so a
        // read-only packaged roms/ no longer makes the flash fail silently.
        fs::path romPath = codeTankDevRomWritePath();
        if (romPath.empty()) romPath = "/tmp/CODETANKDEV.rom";
        std::string error;
        if (!flashCodeTankDevRom("/tmp/pom1_bench.bin", romPath.string(), error)) {
            r.status = "CODETANKDEV.rom flash failed: " + error; r.ok = false; return r;
        }
        if (!emu->loadCodeTankRom(romPath.string(), error)) { r.status = "CODETANKDEV.rom load failed: " + error; r.ok = false; return r; }
        mw_->codeTankJumper = CodeTank::Jumper::Lower16;
        emu->setCodeTankJumper(mw_->codeTankJumper);
        if (!mw_->tms9918Enabled) { mw_->tms9918Enabled = true; mw_->showTMS9918 = true; emu->setTMS9918Enabled(true); }
        if (!mw_->codeTankEnabled) { mw_->codeTankEnabled = true; emu->setCodeTankEnabled(true); }
        emu->hardReset(/*animateBoot=*/false); // DevBench: no ~3 s power-on scenario
        mw_->codeTankPendingWozRunAt = ImGui::GetTime() + 1.0;
        emu->copySnapshot(mw_->uiSnapshot);
        r.console += "[ok] flashed CODETANKDEV.rom (lower bank) - 4000R\n";
        r.status = "CODETANKDEV.rom flashed - boot 4000R"; r.ok = true;
        return r;
    }

    // other asm targets: loadBinary at the cfg entry + run
    std::string error; int bytesLoaded = 0;
    if (emu->loadBinary("/tmp/pom1_bench.bin", wasmJobEntry_, error, &bytesLoaded)) {
        emu->copySnapshot(mw_->uiSnapshot);
        if (t.preset == 2) mw_->showGraphicsCard = true;
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
    if (t.mode == 1 || t.mode == 4) return true;   // hex + BASIC need no toolchain
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
    mw_->startCpu();  // resume free-running (same as the CPU menu's Run)
}

bool Pom1BenchHost::cpuIsRunning() const
{
    return mw_->cpuRunning;  // UI-thread mirror of the run state (friend access)
}

std::string Pom1BenchHost::browseDir() const
{
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const char* p : {"sketchs", "../sketchs", "../../sketchs",
                          "dev/sketchs", "../dev/sketchs", "../../dev/sketchs"})
        if (fs::exists(p, ec)) return fs::absolute(p, ec).string();
    for (const char* p : {"dev", "../dev", "../../dev"})
        if (fs::exists(p, ec)) return fs::absolute(p, ec).string();
    return ".";
}

void Pom1BenchHost::openSerial()
{
    mw_->showTelemetry = true;
}
