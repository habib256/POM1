// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// BusConflicts — declarative table of mutually-exclusive expansion cards.
//
// Purpose. CLAUDE.md describes "Parmigiani's golden rule" — on real Apple-1
// hardware, exactly ONE P-LAB card is plugged at a time, because the 6502
// bus has no arbitration and many P-LAB cards overlap address windows.
// POM1 enforces this in `Memory::setXxxEnabled` paths (each enable call
// disables conflicting cards). Until now those rules lived in prose plus
// scattered if-cascades in `applyMachineConfig` and a few `Memory::set*`
// methods. This table moves them to ONE place, in code, in declarative
// form, so:
//   * a future contributor adding a 13th card can read the conflicts
//     instead of reverse-engineering them from setMicroSDEnabled;
//   * tests can iterate the table to assert each pair conflicts;
//   * the UI can surface "Enabling X will unplug Y" warnings driven from
//     the same table the runtime uses.
//
// Format. Each entry is a pair of card names (matching `Peripheral::name()`
// strings). The relation is symmetric: enabling either card unplugs the
// other. The table does NOT yet drive runtime behaviour — Memory::setXxx
// methods still encode the rules inline. Aligning the runtime with this
// table is the next migration; the table being authoritative documentation
// today is already useful.
//
// Coexistence-with-priority. A handful of cards share addresses but coexist
// because the PeripheralBus dispatch picks a winner via priority (e.g.
// TMS9918 wins over A1-SID at $CC00/$CC01). Those pairs are NOT in this
// table — they're documented inline in `Memory.h` and `PeripheralBus.cpp`.

#ifndef POM1_BUS_CONFLICTS_H
#define POM1_BUS_CONFLICTS_H

#include <array>
#include <string_view>

namespace pom1 {

struct BusConflict {
    std::string_view cardA;
    std::string_view cardB;
    std::string_view reason;   // address window or hardware constraint
};

/// Mutually-exclusive card pairs. Symmetric: order in the pair doesn't
/// matter. Tests iterate this table to verify both
/// `setA(true) ⇒ B disabled` and `setB(true) ⇒ A disabled`.
inline constexpr std::array<BusConflict, 11> kBusConflicts{{
    // GEN2 HGR framebuffer at $2000-$3FFF overlaps the A1-IO/RTC VIA
    // window at $2000-$200F.
    {"GEN2 HGR",  "A1-IO/RTC",   "$2000-$200F overlap"},

    // A1-AUDIO SE at $CC00-$CC1F mirrors the SID register window
    // straight through the TMS9918 ports — they cannot coexist.
    {"A1-AUDIO SE", "TMS9918",   "$CC00-$CC1F vs VDP $CC00/$CC01"},

    // A1-SID prototype at $C800-$CFFF and A1-AUDIO SE at $CC00-$CC1F
    // share the underlying SID instance; only one mapping is plugged.
    {"A1-SID",    "A1-AUDIO SE", "shared SID instance, two windows"},

    // microSD ROM at $8000-$9FFF and CFFA1 firmware at $9000-$AFDF
    // overlap; presets enforce mutual exclusion.
    {"microSD",   "CFFA1",       "$9000-$9FFF overlap"},

    // Juke-Box ROM window ($4000-$BFFF or $8000-$BFFF) blankets nearly
    // everyone. Each conflict is listed explicitly so the table is grep-able.
    {"Juke-Box",  "CFFA1",       "$9000-$AFDF inside $8000-$BFFF window"},
    {"Juke-Box",  "microSD",     "$8000-$9FFF + $A000-$A00F inside ROM window"},
    {"Juke-Box",  "Krusader",    "$F000-$F7FF inside ROM window (RAM-32 jumper edge)"},
    {"Juke-Box",  "Wi-Fi Modem", "$B000-$B003 inside ROM window"},
    {"Juke-Box",  "A1-SID",      "$C800-$CFFF inside ROM window (RAM-16 jumper)"},

    // CodeTank's 16 kB ROM half occupies $4000-$7FFF, which is also the
    // Juke-Box's lower-window territory in RAM-16/ROM-32 mode.
    {"CodeTank",  "Juke-Box",    "$4000-$7FFF overlap"},

    // CodeTank is wired as a TMS9918 daughterboard; it has no edge
    // connector of its own. Listed here so a contributor can grep for
    // "CodeTank" and find the dependency. (Memory enforces this as a
    // *cascade*, not a conflict — see Memory::setCodeTankEnabled().)
    {"CodeTank",  "<requires TMS9918>", "daughterboard wiring (cascade, not conflict)"},
}};

} // namespace pom1

#endif // POM1_BUS_CONFLICTS_H
