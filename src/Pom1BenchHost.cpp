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
    "; HELLO WORLD on the TMS9918 (Graphics I). Loads a tiny font into VRAM and\n"
    "; writes the message to the name table. VDP data=$CC00, ctrl=$CC01.\n"
    "VDAT = $CC00\n"
    "VCTL = $CC01\n"
    ".segment \"CODE\"\n"
    "start:\n"
    "    ldx #0          ; --- init 8 VDP registers (value then $80|reg) ---\n"
    "ireg:\n"
    "    lda regs,x\n"
    "    sta VCTL\n"
    "    txa\n"
    "    ora #$80\n"
    "    sta VCTL\n"
    "    inx\n"
    "    cpx #8\n"
    "    bne ireg\n"
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
    "    lda #$00         ; --- name table $1800: clear 768 to blank ---\n"
    "    sta VCTL\n"
    "    lda #($18|$40)\n"
    "    sta VCTL\n"
    "    lda #$00\n"
    "    ldy #3\n"
    "nclo:\n"
    "    ldx #0\n"
    "ncli:\n"
    "    sta VDAT\n"
    "    inx\n"
    "    bne ncli\n"
    "    dey\n"
    "    bne nclo\n"
    "    lda #$00         ; --- name table $1800: write the message ---\n"
    "    sta VCTL\n"
    "    lda #($18|$40)\n"
    "    sta VCTL\n"
    "    ldx #0\n"
    "msgl:\n"
    "    lda message,x\n"
    "    cmp #$FF\n"
    "    beq done\n"
    "    sta VDAT\n"
    "    inx\n"
    "    bne msgl\n"
    "done:\n"
    "    jmp done\n"
    "; reg0..7: graphics I, name $1800, colour $2000, pattern $0000, backdrop blue\n"
    "regs:\n"
    "    .byte $00,$C0,$06,$80,$00,$36,$07,$04\n"
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
static const char* kSketchAsmGen2 =      // asm x GEN2 HGR (fill page 1)
    "; HELLO - GEN2 HIRES. Soft switches $C250-$C257 (read = set), fill page 1.\n"
    "GEN2 = $C250\n"
    ".segment \"CODE\"\n"
    "start:\n"
    "    bit GEN2+0      ; graphics (TEXT off)\n"
    "    bit GEN2+7      ; hires\n"
    "    bit GEN2+4      ; page 1\n"
    "    bit GEN2+2      ; full screen\n"
    "    lda #$00\n"
    "    sta $00\n"
    "    lda #$20\n"
    "    sta $01         ; ptr = $2000\n"
    "    lda #$2A        ; pixel pattern\n"
    "    ldy #$00\n"
    "fill:\n"
    "    sta ($00),y\n"
    "    iny\n"
    "    bne fill\n"
    "    inc $01\n"
    "    ldx $01\n"
    "    cpx #$40        ; until $4000 ($2000-$3FFF filled)\n"
    "    bne fill\n"
    "    jmp *\n";
static const char* kSketchCText =        // C x Apple dual-4k/8k (WozMon I/O)
    "/* HELLO WORLD in C on a plain text Apple-1 (apple1.c woz_puts, cc65). */\n"
    "#include \"apple1.h\"\n"
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
    "/* HIRES in C on Uncle Bernie's GEN2 colour card (cc65).\n"
    "   Soft switches $C250-$C257 + HST0 — see gen2.h. Upload builds + runs @ $6000. */\n"
    "#include \"gen2.h\"\n"
    "\n"
    "void main(void) {\n"
    "    unsigned x;\n"
    "    gen2_hgr_init();          /* graphics + hires + page1 + full */\n"
    "    gen2_hgr_clear(0);        /* black */\n"
    "    for (x = 0; x < 192; ++x) {     /* draw an X */\n"
    "        gen2_hgr_plot(x, (unsigned char)x);\n"
    "        gen2_hgr_plot(279u - x, (unsigned char)x);\n"
    "    }\n"
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

// POM1 target: machine preset + linker cfg + source mode (0 asm/1 hex/2 raw/3 C)
// + the HELLO-WORLD starter for it. The New dialog picks a (language x machine)
// pair; the first 6 entries are that matrix, ordered language-major:
//   0..2 = asm x {dual-4k, TMS9918, GEN2}, 3..5 = C x {dual-4k, TMS9918, GEN2}.
struct P1T { const char* label; int preset; const char* cfg; const char* lang; int mode;
             bool needsCl65; bool wantsAddr; const char* sketch; };
