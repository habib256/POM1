// POM1 host for the portable bench. See Pom1BenchHost.h.
#include "Pom1BenchHost.h"

#include "MainWindow_ImGui.h"     // mw_ members (friend) + EmulationController
#include "ProcessUtil.h"          // bench::shellQuote / runCapture / whichExe
#include "imgui.h"                // ImGui::GetTime for the CodeTank cold-boot

#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

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
    "; boundaries freely. Pulls in dev/lib/gen2 + dev/lib/hgr.\n"
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
static const char* kSketchTxtGen2Asm =   // asm x GEN2 native TEXT mode ($0400)
    "; HELLO WORLD - GEN2 native TEXT mode (40x24, page $0400, the card's built-in\n"
    "; font). $C251 turns TEXT on; bytes land in the Apple-II-interleaved text page.\n"
    "; Normal (non-inverse/flash) glyphs need bit 7 set, so ORA #$80 each char.\n"
    ".include \"gen2.inc\"\n"
    "ROW12 = $0628          ; row 12 base = $0400 + $80*(12&7) + $28*(12>>3)\n"
    ".code\n"
    "start:\n"
    "    bit GEN2_TEXTON        ; TEXT mode (disables all graphics)\n"
    "    bit GEN2_PAGE1         ; page 1 ($0400)\n"
    "    bit GEN2_MIXOFF        ; full screen\n"
    "    lda #$A0               ; clear page to spaces ($A0 = ' ' | $80)\n"
    "    ldx #0\n"
    "clr:\n"
    "    sta $0400,x\n"
    "    sta $0500,x\n"
    "    sta $0600,x\n"
    "    sta $0700,x\n"
    "    inx\n"
    "    bne clr\n"
    "    ldx #0                 ; write centred message at row 12, col 14\n"
    "msg:\n"
    "    lda message,x\n"
    "    beq done\n"
    "    ora #$80               ; normal video (bit 7 set)\n"
    "    sta ROW12+14,x\n"
    "    inx\n"
    "    bne msg\n"
    "done:\n"
    "    jmp *\n"
    "message:\n"
    "    .byte \"HELLO WORLD\", 0\n";
static const char* kSketchTxtGen2C =     // C x GEN2 native TEXT mode ($0400)
    "/* HELLO WORLD in GEN2 native TEXT mode (40x24, page $0400, built-in font).\n"
    "   gen2_text() turns TEXT on; normal glyphs need bit 7 set. Upload runs @ $6000. */\n"
    "#include \"gen2.h\"\n"
    "\n"
    "void main(void) {\n"
    "    unsigned char *scr = (unsigned char *)(0x0628 + 14);  /* row 12, col 14 */\n"
    "    unsigned char *page = (unsigned char *)0x0400;\n"
    "    const char *s = \"HELLO WORLD\";\n"
    "    unsigned i;\n"
    "    gen2_text();                    /* TEXT on (disables graphics) */\n"
    "    gen2_page1();\n"
    "    gen2_full();\n"
    "    for (i = 0; i < 0x400u; ++i) page[i] = 0xA0;   /* clear to spaces */\n"
    "    while (*s) *scr++ = (unsigned char)(*s++ | 0x80);\n"
    "    for (;;) { /* idle */ }\n"
    "}\n";
static const char* kSketchCText =        // C x Apple dual-4k/8k (WozMon I/O)
    "/* HELLO WORLD in C on a plain text Apple-1, using the shared apple1c text\n"
    "   base (woz_puts/woz_getkey). The same apple1io.h works on the GEN2 card. */\n"
    "#include \"apple1io.h\"\n"
    "\n"
    "void main(void) {\n"
    "    woz_puts((const unsigned char *)\"\\rHELLO WORLD (C / Apple-1)\\r\");\n"
    "    woz_mon();\n"
    "}\n";
static const char* kSketchHex =
    "; POM1 Bench - Wozmon hex. Upload loads + runs (addresses are in the text).\n"
    "0300: A9 A1 20 EF FF 4C 00 03\n"
    "0300R\n";
static const char* kSketchRaw =
    "; Raw bytes - loaded flat at the $ address. '!' via WozMon ECHO.\n"
    "A9 A1 20 EF FF 4C 00 03\n";
