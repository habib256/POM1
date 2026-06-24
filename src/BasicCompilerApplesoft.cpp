// BasicCompilerApplesoft.cpp -- see BasicCompilerApplesoft.h.
//
// Phase 1: a real native-code generator for the INTEGER subset of Applesoft.
// Recursive-descent / precedence-climbing parser -> straight-line ca65 assembly.
// 16-bit signed values; variables and expression temporaries live at fixed BSS
// addresses (no name lookup at run time); control flow is native (GOTO=JMP,
// GOSUB/RETURN=JSR/RTS, FOR/NEXT and IF are native branches). Graphics statements
// call the fixed runtime ABI (rt_hgr/rt_hcolor/rt_plot/rt_line) wired per card.

#include "BasicCompilerApplesoft.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace basicnative {
namespace {

// ---- lexer -----------------------------------------------------------------

enum class T { Eol, Num, Ident, Kw, Op, Comma, LParen, RParen, Semi, Colon, Str };

struct Tok {
    T           t = T::Eol;
    std::string s;        // ident/keyword/op text (upper-cased)
    long        num = 0;  // integer value
    double      fnum = 0; // full numeric value (for the float phase)
    bool        isFloat = false; // literal had a '.' (non-integer)
};

bool isKeyword(const std::string& s)
{
    static const std::set<std::string> kw = {
        "REM","HGR","HGR2","HCOLOR","HPLOT","TO","FOR","NEXT","STEP","IF","THEN",
        "GOTO","GOSUB","RETURN","END","STOP","LET","PRINT","AND","OR","NOT","ABS",
        "INT","SQR","SIN",
    };
    return kw.count(s) != 0;
}

struct Lexer {
    const std::string& src;
    size_t i = 0;
    std::string err;

    explicit Lexer(const std::string& s) : src(s) {}

    // Lex the remainder of the current logical line into toks (until '\n' or end).
    // Returns false (and sets err) on a bad character.
    bool lexLine(std::vector<Tok>& toks)
    {
        toks.clear();
        bool remLine = false;
        while (i < src.size() && src[i] != '\n') {
            char c = src[i];
            if (c == '\r') { ++i; continue; }
            if (c == ' ' || c == '\t') { ++i; continue; }
            if (remLine) { ++i; continue; }   // REM swallows the rest

            if (std::isdigit(static_cast<unsigned char>(c)) ||
                (c == '.' && i + 1 < src.size() && std::isdigit(static_cast<unsigned char>(src[i + 1])))) {
                long v = 0; bool isF = false;
                std::string lit;
                while (i < src.size() && std::isdigit(static_cast<unsigned char>(src[i]))) {
                    v = v * 10 + (src[i] - '0'); lit += src[i]; ++i;
                }
                if (i < src.size() && src[i] == '.') {       // fractional part
                    isF = true; lit += '.'; ++i;
                    while (i < src.size() && std::isdigit(static_cast<unsigned char>(src[i]))) { lit += src[i]; ++i; }
                }
                Tok t; t.t = T::Num; t.num = v; t.fnum = std::strtod(lit.c_str(), nullptr); t.isFloat = isF;
                toks.push_back(t);
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(c))) {
                std::string id;
                while (i < src.size() &&
                       (std::isalnum(static_cast<unsigned char>(src[i])))) {
                    id += static_cast<char>(std::toupper(static_cast<unsigned char>(src[i])));
                    ++i;
                }
                if (i < src.size() && (src[i] == '$' || src[i] == '%')) {
                    if (src[i] == '$') { err = "string variables need a later phase"; return false; }
                    ++i;   // accept % integer suffix, treat as plain int var
                }
                if (id == "REM") { toks.push_back({T::Kw, "REM", 0}); remLine = true; continue; }
                toks.push_back({isKeyword(id) ? T::Kw : T::Ident, id, 0});
                continue;
            }
            if (c == '"') {                 // string literal (source case preserved)
                ++i; std::string s;
                while (i < src.size() && src[i] != '"' && src[i] != '\n') { s += src[i]; ++i; }
                if (i < src.size() && src[i] == '"') ++i;
                toks.push_back({T::Str, s, 0});
                continue;
            }
            if (c == '?') { toks.push_back({T::Kw, "PRINT", 0}); ++i; continue; }
            if (c == ',') { toks.push_back({T::Comma, ",", 0}); ++i; continue; }
            if (c == ';') { toks.push_back({T::Semi, ";", 0}); ++i; continue; }
            if (c == ':') { toks.push_back({T::Colon, ":", 0}); ++i; continue; }
            if (c == '(') { toks.push_back({T::LParen, "(", 0}); ++i; continue; }
            if (c == ')') { toks.push_back({T::RParen, ")", 0}); ++i; continue; }
            // operators, incl. 2-char relationals
            if (std::string("+-*/^=<>").find(c) != std::string::npos) {
                std::string op(1, c);
                if ((c == '<' || c == '>') && i + 1 < src.size() &&
                    (src[i + 1] == '=' || src[i + 1] == '>')) {
                    // <=, >=, <>
                    if (!((c == '>' && src[i + 1] == '<'))) { op += src[i + 1]; ++i; }
                }
                toks.push_back({T::Op, op, 0});
                ++i;
                continue;
            }
            err = std::string("unexpected character '") + c + "'";
            return false;
        }
        if (i < src.size() && src[i] == '\n') ++i;
        return true;
    }
};

// ---- code generator --------------------------------------------------------

struct Line { int number; std::vector<Tok> toks; };

struct Codegen {
    Card card;
    bool fp = false;        // floating-point phase (binary32) vs integer phase
    std::vector<std::string> code;   // emitted lines (peephole-optimized in body())
    std::string err;

    int vw() const { return fp ? 4 : 2; }   // value width in bytes

    std::set<std::string>   vars;       // variable labels needed
    std::set<std::string>   cmpTables;  // comparison truth tables needed (LT,EQ,..)
    int                     maxTemp = 0;
    int                     forCounter = 0;
    int                     labelCounter = 0;

