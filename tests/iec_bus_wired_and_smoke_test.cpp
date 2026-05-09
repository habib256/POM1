// iec_bus_wired_and_smoke — pins the open-collector wired-AND model
// of pom1::IECBus. Single device pulled = LOW; both released = HIGH.

#include "IECBus.h"

#include <cassert>
#include <cstdio>

using pom1::IECBus;

int main() {
    IECBus bus;
    using L = IECBus::Line;

    // Bus idle: every line HIGH (released).
    assert(bus.level(L::ATN));
    assert(bus.level(L::CLK));
    assert(bus.level(L::DATA));
    assert(bus.level(L::SRQ));

    // Host pulls ATN.
    bus.setHostPulled(L::ATN, true);
    assert(!bus.level(L::ATN));
    assert(bus.level(L::CLK));    // unaffected

    // Drive also pulls ATN — still LOW.
    bus.setDrivePulled(L::ATN, true);
    assert(!bus.level(L::ATN));

    // Host releases — drive still pulling — still LOW.
    bus.setHostPulled(L::ATN, false);
    assert(!bus.level(L::ATN));

    // Drive releases — back to HIGH.
    bus.setDrivePulled(L::ATN, false);
    assert(bus.level(L::ATN));

    // release() resets all.
    bus.setHostPulled(L::DATA, true);
    bus.setDrivePulled(L::CLK, true);
    bus.release();
    assert(bus.level(L::DATA));
    assert(bus.level(L::CLK));

    std::printf("iec_bus_wired_and_smoke: OK\n");
    return 0;
}
