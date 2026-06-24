// BasicTokeniserInteger.cpp -- see BasicTokeniserInteger.h for the design.
//
// Apple-1 Integer BASIC (Woz) tokeniser. Reproduces, byte-for-byte, the program
// image the $E000 ROM's syntax-table parser builds (roms/basic.rom == the ROM
// source a1basic.s). Verified against the ROM ORACLE (tests/integer_oracle_dump.cpp,
// build/tests/test_integer_oracle), pinned by tests/basic_tokenise_integer_test.cpp.
//
// Unlike Applesoft's flat keyword scan, Integer BASIC tokenisation is CONTEXT-
// SENSITIVE: the same character emits different tokens by grammatical context. The
// ROM does this with a syntax-table state machine; we mirror the SAME grammar with a
// small recursive-descent parser, emitting the correct token at each context.
//
//   * "(" has several tokens: $2D array subscript (numeric), $2A substring (string),
//     $34 numeric DIM, $22 string DIM, $3F PEEK/RND/SGN/ABS, ")" everywhere = $72.
//   * "=" has four: $71 numeric LET, $70 string LET, $56 FOR, $16 numeric compare,
//     $39 string compare.
//   * PRINT has three verb tokens: $62 numeric first item, $61 string first item,
//     $63 no argument.
//   * "," / ";" carry distinct tokens by statement + operand type.
//
// Image format (verified):
//   * Program stored from HIMEM ($1000) DOWNWARD; lines ascending; pp = lowest
//     address = first line. Result.image = bytes for [pp, HIMEM); pp = himem-size.
//   * Each line: [length][line# lo][line# hi][tokens...][$01]
//       length = tokens.size() + 4 (includes the length byte and the trailing $01).
//   * Numeric constant: [first-digit-char | $80][value lo][value hi]  (16-bit binary)
//   * Variable: each char | $80 (letter then letters/digits). String var adds $40.
//   * String literal: $28 [chars | $80] $29.  REM: $5D then rest-of-line chars | $80.
//   * $01 ends each line. $03 = ":" statement separator.
//
// Pure: <string>/<vector>/<cstdint>/<cctype> only -- links standalone into tests.

#include "BasicTokeniserInteger.h"

#include <algorithm>
#include <cctype>