    // FOR context
    struct ForCtx { std::string var; int id; };
    std::vector<ForCtx> forStack;
    std::set<int>       forSlots;   // FOR ids needing limit/step BSS slots
    std::set<int>       lineNumbers; // all program line numbers (for GOTO/GOSUB checks)

    // verify a GOTO/GOSUB/THEN target exists; emit a clear error if not.
    bool checkTarget(int n, const char* kw) {
        if (lineNumbers.count(n)) return true;
        return fail(std::string(kw) + " " + std::to_string(n) + ": no such line number in the program");
    }

    // current line's tokens + cursor
    const std::vector<Tok>* tk = nullptr;
    size_t p = 0;
    int curLineNo = 0;

    void emit(const std::string& s) { code.push_back(s); }
    bool fail(const std::string& m) { err = "line " + std::to_string(curLineNo) + ": " + m; return false; }

    // ---- peephole optimizer (see optimizePeephole) -----------------------------
    // Produce the final assembly body after running the peephole pass. The native
    // codegen routes every value through 2/4-byte temp slots; the optimizer fuses
    // the resulting "define a temp, then immediately copy it elsewhere" chains so
    // the standalone image stays small (a hard requirement -- code shares the
    // $0300-$1FFF window with the GEN2 framebuffer at $2000).
    std::string body() { optimizePeephole(); std::string s;
        for (const std::string& l : code) { s += l; s += "\n"; } return s; }
    void optimizePeephole();

    const Tok& cur() { static Tok eol; return p < tk->size() ? (*tk)[p] : eol; }
    void adv() { ++p; }
    bool isKw(const char* k) { return cur().t == T::Kw && cur().s == k; }
    bool isOp(const char* o) { return cur().t == T::Op && cur().s == o; }

    std::string varLabel(const std::string& n) { vars.insert(n); return "V_" + n; }
    std::string temp(int d) { if (d + 1 > maxTemp) maxTemp = d + 1; return "T" + std::to_string(d); }

    // constant tracking for strength reduction (set by primary on a bare literal)
    bool lastNum = false;
    long lastNumVal = 0;

    void negate(const std::string& t) {
        emit("\tsec"); emit("\tlda #0"); emit("\tsbc " + t); emit("\tsta " + t);
        emit("\tlda #0"); emit("\tsbc " + t + "+1"); emit("\tsta " + t + "+1");
    }
    // Td = Td * k  via shifts+adds (the multiply a compiler does for a constant).
    void mulByConst(int d, long k);

    // ---- expression codegen: result of expr at precedence >= minPrec into T[d]
    // 16-bit ops are inlined; *, /, comparisons go through the runtime.
    void copy16(const std::string& dst, const std::string& src)
    { emit("\tlda " + src); emit("\tsta " + dst); emit("\tlda " + src + "+1"); emit("\tsta " + dst + "+1"); }
    // copy a value (2 bytes int / 4 bytes float) between labels
    void copyV(const std::string& dst, const std::string& src) {
        for (int i = 0; i < vw(); ++i) {
            std::string off = i ? "+" + std::to_string(i) : "";
            emit("\tlda " + src + off); emit("\tsta " + dst + off);
        }
    }
    // ---- float helpers (binary32; runtime in dev/lib/basicrt/basicrt_float.s) --
    void fpLoadConst(const std::string& dst, double v) {
        float f = static_cast<float>(v); uint8_t b[4]; std::memcpy(b, &f, 4);
        for (int i = 0; i < 4; ++i) {
            std::string off = i ? "+" + std::to_string(i) : "";
            emit("\tlda #$" + hex2(b[i])); emit("\tsta " + dst + off);
        }
    }
    void fpNeg(const std::string& t) { emit("\tlda " + t + "+3"); emit("\teor #$80"); emit("\tsta " + t + "+3"); }
    void fpBinOp(const std::string& op, const std::string& a, const std::string& b);
    // float temp -> 16-bit int into `dst` (2 bytes) via fp_toint16
    void fpToIntInto(const std::string& dst, const std::string& srcTemp) {
        copyV("FA", srcTemp); emit("\tjsr fp_toint16");
        emit("\tlda FA"); emit("\tsta " + dst); emit("\tlda FA+1"); emit("\tsta " + dst + "+1");
    }
    // emit "if value in temp is zero, branch to falseLabel"
    void emitIfFalse(const std::string& tD, const std::string& falseLabel) {
        if (fp) {   // float zero == 0x00000000 (ignore the sign bit of byte 3)
            emit("\tlda " + tD + "+3"); emit("\tand #$7F");
            emit("\tora " + tD); emit("\tora " + tD + "+1"); emit("\tora " + tD + "+2");
            emit("\tbeq " + falseLabel);
        } else {
            emit("\tlda " + tD); emit("\tora " + tD + "+1"); emit("\tbeq " + falseLabel);
        }
    }
    void loadConst(const std::string& dst, long v)
    { unsigned w = static_cast<unsigned>(v) & 0xFFFF;
      emit("\tlda #$" + hex2(w & 0xFF)); emit("\tsta " + dst);
      emit("\tlda #$" + hex2((w >> 8) & 0xFF)); emit("\tsta " + dst + "+1"); }
    static std::string hex2(unsigned b) { const char* h = "0123456789ABCDEF"; std::string s; s += h[(b>>4)&0xF]; s += h[b&0xF]; return s; }

    bool primary(int d);
    bool unary(int d);
    bool binary(int d, int minPrec);
    bool expr(int d) { return binary(d, 0); }

    // op precedence (higher binds tighter): OR=1 AND=2 cmp=3 +-=4 */=5
    static int prec(const std::string& op) {
        if (op == "OR") return 1;
        if (op == "AND") return 2;
        if (op == "=" || op == "<" || op == ">" || op == "<=" || op == ">=" || op == "<>") return 3;
        if (op == "+" || op == "-") return 4;
        if (op == "*" || op == "/") return 5;
        return -1;
    }

    void binOp(const std::string& op, const std::string& a, const std::string& b);

    bool statement();
    bool line();
    bool program(const std::vector<Line>& lines);

