// basic_native_codegen_test.cpp -- pure unit pin for the NATIVE BASIC compiler
// (src/BasicNativeCompiler.cpp): asserts properties of the generated 6502 asm
// without assembling it (no cc65 needed), so it always runs and is fast. The
// sibling basic_native_run test (cc65-gated) proves the asm actually executes and
// is faster than the interpreter.

#include "BasicNativeCompiler.h"

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
    auto r = basicnative::compile(src, basicnative::Card::Gen2, /*float=*/true);
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
        auto r = basicnative::compile("10 X=1.5\n", basicnative::Card::Gen2, /*float=*/false);
        check("float literal rejected (int phase)", !r.ok && r.error.find("line 1") != std::string::npos);
    }

    // FLOAT phase: real binary32 ops, float literals OK, HPLOT coords converted.
    {
        std::string a = genf("10 X=1.5\n20 Y=X*X/3\n30 END\n");
        check("float phase header", has(a, "(float phase)"));
        check("float imports the FP runtime", has(a, "fp_mul") && has(a, "fp_div") && has(a, ".importzp FA, FB"));
        check("float var is 4 bytes", has(a, "V_X: .res 4"));
        std::string b = genf("10 HGR\n20 HPLOT X,Y\n30 END\n");
        check("float HPLOT converts coords (fp_toint16)", has(b, "jsr fp_toint16"));
    }

    if (failures) { std::printf("basic_native_codegen: %d FAILED\n", failures); return 1; }
    std::printf("basic_native_codegen: OK\n");
    return 0;
}