namespace ibasic {

namespace {

// ---------------------------------------------------------------------------
// Token bytes (verified via the ROM oracle / documented in a1basic.s comments).
// ---------------------------------------------------------------------------
// Line / structural
constexpr uint8_t TOK_EOL        = 0x01;  // end of line
constexpr uint8_t TOK_COLON      = 0x03;  // ":" statement separator

// Numeric operators / relational / logical (all live in the ONE expression grammar;
// Integer BASIC has no separate condition syntax). All verified via the ROM oracle.
constexpr uint8_t TOK_ADD        = 0x12;  // "+"
constexpr uint8_t TOK_SUB        = 0x13;  // "-"
constexpr uint8_t TOK_MUL        = 0x14;  // "*"
constexpr uint8_t TOK_DIV        = 0x15;  // "/"
constexpr uint8_t TOK_EQ_NUM     = 0x16;  // "=" numeric compare
constexpr uint8_t TOK_NE_NUM     = 0x17;  // "#" numeric compare
constexpr uint8_t TOK_GE         = 0x18;  // ">=" numeric compare
constexpr uint8_t TOK_GT         = 0x19;  // ">"
constexpr uint8_t TOK_LE         = 0x1A;  // "<=" numeric compare
constexpr uint8_t TOK_NE2_NUM    = 0x1B;  // "<>" numeric compare
constexpr uint8_t TOK_LT         = 0x1C;  // "<"
constexpr uint8_t TOK_AND        = 0x1D;  // "AND"
constexpr uint8_t TOK_OR         = 0x1E;  // "OR"
constexpr uint8_t TOK_MOD        = 0x1F;  // "MOD"
constexpr uint8_t TOK_UPLUS      = 0x35;  // unary "+"
constexpr uint8_t TOK_UMINUS     = 0x36;  // unary "-"
constexpr uint8_t TOK_NOT        = 0x37;  // "NOT"

// Functions
constexpr uint8_t TOK_PEEK       = 0x2E;  // PEEK (uses $3F paren)
constexpr uint8_t TOK_RND        = 0x2F;  // RND  (uses $3F paren)
constexpr uint8_t TOK_SGN        = 0x30;  // SGN  (uses $3F paren)
constexpr uint8_t TOK_ABS        = 0x31;  // ABS  (uses $3F paren)
constexpr uint8_t TOK_LEN_LP     = 0x3B;  // "LEN(" (carries the open paren)

// Parens
constexpr uint8_t TOK_LP_SUBSTR    = 0x2A;  // "(" substring A$(a,b) (in an expression)
constexpr uint8_t TOK_SUBSTR_COMMA = 0x23;  // "," in substring A$(a,b) (oracle: $23)
constexpr uint8_t TOK_LP_STRDEST   = 0x42;  // "(" string-array element as a DESTINATION
                                            //   A$(i)="..."  (single index, no comma)
constexpr uint8_t TOK_LP_ARRAY   = 0x2D;  // "(" numeric array subscript
constexpr uint8_t TOK_LP_STRDIM  = 0x22;  // "(" string DIM
constexpr uint8_t TOK_LP_NUMDIM  = 0x34;  // "(" numeric DIM
constexpr uint8_t TOK_LP_EXPR    = 0x38;  // "(" in numeric expression (grouping)
constexpr uint8_t TOK_LP_FN      = 0x3F;  // "(" for PEEK/RND/SGN/ABS
constexpr uint8_t TOK_RP         = 0x72;  // ")" everywhere

// String operators / separators
constexpr uint8_t TOK_EQ_STR     = 0x39;  // "=" string compare
constexpr uint8_t TOK_NE_STR     = 0x3A;  // "#" string compare
constexpr uint8_t TOK_STR_LIT    = 0x28;  // string literal open
constexpr uint8_t TOK_STR_LIT_END= 0x29;  // string literal close
constexpr uint8_t TOK_STR_VAR    = 0x40;  // trailing "$" on a string variable

// Assignment
constexpr uint8_t TOK_EQ_LET_NUM = 0x71;  // "=" implicit/explicit numeric LET
constexpr uint8_t TOK_EQ_LET_STR = 0x70;  // "=" implicit/explicit string LET
constexpr uint8_t TOK_LET        = 0x5E;  // explicit "LET"

// Statements
constexpr uint8_t TOK_DIM_STR    = 0x4E;  // DIM, FIRST var string
constexpr uint8_t TOK_DIM_NUM    = 0x4F;  // DIM, FIRST var numeric
constexpr uint8_t TOK_DIM_C_STR  = 0x43;  // DIM "," next var string
constexpr uint8_t TOK_DIM_C_NUM  = 0x44;  // DIM "," next var numeric
constexpr uint8_t TOK_TAB        = 0x50;  // TAB
constexpr uint8_t TOK_END        = 0x51;  // END
constexpr uint8_t TOK_INPUT_STR  = 0x52;  // INPUT string (no prompt)
constexpr uint8_t TOK_INPUT_PR   = 0x53;  // INPUT with literal prompt
constexpr uint8_t TOK_INPUT_NUM  = 0x54;  // INPUT numeric (no prompt)
constexpr uint8_t TOK_INPUT_C_STR = 0x26; // INPUT "," before a string var
constexpr uint8_t TOK_INPUT_C_NUM = 0x27; // INPUT "," before a numeric var
constexpr uint8_t TOK_FOR        = 0x55;  // FOR
constexpr uint8_t TOK_EQ_FOR     = 0x56;  // "=" in FOR
constexpr uint8_t TOK_TO         = 0x57;  // TO
constexpr uint8_t TOK_STEP       = 0x58;  // STEP
constexpr uint8_t TOK_NEXT       = 0x59;  // NEXT
constexpr uint8_t TOK_NEXT_COMMA = 0x5A;  // "," in NEXT
constexpr uint8_t TOK_RETURN     = 0x5B;  // RETURN
constexpr uint8_t TOK_GOSUB      = 0x5C;  // GOSUB
constexpr uint8_t TOK_REM        = 0x5D;  // REM (then rest of line, chars | $80)
constexpr uint8_t TOK_GOTO       = 0x5F;  // GOTO
constexpr uint8_t TOK_IF         = 0x60;  // IF
constexpr uint8_t TOK_THEN_LINE  = 0x24;  // THEN <line#>  (implied GOTO)
constexpr uint8_t TOK_THEN_STMT  = 0x25;  // THEN <statement>
constexpr uint8_t TOK_PRINT_STR  = 0x61;  // PRINT, first item string
constexpr uint8_t TOK_PRINT_NUM  = 0x62;  // PRINT, first item numeric
constexpr uint8_t TOK_PRINT_NONE = 0x63;  // PRINT no arg
constexpr uint8_t TOK_POKE       = 0x64;  // POKE
constexpr uint8_t TOK_POKE_COMMA = 0x65;  // "," in POKE
constexpr uint8_t TOK_CALL       = 0x4D;  // CALL

// PRINT item separators
constexpr uint8_t TOK_PR_STR_COMMA = 0x48;  // "," after a string item
constexpr uint8_t TOK_PR_STR_SEMI  = 0x45;  // ";" after a string item
constexpr uint8_t TOK_PR_NUM_COMMA = 0x49;  // "," after a numeric item
constexpr uint8_t TOK_PR_SEMI      = 0x46;  // ";" between items
constexpr uint8_t TOK_PR_SEMI_END  = 0x47;  // ";" at the end of PRINT

// ---------------------------------------------------------------------------
// Recursive-descent tokeniser. One Parser per source line.
// ---------------------------------------------------------------------------

struct ParseError {
    std::string msg;
};

class Parser {
public:
    Parser(const std::string& s, int lineNo) : s_(s), lineNo_(lineNo) {}