    std::string cmpTableName(const std::string& op) {
        if (op == "<")  return "Cmp_LT";
        if (op == ">")  return "Cmp_GT";
        if (op == "=")  return "Cmp_EQ";
        if (op == "<=") return "Cmp_LE";
        if (op == ">=") return "Cmp_GE";
        return "Cmp_NE"; // <>
    }
};

// primary := NUM | VAR | '(' expr ')' | '-' primary | NOT primary | ABS '(' expr ')'
bool Codegen::primary(int d)
{
    std::string Td = temp(d);
    lastNum = false;
    if (cur().t == T::Num) {
        if (fp) fpLoadConst(Td, cur().fnum);
        else {
            if (cur().isFloat) return fail("non-integer literal in the integer phase");
            loadConst(Td, cur().num); lastNum = true; lastNumVal = cur().num;
        }
        adv(); return true;
    }
    if (cur().t == T::Ident) { copyV(Td, varLabel(cur().s)); adv(); return true; }
    if (cur().t == T::LParen) {
        adv();
        if (!expr(d)) return false;
        if (cur().t != T::RParen) return fail("expected ')'");
        adv();
        return true;
    }
    if (isOp("-")) { adv(); if (!primary(d)) return false;
        if (fp) { fpNeg(Td); return true; }
        // negate Td: Td = 0 - Td
        emit("\tsec"); emit("\tlda #0"); emit("\tsbc " + Td); emit("\tsta " + Td);
        emit("\tlda #0"); emit("\tsbc " + Td + "+1"); emit("\tsta " + Td + "+1"); return true; }
    if (isKw("NOT")) { if (fp) return fail("NOT is not supported in the float phase"); adv();
        if (!primary(d)) return false;
        emit("\tlda " + Td); emit("\teor #$FF"); emit("\tsta " + Td);
        emit("\tlda " + Td + "+1"); emit("\teor #$FF"); emit("\tsta " + Td + "+1"); return true; }
    if (isKw("ABS")) { adv(); if (cur().t != T::LParen) return fail("ABS expects '('"); adv();
        if (!expr(d)) return false; if (cur().t != T::RParen) return fail("expected ')'"); adv();
        if (fp) { emit("\tlda " + Td + "+3"); emit("\tand #$7F"); emit("\tsta " + Td + "+3"); return true; }
        std::string done = "Labs" + std::to_string(labelCounter++);
        emit("\tlda " + Td + "+1"); emit("\tbpl " + done);
        emit("\tsec"); emit("\tlda #0"); emit("\tsbc " + Td); emit("\tsta " + Td);
        emit("\tlda #0"); emit("\tsbc " + Td + "+1"); emit("\tsta " + Td + "+1");
        emit(done + ":"); return true; }
    // INT(x): truncate toward zero. In the integer phase values are already whole,
    // so INT is the identity and links nothing; in float it calls fp_int.
    if (isKw("INT")) { adv(); if (cur().t != T::LParen) return fail("INT expects '('"); adv();
        if (!expr(d)) return false; if (cur().t != T::RParen) return fail("expected ')'"); adv();
        if (fp) { copyV("FA", Td); emit("\tjsr fp_int"); copyV(Td, "FA"); }
        return true; }
    // SQR(x) / SIN(x): transcendental, float only (the auto-precision pass forces
    // the float phase whenever either appears, so fp is always true here).
    if (isKw("SQR") || isKw("SIN")) {
        const char* rt = isKw("SQR") ? "fp_sqrt" : "fp_sin";
        const char* nm = isKw("SQR") ? "SQR" : "SIN";
        adv(); if (cur().t != T::LParen) return fail(std::string(nm) + " expects '('"); adv();
        if (!expr(d)) return false; if (cur().t != T::RParen) return fail("expected ')'"); adv();
        if (!fp) return fail(std::string(nm) + " requires the floating-point phase");
        copyV("FA", Td); emit(std::string("\tjsr ") + rt); copyV(Td, "FA");
        return true; }
    return fail("expected a value");
}

bool Codegen::unary(int d) { return primary(d); }

// emit Ta = Ta <op> Tb
void Codegen::binOp(const std::string& op, const std::string& a, const std::string& b)
{
    if (op == "+") {
        emit("\tclc"); emit("\tlda " + a); emit("\tadc " + b); emit("\tsta " + a);
        emit("\tlda " + a + "+1"); emit("\tadc " + b + "+1"); emit("\tsta " + a + "+1");
    } else if (op == "-") {
        emit("\tsec"); emit("\tlda " + a); emit("\tsbc " + b); emit("\tsta " + a);
        emit("\tlda " + a + "+1"); emit("\tsbc " + b + "+1"); emit("\tsta " + a + "+1");
    } else if (op == "AND") {
        emit("\tlda " + a); emit("\tand " + b); emit("\tsta " + a);
        emit("\tlda " + a + "+1"); emit("\tand " + b + "+1"); emit("\tsta " + a + "+1");
    } else if (op == "OR") {
        emit("\tlda " + a); emit("\tora " + b); emit("\tsta " + a);
        emit("\tlda " + a + "+1"); emit("\tora " + b + "+1"); emit("\tsta " + a + "+1");
    } else if (op == "*" || op == "/") {
        copy16("rt_a", a); copy16("rt_b", b);
        emit(op == "*" ? "\tjsr rt_mul" : "\tjsr rt_div");
        copy16(a, "rt_a");
    } else {
        // comparison -> 0/1 in a
        copy16("rt_a", a); copy16("rt_b", b);
        emit("\tjsr rt_cmp16");        // A = 0(a<b) / 1(a==b) / 2(a>b), signed
        std::string tbl = cmpTableName(op); cmpTables.insert(op);
        emit("\ttax"); emit("\tlda " + tbl + ",x"); emit("\tsta " + a);
        emit("\tlda #0"); emit("\tsta " + a + "+1");
    }
}

