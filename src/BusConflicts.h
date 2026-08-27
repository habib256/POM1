// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// BusConflicts — declarative table of mutually-exclusive expansion cards.
//
// Purpose. CLAUDE.md describes "Parmigiani's golden rule" — on real Apple-1
// hardware, exactly ONE P-LAB card is plugged at a time, because the 6502
// bus has no arbitration and many P-LAB cards overlap address windows.
// POM1 enforces this through `CardTopology` plans consumed by both whole-
// configuration requests and compatibility setters. Those rules once lived in prose plus
// scattered if-cascades in `applyMachineConfig` and a few `Memory::set*`
// methods. This table moves them to ONE place, in code, in declarative
// form, so:
//   * a future contributor adding a card can read the conflicts
//     instead of reverse-engineering them from setMicroSDEnabled;
//   * tests can iterate the table to assert each pair conflicts;
//   * the UI can surface "Enabling X will unplug Y" warnings driven from
//     the same table the runtime uses.
//
// Format. Each entry is a pair of stable CardId values. The relation is
// symmetric: enabling either card unplugs the
// other. `CardTopology` is the sole policy interpreter; Memory only executes
// the resulting attach/detach plan.
//
// Coexistence-with-priority. A handful of cards share addresses but coexist
// because the PeripheralBus dispatch picks a winner via priority (e.g.
// TMS9918 wins over A1-SID at $CC00/$CC01). Those pairs are NOT in this
// table. The SID/TMS9918 priority pair is accepted in Fantasy mode and added
// as a Strict-only conflict by `CardTopology`.

#ifndef POM1_BUS_CONFLICTS_H
#define POM1_BUS_CONFLICTS_H

#include <array>
#include <string_view>

#include "CardTypes.h"

namespace pom1 {

struct BusConflict {
    CardId cardA;
    CardId cardB;
    std::string_view reason;   // address window or hardware constraint
};

/// Mutually-exclusive card pairs. Symmetric: order in the pair doesn't
/// matter. Tests iterate this table to verify both
/// `setA(true) ⇒ B disabled` and `setB(true) ⇒ A disabled`.
inline constexpr std::array<BusConflict, 10> kBusConflicts{{
    // GEN2 HGR framebuffer at $2000-$3FFF overlaps the A1-IO/RTC VIA
    // window at $2000-$200F.
    {CardId::Gen2, CardId::A1IoRtc, "$2000-$200F overlap"},

    // A1-AUDIO SE at $CC00-$CC1F mirrors the SID register window
    // straight through the TMS9918 ports — they cannot coexist.
    {CardId::SidSpecialEdition, CardId::Tms9918, "$CC00-$CC1F vs VDP $CC00/$CC01"},

    // A1-SID prototype at $C800-$CFFF and A1-AUDIO SE at $CC00-$CC1F
    // share the underlying SID instance; only one mapping is plugged.
    {CardId::Sid, CardId::SidSpecialEdition, "shared SID instance, two windows"},

    // microSD ROM at $8000-$9FFF and CFFA1 firmware at $9000-$AFDF
    // overlap; presets enforce mutual exclusion.
    {CardId::MicroSD, CardId::Cffa1, "$9000-$9FFF overlap"},

    // Juke-Box ROM window ($4000-$BFFF or $8000-$BFFF) blankets nearly
    // everyone. Each conflict is listed explicitly so the table is grep-able.
    {CardId::JukeBox, CardId::Cffa1, "$9000-$AFDF inside $8000-$BFFF window"},
    {CardId::JukeBox, CardId::MicroSD, "$8000-$9FFF + $A000-$A00F inside ROM window"},
    {CardId::JukeBox, CardId::WifiModem, "$B000-$B003 inside ROM window"},
    {CardId::JukeBox, CardId::Sid, "$C800-$CFFF inside ROM window (RAM-16 jumper)"},

    // CodeTank's 16 kB ROM half occupies $4000-$7FFF, which is also the
    // Juke-Box's lower-window territory in RAM-16/ROM-32 mode.
    {CardId::CodeTank, CardId::JukeBox, "$4000-$7FFF overlap"},

    // The microSD EEPROM also serves Applesoft Lite at $6000-$7FFF,
    // inside CodeTank's fixed $4000-$7FFF window.
    {CardId::CodeTank, CardId::MicroSD, "$6000-$7FFF Applesoft Lite SD ROM overlap"},
}};

} // namespace pom1

#endif // POM1_BUS_CONFLICTS_H
