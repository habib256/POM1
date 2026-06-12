// GEN2 beam-raced rendering — horizontal mid-scanline splits.
//
// Bernie's flagship release-card feature (port of POM2's horizontal_split
// test, adapted to the GEN2 $C25x journal): columns of TEXT alternating
// with graphics columns ON THE SAME SCANLINE, keyed off the HST0 flag.
// v1 scope = exact at the byte-column boundary (same as POM2); the
// transition cycle within a character clock is a later refinement.
//
// Self-contained: GraphicsCard + Gen2VideoScanner only, no Memory.

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
constexpr uint64_t kCpl = Gen2VideoScanner::kCyclesPerLine;

DS hgrState()  { DS d; d.textMode = false; d.hiRes = true;  return d; }
DS textState() { DS d; d.textMode = true;  d.hiRes = false; return d; }

std::vector<uint32_t> renderFrame(GraphicsCard& card, const uint8_t* mem,
                                  const DS& end, const DS& start,
                                  const std::vector<Event>& events)
{
    card.render(mem, end, start, events);
    return std::vector<uint32_t>(card.pixels(), card.pixels() + kW * kH);
}

bool spanEqual(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b,
               int y, int x0, int x1)
{
    return std::memcmp(a.data() + y * kW + x0, b.data() + y * kW + x0,
                       sizeof(uint32_t) * (x1 - x0)) == 0;
}

// Cycle of (scanline y, byte column c): the visible window opens at
// horizontal cycle 25 (frameCycleToPos: byteCol = clamp(hpos - 25, 0, 40)).
uint64_t beamCycle(int y, int col) { return static_cast<uint64_t>(y) * kCpl + 25 + col; }

} // namespace

int main()
{
    // frameCycleToPos sanity — the recorder/replay mapping.
    {
        const auto hbl = GraphicsCard::frameCycleToPos(50 * kCpl + 10);
        CHECK(hbl.scanline == 50 && hbl.byteCol == 0, "HBL cycle -> byteCol 0");
        const auto mid = GraphicsCard::frameCycleToPos(beamCycle(50, 20));
        CHECK(mid.scanline == 50 && mid.byteCol == 20, "cycle 25+20 -> byteCol 20");
        const auto vbl = GraphicsCard::frameCycleToPos(200 * kCpl + 30);
        CHECK(vbl.scanline == kH, "VBL line collapses to 192");
        const auto pal = GraphicsCard::frameCycleToPos(
            300 * kCpl + 30, Gen2VideoScanner::kLinesPerFrame50Hz);
        CHECK(pal.scanline == kH, "50 Hz line 300 collapses to 192");
    }

    // Test memory: HGR page 1 stripes; text page 1 = inverse spaces (white).
    std::vector<uint8_t> mem(0x10000, 0);
    for (int i = 0; i < 0x2000; ++i) mem[0x2000 + i] = (i & 1) ? 0x55 : 0x2A;
    for (int i = 0; i < 0x0400; ++i) mem[0x0400 + i] = 0x20;

    GraphicsCard card;
    const auto refHgr  = renderFrame(card, mem.data(), hgrState(),  hgrState(),  {});
    const auto refText = renderFrame(card, mem.data(), textState(), textState(), {});

    // ── Single-line horizontal split: TEXT_ON at (y=50, col=20), back to
    //    graphics at the start of line 51 (HBL). Left half HGR, right half
    //    TEXT, neighbours untouched. Column 20 = pixel 140.
    {
        std::vector<Event> events = {
            { beamCycle(50, 20),  Kind::TextMode, true  },
            { 51 * kCpl,          Kind::TextMode, false },
        };
        const auto split = renderFrame(card, mem.data(), hgrState(), hgrState(), events);
        CHECK(spanEqual(split, refHgr, 49, 0, kW),    "line 49 untouched (HGR)");
        CHECK(spanEqual(split, refHgr, 50, 0, 140),   "line 50 left = HGR");
        CHECK(spanEqual(split, refText, 50, 140, kW), "line 50 right = TEXT");
        CHECK(spanEqual(split, refHgr, 51, 0, kW),    "line 51 untouched (HGR)");
        CHECK(!spanEqual(split, refHgr, 50, 140, kW),
              "line 50 right really changed");
    }

    // ── Repeating per-scanline split over a band (the Codebreaker "color
    //    peg" pattern): TEXT columns 20-39 on every line of [80, 90). ──────
    {
        std::vector<Event> events;
        for (int y = 80; y < 90; ++y) {
            events.push_back({ beamCycle(y, 20), Kind::TextMode, true  });
            events.push_back({ (static_cast<uint64_t>(y) + 1) * kCpl,
                               Kind::TextMode, false });
        }
        const auto split = renderFrame(card, mem.data(), hgrState(), hgrState(), events);
        for (int y = 80; y < 90; ++y) {
            CHECK(spanEqual(split, refHgr, y, 0, 140),   "band line left = HGR");
            CHECK(spanEqual(split, refText, y, 140, kW), "band line right = TEXT");
        }
        CHECK(spanEqual(split, refHgr, 79, 0, kW), "line 79 untouched");
        CHECK(spanEqual(split, refHgr, 90, 0, kW), "line 90 untouched");
    }

    // ── HGR artifact context across the split: decoding the whole line and
    //    clipping the write-back means the HGR left half is bit-identical to
    //    the full-frame reference even at the boundary byte. ───────────────
    {
        std::vector<Event> events = {
            { beamCycle(100, 1),  Kind::TextMode, true  },
            { 101 * kCpl,         Kind::TextMode, false },
        };
        const auto split = renderFrame(card, mem.data(), hgrState(), hgrState(), events);
        CHECK(spanEqual(split, refHgr, 100, 0, 7),
              "boundary byte keeps full-line artifact context");
        CHECK(spanEqual(split, refText, 100, 7, kW), "rest of line = TEXT");
    }

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("gen2_horizontal_split_smoke: all checks passed\n");
    return 0;
}