// float a = a <op> b  (4-byte binary32 temps; runtime ops on FA/FB)
void Codegen::fpBinOp(const std::string& op, const std::string& a, const std::string& b)
{
    // Logical AND/OR on float truth values (0.0 = false, non-zero = true).
    // Result is the canonical float 1.0 / 0.0. Reduce each operand to a 0/1 byte
    // (true if any of the low 3 bytes or the masked exponent/sign is non-zero),
    // combine, then materialise the float result -- never the fp_cmp path.
    if (op == "AND" || op == "OR") {
        auto truthByte = [&](const std::string& t, const std::string& dst) {
            emit("\tlda " + t + "+3"); emit("\tand #$7F");
            emit("\tora " + t); emit("\tora " + t + "+1"); emit("\tora " + t + "+2");
            std::string nz = "Ltb" + std::to_string(labelCounter);
            std::string dn = "Ltd" + std::to_string(labelCounter++);
            emit("\tbeq " + nz); emit("\tlda #1"); emit("\tjmp " + dn);
            emit(nz + ":\tlda #0"); emit(dn + ":\tsta " + dst);
        };
        truthByte(a, "FA"); truthByte(b, "FB");
        emit("\tlda FA"); emit(op == "AND" ? "\tand FB" : "\tora FB");
        std::string fl = "Llo" + std::to_string(labelCounter);
        std::string dn = "Lld" + std::to_string(labelCounter++);
        emit("\tbeq " + fl);
        emit("\tlda #$00"); emit("\tsta " + a); emit("\tsta " + a + "+1");
        emit("\tlda #$80"); emit("\tsta " + a + "+2"); emit("\tlda #$3F"); emit("\tsta " + a + "+3");  // 1.0
        emit("\tjmp " + dn);
        emit(fl + ":");
        emit("\tlda #$00"); emit("\tsta " + a); emit("\tsta " + a + "+1");
        emit("\tsta " + a + "+2"); emit("\tsta " + a + "+3");                                          // 0.0
        emit(dn + ":");
        return;
    }
    copyV("FA", a); copyV("FB", b);
    if (op == "+" || op == "-" || op == "*" || op == "/") {
        const char* rt = op == "+" ? "fp_add" : op == "-" ? "fp_sub" : op == "*" ? "fp_mul" : "fp_div";
        emit(std::string("\tjsr ") + rt);
        copyV(a, "FA");
        return;
    }
    // comparison -> float 1.0 (true) or 0.0 (false)
    emit("\tjsr fp_cmp");          // A = 0(a<b)/1(==)/2(a>b)
    std::string tbl = cmpTableName(op); cmpTables.insert(op);
    emit("\ttax"); emit("\tlda " + tbl + ",x");
    std::string zero = "Lfz" + std::to_string(labelCounter);
    std::string done = "Lfd" + std::to_string(labelCounter++);
    emit("\tbeq " + zero);
    emit("\tlda #$00"); emit("\tsta " + a); emit("\tsta " + a + "+1");
    emit("\tlda #$80"); emit("\tsta " + a + "+2"); emit("\tlda #$3F"); emit("\tsta " + a + "+3");  // 1.0
    emit("\tjmp " + done);
    emit(zero + ":");
    emit("\tlda #$00"); emit("\tsta " + a); emit("\tsta " + a + "+1");
    emit("\tsta " + a + "+2"); emit("\tsta " + a + "+3");                                          // 0.0
    emit(done + ":");
}

// ---- peephole optimizer ----------------------------------------------------
// The codegen emits values through fixed 2/4-byte slots, producing chains like
//   <define Tn>            ; lda <src|#imm> / sta Tn+0..w-1   (a "store block")
//   lda Tn+0 / sta D+0 ... ; copy block  D <- Tn
// Two transforms run to fixpoint on straight-line spans:
//   CP: a store block to a temp Tn, immediately followed by a copy block D<-Tn
//       with Tn dead afterwards, is rewritten to store straight into D (the
//       intermediate temp vanishes).
//   SC: a resulting self-copy block (D <- D) is deleted.
// Liveness is intra-block only: any label / branch / jmp / jsr / rts ends the
// scan conservatively (Tn assumed live), so cross-flow values are never touched.
namespace {
// strip a "+N" byte offset; return base operand and the offset (0 if none).
bool splitOff(const std::string& op, std::string& base, int& off) {
    size_t plus = op.find('+');
    if (plus == std::string::npos) { base = op; off = 0; return true; }
    base = op.substr(0, plus);
    off = std::atoi(op.c_str() + plus + 1);
    return true;
}
// a single-tab "  mnem operand" line (no label prefix). Returns false for labels,
// blank lines, or label:insn lines.
bool parseInsn(const std::string& l, std::string& mnem, std::string& operand) {
    if (l.size() < 4 || l[0] != '\t') return false;
    size_t sp = l.find(' ', 1);
    if (sp == std::string::npos) { mnem = l.substr(1); operand.clear(); return true; }
    mnem = l.substr(1, sp - 1); operand = l.substr(sp + 1);
    return true;
}
bool isTempName(const std::string& s) {
    if (s.size() < 2 || s[0] != 'T') return false;
    for (size_t i = 1; i < s.size(); ++i) if (!std::isdigit((unsigned char)s[i])) return false;
    return true;
}
bool isFlowLine(const std::string& l) {
    std::string m, o;
    if (!parseInsn(l, m, o)) return true;   // label / blank / label:insn -> boundary
    return m == "jmp" || m == "jsr" || m == "rts" || m == "rti" || m == "brk" ||
           (m.size() == 3 && m[0] == 'b' && m != "bit");   // branches
}
} // namespace

