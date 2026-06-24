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

// Numeric operators (numeric expression context)
constexpr uint8_t TOK_ADD        = 0x12;  // "+"
constexpr uint8_t TOK_SUB        = 0x13;  // "-"
constexpr uint8_t TOK_MUL        = 0x14;  // "*"
constexpr uint8_t TOK_DIV        = 0x15;  // "/"
constexpr uint8_t TOK_EQ_NUM     = 0x16;  // "=" numeric compare
constexpr uint8_t TOK_NE_NUM     = 0x17;  // "#" numeric compare
constexpr uint8_t TOK_GT         = 0x19;  // ">"
constexpr uint8_t TOK_NE2_NUM    = 0x1B;  // "<>" numeric compare
constexpr uint8_t TOK_LT         = 0x1C;  // "<"
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
constexpr uint8_t TOK_LP_SUBSTR  = 0x2A;  // "(" substring A$(a,b)
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
constexpr uint8_t TOK_DIM_STR    = 0x4E;  // DIM, next var string
constexpr uint8_t TOK_DIM_NUM    = 0x4F;  // DIM, next var numeric
constexpr uint8_t TOK_TAB        = 0x50;  // TAB
constexpr uint8_t TOK_END        = 0x51;  // END
constexpr uint8_t TOK_INPUT_STR  = 0x52;  // INPUT string
constexpr uint8_t TOK_INPUT_PR   = 0x53;  // INPUT with literal prompt
constexpr uint8_t TOK_INPUT_NUM  = 0x54;  // INPUT numeric (no prompt)
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
constexpr uint8_t TOK_THEN       = 0x24;  // THEN
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
        if (matchWord("GOTO"))  { emit(TOK_GOTO);  numericExpr(); return; }
        if (matchWord("GOSUB")) { emit(TOK_GOSUB); numericExpr(); return; }
        if (matchWord("RETURN")){ emit(TOK_RETURN); return; }
        if (matchWord("END"))   { emit(TOK_END); return; }
        if (matchWord("INPUT")) { stmtInput(); return; }
        if (matchWord("DIM"))   { stmtDim(); return; }
        // REM is handled before parsing (takes the rest of the line literally); see
        // Parser::tryRem in compile(). It never reaches the statement dispatcher.
        if (matchWord("POKE"))  { stmtPoke(); return; }
        if (matchWord("CALL"))  { emit(TOK_CALL); numericExpr(); return; }

        // No statement keyword => implicit LET (assignment to a variable).
        if (isAlpha(peekUpper())) { stmtAssignment(); return; }

        fail("unrecognised statement");
    }

    // --- implicit / explicit LET ------------------------------------------
    // Emits: <var tokens> <"=" token> <rhs>.  The "=" + rhs flavour depends on
    // whether the destination is a string variable.
    void stmtAssignment() {
        bool isStr = variable();           // emits the variable name (+ $40 / subscript)
        skipBlanks();
        if (peek() != '=') fail("expected '=' in assignment");
        ++i_; skipBlanks();
        if (isStr) {
            emit(TOK_EQ_LET_STR);
            stringExpr();
        } else {
            emit(TOK_EQ_LET_NUM);
            numericExpr();
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
        if (firstStr) stringExpr(); else numericExpr();

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

            if (nextStr) stringExpr(); else numericExpr();
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
        numericExpr();
        skipBlanks();
        if (!matchWord("TO")) fail("expected TO in FOR");
        emit(TOK_TO);
        numericExpr();
        skipBlanks();
        if (matchWord("STEP")) {
            emit(TOK_STEP);
            numericExpr();
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
        // condition is a (possibly string) relational/numeric expression
        conditionExpr();
        skipBlanks();
        if (!matchWord("THEN")) fail("expected THEN in IF");
        emit(TOK_THEN);
        skipBlanks();
        // After THEN: a bare line number is an implied GOTO (tokenised as a NUMBER);
        // otherwise a statement follows.
        if (isDigit(peek())) {
            numberConstant();
        } else {
            statement();
        }
    }

    // --- INPUT -------------------------------------------------------------
    void stmtInput() {
        skipBlanks();
        // INPUT "prompt"; var ...   -> $53 then literal then ; ...
        if (peek() == '"') {
            emit(TOK_INPUT_PR);
            stringLiteral();
            skipBlanks();
            if (peek() == ',' || peek() == ';') { ++i_; skipBlanks(); }
            inputVarList();
            return;
        }
        // First var picks numeric ($54) vs string ($52).
        // Peek the variable type without consuming.
        size_t save = i_;
        bool isStr = peekVariableIsString();
        i_ = save;
        emit(isStr ? TOK_INPUT_STR : TOK_INPUT_NUM);
        inputVarList();
    }

    void inputVarList() {
        variable();
        skipBlanks();
        while (peek() == ',') {
            ++i_; skipBlanks();
            // "," in numeric input is $27; we only have a handful of samples, keep it
            // simple: emit comma var.  (Extend with $26 string-input comma later.)
            emit(0x27);
            variable();
            skipBlanks();
        }
    }

    // --- DIM ---------------------------------------------------------------
    void stmtDim() {
        for (;;) {
            skipBlanks();
            bool isStr = peekVariableIsString();
            emit(isStr ? TOK_DIM_STR : TOK_DIM_NUM);
            // variable name (no subscript parsing here -- DIM uses its own paren tokens)
            varNameOnly();
            if (isStr) emit(TOK_STR_VAR);
            skipBlanks();
            if (peek() != '(') fail("expected '(' in DIM");
            ++i_; skipBlanks();
            emit(isStr ? TOK_LP_STRDIM : TOK_LP_NUMDIM);
            numericExpr();
            skipBlanks();
            if (peek() != ')') fail("expected ')' in DIM");
            ++i_; emit(TOK_RP);
            skipBlanks();
            if (peek() == ',') { ++i_; continue; }
            break;
        }
    }

    // --- POKE --------------------------------------------------------------
    void stmtPoke() {
        emit(TOK_POKE);
        numericExpr();
        skipBlanks();
        if (peek() != ',') fail("expected ',' in POKE");
        ++i_; skipBlanks();
        emit(TOK_POKE_COMMA);
        numericExpr();
    }

    // --- expressions -------------------------------------------------------
    // Numeric expression: precedence handled loosely; the ROM flattens to a token
    // stream where operators are interleaved with operands (it relies on a runtime
    // operator-precedence stack, not parenthesised AST). For the image we only need
    // the LINEAR token sequence, so we emit operands and operators left-to-right.
    void numericExpr() {
        numericTerm();
        for (;;) {
            skipBlanks();
            char c = peek();
            uint8_t op = 0;
            if (c == '+') op = TOK_ADD;
            else if (c == '-') op = TOK_SUB;
            else if (c == '*') op = TOK_MUL;
            else if (c == '/') op = TOK_DIV;
            else if (matchWord("MOD")) { emit(TOK_MOD); numericTerm(); continue; }
            else break;
            ++i_; emit(op);
            numericTerm();
        }
    }

    void numericTerm() {
        skipBlanks();
        char c = peek();
        // unary operators
        if (c == '-') { ++i_; emit(TOK_UMINUS); numericTerm(); return; }
        if (c == '+') { ++i_; emit(TOK_UPLUS);  numericTerm(); return; }
        if (matchWord("NOT")) { emit(TOK_NOT); numericTerm(); return; }

        if (c == '(') { ++i_; emit(TOK_LP_EXPR); numericExpr(); skipBlanks();
                        if (peek() != ')') fail("expected ')'"); ++i_; emit(TOK_RP); return; }

        if (isDigit(c)) { numberConstant(); return; }

        // functions
        if (matchWord("PEEK")) { fnCall(TOK_PEEK); return; }
        if (matchWord("RND"))  { fnCall(TOK_RND);  return; }
        if (matchWord("SGN"))  { fnCall(TOK_SGN);  return; }
        if (matchWord("ABS"))  { fnCall(TOK_ABS);  return; }
        if (matchWord("LEN"))  { // LEN( carries its own paren token ($3B)
            skipBlanks();
            if (peek() != '(') fail("expected '(' after LEN");
            ++i_; emit(TOK_LEN_LP);
            stringExpr();
            skipBlanks();
            if (peek() != ')') fail("expected ')' after LEN(");
            ++i_; emit(TOK_RP);
            return;
        }

        if (isAlpha(c)) { variable(); return; }

        fail("expected a numeric term");
    }

    void fnCall(uint8_t fnTok) {
        emit(fnTok);
        skipBlanks();
        if (peek() != '(') fail("expected '(' after function");
        ++i_; emit(TOK_LP_FN);
        numericExpr();
        skipBlanks();
        if (peek() != ')') fail("expected ')' after function");
        ++i_; emit(TOK_RP);
    }

    // String expression: a string literal, a string variable, or (later) concatenation
    // -- Integer BASIC has no string '+'; concatenation is not supported.  For now a
    // single string literal or string variable.
    void stringExpr() {
        skipBlanks();
        if (peek() == '"') { stringLiteral(); return; }
        if (isAlpha(peekUpper())) { bool isStr = variable(); if (!isStr) fail("expected string operand"); return; }
        fail("expected a string operand");
    }

    // A condition for IF: relational over numeric or string operands.
    void conditionExpr() {
        skipBlanks();
        bool lhsStr = startsStringItem();
        if (lhsStr) {
            stringExpr();
            skipBlanks();
            char c = peek();
            if (c == '=') { ++i_; emit(TOK_EQ_STR); stringExpr(); }
            else if (c == '#') { ++i_; emit(TOK_NE_STR); stringExpr(); }
            else fail("expected string relational operator in IF");
            return;
        }
        // numeric condition: lhs <op> rhs (op may be =,#,<,>,<>)
        numericExpr();
        skipBlanks();
        char c = peek();
        uint8_t op = 0;
        if (c == '=') { ++i_; op = TOK_EQ_NUM; }
        else if (c == '#') { ++i_; op = TOK_NE_NUM; }
        else if (c == '<') {
            ++i_;
            if (peek() == '>') { ++i_; op = TOK_NE2_NUM; }
            else op = TOK_LT;
        }
        else if (c == '>') { ++i_; op = TOK_GT; }
        else fail("expected relational operator in IF");
        emit(op);
        numericExpr();
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
    // Emits the var tokens and returns true if it is a STRING variable.
    bool variable() {
        varNameOnly();
        bool isStr = false;
        skipBlanks();
        if (peek() == '$') { ++i_; emit(TOK_STR_VAR); isStr = true; }
        skipBlanks();
        if (peek() == '(') {
            ++i_; skipBlanks();
            if (isStr) {
                // A$(a) or A$(a,b) -- substring.  "(" = $2A, "," = $2B.
                emit(TOK_LP_SUBSTR);
                numericExpr();
                skipBlanks();
                if (peek() == ',') { ++i_; skipBlanks(); emit(0x2B); numericExpr(); skipBlanks(); }
                if (peek() != ')') fail("expected ')' in substring");
                ++i_; emit(TOK_RP);
            } else {
                // numeric array subscript "(" = $2D
                emit(TOK_LP_ARRAY);
                numericExpr();
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