    // Tokenise the statement body (text AFTER the line number) into `out`.
    // Throws ParseError on an unhandled/ambiguous construct.
    void parse(std::vector<uint8_t>& out) {
        out_ = &out;
        skipBlanks();
        statementList();
    }

private:
    const std::string& s_;
    size_t i_ = 0;
    int lineNo_ = 0;
    std::vector<uint8_t>* out_ = nullptr;

    // --- low-level cursor helpers (blanks are insignificant outside literals) ---
    void skipBlanks() { while (i_ < s_.size() && s_[i_] == ' ') ++i_; }
    bool eol() const { return i_ >= s_.size(); }
    char peek() const { return i_ < s_.size() ? s_[i_] : '\0'; }
    char peekUpper() const { return static_cast<char>(std::toupper(static_cast<unsigned char>(peek()))); }
    void emit(uint8_t b) { out_->push_back(b); }

    [[noreturn]] void fail(const std::string& what) {
        throw ParseError{"line " + std::to_string(lineNo_) + ": " + what};
    }

    // Match the upper-cased keyword `kw` at the cursor (blanks inside the input are
    // skipped, mirroring the ROM). On success advances the cursor past it.
    bool matchWord(const char* kw) {
        size_t j = i_;
        for (const char* p = kw; *p; ++p) {
            while (j < s_.size() && s_[j] == ' ') ++j;
            if (j >= s_.size()) return false;
            if (std::toupper(static_cast<unsigned char>(s_[j])) != *p) return false;
            ++j;
        }
        i_ = j;
        skipBlanks();
        return true;
    }

    static bool isAlpha(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }
    static bool isDigit(char c) { return c >= '0' && c <= '9'; }

    // --- statement level ---------------------------------------------------
    void statementList() {
        for (;;) {
            statement();
            skipBlanks();
            if (eol()) break;
            if (peek() == ':') { emit(TOK_COLON); ++i_; skipBlanks(); continue; }
            fail("unexpected character past end of statement");
        }
    }

    void statement() {
        skipBlanks();
        if (eol()) return;  // empty statement (e.g. trailing ':')

        // Statement keywords. Order: try multi-word/ambiguous first where needed.
        if (matchWord("PRINT")) { stmtPrint(); return; }
        if (peek() == '?')      { ++i_; skipBlanks(); stmtPrint(); return; }
        if (matchWord("LET"))   { emit(TOK_LET); stmtAssignment(); return; }
        if (matchWord("FOR"))   { stmtFor(); return; }
        if (matchWord("NEXT"))  { stmtNext(); return; }
        if (matchWord("IF"))    { stmtIf(); return; }
        if (matchWord("GOTO"))  { emit(TOK_GOTO);  expression(); return; }
        if (matchWord("GOSUB")) { emit(TOK_GOSUB); expression(); return; }
        if (matchWord("RETURN")){ emit(TOK_RETURN); return; }
        if (matchWord("END"))   { emit(TOK_END); return; }
        if (matchWord("INPUT")) { stmtInput(); return; }
        if (matchWord("DIM"))   { stmtDim(); return; }
        // REM is handled before parsing (takes the rest of the line literally); see
        // Parser::tryRem in compile(). It never reaches the statement dispatcher.
        if (matchWord("POKE"))  { stmtPoke(); return; }
        if (matchWord("CALL"))  { emit(TOK_CALL); expression(); return; }
        if (matchWord("TAB"))   { emit(TOK_TAB); expression(); return; }

        // No statement keyword => implicit LET (assignment to a variable).
        if (isAlpha(peekUpper())) { stmtAssignment(); return; }

        fail("unrecognised statement");
    }

    // --- implicit / explicit LET ------------------------------------------
    // Emits: <var tokens> <"=" token> <rhs>.  The "=" + rhs flavour depends on
    // whether the destination is a string variable.
    void stmtAssignment() {
        bool isStr = variable(/*dest=*/true);  // emits the var (+ $40 / dest subscript)
        skipBlanks();
        if (peek() != '=') fail("expected '=' in assignment");
        ++i_; skipBlanks();
        if (isStr) {
            emit(TOK_EQ_LET_STR);
            stringExpr();
        } else {
            emit(TOK_EQ_LET_NUM);
            expression();
        }
    }