void Codegen::optimizePeephole()
{
    const int w = vw();
    // Detect a store block at index i: w consecutive (lda <any> / sta base+j) pairs
    // writing base+0,+1,... in order. Fills base and the w source operands.
    auto storeBlock = [&](size_t i, std::string& base, std::vector<std::string>& src) -> bool {
        if (i + 2 * w > code.size()) return false;
        src.clear();
        for (int j = 0; j < w; ++j) {
            std::string lm, lo, sm, so, sb; int soff;
            if (!parseInsn(code[i + 2*j], lm, lo) || lm != "lda") return false;
            if (!parseInsn(code[i + 2*j + 1], sm, so) || sm != "sta") return false;
            splitOff(so, sb, soff);
            if (soff != j) return false;
            if (j == 0) base = sb; else if (sb != base) return false;
            src.push_back(lo);
        }
        return true;
    };
    auto baseEq = [&](const std::string& op, const std::string& nm) {
        if (op.empty() || op[0] == '#') return false;
        std::string b; int o; splitOff(op, b, o); return b == nm;
    };
    // A jsr to a runtime routine (fp_*/rt_*) never touches compiler temps (Tn live
    // in zero page that only the generated code references), so it is transparent to
    // temp liveness; a jsr to a GOSUB target (L<num>) runs generated code and is not.
    auto jsrTransparent = [&](const std::string& o) {
        return o.rfind("fp_", 0) == 0 || o.rfind("rt_", 0) == 0;
    };
    // Tn dead from index `idx`: redefined (all w bytes stored) before any read, with
    // no intervening control flow. Returns false (live) on any flow line or read.
    auto deadAfter = [&](size_t idx, const std::string& M) -> bool {
        std::set<int> redef;
        for (size_t k = idx; k < code.size(); ++k) {
            const std::string& l = code[k];
            std::string m, o;
            if (!parseInsn(l, m, o)) return false;            // label / boundary
            if (m == "sta") { std::string b; int off; splitOff(o, b, off);
                if (b == M) { redef.insert(off); if ((int)redef.size() == w) return true; }
                continue; }                                   // store is a write
            if (m == "jsr") { if (jsrTransparent(o)) continue; return false; }
            if (isFlowLine(l)) return false;                  // branch/jmp/rts
            if (baseEq(o, M)) return false;                   // a read of M
        }
        return true;                                          // fell off end: dead
    };

    bool changed = true;
    while (changed) {
        changed = false;
        // SC: drop self-copy blocks (D <- D).
        for (size_t i = 0; i + 2*w <= code.size();) {
            std::string base; std::vector<std::string> src;
            if (storeBlock(i, base, src)) {
                bool self = true;
                for (int j = 0; j < w && self; ++j)
                    if (!baseEq(src[j], base) ||
                        src[j] != (j ? base + "+" + std::to_string(j) : base)) self = false;
                if (self) { code.erase(code.begin()+i, code.begin()+i+2*w); changed = true; continue; }
            }
            ++i;
        }
        // CP: store-block defining temp M, whose value's first subsequent use is a
        // copy block D<-M (M dead after), is retargeted to store straight into D.
        // The copy block need not be adjacent -- intervening straight-line code that
        // never touches M or D is skipped -- so operand temps (left into Td, right
        // into T(d+1), then both copied to FA/FB) collapse away too.
        for (size_t i = 0; i + 2*w <= code.size(); ++i) {
            std::string M; std::vector<std::string> src;
            if (!storeBlock(i, M, src) || !isTempName(M)) continue;
            size_t c = i + 2*w;
            // Find M's first reference at/after c. It must be the head of a copy block
            // D<-M; anything else (read in an op, write, flow) aborts the fusion.
            size_t f = code.size(); std::string D; std::vector<std::string> csrc;
            bool ok = false;
            for (size_t k = c; k < code.size(); ++k) {
                std::string m, o;
                if (!parseInsn(code[k], m, o)) break;                       // boundary
                if (m == "jsr") { if (jsrTransparent(o)) continue; break; } // GOSUB: stop
                if (isFlowLine(code[k])) break;
                if (m == "sta") { std::string b; int off; splitOff(o, b, off);
                    if (b == M) break; else continue; }                     // write
                if (baseEq(o, M)) {                                         // first read of M
                    if (storeBlock(k, D, csrc)) {
                        bool isCopy = true;
                        for (int j = 0; j < w && isCopy; ++j)
                            if (csrc[j] != (j ? M + "+" + std::to_string(j) : M)) isCopy = false;
                        if (isCopy) { f = k; ok = true; }
                    }
                    break;                                                  // resolved (copy or not)
                }
            }
            if (!ok) continue;
            // D must not be aliased by M's def sources, nor touched between c and f
            // (we are hoisting D's assignment up to i).
            bool bad = false;
            for (const std::string& a : src) if (baseEq(a, D)) { bad = true; break; }
            for (size_t k = c; k < f && !bad; ++k) {
                std::string m, o; if (!parseInsn(code[k], m, o)) { bad = true; break; }
                if (baseEq(o, D)) bad = true;
                else if (m == "sta") { std::string b; int off; splitOff(o, b, off); if (b == D) bad = true; }
            }
            if (bad) continue;
            if (!deadAfter(f + 2*w, M)) continue;
            for (int j = 0; j < w; ++j)                        // retarget stores to D
                code[i + 2*j + 1] = "\tsta " + D + (j ? "+" + std::to_string(j) : "");
            code.erase(code.begin()+f, code.begin()+f+2*w);   // drop the copy block
            changed = true; break;
        }
    }
}

// Td = Td * k, decomposed into shifts + adds over the set bits of |k| (no runtime
// loop, no general 16x16 multiply): the canonical constant-multiply optimization.
void Codegen::mulByConst(int d, long k)
{
    std::string Td = temp(d), Tsh = temp(d + 1), Tacc = temp(d + 2);
    bool neg = k < 0;
    unsigned uk = static_cast<unsigned>(neg ? -k : k) & 0xFFFF;
    if (uk == 0) { loadConst(Td, 0); return; }
    if (uk == 1) { if (neg) negate(Td); return; }
    copy16(Tsh, Td);            // running shifted multiplicand = Td<<bit
    loadConst(Tacc, 0);
    for (int bit = 0; bit < 16 && (uk >> bit); ++bit) {
        if (uk & (1u << bit)) binOp("+", Tacc, Tsh);   // Tacc += Td<<bit
        if (uk >> (bit + 1)) { emit("\tasl " + Tsh); emit("\trol " + Tsh + "+1"); }
    }
    copy16(Td, Tacc);
    if (neg) negate(Td);
}

