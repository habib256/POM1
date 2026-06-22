// Bench portable module — language definitions. See BenchLang.h.
#include "BenchLang.h"

namespace bench {

// 6502 / ca65: NMOS mnemonics as keywords, ca65 dot-directives + $hex / %binary
// / decimal numbers + ';' line comments.
static const TextEditor::LanguageDefinition& lang6502()
{
    static bool inited = false;
    static TextEditor::LanguageDefinition lang;
    if (inited) return lang;

    static const char* const kMnemonics[] = {
        "ADC","AND","ASL","BCC","BCS","BEQ","BIT","BMI","BNE","BPL","BRK","BVC","BVS",
        "CLC","CLD","CLI","CLV","CMP","CPX","CPY","DEC","DEX","DEY","EOR","INC","INX",
        "INY","JMP","JSR","LDA","LDX","LDY","LSR","NOP","ORA","PHA","PHP","PLA","PLP",
        "ROL","ROR","RTI","RTS","SBC","SEC","SED","SEI","STA","STX","STY","TAX","TAY",
        "TSX","TXA","TXS","TYA",
    };
    for (auto* m : kMnemonics) lang.mKeywords.insert(m);

    using PI = TextEditor::PaletteIndex;
    lang.mTokenRegexStrings.push_back({ "\\\"(\\\\.|[^\\\"])*\\\"", PI::String });
    lang.mTokenRegexStrings.push_back({ "'(\\\\.|[^'\\\\])'",        PI::String });        // ca65 char literal 'A' / #'0'
    lang.mTokenRegexStrings.push_back({ "\\.[a-zA-Z_][a-zA-Z0-9_]*",  PI::Preprocessor }); // ca65 directive
    lang.mTokenRegexStrings.push_back({ "@[a-zA-Z0-9_]+",            PI::Identifier });    // ca65 cheap-local label @name
    lang.mTokenRegexStrings.push_back({ "\\$[0-9a-fA-F]+",            PI::Number });        // $hex
    lang.mTokenRegexStrings.push_back({ "%[01]+",                    PI::Number });        // %binary
    lang.mTokenRegexStrings.push_back({ "[0-9]+",                    PI::Number });        // decimal
    lang.mTokenRegexStrings.push_back({ "[a-zA-Z_][a-zA-Z0-9_]*",    PI::Identifier });    // labels / symbols
    lang.mTokenRegexStrings.push_back({ "[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.\\#\\:\\@\\$]",
                                        PI::Punctuation });

    lang.mCommentStart      = "/*";   // no block comments; sentinel won't appear
    lang.mCommentEnd        = "*/";
    lang.mSingleLineComment = ";";
    lang.mCaseSensitive     = false;  // mnemonics are case-insensitive (lookup upper-cases)
    lang.mAutoIndentation   = true;
    lang.mName              = "6502";

    inited = true;
    return lang;
}

// Motorola 68000 starter set (for NeoST). Common mnemonics, ';' line comments,
// $hex / %binary / decimal, immediate '#'. The NeoST host can extend this.
static const TextEditor::LanguageDefinition& lang68000()
{
    static bool inited = false;
    static TextEditor::LanguageDefinition lang;
    if (inited) return lang;

    static const char* const kMnemonics[] = {
        "MOVE","MOVEA","MOVEQ","MOVEM","MOVEP","LEA","PEA","CLR","EXG","SWAP",
        "ADD","ADDA","ADDI","ADDQ","ADDX","SUB","SUBA","SUBI","SUBQ","SUBX",
        "MULS","MULU","DIVS","DIVU","NEG","NEGX","EXT","CMP","CMPA","CMPI","CMPM","TST",
        "AND","ANDI","OR","ORI","EOR","EORI","NOT","ASL","ASR","LSL","LSR","ROL","ROR",
        "ROXL","ROXR","BTST","BSET","BCLR","BCHG",
        "BRA","BSR","BEQ","BNE","BCC","BCS","BGE","BGT","BLE","BLT","BHI","BLS","BMI","BPL","BVC","BVS",
        "DBRA","DBEQ","DBNE","DBF","JMP","JSR","RTS","RTE","RTR","NOP","TRAP","TRAPV",
        "LINK","UNLK","STOP","RESET","SCC","TAS","CHK","ILLEGAL",
    };
    for (auto* m : kMnemonics) lang.mKeywords.insert(m);

    using PI = TextEditor::PaletteIndex;
    lang.mTokenRegexStrings.push_back({ "\\\"(\\\\.|[^\\\"])*\\\"", PI::String });
    lang.mTokenRegexStrings.push_back({ "\\.[a-zA-Z_][a-zA-Z0-9_]*",  PI::Preprocessor }); // directive (.text/.even…)
    lang.mTokenRegexStrings.push_back({ "\\$[0-9a-fA-F]+",            PI::Number });        // $hex
    lang.mTokenRegexStrings.push_back({ "%[01]+",                    PI::Number });        // %binary
    lang.mTokenRegexStrings.push_back({ "[0-9]+",                    PI::Number });        // decimal
    lang.mTokenRegexStrings.push_back({ "[a-zA-Z_][a-zA-Z0-9_\\.]*", PI::Identifier });    // labels / regs (d0,a7)
    lang.mTokenRegexStrings.push_back({ "[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.\\#\\:\\@\\$]",
                                        PI::Punctuation });

    lang.mCommentStart      = "/*";
    lang.mCommentEnd        = "*/";
    lang.mSingleLineComment = ";";
    lang.mCaseSensitive     = false;
    lang.mAutoIndentation   = true;
    lang.mName              = "68000";

    inited = true;
    return lang;
}

// Apple-1 BASIC — Integer BASIC + Applesoft Lite (the two Bench BASIC targets).
// Keywords are the union of both dialects' statements / functions / operators;
// REM starts a line comment; numbers cover Applesoft floats; variables may carry
// a $ (string) or % (integer) suffix. Case-insensitive (BASIC accepts either).
static const TextEditor::LanguageDefinition& langBasic()
{
    static bool inited = false;
    static TextEditor::LanguageDefinition lang;
    if (inited) return lang;

    static const char* const kKeywords[] = {
        // statements / commands
        "PRINT","INPUT","IF","THEN","FOR","TO","STEP","NEXT","GOTO","GOSUB",
        "RETURN","ON","END","STOP","REM","LET","DIM","DATA","READ","RESTORE","DEF",
        "FN","GET","HOME","TEXT","GR","HGR","HGR2","HCOLOR","HPLOT","COLOR","PLOT",
        "HLIN","VLIN","AT","DRAW","XDRAW","ROT","SCALE","SHLOAD","VTAB","HTAB",
        "INVERSE","NORMAL","FLASH","SPEED","POKE","CALL","WAIT","POP","CONT","RUN",
        "LIST","NEW","CLEAR","CLR","DEL","TRACE","NOTRACE","STORE","RECALL","HIMEM",
        "LOMEM","ONERR","RESUME","AUTO","MAN","DSP","NODSP","SPC","TAB",
        // operators (word form)
        "AND","OR","NOT",
        // numeric functions
        "ABS","ATN","COS","EXP","INT","LOG","RND","SGN","SIN","SQR","TAN","LEN",
        "VAL","ASC","FRE","PDL","POS","SCRN","PEEK","USR",
        // string functions (Applesoft uses the $ form)
        "STR$","CHR$","LEFT$","RIGHT$","MID$",
    };
    for (auto* k : kKeywords) lang.mKeywords.insert(k);

    using PI = TextEditor::PaletteIndex;
    lang.mTokenRegexStrings.push_back({ "\\\"(\\\\.|[^\\\"])*\\\"", PI::String });
    // numbers incl. Applesoft floats / scientific notation (10, 3.14, .5, 1E5)
    lang.mTokenRegexStrings.push_back({ "([0-9]+\\.?[0-9]*|\\.[0-9]+)([eE][-+]?[0-9]+)?",
                                        PI::Number });
    // Applesoft slot I/O PR# / IN# (regexes are compiled case-sensitively, so
    // spell both cases); must precede the identifier rule (first match wins).
    lang.mTokenRegexStrings.push_back({ "([Pp][Rr]|[Ii][Nn])#", PI::Keyword });
    // identifiers + keywords; trailing $ (string) or % (integer) variable suffix
    lang.mTokenRegexStrings.push_back({ "[a-zA-Z][a-zA-Z0-9]*[$%]?", PI::Identifier });
    lang.mTokenRegexStrings.push_back({ "[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.\\#\\:\\@\\$]",
                                        PI::Punctuation });

    lang.mCommentStart      = "\x01";  // BASIC has no block comments; use a SOH
    lang.mCommentEnd        = "\x01";  // sentinel that can never be typed (was "/*"
                                       // which a tight "A/*B" would falsely open)
    lang.mSingleLineComment = "REM";  // matched case-sensitively → uppercase REM
    lang.mCaseSensitive     = false;  // keyword lookup accepts either case
    lang.mAutoIndentation   = false;  // BASIC lines start at column 0
    lang.mName              = "BASIC";

    inited = true;
    return lang;
}

// Bench C — the upstream ImGuiColorTextEdit C() definition (its custom mTokenize
// handles C numbers / strings / char literals, the shared Colorize pass handles
// // and /* */ comments) EXTENDED with the POM1 cc65 library surface so calls
// into the Apple-1 / GEN2 / TMS9918 / gfx / telemetry runtimes stand out as
// KnownIdentifier instead of plain text, plus the cc65 calling-convention
// qualifiers as keywords. We COPY C() (by value) so the upstream cache is left
// untouched, then add to the copy's mKeywords / mIdentifiers.
//
// Every identifier below is a REAL public entry point exported by a header under
// dev/lib (apple1c, gen2c, gfx, tms9918c, telemetry) — sourced from those
// headers, none invented. Macros that expand to a register poke (the gen2_*()
// soft-switch helpers, tele_* one-poke helpers) are intentionally NOT listed:
// they are spelled like calls but resolve to inline volatile accesses, and the
// list stays focused on linked functions a reader can jump to.
static const TextEditor::LanguageDefinition& langC()
{
    static bool inited = false;
    static TextEditor::LanguageDefinition lang;
    if (inited) return lang;

    // Start from the upstream C definition (keeps its mTokenize, the C keyword
    // set, the libc built-ins, comment markers and case sensitivity).
    lang = TextEditor::LanguageDefinition::C();

    // cc65 calling-convention / placement qualifiers — keyword-coloured.
    static const char* const kCc65Keywords[] = {
        "__fastcall__", "__cdecl__", "__A__", "__X__", "__Y__",
        "__asm__", "__attribute__", "__AX__", "__EAX__", "__near__", "__far__",
    };
    for (auto* k : kCc65Keywords) lang.mKeywords.insert(k);

    // POM1 cc65 library entry points — KnownIdentifier-coloured. Names taken
    // verbatim from the dev/lib headers (functions only; macros excluded).
    static const char* const kLibIdentifiers[] = {
        // --- Apple-1 text/keyboard base (dev/lib/apple1c: apple1io.h) ---
        "woz_putc", "woz_puts", "woz_print_hex", "woz_print_hexword", "woz_mon",
        "apple1_iskeypressed", "apple1_getkey", "apple1_readkey",
        // apple1c's lib-own apple1.h adds these line-input helpers:
        "apple1_input_line", "apple1_input_line_prompt",
        // --- GEN2 HGR (dev/lib/gen2c: gen2.h) ---
        "gen2_hgr_init", "gen2_hgr_clear", "gen2_hgr_fill_rect", "gen2_hgr_row",
        "gen2_hgr_fill_pixrect", "gen2_hgr_clear_pixrect", "gen2_hgr_cell",
        "gen2_hgr_plot", "gen2_hgr_unplot", "gen2_hgr_blit", "gen2_hgr_blit7",
        "gen2_hgr_puts", "gen2_hgr_puts8", "gen2_hgr_putu8", "gen2_hgr_putu",
        "gen2_hgr_putu_field", "gen2_hgr_puti", "gen2_hgr_putx",
        "gen2_hgr_hline", "gen2_hgr_vline", "gen2_hgr_line", "gen2_hgr_rect",
        "gen2_hgr_circle", "gen2_hgr_ellipse", "gen2_hgr_puts_color",
        "gen2_hgr_colorize",
        "gen2_lores_init", "gen2_lores_clear", "gen2_lores_setblock",
        "gen2_lores_getblock", "gen2_lores_hlin", "gen2_lores_vlin",
        "gen2_lores_fill_rect", "gen2_wait_vbl",
        "gen2_set_draw_page", "gen2_show_page",
        // --- Card-neutral gfx (dev/lib/gfx: gfx.h) ---
        "gfx_plot", "gfx_hline", "gfx_vline", "gfx_filled_rect", "gfx_clear",
        "gfx_line", "gfx_rect", "gfx_circle", "gfx_ellipse",
        "gfx_utoa", "gfx_itoa", "gfx_hexstr",
        "gfx_cell_glyph", "gfx_cell_color", "gfx_gotoxy", "gfx_putc",
        "gfx_text", "gfx_putu", "gfx_puti", "gfx_putx",
        // --- TMS9918 register / VRAM bus (dev/lib/tms9918c: tms9918.h) ---
        "tms_set_vram_write_addr", "tms_set_vram_read_addr", "tms_write_reg",
        "tms_set_color", "tms_init_regs", "tms_set_interrupt_bit",
        "tms_set_blank", "tms_set_external_video", "tms_wait_end_of_frame",
        "tms_copy_to_vram", "tms_fill_vram", "tms_copy_to_vram_fast",
        // TMS9918 text mode (screen1.h)
        "screen1_load_font", "screen1_cls", "screen1_scroll_up",
        "screen1_prepare", "screen1_putc", "screen1_puts", "screen1_locate",
        "screen1_strinput", "screen1_putcharxy", "screen1_fill_color_attr",
        // TMS9918 bitmap mode (screen2.h)
        "screen2_init_bitmap", "screen2_putc", "screen2_puts", "screen2_plot",
        "screen2_point", "screen2_line", "screen2_ellipse_rect",
        "screen2_circle", "screen2_rect", "screen2_clear", "screen2_filled_rect",
        // TMS9918 sprites (sprites.h)
        "tms_set_total_sprites", "tms_set_sprite", "tms_set_sprite_double_size",
        "tms_set_sprite_magnification", "tms_clear_collisions",
        // TMS9918 sprite shadow SAT (sprite_shadow.h)
        "tms_shadow_init", "tms_shadow_set", "tms_shadow_move",
        "tms_shadow_clear", "tms_shadow_set_terminator", "tms_shadow_flush",
        // TMS9918 vsync (vsync.h)
        "vsync_reset", "vsync_wait", "vsync_wait_n",
        // TMS9918 print helpers (printlib.h)
        "pl_print_dec_u8", "pl_print_dec_u16", "pl_print_hex_u8",
        "pl_print_hex_u16", "woz_print_dec_u8", "woz_print_dec_u16",
        "woz_print_hex_u16", "screen1_print_dec_u8", "screen1_print_dec_u16",
        "screen1_print_hex_u16",
        // TMS9918 random (random.h)
        "srand8", "srand16", "rand8", "rand16", "rand8_below",
        // --- Telemetry side channel (dev/lib/telemetry: telemetry.h) ---
        "tele_put16", "tele_field",
    };
    TextEditor::Identifier libId;
    libId.mDeclaration = "POM1 library function";
    for (auto* k : kLibIdentifiers)
        lang.mIdentifiers.insert(std::make_pair(std::string(k), libId));

    lang.mName = "C";

    inited = true;
    return lang;
}

const TextEditor::LanguageDefinition& langDef(const std::string& language)
{
    if (language == "6502")  return lang6502();
    if (language == "68000") return lang68000();
    if (language == "BASIC") return langBasic();
    if (language == "C")     return langC();
    static const TextEditor::LanguageDefinition plain;   // no highlighting
    return plain;
}

} // namespace bench