static const char* kSketchC =
    "/* Hello world in C for the TMS9918 (apple1-videocard-lib, cc65).\n"
    "   Upload builds a CodeTank ROM with cl65, flashes it and boots 4000R. */\n"
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

// POM1 target: machine preset + linker cfg + source mode (0 asm/1 hex/2 raw/3 C)
// + the HELLO-WORLD starter for it. The New dialog picks a (language x machine)
// pair; the first 8 entries are that matrix, ordered language-major:
//   0..3 = asm x {dual-4k, TMS9918, GEN2 HGR, Bernie TXT},
//   4..7 = C   x {dual-4k, TMS9918, GEN2 HGR, Bernie TXT}.
// (preset indices: dual-4k = 1, TMS9918+CodeTank = 7, GEN2 = 12.)
struct P1T { const char* label; int preset; const char* cfg; const char* lang; int mode;
             bool needsCl65; bool wantsAddr; bool codetankRom; const char* sketch; };
const P1T kP1Targets[] = {
    { "Apple-1 dual 4K/8K (asm)",         1, "apple1_4k.cfg",   "6502", 0, false, false, false, kSketchAsm       },
    { "P-LAB TMS9918 Graphic Card (asm)", 7, "codetank.cfg",    "6502", 0, false, false, true,  kSketchAsmTms    },
    { "Uncle Bernie GEN2 HGR (asm)",     12, "apple1_gen2.cfg", "6502", 0, false, false, false, kSketchAsmGen2   },
    { "Bernie GEN2 TXT (asm)",           12, "apple1_gen2.cfg", "6502", 0, false, false, false, kSketchTxtGen2Asm},
    { "Apple-1 dual 4K/8K (C)",           1, "C-plain",         "C",    3, true,  false, false, kSketchCText     },
    { "P-LAB TMS9918 CodeTank ROM (C)",   7, "C",               "C",    3, true,  false, true,  kSketchC         },
    { "Uncle Bernie GEN2 HGR (C)",       12, "C-gen2",          "C",    3, true,  false, false, kSketchGen2C     },
    { "Bernie GEN2 TXT (C)",             12, "C-gen2",          "C",    3, true,  false, false, kSketchTxtGen2C  },
    { "Wozmon hex (any machine)",        -1, "",                "hex",  1, false, false, false, kSketchHex       },
    { "Raw bytes @ $ (any machine)",     -1, "",                "raw",  2, false, true,  false, kSketchRaw       },
};
const int kP1TargetCount = static_cast<int>(sizeof(kP1Targets) / sizeof(kP1Targets[0]));