// precedence-climbing: parse expr with operators of precedence >= minPrec into Td
bool Codegen::binary(int d, int minPrec)
{
    if (!unary(d)) return false;
    bool leftNum = lastNum; long leftVal = lastNumVal;
    for (;;) {
        std::string op;
        if (cur().t == T::Op) op = cur().s;
        else if (isKw("AND")) op = "AND";
        else if (isKw("OR")) op = "OR";
        else break;
        int pr = prec(op);
        if (pr < minPrec || pr < 0) break;
        adv();
        if (!fp) {
            // strength-reduce constant multiplies: var*K and K*var -> shifts+adds
            if (op == "*" && cur().t == T::Num && !cur().isFloat) {   // <expr> * K
                long k = cur().num; adv();
                mulByConst(d, k); leftNum = false; continue;
            }
            if (op == "*" && leftNum) {                  // K * <expr>
                if (!binary(d + 1, pr + 1)) return false;
                copy16(temp(d), temp(d + 1));
                mulByConst(d, leftVal); leftNum = false; continue;
            }
        }
        if (!binary(d + 1, pr + 1)) return false;    // right operand into T[d+1]
        if (fp) fpBinOp(op, temp(d), temp(d + 1));
        else    binOp(op, temp(d), temp(d + 1));
        leftNum = false;
    }
    return true;
}