    // --- PRINT -------------------------------------------------------------
    void stmtPrint() {
        skipBlanks();
        // No argument?  (end of statement or ':' next)
        if (eol() || peek() == ':') { emit(TOK_PRINT_NONE); return; }

        // The FIRST item picks the PRINT verb token ($61 string / $62 numeric).
        bool firstStr = startsStringItem();
        emit(firstStr ? TOK_PRINT_STR : TOK_PRINT_NUM);
        if (firstStr) stringExpr(); else expression();

        // Subsequent items separated by ';' or ','. The separator token is chosen by
        // the type of the item that FOLLOWS it (verified via the ROM oracle):
        //   ";" + string -> $45 ; ";" + numeric -> $46 ; ";" at end -> $47
        //   "," + string -> $48 ; "," + numeric -> $49
        for (;;) {
            skipBlanks();
            if (eol() || peek() == ':') break;
            char sep = peek();
            if (sep != ';' && sep != ',') fail("expected ';' or ',' in PRINT");
            ++i_; skipBlanks();

            if (sep == ';' && (eol() || peek() == ':')) { emit(TOK_PR_SEMI_END); break; }

            bool nextStr = startsStringItem();
            if (sep == ';') emit(nextStr ? TOK_PR_STR_SEMI : TOK_PR_SEMI);
            else            emit(nextStr ? TOK_PR_STR_COMMA : TOK_PR_NUM_COMMA);

            // A trailing "," (e.g. PRINT A,) just tabs and ends the statement.
            if (eol() || peek() == ':') break;

            if (nextStr) stringExpr(); else expression();
        }
    }

    // --- FOR / NEXT --------------------------------------------------------
    void stmtFor() {
        // FOR <numvar> = <expr> TO <expr> [STEP <expr>]
        // Oracle: 55 C9 56 ... -> FOR token, then the index var, then "=" ($56).
        emit(TOK_FOR);
        bool isStr = variable();
        if (isStr) fail("FOR index must be a numeric variable");
        skipBlanks();
        if (peek() != '=') fail("expected '=' in FOR");
        ++i_; skipBlanks();
        emit(TOK_EQ_FOR);
        expression();
        skipBlanks();
        if (!matchWord("TO")) fail("expected TO in FOR");
        emit(TOK_TO);
        expression();
        skipBlanks();
        if (matchWord("STEP")) {
            emit(TOK_STEP);
            expression();
        }
    }

    void stmtNext() {
        emit(TOK_NEXT);
        variable();   // the index variable
        skipBlanks();
        while (peek() == ',') {
            ++i_; skipBlanks();
            emit(TOK_NEXT_COMMA);
            variable();
            skipBlanks();
        }
    }

    // --- IF ----------------------------------------------------------------
    void stmtIf() {
        emit(TOK_IF);
        // The condition is just an ordinary expression (relational/logical ops are
        // numeric operators). A bare numeric expression with no relop is legal, e.g.
        // "IF B THEN 35".
        expression();
        skipBlanks();
        if (!matchWord("THEN")) fail("expected THEN in IF");
        // THEN before a LINE NUMBER (implied GOTO) is $24; THEN before a STATEMENT
        // is $25 (verified via the oracle).
        if (isDigit(peek())) {
            emit(TOK_THEN_LINE);
            numberConstant();
        } else {
            emit(TOK_THEN_STMT);
            statement();
        }
    }

    // --- INPUT -------------------------------------------------------------
    // INPUT with a literal prompt:  $53 "prompt" <comma> var [<comma> var]...
    //   The comma BEFORE each var picks $26 (string var) / $27 (numeric var).
    // INPUT with no prompt: the FIRST var picks the verb $54 (numeric) / $52 (string),
    //   then subsequent vars are separated by the same $26/$27 commas.
    void stmtInput() {
        skipBlanks();
        if (peek() == '"') {
            emit(TOK_INPUT_PR);
            stringLiteral();
            skipBlanks();
            // The prompt is followed by a comma (or ';') then the variable list. The
            // separator token is the $26/$27 picked by the first var's type.
            if (peek() == ',' || peek() == ';') ++i_;
            inputVarList(/*emitFirstSep=*/true);
            return;
        }
        // No prompt: the first var's type picks the INPUT verb token.
        bool isStr = peekVariableIsString();
        emit(isStr ? TOK_INPUT_STR : TOK_INPUT_NUM);
        inputVarList(/*emitFirstSep=*/false);
    }

