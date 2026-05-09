// iec_snapshot_smoke — round-trip IEC card state through saveSnapshot →
// loadSnapshot. Pins kFlagIECCard (bit 15) + kFlagMicroSD (cascade) +
// the IECCard per-card payload (bus state + Drive1541 channel state +
// .d64 mounted path).
//
// Lives separately from snapshot_smoke because enabling IEC cascade-
// disables CFFA1 (microSD evicts CFFA1 at $9000-$AFDF), and the larger
// snapshot smoke test exercises CFFA1 alongside other cards.

#include "TMS9918.h"
#include "WiFiModem.h"
#include "TerminalCard.h"
#include "A1IO_RTC.h"
#include "PR40Printer.h"
#include "GT6144.h"
#include "JukeBox.h"
#include "CodeTank.h"
#include "CFFA1.h"
#include "MicroSD.h"
#include "SID.h"
#include "Memory.h"
#include "M6502.h"
#include "IECCard.h"
#include "Drive1541.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>

int main() {
    Memory mem;
    M6502 cpu(&mem);

    // Plug IEC: cascade-enables microSD.
    mem.setIECCardEnabled(true);
    assert(mem.isIECCardEnabled());
    assert(mem.isMicroSDEnabled());

    // Mutate Drive1541 error channel to a non-default value so the
    // per-card payload visibly differs from a fresh ctor.
    auto& iec = mem.getIECCard();
    iec.drive().setError(63, "FILE EXISTS", 18, 1);

    // Save snapshot.
    auto path = std::filesystem::temp_directory_path() / "pom1_iec_snap.snap";
    std::error_code rmEc;
    std::filesystem::remove(path, rmEc);
    std::string err;
    if (!mem.saveSnapshot(path.string(), err, &cpu)) {
        std::fprintf(stderr, "saveSnapshot failed: %s\n", err.c_str());
        return 1;
    }

    // Fresh Memory + CPU.
    Memory mem2;
    M6502 cpu2(&mem2);
    assert(!mem2.isIECCardEnabled());
    assert(!mem2.isMicroSDEnabled());

    // Load snapshot.
    if (!mem2.loadSnapshot(path.string(), err, &cpu2)) {
        std::fprintf(stderr, "loadSnapshot failed: %s\n", err.c_str());
        return 1;
    }

    // Cascade survived the round-trip.
    assert(mem2.isIECCardEnabled());
    assert(mem2.isMicroSDEnabled());

    // Drive1541 error channel survived.
    auto& iec2 = mem2.getIECCard();
    auto status = iec2.drive().errorString();
    if (status.find("63, FILE EXISTS,18,01") == std::string::npos) {
        std::fprintf(stderr, "Drive1541 error channel did not round-trip: '%s'\n",
                     status.c_str());
        return 1;
    }

    std::filesystem::remove(path, rmEc);
    std::printf("iec_snapshot_smoke: OK\n");
    return 0;
}
