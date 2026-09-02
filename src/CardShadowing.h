// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// CardShadowing — does a plugged card's bus window sit inside the program that
// was just loaded?
//
// WHY THIS EXISTS
//     `software/Graphic TMS9918/TMS_Logo_16k.txt` spans $0280-$2D00. On the
//     P-LAB Multiplexing Fantasy preset the A1-IO RTC's 65C22 answers at
//     $2000-$200F — sixteen bytes in the middle of the program. LOGO's
//     rotation words run fine; the first word that draws runs to PC=$0000.
//     Nothing said why. The machine is behaving exactly as the hardware would
//     (Parmigiani's one-board rule, deliberately broken by that preset), but a
//     user who plugs a fantasy machine and loads a program has no way to see
//     the collision: the load reports success and the program dies later,
//     somewhere else.
//
//     So POM1 says it. This header is the decision — which overlaps are worth
//     reporting — and it is pure: no Memory, no bus, no log sink, so
//     `card_shadowing_smoke` can put a program and a topology side by side
//     without an emulator.
//
// WHAT IT DOES NOT DO
//     It does NOT unplug anything, and that is deliberate. POM1 already evicts
//     cards before a graphics load (`MainWindow_ImGui::evictStorageCards`,
//     driven by `SoftwareDirRules.h`), and generalising that to "evict whatever
//     overlaps" is the wrong instinct: on a GEN2 machine the framebuffer IS
//     $2000-$3FFF, and an HGR picture loaded there is the whole point of the
//     card. A rule that cannot tell "this card shadows my code" from "this card
//     is what my data is for" must not act on the difference — it reports, and
//     the user decides. `CardCapability::Video` is exactly that discriminator
//     and it is already in the registry, so no second table is needed.
//
// WHERE THE CANDIDATES COME FROM
//     `Memory::cardSlots()`, the existing ordered registry — the same rule
//     `CardTypes.h` states for topology work: extend that registry, never
//     introduce a parallel card table. The caller walks it and passes the
//     plugged rows in; this file holds no card data of its own.

#ifndef POM1_CARD_SHADOWING_H
#define POM1_CARD_SHADOWING_H

#include "CardTypes.h"

#include <cstddef>
#include <cstdint>

namespace pom1 {
namespace shadowing {

/// One contiguous run of bytes a load wrote. Same shape as the pairs
/// `MemoryImage::zones()` returns and `Memory::loadHexDump` hands back.
struct Zone {
    uint16_t first = 0;
    uint16_t last  = 0;
};

/// A plugged card, reduced to what the question needs.
struct Candidate {
    CardId                  card         = CardId::Invalid;
    CardCapability          capabilities = CardCapability::None;
    const CardAddressRange* ranges       = nullptr;
    uint8_t                 rangeCount   = 0;
};

/// A card window found inside a loaded zone, narrowed to the bytes that
/// actually collide — the whole window is rarely what the user needs to see.
struct Shadow {
    CardId   card  = CardId::Invalid;
    uint16_t first = 0;
    uint16_t last  = 0;
};

/// Inclusive-range intersection test. Both ends are inclusive, as everywhere
/// else in POM1's memory map, so a one-byte window ($D012, $CA00) is
/// `first == last` and is NOT the empty range.
constexpr bool rangesOverlap(uint16_t aFirst, uint16_t aLast,
                             uint16_t bFirst, uint16_t bLast)
{
    return aFirst <= bLast && bFirst <= aLast;
}

/// Is this card's window a surface a program may legitimately be loaded into?
///
/// A video card's window is a framebuffer: `--load 2000:picture.bin` on a GEN2
/// machine targets $2000-$3FFF ON PURPOSE. Reporting that as a collision would
/// make the warning fire on the most ordinary graphics workflow POM1 has, and a
/// warning that cries on correct usage is one users learn to ignore.
constexpr bool isProgramTargetable(CardCapability capabilities)
{
    return hasCapability(capabilities, CardCapability::Video);
}

/// Report every candidate window that collides with a loaded zone.
///
/// Allocation-free — the caller supplies the output buffer and gets the count,
/// the same shape as `CardTopology`'s planners. Writes at most `outCapacity`
/// entries and keeps counting past that, so a caller can tell "there were
/// more" from "that was all"; overflow is not an error, because the point is
/// to tell the user something, not to enumerate exhaustively.
inline size_t findShadows(const Zone* zones, size_t zoneCount,
                          const Candidate* candidates, size_t candidateCount,
                          Shadow* out, size_t outCapacity)
{
    size_t found = 0;
    if (!zones || !candidates) return 0;

    for (size_t c = 0; c < candidateCount; ++c) {
        const Candidate& cand = candidates[c];
        if (cand.card == CardId::Invalid) continue;
        if (isProgramTargetable(cand.capabilities)) continue;
        if (!cand.ranges) continue;

        for (uint8_t r = 0; r < cand.rangeCount; ++r) {
            const CardAddressRange& range = cand.ranges[r];
            for (size_t z = 0; z < zoneCount; ++z) {
                if (!rangesOverlap(range.first, range.last,
                                   zones[z].first, zones[z].last))
                    continue;
                if (found < outCapacity && out) {
                    out[found].card  = cand.card;
                    out[found].first = range.first > zones[z].first
                                           ? range.first : zones[z].first;
                    out[found].last  = range.last < zones[z].last
                                           ? range.last : zones[z].last;
                }
                ++found;
                break;   // one report per card window, not one per zone
            }
        }
    }
    return found;
}

} // namespace shadowing
} // namespace pom1

#endif // POM1_CARD_SHADOWING_H
