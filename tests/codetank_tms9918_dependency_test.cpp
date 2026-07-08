// CodeTank ↔ TMS9918 dependency invariants — Memory-level integration test.
//
// Pins the daughterboard relationship modelled after real P-LAB silicon:
//   - CodeTank cannot exist standalone: enabling it auto-plugs TMS9918.
//   - Unplugging TMS9918 cascade-unplugs CodeTank.
//   - Unplugging CodeTank does NOT touch TMS9918 (the host is independent).
//   - CodeTank ↔ microSD are mutually exclusive (the microSD EEPROM decodes
//     Applesoft Lite at $6000-$7FFF, inside CodeTank's $4000-$7FFF window);
//     the TMS9918 host survives the eviction, and the IEC add-on cascades
//     off with its microSD host.
//
// Run from CMAKE_SOURCE_DIR so Memory's constructor finds roms/.

#include "Memory.h"
// Incomplete-type guard (same pattern as the other Memory-level smoke tests):
// Memory holds unique_ptrs of forward-declared peripherals; this test TU
// instantiates Memory so it needs each concrete destructor.
#include "A1IO_RTC.h"
#include "CFFA1.h"
#include "MicroSD.h"
#include "PR40Printer.h"
#include "SID.h"
#include "TMS9918.h"
#include "TerminalCard.h"
#include "WiFiModem.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>

namespace {
void mustBeTrue(bool cond, const char* msg)
{
    if (!cond) {
        std::fprintf(stderr, "ASSERT FAILED: %s\n", msg);
        std::exit(1);
    }
}
} // namespace

int main()
{
    // 1. Enabling CodeTank from a fresh Memory auto-plugs TMS9918.
    {
        Memory mem;
        mustBeTrue(!mem.isTMS9918Enabled(), "fresh Memory: TMS9918 must start unplugged");
        mustBeTrue(!mem.isCodeTankEnabled(), "fresh Memory: CodeTank must start unplugged");

        mem.setCodeTankEnabled(true);
        mustBeTrue(mem.isCodeTankEnabled(),
                   "setCodeTankEnabled(true) must enable CodeTank");
        mustBeTrue(mem.isTMS9918Enabled(),
                   "setCodeTankEnabled(true) must auto-plug the TMS9918 host");
    }

    // 2. Disabling TMS9918 cascade-unplugs CodeTank.
    {
        Memory mem;
        mem.setCodeTankEnabled(true);  // also auto-plugs TMS9918
        mustBeTrue(mem.isTMS9918Enabled() && mem.isCodeTankEnabled(),
                   "setup: both cards plugged");

        mem.setTMS9918Enabled(false);
        mustBeTrue(!mem.isTMS9918Enabled(),
                   "setTMS9918Enabled(false) must disable TMS9918");
        mustBeTrue(!mem.isCodeTankEnabled(),
                   "setTMS9918Enabled(false) must cascade-unplug CodeTank daughterboard");
    }

    // 3. Disabling CodeTank does NOT cascade-unplug TMS9918 — the host is
    //    independent (other peripherals can still want it).
    {
        Memory mem;
        mem.setTMS9918Enabled(true);
        mem.setCodeTankEnabled(true);
        mustBeTrue(mem.isTMS9918Enabled() && mem.isCodeTankEnabled(),
                   "setup: both cards plugged");

        mem.setCodeTankEnabled(false);
        mustBeTrue(!mem.isCodeTankEnabled(),
                   "setCodeTankEnabled(false) must disable CodeTank");
        mustBeTrue(mem.isTMS9918Enabled(),
                   "setCodeTankEnabled(false) must NOT touch the TMS9918 host");
    }

    // 4. Idempotency: re-enabling an already-on CodeTank with TMS9918 already
    //    plugged is a no-op (no cascade ricochet).
    {
        Memory mem;
        mem.setTMS9918Enabled(true);
        mem.setCodeTankEnabled(true);
        mem.setCodeTankEnabled(true);  // no-op
        mustBeTrue(mem.isTMS9918Enabled() && mem.isCodeTankEnabled(),
                   "double-enable CodeTank must remain consistent");
    }

    // 5. Plugging CodeTank evicts the microSD (its Applesoft Lite EEPROM
    //    window $6000-$7FFF sits inside CodeTank's $4000-$7FFF) and
    //    cascade-drops the IEC add-on riding on the microSD's VIA.
    {
        Memory mem;
        mem.setIECCardEnabled(true);   // cascade-plugs microSD
        mustBeTrue(mem.isMicroSDEnabled() && mem.isIECCardEnabled(),
                   "setup: microSD + IEC plugged");

        mem.setCodeTankEnabled(true);
        mustBeTrue(mem.isCodeTankEnabled() && mem.isTMS9918Enabled(),
                   "setCodeTankEnabled(true) must plug CodeTank + TMS9918 host");
        mustBeTrue(!mem.isMicroSDEnabled(),
                   "setCodeTankEnabled(true) must evict the microSD ($6000-$7FFF overlap)");
        mustBeTrue(!mem.isIECCardEnabled(),
                   "microSD eviction must cascade-drop the IEC add-on");
    }

    // 6. Symmetric: plugging the microSD evicts the CodeTank daughterboard
    //    but leaves its TMS9918 host on the bus.
    {
        Memory mem;
        mem.setCodeTankEnabled(true);  // also auto-plugs TMS9918
        mustBeTrue(mem.isCodeTankEnabled() && mem.isTMS9918Enabled(),
                   "setup: CodeTank + TMS9918 plugged");

        mem.setMicroSDEnabled(true);
        mustBeTrue(mem.isMicroSDEnabled(),
                   "setMicroSDEnabled(true) must enable the microSD");
        mustBeTrue(!mem.isCodeTankEnabled(),
                   "setMicroSDEnabled(true) must evict the CodeTank daughterboard");
        mustBeTrue(mem.isTMS9918Enabled(),
                   "microSD eviction must NOT touch the TMS9918 host");
    }

    std::printf("codetank_tms9918_dependency: OK\n");
    return 0;
}
