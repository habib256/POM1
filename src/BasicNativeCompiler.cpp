// BasicNativeCompiler.cpp -- see BasicNativeCompiler.h.
//
// Phase 1: a real native-code generator for the INTEGER subset of Applesoft.
// Recursive-descent / precedence-climbing parser -> straight-line ca65 assembly.
// 16-bit signed values; variables and expression temporaries live at fixed BSS
// addresses (no name lookup at run time); control flow is native (GOTO=JMP,
// GOSUB/RETURN=JSR/RTS, FOR/NEXT and IF are native branches). Graphics statements
// call the fixed runtime ABI (rt_hgr/rt_hcolor/rt_plot/rt_line) wired per card.

#include "BasicNativeCompiler.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace basicnative {
namespace {

// ---- lexer -----------------------------------------------------------------

enum class T { Eol, Num, Ident, Kw, Op, Comma, LParen, RParen, Semi, Colon };

struct Tok {
    T           t = T::Eol;
    std::string s;        // ident/keyword/op text (upper-cased)
    long        num = 0;
};

bool isKeyword(const std::string& s)
{
    static const std::set<std::string> kw = {
        "REM","HGR","HGR2","HCOLOR","HPLOT","TO","FOR","NEXT","STEP","IF","THEN",
        "GOTO","GOSUB","RETURN","END","STOP","LET","PRINT","AND","OR","NOT","ABS",
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

            if (std::isdigit(static_cast<unsigned char>(c))) {
                long v = 0;
                while (i < src.size() && std::isdigit(static_cast<unsigned char>(src[i])))
                    v = v * 10 + (src[i++] - '0');
                if (i < src.size() && src[i] == '.') { err = "floating-point literals need the FP phase (integer compiler)"; return false; }
                toks.push_back({T::Num, "", v});
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
    std::ostringstream out;
    std::string err;

    std::set<std::string>   vars;       // variable labels needed
    std::set<std::string>   cmpTables;  // comparison truth tables needed (LT,EQ,..)
    int                     maxTemp = 0;
    int                     forCounter = 0;
    int                     labelCounter = 0;

    // FOR context
    struct ForCtx { std::string var; int id; };
    std::vector<ForCtx> forStack;
    std::set<int>       forSlots;   // FOR ids needing limit/step BSS slots

    // current line's tokens + cursor
    const std::vector<Tok>* tk = nullptr;
    size_t p = 0;
    int curLineNo = 0;

    void emit(const std::string& s) { out << s << "\n"; }
    bool fail(const std::string& m) { err = "line " + std::to_string(curLineNo) + ": " + m; return false; }

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
    if (cur().t == T::Num) { loadConst(Td, cur().num); lastNum = true; lastNumVal = cur().num; adv(); return true; }
    if (cur().t == T::Ident) { copy16(Td, varLabel(cur().s)); adv(); return true; }
    if (cur().t == T::LParen) {
        adv();
        if (!expr(d)) return false;
        if (cur().t != T::RParen) return fail("expected ')'");
        adv();
        return true;
    }
    if (isOp("-")) { adv(); if (!primary(d)) return false;
        // negate Td: Td = 0 - Td
        emit("\tsec"); emit("\tlda #0"); emit("\tsbc " + Td); emit("\tsta " + Td);
        emit("\tlda #0"); emit("\tsbc " + Td + "+1"); emit("\tsta " + Td + "+1"); return true; }
    if (isKw("NOT")) { adv(); if (!primary(d)) return false;
        emit("\tlda " + Td); emit("\teor #$FF"); emit("\tsta " + Td);
        emit("\tlda " + Td + "+1"); emit("\teor #$FF"); emit("\tsta " + Td + "+1"); return true; }
    if (isKw("ABS")) { adv(); if (cur().t != T::LParen) return fail("ABS expects '('"); adv();
        if (!expr(d)) return false; if (cur().t != T::RParen) return fail("expected ')'"); adv();
        std::string done = "Labs" + std::to_string(labelCounter++);
        emit("\tlda " + Td + "+1"); emit("\tbpl " + done);
        emit("\tsec"); emit("\tlda #0"); emit("\tsbc " + Td); emit("\tsta " + Td);
        emit("\tlda #0"); emit("\tsbc " + Td + "+1"); emit("\tsta " + Td + "+1");
        emit(done + ":"); return true; }
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
        // strength-reduce constant multiplies: var*K and K*var -> shifts+adds
        if (op == "*" && cur().t == T::Num) {        // <expr> * K
            long k = cur().num; adv();
            mulByConst(d, k); leftNum = false; continue;
        }
        if (op == "*" && leftNum) {                  // K * <expr>
            if (!binary(d + 1, pr + 1)) return false;
            copy16(temp(d), temp(d + 1));
            mulByConst(d, leftVal); leftNum = false; continue;
        }
        if (!binary(d + 1, pr + 1)) return false;    // right operand into T[d+1]
        binOp(op, temp(d), temp(d + 1));
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
        if (!expr(0)) return false; emit("\tlda T0"); emit("\tjsr rt_hcolor"); return true;
    }
    if (isKw("HPLOT")) {
        adv();
        if (!expr(0)) return false; copy16("rt_x0", temp(0));
        if (cur().t != T::Comma) return fail("HPLOT expects 'x,y'"); adv();
        if (!expr(0)) return false; copy16("rt_y0", temp(0));
        if (!isKw("TO")) {                       // single point
            copy16("rt_px", "rt_x0"); copy16("rt_py", "rt_y0"); emit("\tjsr rt_plot"); return true;
        }
        while (isKw("TO")) {                      // line chain
            adv();
            if (!expr(0)) return false; copy16("rt_x1", temp(0));
            if (cur().t != T::Comma) return fail("HPLOT TO expects 'x,y'"); adv();
            if (!expr(0)) return false; copy16("rt_y1", temp(0));
            emit("\tjsr rt_line");
            copy16("rt_x0", "rt_x1"); copy16("rt_y0", "rt_y1");
        }
        return true;
    }
    if (isKw("GOTO")) { adv(); if (cur().t != T::Num) return fail("GOTO expects a line number");
        emit("\tjmp L" + std::to_string(cur().num)); adv(); return true; }
    if (isKw("GOSUB")) { adv(); if (cur().t != T::Num) return fail("GOSUB expects a line number");
        emit("\tjsr L" + std::to_string(cur().num)); adv(); return true; }

    if (isKw("IF")) {
        adv();
        if (!expr(0)) return false;
        int ifId = labelCounter++;
        std::string skip = "Lif" + std::to_string(ifId);
        std::string thenL = "Lthen" + std::to_string(ifId);
        // long-branch safe: true -> THEN body, false -> jmp over it
        emit("\tlda T0"); emit("\tora T0+1"); emit("\tbne " + thenL); emit("\tjmp " + skip);
        emit(thenL + ":");
        if (isKw("THEN")) {
            adv();
            if (cur().t == T::Num) { emit("\tjmp L" + std::to_string(cur().num)); adv(); }
            else { if (!statement()) return false;
                   while (cur().t == T::Colon) { adv(); if (!statement()) return false; } }
        } else if (isKw("GOTO")) {
            adv(); if (cur().t != T::Num) return fail("IF .. GOTO expects a line number");
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
        if (!expr(0)) return false; copy16(varLabel(v), temp(0));     // v = start
        if (!isKw("TO")) return fail("FOR expects TO"); adv();
        int id = forCounter++;
        std::string lim = "F" + std::to_string(id) + "_lim";
        std::string stp = "F" + std::to_string(id) + "_step";
        forSlots.insert(id);
        if (!expr(0)) return false; copy16(lim, temp(0));             // limit
        if (isKw("STEP")) { adv(); if (!expr(0)) return false; copy16(stp, temp(0)); }
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
        // v += step
        emit("\tclc"); emit("\tlda " + v); emit("\tadc " + stp); emit("\tsta " + v);
        emit("\tlda " + v + "+1"); emit("\tadc " + stp + "+1"); emit("\tsta " + v + "+1");
        // compare v vs limit (signed)
        copy16("rt_a", v); copy16("rt_b", lim); emit("\tjsr rt_cmp16");    // A=0/1/2
        emit("\tldy " + stp + "+1"); emit("\tbmi " + neg);                 // step<0?
        emit("\tcmp #2"); emit("\tbeq " + done); emit("\tjmp " + top);     // step>=0: loop if v<=lim
        emit(neg + ":"); emit("\tcmp #0"); emit("\tbeq " + done); emit("\tjmp " + top); // step<0: loop if v>=lim
        emit(done + ":");
        return true;
    }

    // assignment: [LET] var = expr
    if (isKw("LET")) adv();
    if (cur().t == T::Ident) {
        std::string v = cur().s; adv();
        if (!isOp("=")) return fail("expected '=' in assignment");
        adv();
        if (!expr(0)) return false;
        copy16(varLabel(v), temp(0));
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

Result compile(const std::string& source, Card card)
{
    Result r;

    // split into numbered lines
    std::vector<Line> lines;
    Lexer lex(source);
    int physical = 0;
    while (lex.i < source.size()) {
        std::vector<Tok> toks;
        ++physical;
        size_t before = lex.i;
        if (!lex.lexLine(toks)) { r.error = "line " + std::to_string(physical) + ": " + lex.err; return r; }
        if (toks.empty()) continue;
        if (toks[0].t != T::Num) { r.error = "line " + std::to_string(physical) + ": program lines must start with a line number"; return r; }
        Line L; L.number = static_cast<int>(toks[0].num);
        L.toks.assign(toks.begin() + 1, toks.end());
        lines.push_back(std::move(L));
        if (lex.i == before) break;   // safety
    }
    if (lines.empty()) { r.error = "no BASIC lines to compile"; return r; }

    // ascending by line number (GOTO/GOSUB targets resolve to labels regardless,
    // but keep source order stable for equal numbers)
    std::stable_sort(lines.begin(), lines.end(),
                     [](const Line& a, const Line& b) { return a.number < b.number; });

    Codegen g; g.card = card;
    if (!g.program(lines)) { r.error = g.err; return r; }

    std::ostringstream o;
    o << "; ---- generated by BasicNativeCompiler (integer phase) ----\n";
    o << ".setcpu \"6502\"\n";
    o << ".import rt_hgr, rt_hcolor, rt_plot, rt_line, rt_mul, rt_div, rt_cmp16\n";
    o << ".importzp rt_px, rt_py, rt_x0, rt_y0, rt_x1, rt_y1, rt_a, rt_b\n";
    o << ".import __BSS_START__\n";
    o << ".export basic_main\n\n";
    o << ".segment \"CODE\"\n";
    o << "basic_main:\n";
    o << "\t; zero BASIC variables (default 0); BSS is one page\n";
    o << "\tlda #0\n\tldx #0\n";
    o << "@bss_zero:\n\tsta __BSS_START__,x\n\tinx\n\tbne @bss_zero\n";
    o << g.out.str();
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

    // BSS: variables, expression temporaries, FOR limit/step slots
    o << ".segment \"BSS\"\n";
    for (const std::string& v : g.vars)
        if (!v.empty() && v[0] != '\0') o << "V_" << v << ": .res 2\n";
    for (int t = 0; t < g.maxTemp; ++t) o << "T" << t << ": .res 2\n";
    for (int id : g.forSlots) {
        o << "F" << id << "_lim: .res 2\n";
        o << "F" << id << "_step: .res 2\n";
    }

    r.asmText = o.str();
    r.ok = true;
    r.lineCount = static_cast<int>(lines.size());
    r.varCount = 0;
    for (const std::string& v : g.vars) if (!v.empty() && v[0] != '\0') ++r.varCount;
    return r;
}

} // namespace basicnative