const P1T kP1Targets[] = {
    { "Apple-1 dual-4k/8k (asm)",         1, "apple1_4k.cfg",   "6502", 0, false, false, kSketchAsm     },
    { "TMS9918 (asm)",                    8, "apple1_4k.cfg",   "6502", 0, false, false, kSketchAsmTms  },
    { "Uncle Bernie GEN2 HGR (asm)",     13, "apple1_gen2.cfg", "6502", 0, false, false, kSketchAsmGen2 },
    { "Apple-1 dual-4k/8k (C)",           1, "C-plain",         "C",    3, true,  false, kSketchCText   },
    { "TMS9918 CodeTank ROM (C)",         8, "C",               "C",    3, true,  false, kSketchC       },
    { "Uncle Bernie GEN2 HGR (C)",       13, "C-gen2",          "C",    3, true,  false, kSketchGen2C   },
    { "Wozmon hex (any machine)",        -1, "",                "hex",  1, false, false, kSketchHex     },
    { "Raw bytes @ $ (any machine)",     -1, "",                "raw",  2, false, true,  kSketchRaw     },
};
const int kP1TargetCount = static_cast<int>(sizeof(kP1Targets) / sizeof(kP1Targets[0]));

// New-dialog axes (language x machine -> target index = lang*3 + machine).
const char* const kP1Languages[] = { "Assembly (ca65)", "C (cc65)" };
const char* const kP1Machines[]  = { "Apple-1 dual-4k/8k", "TMS9918", "GEN2 HGR (Bernie)" };

struct P1Ex { const char* label; bool file; const char* data; int target; const char* asset; uint16_t addr; };
const P1Ex kP1Examples[] = {
    { "A-1-CrazyCycle  (Bernie GEN2 HGR)", true,  "dev/projects/a1_crazycycle/A-1-CrazyCycle.asm", 2,
      "sdcard/NONO/HGR/UBERNIE#062000", 0x2000 },
    { "Telemetry demo  (SDK harness)",     true,  "dev/projects/a1_telemetry_demo/A1_TelemetryDemo.asm", 0, "", 0 },
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

} // namespace

// ─────────────────────────────────────────────────────────────
// Pom1BenchHost
// ─────────────────────────────────────────────────────────────

Pom1BenchHost::Pom1BenchHost(MainWindow_ImGui* mw) : mw_(mw)
{
    for (int i = 0; i < kP1TargetCount; ++i)
        targets_.push_back({ kP1Targets[i].label, kP1Targets[i].label,
                             kP1Targets[i].lang, kP1Targets[i].wantsAddr });
    for (int i = 0; i < kP1ExampleCount; ++i)
        examples_.push_back({ kP1Examples[i].label });
    for (const char* l : kP1Languages) languages_.push_back(l);
    for (const char* m : kP1Machines)  machines_.push_back(m);
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
        // Plain text C (apple1.c woz_puts) uses dev/cc65/apple1_c.cfg.
        const fs::path pcfg = fs::path(devRoot) / "cc65" / "apple1_c.cfg";
        if (fs::exists(pcfg, ec)) plainCfg_ = fs::absolute(pcfg, ec).string();
    }
    gen2COk_  = !cl65_.empty() && !gen2cLib_.empty() && !gen2Cfg_.empty();
    plainCOk_ = !cl65_.empty() && !videocardLib_.empty() && !plainCfg_.empty();
#endif
}

int Pom1BenchHost::defaultTargetIndex() const
{
    probe();
    return toolchainOk_ ? 0 : 6;   // asm dual-4k if cc65 present, else Wozmon hex
}

std::string Pom1BenchHost::starterSketch(int target) const
{
    if (target < 0 || target >= kP1TargetCount) return "";
    return kP1Targets[target].sketch ? kP1Targets[target].sketch : "";
}

const std::vector<std::string>& Pom1BenchHost::languages() const { return languages_; }
const std::vector<std::string>& Pom1BenchHost::machines()  const { return machines_; }

int Pom1BenchHost::targetFor(int language, int machine) const
{
    if (language < 0 || language > 1 || machine < 0 || machine > 2) return -1;
    return language * 3 + machine;   // matches the kP1Targets matrix ordering
}