    // Parse the comma-separated variable list of an INPUT. If `emitFirstSep`, a
    // separator token ($26/$27, chosen by the first var) precedes the first var
    // (used after a literal prompt).
    void inputVarList(bool emitFirstSep) {
        bool first = true;
        for (;;) {
            skipBlanks();
            if (!emitFirstSep && first) {
                // first var after the verb token: no separator
            } else {
                bool isStr = peekVariableIsString();
                emit(isStr ? TOK_INPUT_C_STR : TOK_INPUT_C_NUM);
            }
            variable();
            first = false;
            skipBlanks();
            if (peek() != ',') break;
            ++i_;
            emitFirstSep = true;   // every subsequent var gets a separator
        }
    }

    // --- DIM ---------------------------------------------------------------
    // DIM var(n)[,var(n)]...  The FIRST var's keyword is $4E (string) / $4F (numeric);
    // subsequent vars are introduced by the comma tokens $43 (string) / $44 (numeric),
    // NOT a repeated $4E/$4F. "(" is $22 (string DIM) / $34 (numeric DIM).
    void stmtDim() {
        bool first = true;
        for (;;) {
            skipBlanks();
            bool isStr = peekVariableIsString();
            if (first) emit(isStr ? TOK_DIM_STR : TOK_DIM_NUM);
            else       emit(isStr ? TOK_DIM_C_STR : TOK_DIM_C_NUM);
            // variable name (DIM uses its own paren tokens, no subscript parsing here)
            varNameOnly();
            skipBlanks();
            if (isStr) {
                if (peek() != '$') fail("expected '$' on string DIM variable");
                ++i_; emit(TOK_STR_VAR);
                skipBlanks();
            }
            if (peek() != '(') fail("expected '(' in DIM");
            ++i_; skipBlanks();
            emit(isStr ? TOK_LP_STRDIM : TOK_LP_NUMDIM);
            expression();
            skipBlanks();
            if (peek() != ')') fail("expected ')' in DIM");
            ++i_; emit(TOK_RP);
            skipBlanks();
            first = false;
            if (peek() == ',') { ++i_; continue; }
            break;
        }
    }

    // --- POKE --------------------------------------------------------------
    void stmtPoke() {
        emit(TOK_POKE);
        expression();
        skipBlanks();
        if (peek() != ',') fail("expected ',' in POKE");
        ++i_; skipBlanks();
        emit(TOK_POKE_COMMA);
        expression();
    }

    // --- expressions -------------------------------------------------------
    // Integer BASIC has NO separate "condition" grammar: relational (=,#,<,>,<=,>=,
    // <>) and logical (AND,OR,NOT) operators are ordinary numeric operators in the
    // ONE expression grammar. The ROM emits operators interleaved with operands and
    // resolves precedence at RUN time via an operator stack -- the stored image is
    // just the LINEAR token sequence, so we emit operands + operators left-to-right
    // (verified byte-exact against the oracle, including nested "(...)").
    //
    // An operand may be a STRING (literal / string var); after a string operand the
    // only legal operators are "="/"#", which emit the STRING-compare tokens
    // ($39/$3A) and consume a string operand; the comparison RESULT is numeric, so
    // any following operators are numeric again. expression() returns true if the
    // value it just produced is (still) a string -- i.e. a bare string with no
    // comparison applied (callers use it to pick string vs numeric token flavours).
    bool expression() {
        bool lhsStr = primary();
        for (;;) {
            skipBlanks();
            char c = peek();

            // "="/"#": string-compare if the left operand is a string, else numeric.
            if (c == '=' || c == '#') {
                ++i_;
                if (lhsStr) {
                    emit(c == '=' ? TOK_EQ_STR : TOK_NE_STR);
                    bool rhsStr = primary();
                    if (!rhsStr) fail("expected a string operand after string comparison");
                } else {
                    emit(c == '=' ? TOK_EQ_NUM : TOK_NE_NUM);
                    primary();
                }
                lhsStr = false;                 // comparison result is numeric
                continue;
            }

            // Everything else is a numeric operator (Integer BASIC has no string '+').
            uint8_t op = 0;
            if (c == '+') { op = TOK_ADD; ++i_; }
            else if (c == '-') { op = TOK_SUB; ++i_; }
            else if (c == '*') { op = TOK_MUL; ++i_; }
            else if (c == '/') { op = TOK_DIV; ++i_; }
            else if (c == '>') { ++i_; if (peek() == '=') { ++i_; op = TOK_GE; } else op = TOK_GT; }
            else if (c == '<') { ++i_; if (peek() == '=') { ++i_; op = TOK_LE; }
                                       else if (peek() == '>') { ++i_; op = TOK_NE2_NUM; }
                                       else op = TOK_LT; }
            else if (matchWord("AND")) op = TOK_AND;
            else if (matchWord("OR"))  op = TOK_OR;
            else if (matchWord("MOD")) op = TOK_MOD;
            else break;

            if (lhsStr) fail("a string value cannot be used in a numeric expression");
            emit(op);
            // The operand that follows may itself be a STRING (e.g. the right-hand
            // side of "OR" in `D$="N" OR D$="NO"`), about to be string-compared on the
            // next "="/"#". Track its type so that comparison picks $39/$3A, not $16.
            lhsStr = primary();
        }
        return lhsStr;
    }

