// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// card_shadowing_smoke — which card windows a load should be warned about.
//
// Pure: links NOTHING. The subject is a decision over a program's zones and a
// topology, and the point of extracting it (same seam rule as
// SoftwareDirRules.h / LayoutDecisions.h) is that it is answerable without an
// emulator, a bus or a log sink.
//
// Six sections:
//   1. inclusive-range overlap, including the one-byte windows POM1 really has.
//   2. the reported case: the A1-IO RTC inside TMS_Logo_16k.
//   3. the control that keeps the warning worth reading — a video card's
//      window is NOT reported, because loading a picture into a framebuffer is
//      the point, not a collision.
//   4. the reported range is the INTERSECTION, not the whole window.
//   5. multi-zone dumps, multi-range cards, and one report per window.
//   6. the output cap counts past what it stores, and nothing is written for
//      an empty or absent input.

#include "CardShadowing.h"

#include <cassert>
#include <cstdio>

using namespace pom1;
using namespace pom1::shadowing;

namespace {

// The real windows, copied from Memory::cardSlots() — the table under test is
// the caller's, so these fixtures name what the registry actually declares.
constexpr CardAddressRange kRtc[]    = {{0x2000, 0x200F}};                    // A1-IO & RTC
constexpr CardAddressRange kGen2[]   = {{0x2000, 0x5FFF}, {0xC200, 0xC7FF}};  // GEN2 HGR (Video)
constexpr CardAddressRange kMicroSD[] = {{0x6000, 0x7FFF}, {0x8000, 0x9FFF}, {0xA000, 0xA00F}};
constexpr CardAddressRange kJukeBox[] = {{0x4000, 0xBFFF}, {0xCA00, 0xCA00}};
constexpr CardAddressRange kTerm[]   = {{0xD012, 0xD012}};                    // one byte

constexpr CardCapability kPlain = CardCapability::Snapshot;
constexpr CardCapability kVideo = CardCapability::Snapshot | CardCapability::Video;
constexpr CardCapability kStore = CardCapability::Snapshot | CardCapability::Storage;

Candidate rtc()     { return {CardId::A1IoRtc,      kPlain, kRtc,     1}; }
Candidate gen2()    { return {CardId::Gen2,         kVideo, kGen2,    2}; }
Candidate microsd() { return {CardId::MicroSD,      kStore, kMicroSD, 3}; }
Candidate jukebox() { return {CardId::JukeBox,      kStore, kJukeBox, 2}; }
Candidate terminal(){ return {CardId::TerminalCard, kPlain, kTerm,    1}; }

// TMS_Logo_16k.txt: $0280-$2D00, the program that started this.
constexpr Zone kLogo[] = {{0x0280, 0x2D00}};

size_t count(const Zone* z, size_t zn, const Candidate* c, size_t cn)
{
    Shadow out[8];
    return findShadows(z, zn, c, cn, out, 8);
}

} // namespace