// ---- statements ------------------------------------------------------------
bool Codegen::statement()
{
    if (cur().t == T::Eol || cur().t == T::Colon) return true;   // empty

    if (isKw("REM")) { p = tk->size(); return true; }
    if (isKw("END") || isKw("STOP")) { adv(); emit("\tjmp basic_done"); return true; }
    if (isKw("RETURN")) { adv(); emit("\trts"); return true; }

    if (isKw("HGR") || isKw("HGR2")) {
        bool pg2 = isKw("HGR2"); adv();
        emit(pg2 ? "\tlda #1" : "\tlda #0"); emit("\tjsr rt_hgr"); return true;
    }
    if (isKw("HCOLOR")) {
        adv(); if (!isOp("=")) return fail("HCOLOR expects '='"); adv();
        if (!expr(0)) return false;
        if (fp) { copyV("FA", temp(0)); emit("\tjsr fp_toint16"); emit("\tlda FA"); }
        else    emit("\tlda T0");
        emit("\tjsr rt_hcolor"); return true;
    }
    if (isKw("HPLOT")) {
        // coordinates: int values go straight to the 16-bit rt_ slots; float
        // values are converted with fp_toint16 first.
        auto coord = [&](const std::string& dst) -> bool {
            if (!expr(0)) return false;
            if (fp) fpToIntInto(dst, temp(0)); else copy16(dst, temp(0));
            return true;
        };
        adv();
        if (!coord("rt_x0")) return false;
        if (cur().t != T::Comma) return fail("HPLOT expects 'x,y'"); adv();
        if (!coord("rt_y0")) return false;
        if (!isKw("TO")) {                       // single point
            copy16("rt_px", "rt_x0"); copy16("rt_py", "rt_y0"); emit("\tjsr rt_plot"); return true;
        }
        while (isKw("TO")) {                      // line chain
            adv();
            if (!coord("rt_x1")) return false;
            if (cur().t != T::Comma) return fail("HPLOT TO expects 'x,y'"); adv();
            if (!coord("rt_y1")) return false;
            emit("\tjsr rt_line");
            copy16("rt_x0", "rt_x1"); copy16("rt_y0", "rt_y1");
        }
        return true;
    }
    if (isKw("GOTO")) { adv(); if (cur().t != T::Num) return fail("GOTO expects a line number");
        if (!checkTarget(static_cast<int>(cur().num), "GOTO")) return false;
        emit("\tjmp L" + std::to_string(cur().num)); adv(); return true; }
    if (isKw("GOSUB")) { adv(); if (cur().t != T::Num) return fail("GOSUB expects a line number");
        if (!checkTarget(static_cast<int>(cur().num), "GOSUB")) return false;
        emit("\tjsr L" + std::to_string(cur().num)); adv(); return true; }

    if (isKw("IF")) {
        adv();
        if (!expr(0)) return false;
        int ifId = labelCounter++;
        std::string skip = "Lif" + std::to_string(ifId);
        std::string thenL = "Lthen" + std::to_string(ifId);
        std::string falseL = "Liff" + std::to_string(ifId);
        // long-branch safe: false -> falseL -> jmp over THEN body; true -> thenL
        emitIfFalse("T0", falseL);
        emit("\tjmp " + thenL);
        emit(falseL + ":"); emit("\tjmp " + skip);
        emit(thenL + ":");
        if (isKw("THEN")) {
            adv();
            if (cur().t == T::Num) { if (!checkTarget(static_cast<int>(cur().num), "THEN")) return false;
                                     emit("\tjmp L" + std::to_string(cur().num)); adv(); }
            else { if (!statement()) return false;
                   while (cur().t == T::Colon) { adv(); if (!statement()) return false; } }
        } else if (isKw("GOTO")) {
            adv(); if (cur().t != T::Num) return fail("IF .. GOTO expects a line number");
            if (!checkTarget(static_cast<int>(cur().num), "GOTO")) return false;
            emit("\tjmp L" + std::to_string(cur().num)); adv();
        } else return fail("IF expects THEN or GOTO");
        emit(skip + ":");
        return true;
    }

    if (isKw("FOR")) {
        adv();
        if (cur().t != T::Ident) return fail("FOR expects a variable");
        std::string v = cur().s; adv();
        if (!isOp("=")) return fail("FOR expects '='"); adv();
        if (!expr(0)) return false; copyV(varLabel(v), temp(0));      // v = start
        if (!isKw("TO")) return fail("FOR expects TO"); adv();
        int id = forCounter++;
        std::string lim = "F" + std::to_string(id) + "_lim";
        std::string stp = "F" + std::to_string(id) + "_step";
        forSlots.insert(id);
        if (!expr(0)) return false; copyV(lim, temp(0));              // limit
        if (isKw("STEP")) { adv(); if (!expr(0)) return false; copyV(stp, temp(0)); }
        else if (fp) fpLoadConst(stp, 1.0);
        else loadConst(stp, 1);
        emit("Lfor" + std::to_string(id) + ":");
        forStack.push_back({v, id});
        return true;
    }

    if (isKw("NEXT")) {
        adv();
        if (forStack.empty()) return fail("NEXT without FOR");
        ForCtx ctx = forStack.back();
        if (cur().t == T::Ident) {
            if (cur().s != ctx.var) return fail("NEXT " + cur().s + " does not match FOR " + ctx.var);
            adv();
        }
        forStack.pop_back();
        std::string v = varLabel(ctx.var);
        std::string lim = "F" + std::to_string(ctx.id) + "_lim";
        std::string stp = "F" + std::to_string(ctx.id) + "_step";
        std::string top = "Lfor" + std::to_string(ctx.id);
        std::string neg = "Lneg" + std::to_string(labelCounter);
        std::string done = "Lnxt" + std::to_string(labelCounter++);
        if (fp) {
            // v += step (float); compare v,lim (float); step sign from step byte 3 bit7
            copyV("FA", v); copyV("FB", stp); emit("\tjsr fp_add"); copyV(v, "FA");
            copyV("FA", v); copyV("FB", lim); emit("\tjsr fp_cmp");         // A=0/1/2
            emit("\tldy " + stp + "+3"); emit("\tbmi " + neg);             // step<0?
        } else {
            // v += step (int)
            emit("\tclc"); emit("\tlda " + v); emit("\tadc " + stp); emit("\tsta " + v);
            emit("\tlda " + v + "+1"); emit("\tadc " + stp + "+1"); emit("\tsta " + v + "+1");
            copy16("rt_a", v); copy16("rt_b", lim); emit("\tjsr rt_cmp16"); // A=0/1/2
            emit("\tldy " + stp + "+1"); emit("\tbmi " + neg);             // step<0?
        }
        emit("\tcmp #2"); emit("\tbeq " + done); emit("\tjmp " + top);     // step>=0: loop if v<=lim
        emit(neg + ":"); emit("\tcmp #0"); emit("\tbeq " + done); emit("\tjmp " + top); // step<0: loop if v>=lim
        emit(done + ":");
        return true;
    }

    if (isKw("PRINT")) {
        adv();
        bool trailingSep = false;
        while (cur().t != T::Eol && cur().t != T::Colon) {
            if (cur().t == T::Str) {
                for (char ch : cur().s) {                 // emit each char via rt_putc
                    emit("\tlda #$" + hex2(static_cast<unsigned char>(ch)));
                    emit("\tjsr rt_putc");
                }
                adv(); trailingSep = false;
            } else if (cur().t == T::Semi) {
                adv(); trailingSep = true;                // ';' = no separator
            } else if (cur().t == T::Comma) {
                emit("\tlda #$20"); emit("\tjsr rt_putc"); // ',' = a space (simplified TAB)
                adv(); trailingSep = true;
            } else {                                      // a numeric expression
                if (!expr(0)) return false;
                if (fp) { copyV("FA", temp(0)); emit("\tjsr fp_toint16"); copy16("rt_a", "FA"); }
                else    copy16("rt_a", temp(0));
                emit("\tjsr rt_print");                   // (float: integer part)
                trailingSep = false;
            }
        }
        if (!trailingSep) emit("\tjsr rt_printcr");        // newline unless trailing ';'/','
        return true;
    }

    // assignment: [LET] var = expr
    if (isKw("LET")) adv();
    if (cur().t == T::Ident) {
        std::string v = cur().s;
        // An Applesoft reserved word the native compiler doesn't implement (lo-res
        // graphics, error trapping, I/O, …) lexes as a variable here; flag it clearly
        // rather than the cryptic "expected '='". These all run via the Applesoft
        // TOKENISER (Inject / "compile (tokeniser)" mode), which drives the full ROM
        // command set. The native compiler targets the HGR/integer/float fast path.
        static const std::set<std::string> kUnsupported = {
            "GR","COLOR","PLOT","HLIN","VLIN","TEXT","HOME","ONERR","RESUME",
            "INPUT","GET","READ","DATA","DIM","DEF","ON","POKE","CALL","WAIT","POP",
            "VTAB","HTAB","INVERSE","NORMAL","FLASH","DRAW","XDRAW","SCALE","ROT",
            "STORE","RECALL","TRACE","NOTRACE","SPEED","DEL","CLEAR","HIMEM","LOMEM"
        };
        if (kUnsupported.count(v))
            return fail("'" + v + "' is not supported by the native compiler — use the "
                        "Applesoft tokeniser (Inject / tokenise mode) for lo-res graphics, "
                        "ONERR, INPUT and the rest of the ROM command set");
        adv();
        if (!isOp("=")) return fail("expected '=' in assignment");
        adv();
        if (!expr(0)) return false;
        copyV(varLabel(v), temp(0));
        return true;
    }

    return fail("unsupported statement '" + cur().s + "'");
}

bool Codegen::line()
{
    emit("L" + std::to_string(curLineNo) + ":");
    if (!statement()) return false;
    while (cur().t == T::Colon) { adv(); if (!statement()) return false; }
    if (cur().t != T::Eol) return fail("unexpected token after statement");
    return true;
}

bool Codegen::program(const std::vector<Line>& lines)
{
    for (const Line& L : lines) {
        tk = &L.toks; p = 0; curLineNo = L.number;
        if (!line()) return false;
    }
    if (!forStack.empty()) return fail("missing NEXT for FOR " + forStack.back().var);
    emit("\tjmp basic_done");
    return true;
}

} // namespace

