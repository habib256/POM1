// pia_ddr_smoke_test.cpp -- the PIA 6821's register banking ($D010-$D013).
//
// Each of the PIA's two ports hides TWO registers behind one address, selected
// by bit 2 of that port's control register:
//
//     CRx bit 2 = 0  ->  the data address is the DATA DIRECTION register
//     CRx bit 2 = 1  ->  it is the peripheral (data) register
//
//     $D010 KBD / DDRA      $D011 CRA
//     $D012 DSP / DDRB      $D013 CRB
//
// POM1 modelled none of it: $D013 fell through to plain RAM and $D012 always
// read back the last glyph written. Uncle Bernie's Codebreaker probes exactly
// this to tell real hardware from an emulator --
//
//     STA $D013   ; CRB := $00, bank in the direction register
//     LDA $D012   ; read DDRB
//     LDX #$A7
//     STX $D013   ; CRB := $A7, bank the data register back
//     CMP #$7F    ; DDRB must be $7F, what the Woz Monitor programs
//     BNE advert  ; else print "HEY ! I WANT TO RUN ON A REAL APPLE-1 !"
//
// -- and POM1 failed it. This test is that exact sequence, plus the invariants
// the fix must not break.
//
// NOTE the seeded state: POM1 starts the PIA in its POST-reset condition
// (CRA = CRB = $A7, DDRB = $7F) rather than the silicon's all-zero power-on
// state. That is deliberate and load-bearing -- POM1 jumps straight into
// programs (--run, DevBench Run, jumpTo) without executing the Monitor's reset
// at $FF00, and with a zeroed CRB the Monitor's own ECHO would write its
// characters into DDRB and then hang forever on its `BIT $D012 / BMI` as soon
// as one with bit 7 set landed there. Every entry path must see the PIA that
// software actually meets on a running machine.

#include "TMS9918.h"      // IWYU pragma: keep
#include "WiFiModem.h"    // IWYU pragma: keep
#include "TerminalCard.h" // IWYU pragma: keep
#include "A1IO_RTC.h"     // IWYU pragma: keep
#include "PR40Printer.h"  // IWYU pragma: keep
#include "DisplayDevice.h"
#include "Memory.h"
#include "TerminalTiming.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

class Cap : public DisplayDevice {
public:
    void onChar(char c) override { text.push_back(static_cast<char>(c & 0x7F)); }
    std::string text;
};

int fail(const char* what) { std::fprintf(stderr, "  → %s\n", what); return 1; }

} // namespace

