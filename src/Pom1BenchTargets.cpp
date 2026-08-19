// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Pom1BenchTargets.cpp — the DevBench's (language x machine) TARGET TABLE and
// the starter sketches it points at, plus the two path helpers both bench TUs
// need. Data, not logic: same idea as MachinePresets.cpp for the machine
// presets, and for the same reason — Pom1BenchHost.cpp was a 3957-line god
// file in which ~400 lines of embedded hello-world listings sat between the
// build pipeline and the language injectors.
//
// The `.preset` column uses the NAMED preset indices (md::kPreset*), never a
// literal: a kMachinePresets[] reorder must stay a one-line edit in
// MachinePresets.h rather than a silent DevBench breakage.

#include "Pom1BenchTargets.h"
#include "MachinePresets.h"       // the named kPreset* indices — NOT MainWindow_Internal.h,
                                  // which would drag imgui.h into this data TU

#include <cstdlib>
#include <string>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace md = pom1;

namespace pom1::benchhost {

// HELLO-WORLD starters, one per (language x machine) the New dialog offers.
extern const char* const kSketchAsm =          // asm x Apple dual-4k/8k (text)
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
extern const char* const kSketchAsmTms =       // asm x TMS9918 (Graphics I text, loads a font)
    "; HELLO WORLD on the TMS9918 (Graphics I). Proper init: regs loaded with the\n"
    "; display BLANKED, all 16 KB VRAM cleared, sprites parked, then font/colour/\n"
    "; message loaded and the screen turned on LAST. VDP data=$CC00, ctrl=$CC01.\n"
    "; Upload flashes this into CODETANKDEV.rom (bank picked in the toolbar) and\n"
    "; boots 4000R - it runs in place from the ROM window, like every TMS9918 program.\n"
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
extern const char* const kSketchAsmGen2 =      // asm x GEN2 HGR (BBFont text)
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
extern const char* const kSketchCText =        // C x Apple dual-4k/8k (WozMon I/O)
    "/* HELLO WORLD in C on a plain text Apple-1, using the shared apple1c text\n"
    "   base (woz_puts/apple1_getkey). The same apple1c.h works on the GEN2 card. */\n"
    "#include \"apple1c.h\"\n"
    "\n"
    "void main(void) {\n"
    "    woz_puts((const unsigned char *)\"\\rHELLO WORLD (C / Apple-1)\\r\");\n"
    "    woz_mon();\n"
    "}\n";
extern const char* const kSketchHex =
    "; POM1 Bench - Wozmon hex. Upload loads + runs (addresses are in the text).\n"
    "0300: A9 A1 20 EF FF 4C 00 03\n"
    "0300R\n";
extern const char* const kSketchC =
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
extern const char* const kSketchGen2C =
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
extern const char* const kSketchBasicInteger =     // Integer BASIC ($E000, Apple-1 text)
    "10 PRINT \"HELLO FROM INTEGER BASIC\"\n"
    "20 FOR I=1 TO 5\n"
    "30 PRINT \"  LINE \";I\n"
    "40 NEXT I\n"
    "50 END\n";
extern const char* const kSketchBasicApplesoft =   // Applesoft Lite ($6000, microSD)
    "10 PRINT \"HELLO FROM APPLESOFT LITE\"\n"
    "20 FOR I = 1 TO 5\n"
    "30 PRINT \"  1/\"; I; \" = \"; 1 / I\n"
    "40 NEXT I\n"
    "50 END\n";
// Applesoft GEN2 ($9800 on the GEN2 card): the applesoft-gen2 interpreter —
// Applesoft with the GEN2 graphics command set, shipped prebuilt as
// roms/applesoft-gen2.rom (graphics-BASIC demos under sketchs/basic_applesoft).
// PRINT goes to the GEN2 screen, APRINT to the Apple-1.
extern const char* const kSketchBasicApplesoftGen2 =
    "10 HGR : HCOLOR=3\n"
    "20 HPLOT 0,0 TO 279,191\n"
    "30 HPLOT 0,191 TO 279,0\n"
    "40 GR : COLOR=13 : PLOT 20,20\n"
    "50 TEXT : HOME : VTAB 12 : HTAB 12 : PRINT \"HELLO GEN2\"\n";

// LOGO starters. The Bench cold-starts the resident APPLE-1 LOGO V2.6 interpreter,
// then pokes the procedure table directly (LogoProgramLoader) and feeds ONE entry
// line — no per-line keyboard typing (which the REPEAT break-poll would drop). The
// TO MAIN … END / MAIN shape is the recommended idiom: the body is poked, only the
// bare "MAIN" call is queued. Pure C++, so both variants run on WASM too.
// NOTE the dialect: turns are TR (turn right) / TL (turn left) — NOT RT/LT. Nested
// REPEAT is supported one level deep (the outer square-rosette below). More runnable
// programs (machine-neutral, from the LOGO manual §11) live in sketchs/logo/.
extern const char* const kSketchLogoTms =          // LOGO TMS9918 (CodeTank lower, 4000R)
    "TO MAIN\n"
    "  CS\n"
    "  REPEAT 40 [ REPEAT 4 [ FD 60 TR 90 ] TR 10]\n"
    "END\n"
    "MAIN\n";
extern const char* const kSketchLogoGen2 =         // LOGO GEN2 HGR ($6000, 6000R)
    "TO SQUARE\n"
    "  REPEAT 4 [ FD 50 TR 90 ]\n"
    "END\n"
    "TO FLOWER\n"
    "  REPEAT 36 [ TR 10 SQUARE ]\n"
    "END\n"
    "CS\n"
    "SETPC 13\n"
    "FLOWER\n";

// The stabilised language cartridge Codetank_BASIC_LOGO.rom carries BOTH
// DevBench interpreters: APPLE-1 LOGO V2.6 in the LOWER bank and the Applesoft
// TMS9918 in the UPPER bank (4000R with the matching jumper). It ships under
// roms/codetank/; unlike the CODETANKDEV flash slots it is never rewritten, so
// a read-only bundled copy is fine. Returns "" if absent anywhere.
std::string codeTankBasicLogoRomReadPath() {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (const char* env = std::getenv("POM1_CODETANK_DEV_DIR"); env && *env) {
        std::string p = (fs::path(env) / "Codetank_BASIC_LOGO.rom").string();
        if (fs::exists(p, ec)) return p;
    }
    for (const char* c : {"roms/codetank/Codetank_BASIC_LOGO.rom",
                          "../roms/codetank/Codetank_BASIC_LOGO.rom",
                          "../../roms/codetank/Codetank_BASIC_LOGO.rom"})
        if (fs::exists(c, ec)) return c;
    return {};
}

std::filesystem::path benchScratchDir(std::error_code& ec)
{
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path(ec);
    if (ec || base.empty()) return base;
#if defined(_WIN32)
    const unsigned long pid = static_cast<unsigned long>(::_getpid());
#else
    const unsigned long pid = static_cast<unsigned long>(::getpid());
#endif
    const fs::path dir = base / ("pom1_bench_" + std::to_string(pid));
    std::error_code mkec;
    fs::create_directories(dir, mkec);
    if (mkec) return base;   // fall back to the shared dir rather than fail the build
    return dir;
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
const P1T kP1Targets[] = {
    // .preset column: named preset indices (md::kPreset*) so a kMachinePresets[]
    // reorder is a one-line edit here + in MachinePresets.h, never a silent
    // DevBench mismatch. -1 = "any machine" (no preset switch).
    { "Apple-1 dual 4K/8K (asm)",         md::kPresetCC65Bench,    "apple1_4k.cfg",   "6502",  0, false, false, kSketchAsm            },
    { "P-LAB TMS9918 Graphic Card (asm)", md::kPresetTMS9918Bench, "codetank.cfg",    "6502",  0, false, true,  kSketchAsmTms         },
    { "Uncle Bernie GEN2 HGR (asm)",      md::kPresetGen2Bench,    "apple1_gen2.cfg", "6502",  0, false, false, kSketchAsmGen2        },
    { "Apple-1 dual 4K/8K (C)",           md::kPresetCC65Bench,    "C-plain",         "C",     3, true,  false, kSketchCText          },
    { "P-LAB TMS9918 CodeTank ROM (C)",   md::kPresetTMS9918Bench, "C",               "C",     3, true,  true,  kSketchC              },
    { "Uncle Bernie GEN2 HGR (C)",        md::kPresetGen2Bench,    "C-gen2",          "C",     3, true,  false, kSketchGen2C          },
    { "Wozmon hex (any machine)",         -1,                      "",                "hex",   1, false, false, kSketchHex            },
    { "Integer BASIC (interpreter, E000R)",          md::kPresetCC65Bench, "E000R", "BASIC", 4, false, false, kSketchBasicInteger   },
    { "Applesoft Lite (interpreter, microSD 6000R)", md::kPresetMicroSD,   "6000R", "BASIC", 4, false, false, kSketchBasicApplesoft },
    // Target 9: Applesoft GEN2 — BASIC injection on the GEN2 card (preset 2),
    // applesoft-gen2 interpreter ROM loaded HIGH in RAM at $9800 (HIMEM just
    // below it) so BASIC owns ~37 KB of $0801-$97FF — real-silicon faithful.
    { "Applesoft GEN2 (interpreter, 9800R)",     md::kPresetGen2Bench, "9800R", "BASIC", 4, false, false, kSketchBasicApplesoftGen2 },
    // Target 10: Applesoft Lite on a bare Apple-1 — the CFFA1 flavour ROM at
    // $E000 (roms/applesoft-lite-cffa1.rom), cold start E000R, 64 KB-relaxed.
    { "Applesoft Lite (interpreter, Apple-1 E000R)", md::kPresetCC65Bench, "E000R", "BASIC", 4, false, false, kSketchBasicApplesoft     },
    // Target 11: Applesoft TMS9918 — the applesoft-tms9918 interpreter as a
    // CodeTank ROM cartridge ($4000-$7FFF), preset 1, cold start 4000R.
    { "Applesoft TMS9918 (interpreter, 4000R)",  md::kPresetTMS9918Bench, "4000R", "BASIC", 4, false, true,  kSketchBasicApplesoftGen2 },
    // Targets 12-13: NATIVE Applesoft compiler (basicnative::compile) — standalone
    // 6502, NO interpreter (~20x faster). Mode 5 routes to compileBasicNative(),
    // which mirrors tools/basicc_native.sh (ca65 prog + minimal runtime + optional
    // float, then ld65 against basicc_native.cfg) and loadBinary+runs the result at
    // $0300. cfg=nullptr (the native build hardcodes basicc_native.cfg). DESKTOP
    // only: the ctor lists every target on both platforms (targetMap_ is the
    // identity), so these two are VISIBLE under WASM but the mode-5 dispatch in
    // build() refuses them there, and nativeSiblingOf() returns -1 so the
    // Inject/Native toggle collapses to Inject. GEN2 = preset 2
    // (HGR page 1 framebuffer); TMS = preset 1 (code runs from $0300 RAM, draws to
    // the VDP at $CC00/$CC01 — NOT a CodeTank cartridge, so codetankRom = false).
    { "Applesoft GEN2 (native, 0300R)",          md::kPresetGen2Bench,    nullptr, "BASIC", 5, false, false, kSketchBasicApplesoftGen2 },
    { "Applesoft TMS9918 (native, 0300R)",       md::kPresetTMS9918Bench, nullptr, "BASIC", 5, false, false, kSketchBasicApplesoftGen2 },
    // Targets 14-15: APPLE-1 LOGO V2.6 turtle interpreter — listing INJECTION (mode
    // 6, injectLogo). The resident interpreter is cold-started, then LogoProgramLoader
    // pokes its procedure table directly + feeds ONE entry line (no keyboard typing —
    // the REPEAT break-poll would drop pasted lines). cfg = cold-start command.
    //   14 LOGO TMS9918 — Codetank_BASIC_LOGO.rom LOWER bank ($4000, jumper Lower), preset 1,
    //      16 KB-strict (proc_table $E431, n_procs $0260). codetankRom = true.
    //   15 LOGO GEN2 HGR — roms/logo-gen2.rom loaded at $6000, preset 2, 48 KB
    //      (proc_table $B431, n_procs $02E3).
    { "LOGO TMS9918 (interpreter, 4000R)",        md::kPresetTMS9918Bench, "4000R", "LOGO",  6, false, true,  kSketchLogoTms  },
    { "LOGO GEN2 HGR (interpreter, 6000R)",       md::kPresetGen2Bench,    "6000R", "LOGO",  6, false, false, kSketchLogoGen2 },
};
const int kP1TargetCount = static_cast<int>(sizeof(kP1Targets) / sizeof(kP1Targets[0]));

} // namespace pom1::benchhost