    // A primary operand. Returns true if it is a STRING value.
    bool primary() {
        skipBlanks();
        char c = peek();

        // Unary operators (numeric only).
        if (c == '-') { ++i_; emit(TOK_UMINUS); primary(); return false; }
        if (c == '+') { ++i_; emit(TOK_UPLUS);  primary(); return false; }
        if (matchWord("NOT")) { emit(TOK_NOT); primary(); return false; }

        // Grouping "(" expr ")" -- numeric grouping token $38.
        if (c == '(') {
            ++i_; emit(TOK_LP_EXPR);
            expression();
            skipBlanks();
            if (peek() != ')') fail("expected ')'");
            ++i_; emit(TOK_RP);
            return false;
        }

        if (c == '"') { stringLiteral(); return true; }
        if (isDigit(c)) { numberConstant(); return false; }

        // Functions (numeric).
        if (matchWord("PEEK")) { fnCall(TOK_PEEK); return false; }
        if (matchWord("RND"))  { fnCall(TOK_RND);  return false; }
        if (matchWord("SGN"))  { fnCall(TOK_SGN);  return false; }
        if (matchWord("ABS"))  { fnCall(TOK_ABS);  return false; }
        if (matchWord("LEN"))  {                 // LEN( carries its own paren ($3B)
            skipBlanks();
            if (peek() != '(') fail("expected '(' after LEN");
            ++i_; emit(TOK_LEN_LP);
            expression();
            skipBlanks();
            if (peek() != ')') fail("expected ')' after LEN(");
            ++i_; emit(TOK_RP);
            return false;
        }

        if (isAlpha(c)) return variable();       // numeric or string variable

        fail("expected an operand");
    }

    void fnCall(uint8_t fnTok) {
        emit(fnTok);
        skipBlanks();
        if (peek() != '(') fail("expected '(' after function");
        ++i_; emit(TOK_LP_FN);
        expression();
        skipBlanks();
        if (peek() != ')') fail("expected ')' after function");
        ++i_; emit(TOK_RP);
    }

    // A string operand specifically (INPUT / assignment RHS). Integer BASIC has no
    // string concatenation, so this is a single string literal or string variable.
    void stringExpr() {
        skipBlanks();
        if (peek() == '"') { stringLiteral(); return; }
        if (isAlpha(peekUpper())) { bool isStr = variable(); if (!isStr) fail("expected string operand"); return; }
        fail("expected a string operand");
    }

    // --- literals / variables / numbers -----------------------------------
    // Numeric constant: [first-digit-char | $80][value lo][value hi].
    void numberConstant() {
        skipBlanks();
        if (!isDigit(peek())) fail("expected a number");
        char first = peek();
        long v = 0;
        while (isDigit(peek())) { v = v * 10 + (peek() - '0'); ++i_; }
        uint16_t val = static_cast<uint16_t>(v & 0xFFFF);  // 16-bit wrap (Integer BASIC)
        emit(static_cast<uint8_t>(first) | 0x80);
        emit(val & 0xFF);
        emit((val >> 8) & 0xFF);
    }

    // String literal: $28 [chars | $80] $29.
    void stringLiteral() {
        if (peek() != '"') fail("expected string literal");
        ++i_;
        emit(TOK_STR_LIT);
        while (i_ < s_.size() && s_[i_] != '"') {
            emit(static_cast<uint8_t>(s_[i_]) | 0x80);
            ++i_;
        }
        if (i_ >= s_.size()) fail("unterminated string literal");
        ++i_;  // consume closing quote
        emit(TOK_STR_LIT_END);
    }

    // Emit a variable NAME only (chars | $80), no $ / subscript handling.
    // Returns the consumed name string (upper-cased) for callers that peek.
    void varNameOnly() {
        skipBlanks();
        if (!isAlpha(peekUpper())) fail("expected a variable name");
        // First char a letter, then letters/digits.
        emit(static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(peek()))) | 0x80);
        ++i_;
        while (i_ < s_.size()) {
            char c = static_cast<char>(std::toupper(static_cast<unsigned char>(s_[i_])));
            if (isAlpha(c) || isDigit(c)) { emit(static_cast<uint8_t>(c) | 0x80); ++i_; }
            else break;
        }
    }