int main()
{
    Memory mem;
    mem.initMemory();

    // ---- 1. Codebreaker's probe, instruction for instruction ---------------
    mem.memWrite(0xD013, 0x00);              // CRB := 0  -> DDRB banked in
    const uint8_t ddrb = mem.memRead(0xD012);
    mem.memWrite(0xD013, 0xA7);              // CRB := $A7 -> data register back
    if (ddrb != 0x7F) {
        std::fprintf(stderr,
            "  → DDRB reads $%02X, expected $7F. Codebreaker (and anything else "
            "probing the PIA) will conclude it is on an emulator.\n", ddrb);
        return 1;
    }

    // ---- 2. $D013 is a register, not RAM -----------------------------------
    // It used to fall through to the backing array, so it read back whatever
    // was last written to it — which is ALSO what a stored CR does, hence the
    // sharper check: the alias $D0F3 must reach the same register.
    if (mem.memRead(0xD013) != 0xA7) return fail("CRB did not read back $A7");
    mem.memWrite(0xD0F3, 0x00);              // PIA decodes only A0-A1
    if (mem.memRead(0xD012) != 0x7F)
        return fail("the $D0x3 alias does not reach CRB — A0-A1 decoding lost");
    mem.memWrite(0xD013, 0xA7);

    // ---- 3. The direction register is writable while banked in -------------
    mem.memWrite(0xD013, 0x00);
    mem.memWrite(0xD012, 0x3C);
    if (mem.memRead(0xD012) != 0x3C) return fail("DDRB is not writable when banked in");
    mem.memWrite(0xD012, 0x7F);              // put it back
    mem.memWrite(0xD013, 0xA7);

    // ---- 4. ...and a banked-in write must NOT reach the display ------------
    Cap cap;
    mem.setDisplayDevice(&cap);
    mem.memWrite(0xD013, 0x00);
    mem.memWrite(0xD012, 0x41);              // 'A' — a DDR program, not a glyph
    mem.memWrite(0xD013, 0xA7);
    if (!cap.text.empty())
        return fail("a write to the DIRECTION register printed a character");
    mem.memWrite(0xD012, 0x7F);              // restore DDRB via the data path?  no-op:
    mem.memWrite(0xD013, 0x00);
    mem.memWrite(0xD012, 0x7F);
    mem.memWrite(0xD013, 0xA7);

    // ---- 5. With the data register selected, the display still works -------
    // The whole point of the seeded post-reset state: printing must work on a
    // machine that never executed the Monitor's reset code.
    cap.text.clear();
    mem.memWrite(0xD012, 0xC8);              // 'H' with the strobe bit
    mem.memWrite(0xD012, 0xC9);              // 'I'
    if (cap.text != "HI") {
        std::fprintf(stderr, "  → display wrote \"%s\", expected \"HI\"\n",
                     cap.text.c_str());
        return 1;
    }
    // The Monitor's reset-time `LDY #$7F / STY $D012` still must not paint.
    cap.text.clear();
    mem.memWrite(0xD012, 0x7F);
    if (!cap.text.empty()) return fail("the raw-$7F reset write painted a glyph");

    // ---- 6. Keyboard side untouched ----------------------------------------
    // $D011 keeps its historical read semantics (bit 7 = key ready, nothing
    // else): every Apple-1 program tests it with BIT/BPL, and returning the
    // control bits too would change every read in the corpus for no caller.
    mem.setDisplayDevice(nullptr);
    if (mem.memRead(0xD011) != 0x00) return fail("$D011 should read $00 with no key");
    mem.setKeyPressed('Z');
    if (mem.memRead(0xD011) != 0x80) return fail("$D011 should read $80 with a key ready");
    if (mem.memRead(0xD010) != 0xDA) return fail("$D010 should return 'Z' | $80");

    // ---- 7. A reset restores the post-reset PIA ----------------------------
    mem.memWrite(0xD013, 0x00);              // leave it banked the wrong way
    mem.resetMemory();
    if (mem.memRead(0xD013) != 0xA7)
        return fail("resetMemory did not restore CRB to its post-reset $A7");
    mem.memWrite(0xD013, 0x00);
    if (mem.memRead(0xD012) != 0x7F)
        return fail("resetMemory did not restore DDRB to $7F");

    // ---- 8. The banking survives a snapshot round-trip ---------------------
    // The four shadow registers are NOT reconstructible from the 64 KB RAM
    // image: DDRA/DDRB never reach mem[] at all (memWrite returns before the
    // store when the CR banks them in), and while CRA/CRB are mirrored there,
    // every READ answers from the member. They were missing from the MEM
    // section until v6, so a restore silently kept the LIVE machine's banking
    // — load a state taken mid-probe and Codebreaker's `LDA $D012` came back
    // with the display port instead of DDRB. Rewind replays the same blobs, so
    // scrubbing the timeline hit it too.
    {
        Memory a;
        a.initMemory();
        a.memWrite(0xD013, 0x00);            // CRB := 0 → DDRB banked in
        a.memWrite(0xD012, 0x3C);            // program DDRB
        a.memWrite(0xD011, 0x04);            // CRA := 4 → keyboard DATA selected
        a.memWrite(0xD013, 0x00);            // (still banked in when we snapshot)

        const std::vector<uint8_t> blob = a.saveSnapshotToBuffer(nullptr);
        if (blob.empty()) return fail("saveSnapshotToBuffer produced nothing");

        Memory b;                            // fresh: CRB = $A7, DDRB = $7F
        b.initMemory();
        std::string err;
        if (!b.loadSnapshotFromBuffer(blob, err, nullptr))
            return fail(("snapshot restore failed: " + err).c_str());

        if (b.memRead(0xD013) != 0x00)
            return fail("CRB did not survive the snapshot round-trip");
        if (b.memRead(0xD012) != 0x3C)
            return fail("DDRB did not survive the snapshot round-trip");
        b.memWrite(0xD013, 0xA7);            // bank the data register back
        if (b.memRead(0xD013) != 0xA7)
            return fail("CRB is not writable after a restore");
    }

    // -----------------------------------------------------------------
    // §9 The display busy model behind PB7 — the wiring, not the arithmetic.
    //
    // terminal_timing_smoke pins pom1::terminal on its own; what it cannot see
    // is whether Memory actually consults it. A $D012 write must arm the busy
    // countdown the SELECTED model asks for, and the default must remain the
    // historical fixed delay every shipped program is validated on.
    // -----------------------------------------------------------------
    {
        Memory m;
        m.initMemory();

        // Default: the fixed countdown. PB7 must read busy right after a write
        // and stay busy well past a whole video field.
        if (m.displayBusyModel() != pom1::terminal::BusyModel::FixedDelay)
            return fail("the default display busy model must stay FixedDelay");
        m.memWrite(0xD012, 0xC1);                      // 'A' through the data register
        if (!(m.memRead(0xD012) & 0x80))
            return fail("PB7 should be busy immediately after a $D012 write");
        m.advanceCycles(pom1::terminal::kFieldCycles);  // 17030 — one field
        if (!(m.memRead(0xD012) & 0x80))
            return fail("FixedDelay is 17045 cycles: still busy after one 17030-cycle field");
        m.advanceCycles(64);                            // 17030 + 64 > 17045
        if (m.memRead(0xD012) & 0x80)
            return fail("FixedDelay should have expired by 17094 cycles");

        // FieldSync: the busy ends ON the field boundary, so it is never longer
        // than one field — which is exactly what distinguishes it above.
        Memory f;
        f.initMemory();
        f.setDisplayBusyModel(pom1::terminal::BusyModel::FieldSync);
        if (f.displayBusyModel() != pom1::terminal::BusyModel::FieldSync)
            return fail("setDisplayBusyModel did not take");
        f.memWrite(0xD012, 0xC1);
        if (!(f.memRead(0xD012) & 0x80))
            return fail("PB7 should be busy immediately after a $D012 write (FieldSync)");
        f.advanceCycles(pom1::terminal::kFieldCycles);
        if (f.memRead(0xD012) & 0x80)
            return fail("FieldSync must never stay busy beyond one 17030-cycle field");
    }

    std::printf("pia_ddr_smoke: OK (DDRB reads $7F through the CRB bank, "
                "$D013 is a register, display and keyboard unchanged, "
                "banking survives a snapshot)\n");
    return 0;
}
