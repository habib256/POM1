// woz_backspace_smoke_test.cpp -- the Apple-1 has no destructive backspace.
//
// github #38. The Apple-1 terminal section is a shift-register display driven
// by 74LS counter logic: it can advance, wrap, scroll and CR, and that is the
// whole instruction set. Nothing can move the cursor left, and nothing can
// blank a cell that has already been shifted in. So "erase the character left
// of the cursor" is not a thing the machine can do -- POM1 used to do it anyway
// (Screen_ImGui treated $08 as cursor-left + blank), which is what the issue
// reported.
//
// What the Woz Monitor does instead is visible in its own byte stream, and that
// is what this test pins, straight out of roms/WozMonitor.rom:
//
//     NEXTCHAR  LDA KBD        ; $DF for '_'
//               STA IN,Y       ; stored FIRST
//               JSR ECHO       ; ...and echoed BEFORE anything is tested,
//               CMP #$8D       ;    so the '_' is already on screen
//               BNE NOTCR
//     NOTCR     CMP #$DF       ; "_"?
//               BEQ BACKSPACE
//     BACKSPACE DEY            ; the ONLY effect: step the input index back
//
// So the underscore is printed and the character leaves the input line buffer,
// while the screen keeps a trail of underscores. Both halves are asserted here:
// the echo stream (what the display receives) and IN itself (what the monitor
// will go on to parse).
//
// This is the specification POM1's host keyboard targets -- MainWindow_Keyboard
// maps the Backspace key to '_' rather than $08 precisely so that a host key
// lands on this path. The display-side half of the fix (Screen_ImGui dropping
// $08 like any other control code) is not reachable from here: UI sources are
// deliberately kept out of the test binaries (see the note in CMakeLists.txt).

#include "TMS9918.h"      // IWYU pragma: keep
#include "WiFiModem.h"    // IWYU pragma: keep
#include "TerminalCard.h" // IWYU pragma: keep
#include "A1IO_RTC.h"     // IWYU pragma: keep
#include "PR40Printer.h"  // IWYU pragma: keep
#include "DisplayDevice.h"
#include "M6502.h"
#include "Memory.h"

#include <cstdio>
#include <string>

namespace {

// Everything the CPU writes to $D012, i.e. exactly what the screen would show.
class CaptureDisplay : public DisplayDevice {
public:
    void onChar(char c) override { text.push_back(c); }
    std::string text;
};

// One keystroke, then enough cycles for the monitor to drain it. The Apple-1
// keyboard is a single latch, so two keys pushed without running the CPU in
// between lose the first.
void typeKey(M6502& cpu, Memory& mem, char c)
{
    mem.setKeyPressed(c);
    cpu.run(100000);
}

int fail(const char* what) { std::fprintf(stderr, "  -> %s\n", what); return 1; }

// The Woz Monitor's input line buffer.
constexpr uint16_t kIN = 0x0200;

} // namespace

int main()
{
    Memory mem;
    mem.initMemory();

    // The monitor must really be the one in ROM -- $FF00 is CLD/CLI.
    const uint8_t* m = mem.getMemoryPointer();
    if (m[0xFF00] != 0xD8 || m[0xFF01] != 0x58)
        return fail("roms/WozMonitor.rom is not at $FF00 (run ctest from the repo root)");

    CaptureDisplay disp;
    mem.setDisplayDevice(&disp);
    M6502 cpu(&mem);
    cpu.setProgramCounter(0xFF00);
    cpu.start();
    cpu.run(100000);            // reset -> "\" prompt, then spin on NEXTCHAR

    // The reset path programs the PIA with LDY #$7F / STY $D012. That is a DDR
    // setup, not a glyph, and POM1 filters the raw $7F so it cannot paint a
    // spurious '_' -- which would be indistinguishable from the very character
    // this test is about.
    if (disp.text.find('_') != std::string::npos)
        return fail("reset painted a spurious '_' ($7F DDR write leaked to the display)");
    if (disp.text.find('\\') == std::string::npos)
        return fail("the Woz Monitor never printed its '\\' prompt");

    // ---- Type A B _ C ------------------------------------------------------
    disp.text.clear();
    typeKey(cpu, mem, 'A');
    typeKey(cpu, mem, 'B');
    typeKey(cpu, mem, '_');
    typeKey(cpu, mem, 'C');

    // 1. The screen. Every key echoes, the '_' included, and NOTHING is
    //    removed: a destructive backspace would have produced "AC".
    if (disp.text != "AB_C") {
        std::fprintf(stderr,
            "  -> the display received \"%s\", expected \"AB_C\".\n"
            "     The '_' must be echoed like any other glyph and no character\n"
            "     may be erased -- the Apple-1 display cannot un-shift a cell.\n",
            disp.text.c_str());
        return 1;
    }

    // 2. The input buffer. BACKSPACE's DEY means the 'C' overwrites the 'B',
    //    so the monitor goes on to parse "AC" -- the character really is gone
    //    from the line even though it is still on screen. Keys arrive with the
    //    strobe bit set, so IN holds $C1 'A' and $C3 'C'.
    size_t at = std::string::npos;
    for (uint16_t i = 0; i < 16; ++i) {
        if (m[kIN + i] == 0xC1 && m[kIN + i + 1] == 0xC3) { at = i; break; }
    }
    if (at == std::string::npos) {
        std::fprintf(stderr,
            "  -> IN ($0200) does not hold 'A','C' adjacently, so the '_' did not\n"
            "     drop the 'B' from the input line. Buffer:");
        for (uint16_t i = 0; i < 8; ++i) std::fprintf(stderr, " %02X", m[kIN + i]);
        std::fprintf(stderr, "\n");
        return 1;
    }

    // ---- And the line still parses -----------------------------------------
    // The proof that "AC" is what the monitor kept: RETURN on this line makes it
    // interpret AC as an address and print its contents, not AB or ABC.
    disp.text.clear();
    typeKey(cpu, mem, '\r');
    if (disp.text.find("00AC") == std::string::npos) {
        std::fprintf(stderr,
            "  -> RETURN did not make the monitor examine $00AC. It echoed:\n     \"%s\"\n",
            disp.text.c_str());
        return 1;
    }

    std::printf("woz_backspace_smoke: OK — '_' prints and edits the line, "
                "nothing is erased on screen\n");
    return 0;
}