int main()
{
    // ── 1. inclusive overlap ─────────────────────────────────────────────
    {
        static_assert(rangesOverlap(0x2000, 0x200F, 0x0280, 0x2D00), "inside");
        static_assert(rangesOverlap(0x0280, 0x2D00, 0x2000, 0x200F), "symmetric");
        // Touching at one end IS an overlap: both bounds are inclusive, as
        // everywhere in POM1's memory map.
        static_assert(rangesOverlap(0x2000, 0x2FFF, 0x2FFF, 0x3000), "touching high");
        static_assert(rangesOverlap(0x2000, 0x2FFF, 0x1000, 0x2000), "touching low");
        static_assert(!rangesOverlap(0x2000, 0x2FFF, 0x3000, 0x3FFF), "adjacent, disjoint");
        static_assert(!rangesOverlap(0x3000, 0x3FFF, 0x2000, 0x2FFF), "adjacent, disjoint");
        // A one-byte window ($D012, $CA00) is first == last, never empty.
        static_assert(rangesOverlap(0xD012, 0xD012, 0xD000, 0xD0FF), "one-byte window");
        static_assert(!rangesOverlap(0xD012, 0xD012, 0xD013, 0xD013), "one-byte miss");
        // Runtime calls too: instrumentation cannot see a constant folded at
        // compile time, and a header that is only static_assert'ed reads as
        // uncovered while being more strongly proved than a runtime assert can
        // manage. Same trick as shortcut_table_smoke.
        volatile uint16_t a = 0x2000, b = 0x200F, c = 0x0280, d = 0x2D00;
        assert(rangesOverlap(a, b, c, d));
        assert(!rangesOverlap(0x2000, 0x2FFF, 0x3000, 0x3FFF));
        std::puts("  [PASS] 1. inclusive-range overlap");
    }

    // ── 2. the reported case ─────────────────────────────────────────────
    {
        const Candidate cands[] = {rtc()};
        Shadow out[4];
        assert(findShadows(kLogo, 1, cands, 1, out, 4) == 1);
        assert(out[0].card == CardId::A1IoRtc);
        std::puts("  [PASS] 2. the A1-IO RTC is reported inside TMS_Logo_16k");
    }

    // ── 3. a video card's window is NOT a collision ──────────────────────
    {
        // The control that keeps the warning worth reading. GEN2's $2000-$5FFF
        // is a framebuffer: `--load 2000:picture.bin` targets it deliberately,
        // and a warning that fires on POM1's most ordinary graphics workflow is
        // one users learn to ignore.
        const Candidate video[] = {gen2()};
        assert(count(kLogo, 1, video, 1) == 0);
        // …and the check is the CAPABILITY, not the card: the same windows
        // declared by a non-video card ARE reported.
        Candidate impostor = gen2();
        impostor.card = CardId::A1IoRtc;
        impostor.capabilities = kPlain;
        assert(count(kLogo, 1, &impostor, 1) == 1);
        static_assert(isProgramTargetable(kVideo), "video is targetable");
        static_assert(!isProgramTargetable(kPlain), "plain is not");
        static_assert(!isProgramTargetable(kStore), "storage is not");
        std::puts("  [PASS] 3. framebuffer windows are excluded, by capability");
    }

    // ── 4. the reported range is the intersection ────────────────────────
    {
        // What the user needs is the bytes that actually collide. Juke-Box
        // claims $4000-$BFFF; a program at $0280-$2D00 does not reach it, but
        // one at $3000-$5000 collides only on $4000-$5000.
        const Zone zone[] = {{0x3000, 0x5000}};
        const Candidate cands[] = {jukebox()};
        Shadow out[4];
        assert(findShadows(zone, 1, cands, 1, out, 4) == 1);
        assert(out[0].first == 0x4000 && out[0].last == 0x5000);
        // The other direction: a window entirely inside the zone reports the
        // window, not the zone.
        const Zone whole[] = {{0x0000, 0xFFFF}};
        const Candidate rtcOnly[] = {rtc()};
        assert(findShadows(whole, 1, rtcOnly, 1, out, 4) == 1);
        assert(out[0].first == 0x2000 && out[0].last == 0x200F);
        std::puts("  [PASS] 4. the reported range is the intersection");
    }

    // ── 5. many zones, many ranges ───────────────────────────────────────
    {
        // A multi-zone dump — games_chess is $0280 low + $E000 high — against a
        // card with three windows.
        const Zone chess[] = {{0x0280, 0x0FFF}, {0xE000, 0xEFFF}};
        assert(count(chess, 2, nullptr, 0) == 0);
        const Candidate sd[] = {microsd()};
        assert(count(chess, 2, sd, 1) == 0);          // none of its three windows
        const Zone wide[] = {{0x5000, 0x9000}};
        Shadow out[8];
        // $6000-$7FFF and $8000-$9FFF both collide; $A000-$A00F does not.
        assert(findShadows(wide, 1, sd, 1, out, 8) == 2);
        // One report per WINDOW, never one per zone: a window straddling two
        // zones of the same dump is still one thing to tell the user about.
        const Zone split[] = {{0x6000, 0x6100}, {0x6200, 0x6300}};
        assert(findShadows(split, 2, sd, 1, out, 8) == 1);
        // Several cards report several times.
        const Candidate many[] = {rtc(), microsd(), jukebox(), terminal(), gen2()};
        const Zone everything[] = {{0x0000, 0xFFFF}};
        // rtc 1 + microsd 3 + jukebox 2 + terminal 1, gen2 excluded (video).
        assert(findShadows(everything, 1, many, 5, out, 8) == 7);
        std::puts("  [PASS] 5. multi-zone dumps and multi-window cards");
    }

    // ── 6. the cap, and absent input ─────────────────────────────────────
    {
        const Candidate many[] = {rtc(), microsd(), jukebox(), terminal()};
        const Zone everything[] = {{0x0000, 0xFFFF}};
        Shadow small[2];
        // Counts past what it stores, so a caller can say "and N more" rather
        // than silently reporting two of seven.
        assert(findShadows(everything, 1, many, 4, small, 2) == 7);
        assert(small[0].card != CardId::Invalid && small[1].card != CardId::Invalid);
        // Nothing to say about nothing.
        assert(findShadows(nullptr, 0, many, 4, small, 2) == 0);
        assert(findShadows(everything, 1, nullptr, 0, small, 2) == 0);
        assert(findShadows(everything, 0, many, 4, small, 2) == 0);
        // An Invalid row (the flag-only registry rows) is skipped, not crashed on.
        Candidate invalid = rtc();
        invalid.card = CardId::Invalid;
        assert(findShadows(everything, 1, &invalid, 1, small, 2) == 0);
        // A row with a null range table is skipped too.
        Candidate noRanges = rtc();
        noRanges.ranges = nullptr;
        assert(findShadows(everything, 1, &noRanges, 1, small, 2) == 0);
        std::puts("  [PASS] 6. the output cap counts past itself; empty input is inert");
    }

    std::puts("card_shadowing_smoke: all sections passed");
    return 0;
}
