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
    lang.mTokenRegexStrings.push_back({ "\\.[a-zA-Z_][a-zA-Z0-9_]*",  PI::Preprocessor }); // ca65 directive
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

const TextEditor::LanguageDefinition& langDef(const std::string& language)
{
    if (language == "6502")  return lang6502();
    if (language == "68000") return lang68000();
    if (language == "C")     return TextEditor::LanguageDefinition::C();
    static const TextEditor::LanguageDefinition plain;   // no highlighting
    return plain;
}

} // namespace bench
