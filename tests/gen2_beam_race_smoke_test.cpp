// GEN2 beam-raced rendering — vertical splits + display modes.
//
// Phase 3 of the GEN2 beam-racing back-port (port of POM2's
// beam_race_composite test, LUT path, adapted to the GEN2 $C25x journal).
// Self-contained: GraphicsCard + Gen2VideoScanner only, no Memory.
//
// Pins:
//   * Mode renders — TEXT (B&W glyphs), LORES (16-colour blocks), HIRES,
//     MIXED (bottom 4 text rows), PAGE2 ($4000 HGR / $0800 text).
//   * Vertical beam split: a TEXT_ON flip journaled at scanline 96 renders
//     HGR rows above and TEXT rows below — pixel-identical to the
//     corresponding full-frame references.
//   * One-direction PAGE2 flips apply frame-wide (double-buffer heuristic).

#include "GraphicsCard.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { \
    std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

using DS    = GraphicsCard::DisplayState;
using Event = GraphicsCard::Event;
using Kind  = GraphicsCard::EventKind;

constexpr int kW = GraphicsCard::kHiresWidth;
constexpr int kH = GraphicsCard::kHiresHeight;

DS hgrState()  { DS d; d.textMode = false; d.hiRes = true;  return d; }
DS textState() { DS d; d.textMode = true;  d.hiRes = false; return d; }
DS loresState(){ DS d; d.textMode = false; d.hiRes = false; return d; }

// Render under `end`/`start`/`events` and snapshot the pixel buffer.
std::vector<uint32_t> renderFrame(GraphicsCard& card, const uint8_t* mem,
                                  const DS& end, const DS& start,
                                  const std::vector<Event>& events)
{
    card.render(mem, end, start, events);
    return std::vector<uint32_t>(card.pixels(), card.pixels() + kW * kH);
}

bool rowsEqual(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b,
               int y0, int y1)
{
    return std::memcmp(a.data() + y0 * kW, b.data() + y0 * kW,
                       sizeof(uint32_t) * kW * (y1 - y0)) == 0;
}

} // namespace

int main()
{
    // 64 KB test memory: HGR page 1 = a recognisable stripe pattern, HGR
    // page 2 = a different one; text page 1 filled with inverse spaces
    // (solid white cells), text page 2 with normal spaces (black cells).
    std::vector<uint8_t> mem(0x10000, 0);
    for (int i = 0; i < 0x2000; ++i) mem[0x2000 + i] = (i & 1) ? 0x55 : 0x2A;
    for (int i = 0; i < 0x2000; ++i) mem[0x4000 + i] = 0x7F;
    for (int i = 0; i < 0x0400; ++i) mem[0x0400 + i] = 0x20;   // inverse ' '
    for (int i = 0; i < 0x0400; ++i) mem[0x0800 + i] = 0xA0;   // normal ' '

    GraphicsCard card;

    // ── Full-frame mode references ─────────────────────────────────────────
    const auto refHgr   = renderFrame(card, mem.data(), hgrState(),  hgrState(),  {});
    const auto refText  = renderFrame(card, mem.data(), textState(), textState(), {});
    DS hgr2 = hgrState(); hgr2.page2 = true;
    const auto refHgrP2 = renderFrame(card, mem.data(), hgr2, hgr2, {});
    const auto refLores = renderFrame(card, mem.data(), loresState(), loresState(), {});

    CHECK(!rowsEqual(refHgr, refText, 0, kH), "TEXT render differs from HGR");
    CHECK(!rowsEqual(refHgr, refHgrP2, 0, kH), "HGR page 2 differs from page 1");
    CHECK(!rowsEqual(refLores, refText, 0, kH), "LORES differs from TEXT");

    // TEXT page 1 = inverse spaces = solid white rows (B&W per Bernie).
    {
        bool allWhite = true;
        for (int x = 0; x < kW; ++x)
            if ((refText[x] & 0x00FFFFFFu) != 0x00FFFFFFu) allWhite = false;
        CHECK(allWhite, "inverse-space TEXT row renders white (B&W)");
        DS text2 = textState(); text2.page2 = true;
        const auto refText2 = renderFrame(card, mem.data(), text2, text2, {});
        bool allBlack = true;
        for (int x = 0; x < kW; ++x)
            if ((refText2[x] & 0x00FFFFFFu) != 0) allBlack = false;
        CHECK(allBlack, "normal-space TEXT page 2 renders black");
    }

    // MIXED HGR: top 160 lines = HGR reference, bottom 32 = TEXT reference.
    {
        DS mixed = hgrState(); mixed.mixedMode = true;
        const auto refMixed = renderFrame(card, mem.data(), mixed, mixed, {});
        CHECK(rowsEqual(refMixed, refHgr, 0, 160), "MIXED top 160 = HGR");
        CHECK(rowsEqual(refMixed, refText, 160, kH), "MIXED bottom 32 = TEXT");
    }

    // ── Vertical beam split: TEXT_ON journaled at scanline 96, HBL ────────
    {
        std::vector<Event> events = {
            { 96 * Gen2VideoScanner::kCyclesPerLine, Kind::TextMode, true },
        };
        const auto split = renderFrame(card, mem.data(), textState(), hgrState(), events);
        CHECK(rowsEqual(split, refHgr, 0, 96),   "split: rows 0-95 = HGR");
        CHECK(rowsEqual(split, refText, 96, kH), "split: rows 96-191 = TEXT");
    }

    // ── One-direction PAGE2 flip = double-buffer flip, applied frame-wide ─
    {
        std::vector<Event> events = {
            { 96 * Gen2VideoScanner::kCyclesPerLine, Kind::Page2, true },
        };
        const auto flip = renderFrame(card, mem.data(), hgr2, hgrState(), events);
        CHECK(rowsEqual(flip, refHgrP2, 0, kH),
              "one-direction PAGE2 flip applies to the whole frame");
    }

    // ── Both-direction PAGE2 flips = real beam-raced page split ───────────
    {
        std::vector<Event> events = {
            {  64 * Gen2VideoScanner::kCyclesPerLine, Kind::Page2, true  },
            { 128 * Gen2VideoScanner::kCyclesPerLine, Kind::Page2, false },
        };
        const auto split = renderFrame(card, mem.data(), hgrState(), hgrState(), events);
        CHECK(rowsEqual(split, refHgr,   0,  64), "page split: top = page 1");
        CHECK(rowsEqual(split, refHgrP2, 64, 128), "page split: middle = page 2");
        CHECK(rowsEqual(split, refHgr, 128,  kH), "page split: bottom = page 1");
    }

    // ── VBL events (scanline >= 192) don't disturb the visible frame ──────
    {
        std::vector<Event> events = {
            { 200 * Gen2VideoScanner::kCyclesPerLine, Kind::TextMode, true },
        };
        const auto vbl = renderFrame(card, mem.data(), hgrState(), hgrState(), events);
        CHECK(rowsEqual(vbl, refHgr, 0, kH), "VBL-only event leaves frame = HGR");
    }

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("gen2_beam_race_smoke: all checks passed\n");
    return 0;
}