void Pom1BenchHost::onTargetSelected(int target)
{
    if (target < 0 || target >= kP1TargetCount) return;
    const P1T& t = kP1Targets[target];
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
    auto* emu = mw_->emulation.get();
    std::string error; int bytesLoaded = 0; bool ok = false; uint16_t entry = 0;
    if (kP1Targets[target].mode == 1) {   // Wozmon hex
        const fs::path tmp = dir / "pom1_bench_sketch.txt";
        std::ofstream(tmp, std::ios::binary).write(src.data(), static_cast<std::streamsize>(src.size()));
        std::vector<std::pair<uint16_t, uint16_t>> zones;
        ok = emu->loadHexDump(tmp.string(), entry, error, &bytesLoaded, &zones);
    } else {                              // raw bytes
        std::vector<unsigned char> bytes; parseHexTokens(src.c_str(), bytes);
        try { entry = static_cast<uint16_t>(std::stoul(addrHex, nullptr, 16)); } catch (...) { entry = 0x0300; }
        if (bytes.empty()) error = "no hex bytes parsed";
        else {
            const fs::path tmp = dir / "pom1_bench_sketch.bin";
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
    const P1T& t = kP1Targets[target];

    if (t.mode == 1 || t.mode == 2) {     // hex/raw: no compile
        if (!run) { r.status = "Nothing to verify (hex/raw)"; r.showConsole = false; return r; }
        return directLoad(target, src, addrHex);
    }

#if POM1_IS_WASM
    r.status = "cc65 build is desktop-only"; return r;
#else
    probe();
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir  = fs::temp_directory_path(ec);
    const fs::path binB = dir / "pom1_bench.bin";
    const std::string cfgTag = t.cfg ? t.cfg : "";
    const bool cmode  = (t.mode == 3);
    const bool gen2c  = cmode && cfgTag == "C-gen2";    // GEN2 HGR (loadBinary @ $6000)
    const bool plainc = cmode && cfgTag == "C-plain";   // plain text (loadBinary @ $0300)
    const bool codetankc = cmode && !gen2c && !plainc;  // TMS9918 CodeTank ROM
    r.showConsole = true;
    uint16_t entry = 0;

    if (cmode) {
        const bool ready = gen2c ? gen2COk_ : plainc ? plainCOk_ : cl65Ok_;
        if (!ready) {
            r.console = gen2c ? "cl65 / gen2c lib not found (needs dev/)\n"
                      : plainc ? "cl65 / apple1.c lib not found (needs dev/)\n"
                               : "cl65 / videocard-lib not found (needs dev/)\n";
            r.status = "cc65 cl65 missing"; return r;
        }
        const fs::path srcC = dir / "pom1_bench.c";
        std::ofstream(srcC, std::ios::binary).write(src.data(), static_cast<std::streamsize>(src.size()));
        std::string cmd; const char* tag;
        if (gen2c) {
            tag = "GEN2 HGR";
            cmd = bench::shellQuote(cl65_) + " -t none -Oirs -C " + bench::shellQuote(gen2Cfg_) +
                " -I " + bench::shellQuote(gen2cLib_) + " " + bench::shellQuote(srcC.string()) +
                " " + bench::shellQuote(gen2cLib_ + "/gen2.c") +
                " -o " + bench::shellQuote(binB.string());
        } else if (plainc) {
            tag = "Apple-1 text";
            const std::string& lib = videocardLib_;
            cmd = bench::shellQuote(cl65_) + " -t none -Oirs -C " + bench::shellQuote(plainCfg_) +
                " -I " + bench::shellQuote(lib) + " " + bench::shellQuote(srcC.string()) +
                " " + bench::shellQuote(lib + "/apple1.c") + " " + bench::shellQuote(lib + "/apple1_asm.s") +
                " -o " + bench::shellQuote(binB.string());
        } else {
            tag = "CodeTank ROM";
            const std::string& lib = videocardLib_;
            cmd = bench::shellQuote(cl65_) + " -t none -Oirs -C " + bench::shellQuote(codetankCfg_) +
                " -I " + bench::shellQuote(lib) + " " + bench::shellQuote(srcC.string()) +
                " " + bench::shellQuote(lib + "/apple1_asm.s") + " " + bench::shellQuote(lib + "/tms9918.c") +
                " " + bench::shellQuote(lib + "/screen1.c") + " " + bench::shellQuote(lib + "/c64font.c") +
                " -o " + bench::shellQuote(binB.string());
        }
        std::string out;
        const int rc = bench::runCapture(cmd, out);
        r.console = std::string("$ cl65 -t none [") + tag + "]\n" + out;
        if (rc != 0) { parseErrorMarkers(out, r.errors); r.status = "cl65 failed (see Build output)"; return r; }
        r.console += std::string("[ok] compiled + linked (") + tag + ")\n";
        entry = gen2c ? parseCfgLoadAddr(gen2Cfg_) : plainc ? parseCfgLoadAddr(plainCfg_) : 0x4000;
        if (entry == 0) entry = plainc ? 0x0300 : 0x6000;
    } else {
        if (!toolchainOk_) { r.console = "cc65 (ca65/ld65) not found\n"; r.status = "cc65 missing"; return r; }
        const fs::path srcS = dir / "pom1_bench.s";
        const fs::path objO = dir / "pom1_bench.o";
        std::ofstream(srcS, std::ios::binary).write(src.data(), static_cast<std::streamsize>(src.size()));
        std::string cfgPath;
        if (!t.cfg[0]) {
            const fs::path e2 = dir / "pom1_bench_default.cfg";
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
        if (rc != 0) { parseErrorMarkers(out, r.errors); r.status = "ca65 failed (see Build output)"; return r; }
        const std::string ld = bench::shellQuote(ld65_) + " -C " + bench::shellQuote(cfgPath) + " " +
            bench::shellQuote(objO.string()) + " -o " + bench::shellQuote(binB.string());
        rc = bench::runCapture(ld, out);
        r.console += "$ ld65 -C " + cfgPath + "\n" + out;
        if (rc != 0) { r.status = "ld65 failed (see Build output)"; return r; }
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
            char msg[128]; std::snprintf(msg, sizeof(msg), "Built %d B - running @ $%04X", bytesLoaded, entry);
            r.status = msg; r.ok = true;
        } else { r.status = "load failed: " + error; r.ok = false; }
        return r;
    }
    if (codetankc) {
        // CodeTank ROM: pad 16K -> 32K (lower bank), flash, jumper, reset, 4000R.
        std::ifstream in(binB, std::ios::binary);
        std::vector<unsigned char> rom(0x8000, 0xFF);
        in.read(reinterpret_cast<char*>(rom.data()), 0x4000);
        const fs::path romPath = dir / "pom1_bench_codetank.rom";
        std::ofstream(romPath, std::ios::binary)
            .write(reinterpret_cast<const char*>(rom.data()), static_cast<std::streamsize>(rom.size()));
        std::string error;
        if (!emu->loadCodeTankRom(romPath.string(), error)) { r.status = "CodeTank ROM load failed: " + error; return r; }
        mw_->codeTankJumper = CodeTank::Jumper::Lower16;
        emu->setCodeTankJumper(mw_->codeTankJumper);
        if (!mw_->tms9918Enabled) { mw_->tms9918Enabled = true; mw_->showTMS9918 = true; emu->setTMS9918Enabled(true); }
        if (!mw_->codeTankEnabled) { mw_->codeTankEnabled = true; emu->setCodeTankEnabled(true); }
        emu->hardReset();
        mw_->codeTankPendingWozRunAt = ImGui::GetTime() + 1.0;
        emu->copySnapshot(mw_->uiSnapshot);
        r.console += "[ok] flashed CodeTank ROM (lower bank) - 4000R\n";
        r.status = "CodeTank ROM flashed - booting 4000R"; r.ok = true;
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
        char msg[128]; std::snprintf(msg, sizeof(msg), "Built %d B - running @ $%04X", bytesLoaded, entry);
        r.status = msg; r.ok = true;
    } else { r.status = "load failed: " + error; r.ok = false; }
    return r;
#endif
}

bool Pom1BenchHost::toolchainReady(int target) const
{
    if (target < 0 || target >= kP1TargetCount) return false;
    const P1T& t = kP1Targets[target];
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
    const P1T& t = kP1Targets[target];
    if (t.mode == 1 || t.mode == 2) return "";
    probe();
    if (!t.needsCl65) return toolchainOk_ ? "ca65/ld65 ready" : "needs cc65 (ca65/ld65)";
    return toolchainReady(target) ? "cl65 ready" : "needs cl65 + dev/";
}

void Pom1BenchHost::stop()
{
    mw_->stopCpu();   // halt the emulated CPU (same as the CPU menu's Stop)
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