    // Look ahead: is the variable at the cursor a string variable (ends in '$')?
    // Does NOT consume.
    bool peekVariableIsString() {
        size_t j = i_;
        while (j < s_.size() && s_[j] == ' ') ++j;
        if (j >= s_.size() || !isAlpha(static_cast<char>(std::toupper(static_cast<unsigned char>(s_[j]))))) return false;
        ++j;
        while (j < s_.size()) {
            char c = static_cast<char>(std::toupper(static_cast<unsigned char>(s_[j])));
            if (isAlpha(c) || isDigit(c)) ++j;
            else break;
        }
        while (j < s_.size() && s_[j] == ' ') ++j;
        return j < s_.size() && s_[j] == '$';
    }

    // Parse a full variable reference: name (+ "$" for string) (+ subscript/substring).
    // Emits the var tokens and returns true if it is a STRING variable. `dest` marks
    // an assignment DESTINATION, where a string element  A$(i)="..."  uses the string-
    // array-dest paren ($42, single index) rather than the substring paren ($2A).
    bool variable(bool dest = false) {
        varNameOnly();
        bool isStr = false;
        skipBlanks();
        if (peek() == '$') { ++i_; emit(TOK_STR_VAR); isStr = true; }
        skipBlanks();
        if (peek() == '(') {
            ++i_; skipBlanks();
            if (isStr && dest) {
                // A$(i) = ... -- string-array element destination.  "(" = $42.
                emit(TOK_LP_STRDEST);
                expression();
                skipBlanks();
                if (peek() != ')') fail("expected ')' in string-array destination");
                ++i_; emit(TOK_RP);
            } else if (isStr) {
                // A$(a) or A$(a,b) -- substring (read).  "(" = $2A, "," = $23, ")" = $72.
                emit(TOK_LP_SUBSTR);
                expression();
                skipBlanks();
                if (peek() == ',') { ++i_; skipBlanks(); emit(TOK_SUBSTR_COMMA); expression(); skipBlanks(); }
                if (peek() != ')') fail("expected ')' in substring");
                ++i_; emit(TOK_RP);
            } else {
                // numeric array subscript "(" = $2D
                emit(TOK_LP_ARRAY);
                expression();
                skipBlanks();
                if (peek() != ')') fail("expected ')' in array subscript");
                ++i_; emit(TOK_RP);
            }
        }
        return isStr;
    }

    // Decide whether the upcoming PRINT/IF item is a STRING item (a literal or string
    // variable) vs a numeric item.  Does NOT consume.
    bool startsStringItem() {
        size_t j = i_;
        while (j < s_.size() && s_[j] == ' ') ++j;
        if (j >= s_.size()) return false;
        if (s_[j] == '"') return true;
        // a string variable: name then '$'
        if (isAlpha(static_cast<char>(std::toupper(static_cast<unsigned char>(s_[j]))))) {
            size_t k = j + 1;
            while (k < s_.size()) {
                char c = static_cast<char>(std::toupper(static_cast<unsigned char>(s_[k])));
                if (isAlpha(c) || isDigit(c)) ++k; else break;
            }
            while (k < s_.size() && s_[k] == ' ') ++k;
            // Don't treat LEN(/PEEK(/etc. specially here -- those are numeric functions
            // and won't have a '$'.
            return k < s_.size() && s_[k] == '$';
        }
        return false;
    }

public:
    // REM handling needs the raw tail (with leading space) -- exposed for the driver.
    // Returns true if this line was a REM (already handled), populating `out`.
    static bool tryRem(const std::string& body, int lineNo, std::vector<uint8_t>& out);
};

// REM: emit $5D, then the rest of the source line verbatim as chars | $80, INCLUDING
// the single space that follows "REM". The ROM keeps that separating space (oracle:
// "10 REM HELLO" -> 5D A0 C8 C5 CC CC CF).  We do NOT upper-case here? -- the live
// keyboard path upper-cases, and the oracle was fed upper-case; to match, characters
// are taken as-is from the (already upper-cased) body.
bool Parser::tryRem(const std::string& body, int lineNo, std::vector<uint8_t>& out) {
    (void)lineNo;
    // Find "REM" at the start (after leading blanks), case-insensitive.
    size_t i = 0;
    while (i < body.size() && body[i] == ' ') ++i;
    auto up = [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); };
    if (i + 3 > body.size()) return false;
    if (up(body[i]) != 'R' || up(body[i+1]) != 'E' || up(body[i+2]) != 'M') return false;
    // Must be followed by end-of-word (space, end, or non-alnum) so we don't match
    // e.g. "REMARK" -- though Integer BASIC has no such keyword, be safe.
    size_t after = i + 3;
    if (after < body.size()) {
        char c = up(body[after]);
        if (std::isalnum(static_cast<unsigned char>(c))) return false;
    }
    out.push_back(TOK_REM);
    // Copy the rest of the line as-is (chars | $80), starting right after "REM".
    for (size_t k = after; k < body.size(); ++k) {
        out.push_back(static_cast<uint8_t>(body[k]) | 0x80);
    }
    return true;
}