// New-dialog axes (language x machine -> target index = lang*4 + machine).
// kP1*Hints are parallel to the labels and surface as combo-entry tooltips.
const char* const kP1Languages[] = { "Assembly  —  ca65 / ld65", "C  —  cc65 / cl65" };
const char* const kP1LanguageHints[] = {
    "MOS 6502 assembler (cc65's ca65 + ld65). Links against the apple1 / tms9918 /\n"
    "gen2 equate libraries under dev/lib via the per-target linker .cfg.",
    "C cross-compiler (cc65's cl65). Pulls in the apple1.c runtime, or the\n"
    "apple1-videocard-lib (TMS9918) / gen2 C runtime depending on the target.",
};
const char* const kP1Machines[]  = {
    "Apple-1 dual 4K/8K  (text) - start here",
    "P-LAB Graphic Card  (TMS9918)",
    "Uncle Bernie GEN2 HGR  (colour)",
    "Bernie GEN2 TXT  (40x24 text)",
};
const char* const kP1MachineHints[] = {
    "Stock Apple-1: 40x24 text printed through the WozMon ECHO routine ($FFEF).\n"
    "Easiest place to start - no graphics card needed. Preset 1 (dual 4K/8K RAM).",
    "P-LAB Graphic Card by Claudio Parmigiani — TMS9918 VDP, Graphics I mode,\n"
    "256x192, data port $CC00 / control $CC01. Preset 7. Upload flashes the build\n"
    "into CODETANKDEV.rom and boots 4000R (all TMS9918 code runs from CodeTank).",
    "Uncle Bernie's GEN2 colour card — Apple II-style HIRES (280x192) driven by\n"
    "the soft switches $C250-$C257. Hello world uses the BBFont. Preset 12.",
    "Uncle Bernie's GEN2 in native TEXT mode — 40x24, page $0400, the card's\n"
    "built-in font ($C251 TEXT on). Preset 12.",
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

struct P1Ex { const char* label; bool file; const char* data; int target; const char* asset; uint16_t addr; };
const P1Ex kP1Examples[] = {
    { "1 - Print a character (asm)",       false, kEx_char,       0, "", 0 },
    { "2 - Print a string (asm)",          false, kEx_string,     0, "", 0 },
    { "3 - Count 0 to 9 (asm)",            false, kEx_loop,       0, "", 0 },
    { "4 - Echo the keyboard (asm)",       false, kEx_keyboard,   0, "", 0 },
    { "5 - Hello in C",                    false, kEx_c_hello,    4, "", 0 },
    { "6 - Keyboard echo in C",            false, kEx_c_keyboard, 4, "", 0 },
    { "A-1-CrazyCycle  (Bernie GEN2 HGR)", true,  "dev/projects/a1_crazycycle/A-1-CrazyCycle.asm", 2,
      "sdcard/NONO/HGR/UBERNIE#062000", 0x2000 },
    { "Telemetry demo  (SDK harness)",     true,  "dev/projects/a1_telemetry_demo/A1_TelemetryDemo.asm", 0, "", 0 },
    { "Snake telemetry  (Bernie GEN2 HGR)", true, "dev/projects/gen2_snake_telemetry/GEN2Snake.c", 6, "", 0 },
};
const int kP1ExampleCount = static_cast<int>(sizeof(kP1Examples) / sizeof(kP1Examples[0]));

void parseHexTokens(const char* s, std::vector<unsigned char>& out)
{
    const char* p = s;
    while (*p) {
        while (*p && !std::isxdigit((unsigned char)*p)) ++p;
        if (!*p) break;
        int v = 0, digits = 0;
        while (*p && std::isxdigit((unsigned char)*p) && digits < 2) {
            char c = *p++;
            int d = (c <= '9') ? c - '0' : (c | 0x20) - 'a' + 10;
            v = v * 16 + d; ++digits;
        }
        out.push_back(static_cast<unsigned char>(v & 0xFF));
    }
}

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

Pom1BenchHost::Pom1BenchHost(MainWindow_ImGui* mw) : mw_(mw)
{
#if POM1_IS_WASM
    // The browser has no cc65 toolchain, so the Bench is restricted to the
    // toolchain-free Wozmon-hex target (kP1Targets[8]) for now — no cc65 asm/C
    // targets, no examples, no language x machine matrix.
    targets_.push_back({ kP1Targets[8].label, kP1Targets[8].label,
                         kP1Targets[8].lang, kP1Targets[8].wantsAddr });
    targetMap_.push_back(8);
#else
    for (int i = 0; i < kP1TargetCount; ++i) {
        targets_.push_back({ kP1Targets[i].label, kP1Targets[i].label,
                             kP1Targets[i].lang, kP1Targets[i].wantsAddr });
        targetMap_.push_back(i);
    }
    for (int i = 0; i < kP1ExampleCount; ++i)
        examples_.push_back({ kP1Examples[i].label });
    for (const char* l : kP1Languages)     languages_.push_back(l);
    for (const char* m : kP1Machines)      machines_.push_back(m);
    for (const char* h : kP1LanguageHints) languageHints_.push_back(h);
    for (const char* h : kP1MachineHints)  machineHints_.push_back(h);
#endif
}

void Pom1BenchHost::probe() const
{
    if (probed_) return;
    probed_ = true;
#if !POM1_IS_WASM
    namespace fs = std::filesystem;
    std::error_code ec;
    ca65_ = bench::whichExe("ca65");
    ld65_ = bench::whichExe("ld65");
    toolchainOk_ = !ca65_.empty() && !ld65_.empty();

    std::string devRoot;
    for (const char* p : {"dev", "../dev", "../../dev"})
        if (fs::exists(fs::path(p) / "cc65", ec)) { devRoot = p; break; }
    if (!devRoot.empty()) {
        std::string flags;
        for (const auto& e : fs::directory_iterator(fs::path(devRoot) / "lib", ec))
            if (e.is_directory(ec))
                flags += "-I " + bench::shellQuote(fs::absolute(e.path(), ec).string()) + " ";
        libFlags_ = flags;
    }
    cl65_ = bench::whichExe("cl65");
    if (!devRoot.empty()) {
        const fs::path vroot = fs::path(devRoot) / "apple1-videocard-lib";
        if (fs::exists(vroot / "lib", ec)) videocardLib_ = fs::absolute(vroot / "lib", ec).string();
        const fs::path cfg = vroot / "cc65" / "codetank_c.cfg";
        if (fs::exists(cfg, ec)) codetankCfg_ = fs::absolute(cfg, ec).string();
    }
    cl65Ok_ = !cl65_.empty() && !videocardLib_.empty() && !codetankCfg_.empty();

    // GEN2 HGR C: the gen2c lib + its cfg under dev/.
    if (!devRoot.empty()) {
        const fs::path glib = fs::path(devRoot) / "lib" / "gen2c";
        const fs::path gcfg = fs::path(devRoot) / "cc65" / "apple1_gen2_c.cfg";
        if (fs::exists(glib, ec)) gen2cLib_ = fs::absolute(glib, ec).string();
        if (fs::exists(gcfg, ec)) gen2Cfg_  = fs::absolute(gcfg, ec).string();
        // Plain text C uses dev/cc65/apple1_c.cfg + the shared apple1c text base.
        const fs::path pcfg = fs::path(devRoot) / "cc65" / "apple1_c.cfg";
        if (fs::exists(pcfg, ec)) plainCfg_ = fs::absolute(pcfg, ec).string();
        // Shared Apple-1 text/keyboard C base (woz_puts/woz_getkey) — card-neutral,
        // linked by both the plain-text and GEN2 HGR C targets so either can do
        // terminal I/O. The graphics runtimes (gen2c / videocard-lib) sit on top.
        const fs::path a1c = fs::path(devRoot) / "lib" / "apple1c";
        if (fs::exists(a1c, ec)) apple1cLib_ = fs::absolute(a1c, ec).string();
        // Header-only telemetry side-channel kit (telemetry.h). No .c to link —
        // just an include dir, folded into every C build below.
        const fs::path tele = fs::path(devRoot) / "lib" / "telemetry";
        if (fs::exists(tele, ec)) telemetryLib_ = fs::absolute(tele, ec).string();
    }
    gen2COk_  = !cl65_.empty() && !gen2cLib_.empty() && !gen2Cfg_.empty();
    plainCOk_ = !cl65_.empty() && !apple1cLib_.empty() && !plainCfg_.empty();
#endif
}

int Pom1BenchHost::defaultTargetIndex() const
{
#if POM1_IS_WASM
    return 0;   // the only target = Wozmon hex
#else
    probe();
    return toolchainOk_ ? 0 : 8;   // asm dual-4k if cc65 present, else Wozmon hex
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
    if (language < 0 || language > 1 || machine < 0 || machine > 3) return -1;
    return language * 4 + machine;   // matches the kP1Targets matrix ordering
}

void Pom1BenchHost::onTargetSelected(int target)
{
    if (target < 0 || target >= kP1TargetCount) return;
    const P1T& t = kP1Targets[p1(target)];
    if (t.preset >= 0 && t.preset != mw_->activePresetIndex)
        mw_->applyMachineConfig(t.preset);
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
    if (e.data && std::strstr(e.data, "gen2_snake_telemetry")) {
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

bench::BuildResult Pom1BenchHost::directLoad(int target, const std::string& src, const std::string& addrHex)
{
    namespace fs = std::filesystem;
    bench::BuildResult r; r.showConsole = false;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec);
    if (ec || dir.empty()) { r.status = "no temp directory available"; r.ok = false; return r; }
    TempFileSweeper sweep;
    auto* emu = mw_->emulation.get();
    std::string error; int bytesLoaded = 0; bool ok = false; uint16_t entry = 0;
    if (kP1Targets[p1(target)].mode == 1) {   // Wozmon hex
        const fs::path tmp = dir / "pom1_bench_sketch.txt";
        sweep.add(tmp);
        std::ofstream(tmp, std::ios::binary).write(src.data(), static_cast<std::streamsize>(src.size()));
        std::vector<std::pair<uint16_t, uint16_t>> zones;
        ok = emu->loadHexDump(tmp.string(), entry, error, &bytesLoaded, &zones);
    } else {                              // raw bytes
        std::vector<unsigned char> bytes; parseHexTokens(src.c_str(), bytes);
        try { entry = static_cast<uint16_t>(std::stoul(addrHex, nullptr, 16)); } catch (...) { entry = 0x0300; }
        if (bytes.empty()) error = "no hex bytes parsed";
        else {
            const fs::path tmp = dir / "pom1_bench_sketch.bin";
            sweep.add(tmp);
            std::ofstream(tmp, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                                       static_cast<std::streamsize>(bytes.size()));
            ok = emu->loadBinary(tmp.string(), entry, error, &bytesLoaded);
        }
    }
    if (ok) {
        emu->copySnapshot(mw_->uiSnapshot);
        char m[128]; std::snprintf(m, sizeof(m), "Uploaded %d B - running @ $%04X", bytesLoaded, entry);
        r.status = m; r.ok = true;
    } else { r.status = "Upload failed: " + error; r.ok = false; }
    return r;
}

bench::BuildResult Pom1BenchHost::build(int target, const std::string& src, const std::string& addrHex, bool run)
{
    bench::BuildResult r;
    if (target < 0 || target >= kP1TargetCount) { r.status = "bad target"; return r; }
    const P1T& t = kP1Targets[p1(target)];

    if (t.mode == 1 || t.mode == 2) {     // hex/raw: no compile
        if (!run) { r.status = "Nothing to verify (hex/raw)"; r.showConsole = false; return r; }
        return directLoad(target, src, addrHex);
    }

#if POM1_IS_WASM
    r.console = "cc65 build is desktop-only - the in-browser POM1 has no ca65/ld65/cl65.\n"
                "Compile in the desktop build, or paste a Wozmon hex dump (hex/raw target) instead.\n";
    r.status = "cc65 build is desktop-only"; return r;
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
    uint16_t entry = 0;

    if (cmode) {
        const bool ready = gen2c ? gen2COk_ : plainc ? plainCOk_ : cl65Ok_;
        if (!ready) {
            r.console = gen2c ? "cl65 / gen2c lib not found (needs dev/)\n"
                      : plainc ? "cl65 / apple1c lib not found (needs dev/)\n"
                               : "cl65 / videocard-lib not found (needs dev/)\n";
            r.console += "The dev/ source tree must be present (clone the repo; release bundles omit dev/).\n";
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
            // Optionally fold in the shared apple1c text base so GEN2 C programs
            // can also print to the WOZ terminal / read the keyboard.
            std::string a1c;
            if (!apple1cLib_.empty())
                a1c = " -I " + bench::shellQuote(apple1cLib_) +
                      " " + bench::shellQuote(apple1cLib_ + "/apple1io.c") +
                      " " + bench::shellQuote(apple1cLib_ + "/apple1io_asm.s");
            cmd = bench::shellQuote(cl65_) + " -t none -Oirs -C " + bench::shellQuote(gen2Cfg_) +
                " -I " + bench::shellQuote(gen2cLib_) + a1c + tele + " " + bench::shellQuote(srcC.string()) +
                " " + bench::shellQuote(gen2cLib_ + "/gen2.c") +
                " " + bench::shellQuote(gen2cLib_ + "/gen2_blit.s") +
                " -o " + bench::shellQuote(binB.string());
        } else if (plainc) {
            tag = "Apple-1 text";
            const std::string& lib = apple1cLib_;   // shared, card-neutral text base
            cmd = bench::shellQuote(cl65_) + " -t none -Oirs -C " + bench::shellQuote(plainCfg_) +
                " -I " + bench::shellQuote(lib) + tele + " " + bench::shellQuote(srcC.string()) +
                " " + bench::shellQuote(lib + "/apple1io.c") + " " + bench::shellQuote(lib + "/apple1io_asm.s") +
                " -o " + bench::shellQuote(binB.string());
        } else {
            tag = "CodeTank ROM";
            const std::string& lib = videocardLib_;
            cmd = bench::shellQuote(cl65_) + " -t none -Oirs -C " + bench::shellQuote(codetankCfg_) +
                " -I " + bench::shellQuote(lib) + tele + " " + bench::shellQuote(srcC.string()) +
                " " + bench::shellQuote(lib + "/apple1_asm.s") + " " + bench::shellQuote(lib + "/tms9918.c") +
                " " + bench::shellQuote(lib + "/screen1.c") + " " + bench::shellQuote(lib + "/c64font.c") +
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
        std::string cfgPath;
        if (!t.cfg[0]) {
            const fs::path e2 = dir / "pom1_bench_default.cfg";
            sweep.add(e2);
            std::ofstream(e2, std::ios::binary) << kBenchEmbeddedCfg;
            cfgPath = e2.string();
        } else {
            for (const char* pre : {"dev/cc65/", "../dev/cc65/", "../../dev/cc65/"}) {
                const fs::path p = fs::path(pre) / t.cfg;
                if (fs::exists(p, ec)) { cfgPath = fs::absolute(p, ec).string(); break; }
            }
            if (cfgPath.empty()) { r.console = std::string("linker cfg not found (needs dev/): ") + t.cfg + "\n"; r.status = "ld65 cfg missing"; return r; }
        }
        std::string out;
        const std::string ca = bench::shellQuote(ca65_) + " " + libFlags_ +
            bench::shellQuote(srcS.string()) + " -o " + bench::shellQuote(objO.string());
        int rc = bench::runCapture(ca, out);
        r.console = "$ ca65 [" + std::string(t.label) + "]\n" + out;
        if (rc != 0) { parseErrorMarkers(out, r.errors); r.console += humanizeCc65(out); r.status = "ca65 failed (see Build output)"; return r; }
        const std::string ld = bench::shellQuote(ld65_) + " -C " + bench::shellQuote(cfgPath) + " " +
            bench::shellQuote(objO.string()) + " -o " + bench::shellQuote(binB.string());
        rc = bench::runCapture(ld, out);
        r.console += "$ ld65 -C " + cfgPath + "\n" + out;
        if (rc != 0) { r.console += humanizeCc65(out); r.status = "ld65 failed (see Build output)"; return r; }
        r.console += "[ok] assembled + linked\n";
        entry = parseCfgLoadAddr(cfgPath);
        if (entry == 0) { try { entry = static_cast<uint16_t>(std::stoul(addrHex, nullptr, 16)); } catch (...) { entry = 0x0300; } }
    }

    if (!run) { r.status = "Verify OK"; r.ok = true; return r; }

    auto* emu = mw_->emulation.get();
    if (gen2c || plainc) {
        // GEN2 HGR / plain text C: load + run (the target's preset already plugged
        // the right card). loadBinary resets + runs at the cfg's entry.
        std::string error; int bytesLoaded = 0;
        if (emu->loadBinary(binB.string(), entry, error, &bytesLoaded)) {
            emu->copySnapshot(mw_->uiSnapshot);
            const char* where = "";   // point the user at where the output appears
            if (gen2c) { mw_->showGraphicsCard = true; where = " - see the GEN2 HGR window"; }
            char msg[160]; std::snprintf(msg, sizeof(msg), "Built %d B - running @ $%04X%s", bytesLoaded, entry, where);
            r.status = msg; r.ok = true;
        } else { r.status = "load failed: " + error; r.ok = false; }
        return r;
    }
    if (t.codetankRom) {
        // Unified CODETANKDEV path (TMS9918 asm + C targets): wrap the build into a
        // persistent CodeTank dev ROM (roms/codetank/CODETANKDEV.rom), flash it,
        // jumper to the lower 16K bank, reset and boot 4000R. Living under
        // roms/codetank/ means the dev cartridge also shows up in
        // File > P-LAB CodeTank Library, so it's reusable across uploads.
        std::ifstream in(binB, std::ios::binary);
        std::vector<unsigned char> rom(0x8000, 0xFF);
        in.read(reinterpret_cast<char*>(rom.data()), 0x4000);
        fs::path romPath;
        for (const char* pre : {"roms/codetank", "../roms/codetank", "../../roms/codetank"})
            if (fs::exists(fs::path(pre), ec)) { romPath = fs::path(pre) / "CODETANKDEV.rom"; break; }
        if (romPath.empty()) romPath = dir / "CODETANKDEV.rom";   // fallback: no roms/codetank/ dir
        std::ofstream(romPath, std::ios::binary)
            .write(reinterpret_cast<const char*>(rom.data()), static_cast<std::streamsize>(rom.size()));
        std::string error;
        if (!emu->loadCodeTankRom(romPath.string(), error)) { r.status = "CODETANKDEV.rom load failed: " + error; return r; }
        mw_->codeTankJumper = CodeTank::Jumper::Lower16;
        emu->setCodeTankJumper(mw_->codeTankJumper);
        if (!mw_->tms9918Enabled) { mw_->tms9918Enabled = true; mw_->showTMS9918 = true; emu->setTMS9918Enabled(true); }
        if (!mw_->codeTankEnabled) { mw_->codeTankEnabled = true; emu->setCodeTankEnabled(true); }
        emu->hardReset();
        mw_->codeTankPendingWozRunAt = ImGui::GetTime() + 1.0;
        emu->copySnapshot(mw_->uiSnapshot);
        r.console += "[ok] flashed CODETANKDEV.rom (lower bank) - 4000R\n";
        r.status = "CODETANKDEV.rom flashed - booting 4000R - see the TMS9918 window"; r.ok = true;
        return r;
    }

    // asm: stage companion asset (e.g. CrazyCycle's UBERNIE @ $2000), then run.
    if (!extraAsset_.empty()) {
        std::string ap;
        for (const char* pre : {"", "../", "../../"}) {
            const fs::path p = fs::path(pre) / extraAsset_;
            if (fs::exists(p, ec)) { ap = p.string(); break; }
        }
        std::string aerr; char amsg[160];
        if (!ap.empty() && emu->loadBinaryToRam(ap, extraAssetAddr_, aerr))
            std::snprintf(amsg, sizeof(amsg), "[ok] asset -> $%04X (%s)\n", extraAssetAddr_, extraAsset_.c_str());
        else
            std::snprintf(amsg, sizeof(amsg), "[warn] asset not loaded: %s\n", extraAsset_.c_str());
        r.console += amsg;
    }
    std::string error; int bytesLoaded = 0;
    if (emu->loadBinary(binB.string(), entry, error, &bytesLoaded)) {
        emu->copySnapshot(mw_->uiSnapshot);
        const char* where = "";   // GEN2 asm targets render in the graphics window
        if (t.preset == 12) { mw_->showGraphicsCard = true; where = " - see the GEN2 HGR window"; }
        char msg[160]; std::snprintf(msg, sizeof(msg), "Built %d B - running @ $%04X%s", bytesLoaded, entry, where);
        r.status = msg; r.ok = true;
    } else { r.status = "load failed: " + error; r.ok = false; }
    return r;
#endif
}

bool Pom1BenchHost::toolchainReady(int target) const
{
    if (target < 0 || target >= kP1TargetCount) return false;
    const P1T& t = kP1Targets[p1(target)];
    if (t.mode == 1 || t.mode == 2) return true;
    probe();
    if (!t.needsCl65) return toolchainOk_;
    const std::string cfg = t.cfg ? t.cfg : "";
    if (cfg == "C-gen2")  return gen2COk_;
    if (cfg == "C-plain") return plainCOk_;
    return cl65Ok_;
}

std::string Pom1BenchHost::toolchainHint(int target) const
{
    if (target < 0 || target >= kP1TargetCount) return "";
    const P1T& t = kP1Targets[p1(target)];
    if (t.mode == 1 || t.mode == 2) return "";
    probe();
    if (!t.needsCl65) return toolchainOk_ ? "ca65/ld65 ready" : "needs cc65 (ca65/ld65)";
    return toolchainReady(target) ? "cl65 ready" : "needs cl65 + dev/";
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
    s += std::string("dev/ source tree : ") +
         (libFlags_.empty() ? "NOT found (clone the repo - release bundles omit dev/)" : "found") + "\n";
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
    for (const char* p : {"dev", "../dev", "../../dev"})
        if (fs::exists(p, ec)) return fs::absolute(p, ec).string();
    return ".";
}

void Pom1BenchHost::openSerial()
{
    mw_->showTelemetry = true;
}
