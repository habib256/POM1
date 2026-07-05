// basic_native_codegen_test.cpp -- pure unit pin for the NATIVE BASIC compiler
// (src/BasicCompilerApplesoft.cpp): asserts properties of the generated 6502 asm
// without assembling it (no cc65 needed), so it always runs and is fast. The
// sibling basic_native_run test (cc65-gated) proves the asm actually executes and
// is faster than the interpreter.

#include "BasicCompilerApplesoft.h"

#include <cstdio>
#include <string>

namespace {
int failures = 0;

std::string gen(const std::string& src)
{
    auto r = basicnative::compile(src, basicnative::Card::Gen2);
    if (!r.ok) { std::printf("FAIL compile: %s\n", r.error.c_str()); ++failures; return {}; }
    return r.asmText;
}
std::string genf(const std::string& src)   // float phase
{
    auto r = basicnative::compile(src, basicnative::Card::Gen2, basicnative::FpMode::Float);
    if (!r.ok) { std::printf("FAIL float compile: %s\n", r.error.c_str()); ++failures; return {}; }
    return r.asmText;
}
bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
void check(const char* what, bool ok) { std::printf("%s: %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++failures; }
}

int main()
{
    // Constant multiply is strength-reduced to shifts+adds, NOT a runtime multiply.
    {
        std::string a = gen("10 Z=X*3\n20 END\n");
        check("X*3 uses shifts+adds (asl)", has(a, "asl T"));
        check("X*3 avoids the general multiply", !has(a, "jsr rt_mul"));
    }
    // A variable*variable multiply DOES use the runtime multiply.
    {
        std::string a = gen("10 Z=X*Y\n20 END\n");
        check("X*Y calls rt_mul", has(a, "jsr rt_mul"));
    }
    // FOR/NEXT compiles to a native counter loop with a signed compare.
    {
        std::string a = gen("10 FOR I=0 TO 9\n20 NEXT I\n30 END\n");
        check("FOR/NEXT uses rt_cmp16", has(a, "jsr rt_cmp16"));
        check("FOR loop has a back-branch label", has(a, "Lfor0:"));
    }
    // GOTO/GOSUB are native jumps; line labels exist.
    {
        std::string a = gen("10 GOSUB 30\n20 GOTO 40\n30 RETURN\n40 END\n");
        check("GOSUB -> jsr Lnn", has(a, "jsr L30"));
        check("GOTO -> jmp Lnn", has(a, "jmp L40"));
        check("RETURN -> rts", has(a, "\trts"));
        check("line 30 labelled", has(a, "L30:"));
    }
    // Graphics statements call the runtime ABI; HPLOT..TO draws a line.
    {
        std::string a = gen("10 HGR : HCOLOR=3\n20 HPLOT 0,0 TO 9,9\n30 HPLOT 5,5\n40 END\n");
        check("HGR -> rt_hgr", has(a, "jsr rt_hgr"));
        check("HCOLOR -> rt_hcolor", has(a, "jsr rt_hcolor"));
        check("HPLOT..TO -> rt_line", has(a, "jsr rt_line"));
        check("HPLOT point -> rt_plot", has(a, "jsr rt_plot"));
    }
    // POKE compiles to an indirect store through the rt_a pointer (added so ported
    // Apple II programs that hit soft switches — e.g. POKE 49746,0 = GEN2 $C252
    // MIX_OFF — assemble natively). The address is parked in rt_x0 across the
    // value's evaluation, then loaded into the rt_a zero-page pointer.
    {
        std::string a = gen("10 POKE 768,65\n20 END\n");
        check("POKE -> indirect store sta (rt_a),y", has(a, "sta (rt_a),y"));
        check("POKE parks the address in rt_x0", has(a, "sta rt_x0"));
        check("POKE loads the pointer rt_a", has(a, "sta rt_a"));
    }
    // POKE also works in the float phase (address/value converted via fp_toint16),
    // which is the phase BoySurface.apf's `HGR : POKE 49746,0` compiles in.
    {
        std::string a = genf("10 POKE 49746,0\n20 END\n");
        check("POKE (float phase) -> indirect store", has(a, "sta (rt_a),y"));
        check("POKE (float phase) converts via fp_toint16", has(a, "jsr fp_toint16"));
    }
    // A duplicate line number emits its L<n>: label only ONCE — ca65 rejects a
    // doubled label ("Symbol 'Lnn' is already defined"). Lines are stable-sorted so
    // the duplicate's code still runs via fall-through, and GOTO/GOSUB resolve to
    // the first occurrence (matching the interpreter's FNDLIN).
    {
        std::string a = gen("10 GR\n20 PLOT 1,1\n20 PLOT 2,2\n30 END\n");
        size_t n = 0, p = 0;
        while ((p = a.find("L20:", p)) != std::string::npos) { ++n; p += 4; }
        check("duplicate line 20 -> exactly one L20: label", n == 1);
        // both duplicate-numbered lines still emit code (two lo-res plots).
        size_t plots = 0; p = 0;
        while ((p = a.find("jsr rt_loresplot", p)) != std::string::npos) { ++plots; p += 1; }
        check("duplicate line's code still runs (two PLOTs)", plots == 2);
    }
    // PRINT emits string chars + signed-decimal numbers + a trailing newline.
    {
        std::string a = gen("10 PRINT \"X=\";N\n20 END\n");
        check("PRINT string -> rt_putc", has(a, "jsr rt_putc"));
        check("PRINT number -> rt_print", has(a, "jsr rt_print"));
        check("PRINT newline -> rt_printcr", has(a, "jsr rt_printcr"));
    }
    {   // trailing ';' suppresses the newline
        std::string a = gen("10 PRINT \"A\";\n20 END\n");
        check("PRINT trailing ; suppresses newline", !has(a, "jsr rt_printcr"));
    }

    // Structural: entry, halt, zero-page variables.
    {
        std::string a = gen("10 X=5\n20 END\n");
        check("entry label basic_main", has(a, "basic_main:"));
        check("halt label basic_done", has(a, "basic_done:"));
        check("variables in zero page", has(a, ".segment \"ZEROPAGE\"") && has(a, "V_X: .res 2"));
    }
    // Floating-point literals are rejected in the INTEGER phase.
    {
        auto r = basicnative::compile("10 X=1.5\n", basicnative::Card::Gen2, basicnative::FpMode::Int);
        check("float literal rejected (int phase)", !r.ok && r.error.find("line 1") != std::string::npos);
    }

    // FLOAT phase: real binary32 ops, float literals OK, HPLOT coords converted.
    {
        std::string a = genf("10 X=1.5\n20 Y=X*X/3\n30 END\n");
        check("float phase header", has(a, "float phase"));
        check("float imports the FP runtime", has(a, "fp_mul") && has(a, "fp_div") && has(a, "FA, FB"));
        check("float var is 4 bytes", has(a, "V_X: .res 4"));
        std::string b = genf("10 HGR\n20 HPLOT X,Y\n30 END\n");
        check("float HPLOT converts coords (fp_toint16)", has(b, "jsr fp_toint16"));
    }

    // Auto precision: integer unless a fraction is needed. A '/' or a decimal
    // literal -> float; otherwise integer (so the float runtime is never linked).
    {
        auto i = basicnative::compile("10 X=5+2\n20 END\n", basicnative::Card::Gen2);   // Auto
        check("auto: all-integer stays integer", i.ok && !i.usesFloat);
        auto f1 = basicnative::compile("10 X=5/2\n20 END\n", basicnative::Card::Gen2);  // '/' -> float
        check("auto: '/' picks float", f1.ok && f1.usesFloat);
        auto f2 = basicnative::compile("10 X=1.5\n20 END\n", basicnative::Card::Gen2);  // decimal -> float
        check("auto: decimal picks float", f2.ok && f2.usesFloat);
    }
    // Minimal runtime: import ONLY what's used. A pure-compute program pulls no
    // graphics/print/float; an int program never imports fp_*.
    {
        std::string a = gen("10 FOR I=0 TO 9\n20 NEXT I\n30 END\n");
        check("no HPLOT -> no rt_plot import", !has(a, ".import") || !has(a, "rt_plot"));
        check("int program imports no fp_", !has(a, "fp_add") && !has(a, "fp_mul"));
        std::string g = gen("10 HGR\n20 HPLOT 1,1\n30 END\n");
        check("no PRINT -> no rt_print import", !has(g, "rt_printcr"));
    }
    // Clear, line-precise errors naming the exact Applesoft line.
    {
        auto e = basicnative::compile("10 PRINT\n20 FOR\n30 END\n", basicnative::Card::Gen2);
        check("error names the bad Applesoft line", !e.ok && has(e.error, "line 20"));
        auto g2 = basicnative::compile("10 GOTO 99\n20 END\n", basicnative::Card::Gen2);
        check("GOTO to missing line is caught", !g2.ok && has(g2.error, "99") && has(g2.error, "no such line"));
    }

    // ---- Applesoft-fidelity regressions (bug hunt round 2) -----------------
    // NOT binds LOOSER than the relational operators: "IF NOT A = B" means
    // "NOT (A = B)", so the comparison (rt_cmp16) must be emitted BEFORE NOT's
    // bitwise complement (the pre-fix code bound NOT tightest -> complement first).
    {
        std::string a = gen("10 IF NOT A = B THEN 100\n100 END\n");
        size_t cmp = a.find("jsr rt_cmp16");
        size_t eor = a.find("eor #$FF");
        check("NOT precedence: compare before complement",
              cmp != std::string::npos && eor != std::string::npos && cmp < eor);
    }
    // Applesoft AND/OR are BITWISE on int16, not logical truth. In the float
    // phase: coerce each operand to int16 (fp_toint16), combine bitwise (and/ora
    // FA), then back to float (fp_fromint16) -- never a 1.0/0.0 materialisation.
    {
        std::string a = genf("10 X=1/2\n20 Z=6 AND 3\n30 END\n");
        check("float AND coerces via fp_toint16",  has(a, "jsr fp_toint16"));
        check("float AND combines bitwise",        has(a, "and FA"));
        check("float AND returns a float",         has(a, "jsr fp_fromint16"));
    }
    // INT floors toward -inf: fp_int truncates toward zero, so a negative
    // fractional value is corrected down by 1 via a compare + subtract.
    {
        std::string a = genf("10 X=INT(Y)\n20 END\n");
        check("INT floor: truncates (fp_int)",         has(a, "jsr fp_int"));
        check("INT floor: compares trunc vs x",        has(a, "jsr fp_cmp"));
        check("INT floor: drops by 1 when trunc > x",  has(a, "jsr fp_sub"));
    }
    // Integer-typed target A% truncates toward zero on store in the float phase.
    {
        std::string a = genf("10 A%=Y/2\n20 END\n");
        check("A% store truncates via fp_int", has(a, "jsr fp_int") && has(a, "V_A_I"));
    }
    // "IF cond THEN <line> : stmts": the trailing statements are part of the
    // (conditional) consequent and must NOT leak out to run on the FALSE branch.
    // So no store to the tail var appears AFTER the IF's skip label.
    {
        std::string a = gen("10 IF A THEN 100 : B=2\n100 END\n");
        size_t skip = a.find("Lif0:");
        check("IF THEN <line> : tail stays inside the consequent",
              skip != std::string::npos && a.find("sta V_B", skip) == std::string::npos);
    }

    if (failures) { std::printf("basic_native_codegen: %d FAILED\n", failures); return 1; }
    std::printf("basic_native_codegen: OK\n");
    return 0;
}
