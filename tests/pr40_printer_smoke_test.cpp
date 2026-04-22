// SWTPC PR-40 printer smoke test — pins the 1976 Jobs-hack semantics.
//
// The PR-40 is a passive sniffer on $D012 with a 40-char FIFO, a CR-or-full
// trigger, and an ~0.8 s mechanical cycle that (via the DPDT switch + NAND
// in the 1976 mod) stalls the Apple 1's BIT $D012 / BMI loop in the Woz
// Monitor. This test exercises the emulation-side invariants without a
// CPU: we drive Memory::memWrite(0xD012, c) directly and observe PR40Printer
// through Memory::memRead and the snapshot API.
//
// Assertions pinned:
//   1. Idle state: PR-40 plugged, FIFO empty, $D012 read bit 7 = 0.
//   2. "HELLO" + CR captures line "HELLO" into the paper roll.
//   3. Mixed mode: during the mechanical cycle, $D012 read bit 7 = 1
//      (NAND-inverted Data Accepted reaches PB7).
//   4. After advanceCycles(kMechCycleCpu + slack), bit 7 drops back to 0.
//   5. FIFO fills (40 chars, no CR) → also triggers a mechanical cycle.
//   6. Switch Off: writes are ignored, no paper growth, no CPU stall.

#include "Memory.h"
#include "PR40Printer.h"

// Incomplete-type guard (same pattern as the other smoke tests): Memory
// holds unique_ptrs of forward-declared peripherals; the test TU instantiates
// Memory so it needs each concrete destructor.
#include "A1IO_RTC.h"
#include "CFFA1.h"
#include "MicroSD.h"
#include "SID.h"
#include "TMS9918.h"
#include "TerminalCard.h"
#include "WiFiModem.h"

#include <cassert>
#include <cstdio>
#include <string>

namespace {

bool dsp_busy(Memory& mem) {
    return (mem.memRead(0xD012) & 0x80) != 0;
}

void write_str(Memory& mem, const char* s) {
    for (const char* p = s; *p; ++p) {
        mem.memWrite(0xD012, static_cast<unsigned char>(*p) | 0x80);
    }
}

} // namespace

int main()
{
    Memory mem;
    mem.initMemory();

    mem.setPR40Enabled(true);
    mem.getPR40().setMode(PR40Printer::SwitchMode::Mixed);

    // 1. Idle.
    assert(!dsp_busy(mem));
    {
        PR40Printer::Snapshot s;
        mem.getPR40().copySnapshot(s);
        assert(s.fifoLevel == 0);
        assert(s.recentLines.empty());
        assert(!s.busy);
    }

    // 2. HELLO + CR captures a line.
    write_str(mem, "HELLO");
    mem.memWrite(0xD012, 0x0D);      // CR triggers flush
    {
        PR40Printer::Snapshot s;
        mem.getPR40().copySnapshot(s);
        assert(!s.recentLines.empty());
        assert(s.recentLines.back() == "HELLO");
        assert(s.linesPrinted == 1);
    }

    // 3. Mixed: during the mechanical cycle, $D012 read is busy.
    assert(dsp_busy(mem));

    // 4. Drain the mechanical cycle. memRead's displayBusyCycles decayed by
    //    each memRead's advanceCycles... wait — displayBusyCycles is NOT
    //    driven by our memWrites directly for bit 7 (the write did seed it
    //    though, via memWrite $D012). Either way, both counters must reach
    //    zero after a big advance.
    mem.advanceCycles(PR40Printer::kMechCycleCpu + 1000);
    assert(!dsp_busy(mem));

    // 5. FIFO-full trigger: 40 printable chars without a CR must fire the
    //    mechanical cycle on char #40.
    for (int i = 0; i < PR40Printer::kFifoCapacity; ++i) {
        mem.memWrite(0xD012, 'A' | 0x80);
    }
    {
        PR40Printer::Snapshot s;
        mem.getPR40().copySnapshot(s);
        // The flush drains the FIFO into the paper roll.
        assert(s.fifoLevel == 0);
        assert(s.linesPrinted == 2);
        assert(s.busy);
        assert(s.recentLines.back() == std::string(40, 'A'));
    }
    mem.advanceCycles(PR40Printer::kMechCycleCpu + 1000);

    // 6. Switch Off: writes are dropped, no paper growth, no CPU stall.
    mem.getPR40().setMode(PR40Printer::SwitchMode::Off);
    {
        const int before = [&]{
            PR40Printer::Snapshot s;
            mem.getPR40().copySnapshot(s);
            return s.linesPrinted;
        }();
        write_str(mem, "IGNORED");
        mem.memWrite(0xD012, 0x0D);
        PR40Printer::Snapshot s;
        mem.getPR40().copySnapshot(s);
        assert(s.linesPrinted == before);
        // The video's own delay (displayBusyCycles) may still be armed from
        // the $D012 writes — that's the correct behaviour for Off mode, the
        // printer just isn't contributing anymore. Drain and verify.
        mem.advanceCycles(1000000);
        assert(!dsp_busy(mem));
    }

    std::puts("pr40_printer_smoke: OK");
    return 0;
}
