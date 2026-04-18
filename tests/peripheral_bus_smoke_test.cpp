// PeripheralBus dispatch smoke test — self-contained, no framework.
//
// The Klaus Dormann 6502 test runs with Memory::setTestMode(true), which
// bypasses the PeripheralBus entirely. Nothing therefore pins the bus
// dispatch semantics. This test exercises the four invariants most
// likely to regress: pageMask fast-path miss, basic read routing,
// priority ordering at overlapping ranges, enable/disable round-trip,
// and empty-onWrite sniffer pass-through.
//
// Any assertion failure aborts via assert() with the usual stderr trace
// plus a non-zero exit code — enough for ctest to report the regression.

#include "PeripheralBus.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

int main() {
    PeripheralBus bus;

    int sid_hits = 0, tms_hits = 0, sniffer_read_hits = 0;

    // SID-style: whole $C800-$CFFF window, priority 0, full R/W.
    auto sid_h = bus.registerHandle("SID", {0xC800, 0xCFFF}, /*priority*/ 0,
        [&](uint16_t /*a*/) { ++sid_hits; return uint8_t{0x11}; },
        [](uint16_t, uint8_t) {});

    // TMS9918-style: overlaps SID at $CC00/$CC01 but priority 10 wins.
    bus.registerHandle("TMS", {0xCC00, 0xCC01}, /*priority*/ 10,
        [&](uint16_t /*a*/) { ++tms_hits; return uint8_t{0x22}; },
        [](uint16_t, uint8_t) {});

    // Cassette sniffer: read handler only, no onWrite → writes must fall
    // through to raw RAM (modelled here as tryWrite returning false).
    bus.registerHandle("ACI", {0xC000, 0xC0FF}, /*priority*/ 0,
        [&](uint16_t /*a*/) { ++sniffer_read_hits; return uint8_t{0xFE}; },
        /*onWrite=*/ {});

    uint8_t v = 0;

    // 1. pageMask miss: $1234 is outside every registered range — no handler runs.
    assert(!bus.tryRead(0x1234, v));
    assert(sid_hits == 0 && tms_hits == 0);

    // 2. Basic dispatch: $C801 hits SID (and only SID).
    assert(bus.tryRead(0xC801, v) && v == 0x11);
    assert(sid_hits == 1 && tms_hits == 0);

    // 3. Priority: $CC00 is in both SID and TMS ranges, priority 10 > 0.
    assert(bus.tryRead(0xCC00, v) && v == 0x22);
    assert(tms_hits == 1);
    // SID must NOT also be invoked at $CC00 (the bus returns after the first hit).
    assert(sid_hits == 1);

    // 4. Enable/disable round-trip: disable SID → $C801 falls through,
    //    re-enable → dispatch resumes.
    bus.setEnabled(sid_h, false);
    assert(!bus.isEnabled(sid_h));
    assert(!bus.tryRead(0xC801, v));       // no other handler covers $C801
    assert(sid_hits == 1);                 // unchanged
    bus.setEnabled(sid_h, true);
    assert(bus.isEnabled(sid_h));
    assert(bus.tryRead(0xC801, v) && v == 0x11);
    assert(sid_hits == 2);

    // 5. Sniffer pass-through: ACI has no onWrite, so tryWrite returns false
    //    and the caller (Memory::memWrite) lets the byte land in raw RAM.
    //    The sniffer's onRead remains untouched by this write.
    assert(!bus.tryWrite(0xC050, 0xAA));
    assert(sniffer_read_hits == 0);

    // 6. High-priority handle correctness — regression for the bug where
    //    registerHandle returned entries.back().insertionIndex AFTER sort:
    //    a priority-20 entry was moved to the front by sortEntries(), so
    //    back() was some unrelated lower-priority entry. The returned
    //    handle pointed at the wrong entry and setEnabled/isEnabled flipped
    //    the wrong one, leaving the real entry stuck at enabled=true (this
    //    made the Juke-Box shadow $4000-$BFFF with $FF even when its UI
    //    flag said "off"). Minimal repro: register high-prio handler, try
    //    to disable it, verify it actually stops firing.
    bool hi_called = false;
    auto hi_h = bus.registerHandle("HI", {0x4000, 0x4FFF}, /*priority*/ 20,
        [&](uint16_t) { hi_called = true; return uint8_t{0x77}; }, {});
    bus.setEnabled(hi_h, false);
    assert(!bus.isEnabled(hi_h));          // the REAL HI entry, not a neighbour
    assert(!bus.tryRead(0x4000, v));        // disabled → no dispatch
    assert(!hi_called);

    std::puts("PeripheralBus smoke test: all 6 invariants OK");
    return 0;
}