Result compile(const std::string& source, Card card, FpMode mode)
{
    Result r;

    // split into numbered lines (the FIRST token of each line is its Applesoft
    // line number, used verbatim in every diagnostic so an error points at the
    // exact line the author must fix).
    std::vector<Line> lines;
    Lexer lex(source);
    int physical = 0;
    while (lex.i < source.size()) {
        std::vector<Tok> toks;
        ++physical;
        size_t before = lex.i;
        if (!lex.lexLine(toks)) { r.error = "line " + std::to_string(physical) + ": " + lex.err; return r; }
        if (toks.empty()) continue;
        if (toks[0].t != T::Num)
            return (r.error = "line " + std::to_string(physical) +
                    ": every program line must start with a line number"), r;
        if (toks[0].isFloat)
            return (r.error = "line " + std::to_string(physical) +
                    ": line number must be a whole number"), r;
        Line L; L.number = static_cast<int>(toks[0].num);
        L.toks.assign(toks.begin() + 1, toks.end());
        lines.push_back(std::move(L));
        if (lex.i == before) break;   // safety
    }
    if (lines.empty()) { r.error = "no BASIC lines to compile (empty program)"; return r; }

    // ascending by line number (GOTO/GOSUB targets resolve to labels regardless,
    // but keep source order stable for equal numbers)
    std::stable_sort(lines.begin(), lines.end(),
                     [](const Line& a, const Line& b) { return a.number < b.number; });

    // Auto precision: integer unless a line uses a fraction (decimal literal) or a
    // '/' (Applesoft division is real) -> then binary32 float. This keeps a program
    // that needs no floats from ever linking the float runtime.
    bool floatMode;
    if (mode == FpMode::Float) floatMode = true;
    else if (mode == FpMode::Int) floatMode = false;
    else {
        floatMode = false;
        for (const Line& L : lines)
            for (const Tok& t : L.toks)
                if ((t.t == T::Num && t.isFloat) || (t.t == T::Op && t.s == "/") ||
                    (t.t == T::Kw && (t.s == "SQR" || t.s == "SIN"))) floatMode = true;
    }

    Codegen g; g.card = card; g.fp = floatMode;
    for (const Line& L : lines) g.lineNumbers.insert(L.number);   // for GOTO/GOSUB checks
    if (!g.program(lines)) { r.error = g.err; return r; }
    r.usesFloat = floatMode;

    const int W = floatMode ? 4 : 2;   // value width
    const std::string body = g.body();

    // Import ONLY the runtime symbols the generated code actually references, so the
    // build can assemble a minimal runtime (no unused routines / tables linked).
    auto uses = [&](const std::string& sym) {
        // match whole-symbol token (followed by non-identifier char)
        size_t p = 0;
        while ((p = body.find(sym, p)) != std::string::npos) {
            char after = (p + sym.size() < body.size()) ? body[p + sym.size()] : '\n';
            if (!(std::isalnum(static_cast<unsigned char>(after)) || after == '_')) return true;
            p += sym.size();
        }
        return false;
    };
    const char* codeSyms[] = {"rt_hgr","rt_hcolor","rt_plot","rt_line","rt_mul","rt_div",
                              "rt_cmp16","rt_print","rt_printcr","rt_putc",
                              "fp_fromint16","fp_toint16","fp_add","fp_sub","fp_mul","fp_div","fp_cmp",
                              "fp_int","fp_sqrt","fp_sin"};
    const char* zpSyms[]   = {"rt_px","rt_py","rt_x0","rt_y0","rt_x1","rt_y1","rt_a","rt_b","FA","FB"};
    std::vector<std::string> codeImp, zpImp;
    for (const char* s : codeSyms) if (uses(s)) { codeImp.push_back(s); r.runtimeFeatures.push_back(s); }
    for (const char* s : zpSyms)   if (uses(s)) zpImp.push_back(s);

    std::ostringstream o;
    o << "; ---- generated by BasicCompilerApplesoft (" << (floatMode ? "float" : "integer")
      << " phase, minimal runtime) ----\n";
    o << ".setcpu \"6502\"\n";
    auto emitImport = [&](const char* kind, const std::vector<std::string>& v) {
        if (v.empty()) return;
        o << kind << ' ';
        for (size_t i = 0; i < v.size(); ++i) o << (i ? ", " : "") << v[i];
        o << "\n";
    };
    emitImport(".import", codeImp);
    emitImport(".importzp", zpImp);
    o << ".export basic_main\n\n";

    // Declare zero-page storage FIRST so ca65 knows these labels are zp when it
    // sees them used below -> short/fast zp addressing. Variables + temporaries +
    // FOR limit/step slots (2 bytes int / 4 bytes float).
    o << ".segment \"ZEROPAGE\"\n";
    for (const std::string& v : g.vars)
        if (!v.empty() && v[0] != '\0') o << "V_" << v << ": .res " << W << "\n";
    for (int t = 0; t < g.maxTemp; ++t) o << "T" << t << ": .res " << W << "\n";
    for (int id : g.forSlots) {
        o << "F" << id << "_lim: .res " << W << "\n";
        o << "F" << id << "_step: .res " << W << "\n";
    }
    o << "\n.segment \"CODE\"\n";
    o << "basic_main:\n";
    // Variables default to 0 / 0.0 (both all-zero bytes). Temporaries + FOR slots
    // are always written before read, so they need no init.
    o << "\tlda #0\n";
    for (const std::string& v : g.vars)
        if (!v.empty() && v[0] != '\0')
            for (int i = 0; i < W; ++i)
                o << "\tsta V_" << v << (i ? "+" + std::to_string(i) : "") << "\n";
    o << body;
    o << "basic_done:\n\tjmp basic_done\n\n";

    // comparison truth tables (index by rt_cmp16 result 0/1/2)
    if (!g.cmpTables.empty()) {
        o << ".segment \"RODATA\"\n";
        auto tbl = [&](const std::string& op, int lt, int eq, int gt) {
            if (g.cmpTables.count(op))
                o << g.cmpTableName(op) << ": .byte " << lt << "," << eq << "," << gt << "\n";
        };
        tbl("<", 1,0,0); tbl("=",0,1,0); tbl(">",0,0,1);
        tbl("<=",1,1,0); tbl(">=",0,1,1); tbl("<>",1,0,1);
        o << "\n";
    }

    r.asmText = o.str();
    r.ok = true;
    r.lineCount = static_cast<int>(lines.size());
    r.varCount = 0;
    for (const std::string& v : g.vars) if (!v.empty() && v[0] != '\0') ++r.varCount;
    return r;
}

} // namespace basicnative