void appendHexZone(std::string& hex, uint16_t addr, const std::vector<uint8_t>& bytes) {
    auto hx = [](int v) -> char { return static_cast<char>(v < 10 ? '0' + v : 'A' + v - 10); };
    size_t i = 0;
    while (i < bytes.size()) {
        uint16_t a = static_cast<uint16_t>(addr + i);
        hex += hx((a >> 12) & 0xF); hex += hx((a >> 8) & 0xF);
        hex += hx((a >> 4) & 0xF);  hex += hx(a & 0xF); hex += ':';
        for (int n = 0; n < 16 && i < bytes.size(); ++n, ++i) {
            hex += ' ';
            hex += hx((bytes[i] >> 4) & 0xF);
            hex += hx(bytes[i] & 0xF);
        }
        hex += '\n';
    }
}

struct Line {
    int                  number = 0;
    std::vector<uint8_t> tokens;
};

} // namespace

Result compile(const std::string& source, uint16_t himem) {
    Result r;
    r.himem = himem;

    // 1) Split into physical lines, parse the leading line number, tokenise body.
    std::vector<Line> lines;
    size_t pos = 0;
    int physical = 0;
    while (pos <= source.size()) {
        size_t nl = source.find('\n', pos);
        std::string raw = (nl == std::string::npos) ? source.substr(pos)
                                                     : source.substr(pos, nl - pos);
        pos = (nl == std::string::npos) ? source.size() + 1 : nl + 1;
        ++physical;

        // The Apple-1 keyboard is upper-case only, so the ROM only ever sees upper
        // case input. Upper-case everything except inside string literals -- but the
        // ROM upper-cases the WHOLE input line at read time, so we upper-case all.
        std::string line;
        line.reserve(raw.size());
        for (char ch : raw) {
            if (ch == '\r') continue;
            line += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }

        size_t j = 0;
        while (j < line.size() && line[j] == ' ') ++j;
        if (j >= line.size()) continue;  // blank line

        if (!(line[j] >= '0' && line[j] <= '9')) {
            r.error = "line " + std::to_string(physical) +
                      ": Integer BASIC program lines must start with a line number";
            return r;
        }
        long num = 0;
        while (j < line.size() && line[j] >= '0' && line[j] <= '9') {
            num = num * 10 + (line[j] - '0');
            ++j;
        }
        if (num > 65535) {
            r.error = "line " + std::to_string(physical) + ": line number > 65535";
            return r;
        }

        Line L;
        L.number = static_cast<int>(num);
        std::string body = line.substr(j);

        // REM takes the rest of the line literally -- handle before the parser.
        if (!Parser::tryRem(body, physical, L.tokens)) {
            try {
                Parser p(body, physical);
                p.parse(L.tokens);
            } catch (const ParseError& e) {
                r.error = e.msg;
                return r;
            }
        }
        lines.push_back(std::move(L));
    }

    if (lines.empty()) { r.error = "no BASIC lines to compile"; return r; }

    // Integer BASIC stores lines ascending by number; stable-sort keeps source order
    // for accidental duplicates.
    std::stable_sort(lines.begin(), lines.end(),
                     [](const Line& a, const Line& b) { return a.number < b.number; });

    // 2) Lay out the image: lines ascending, each [len][num lo][num hi][toks][$01].
    //    Stored DOWNWARD from himem; pp = himem - total.
    std::vector<uint8_t> image;
    for (const Line& L : lines) {
        const size_t len = L.tokens.size() + 4;  // length byte + num(2) + tokens + $01
        if (len > 0xFF) {
            r.error = "line " + std::to_string(L.number) + ": tokenised line too long (>255 bytes)";
            return r;
        }
        image.push_back(static_cast<uint8_t>(len));
        image.push_back(static_cast<uint8_t>(L.number & 0xFF));
        image.push_back(static_cast<uint8_t>((L.number >> 8) & 0xFF));
        for (uint8_t b : L.tokens) image.push_back(b);
        image.push_back(TOK_EOL);
    }

    r.pp = static_cast<uint16_t>(himem - image.size());
    r.image = std::move(image);
    r.lineCount = static_cast<int>(lines.size());
    r.ok = true;

    appendHexZone(r.hex, r.pp, r.image);
    return r;
}

} // namespace ibasic
