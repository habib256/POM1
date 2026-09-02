// Does a rewind actually replay the beam?
//
// The GEN2's mid-frame soft-switch flips live in a per-frame JOURNAL, and the
// renderer replays that journal to split a frame — vertical bands, horizontal
// mid-scanline splits, DROL-class double buffering. The journal is serialised
// into the snapshot (GEN2VID, v5+) precisely so a restore does not lose them,
// and MemorySnapshot.cpp says so:
//
//     "Without it a beam-split frame restored mid-scene (DROL-class
//      double-buffering, horizontal splits) loses its per-line flips and shows
//      the plain end-of-frame latch until the program flips a switch again."
//
// Nothing tested that claim. The snapshot tests check that a GEN2VID section
// EXISTS and that its name does not collide; the beam tests render splits but
// never save one. So the one thing the serialisation was written for — a rewind
// that replays the beam — was unverified.
//
// Covered:
//   §1  a mid-frame flip is journaled and published at the frame rollover;
//   §2  the journal survives a snapshot round-trip, event for event, with the
//       frame-start state that gives those events meaning;
//   §3  THE PROOF — the restored machine renders pixel-identically;
//   §4  THE CONTROL — dropping the journal changes the picture. Without this,
//       §3 would pass just as happily against a snapshot that lost the journal.

#include "Memory.h"
#include "GraphicsCard.h"
#include "Gen2VideoScanner.h"
#include "M6502.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { \
    std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

constexpr int kW = GraphicsCard::kHiresWidth;
constexpr int kH = GraphicsCard::kHiresHeight;

// Soft switches, Bernie Table 1 ($C250-$C257).
constexpr uint16_t kTextOff = 0xC250, kTextOn = 0xC251;
constexpr uint16_t kPage1   = 0xC254, kHiresOn = 0xC257;

// How far to beam before flipping. Which scanline that lands on is read back
// from the journal, not assumed — see §4.
constexpr int kSplitCycle = 96 * 65;              // 65 CPU cycles per scanline

void paintDistinguishablePages(Memory& m)
{
    uint8_t* ram = m.getMemoryPointerMutable();
    // HGR page 1: every bit set, so graphics rows come out solid white.
    for (int a = 0x2000; a < 0x4000; ++a) ram[a] = 0x7F;
    // Text page 1: $A0 is a NORMAL space, which renders black. $20 would be an
    // INVERSE space — a solid white block, pixel-identical to the HGR fill
    // above, which made the first draft of this test unable to tell the two
    // modes apart. The control in §4 is what caught it.
    for (int a = 0x0400; a < 0x0800; ++a) ram[a] = 0xA0;
}

// A machine whose video phase is deterministic. The GEN2's power-on scanner
// phase is randomised on purpose (silicon fidelity), which makes "flip a switch
// at line N" a different line on every run — the headless CLI path forces the
// same determinism for the same reason.
void bringUpDeterministic(Memory& m)
{
    m.initMemory();
    m.setGen2RandomScannerPhase(false);
    m.setHgrFramebufferAttached(true);
    paintDistinguishablePages(m);
}

std::vector<uint32_t> renderFrom(GraphicsCard& card, const Memory& m,
                                 const std::vector<Gen2VideoScanner::Event>& events)
{
    card.render(m.getMemoryPointer(), m.gen2DisplayState(),
                m.gen2PublishedFrameStartState(), events,
                Gen2VideoScanner::kLinesPerFrame);
    return std::vector<uint32_t>(card.pixels(), card.pixels() + kW * kH);
}

} // namespace

int main()
{
    // -----------------------------------------------------------------
    // §1 Journal a mid-frame split, and let the frame roll over.
    // -----------------------------------------------------------------
    Memory a;
    bringUpDeterministic(a);

    // Start the frame in HIRES graphics, page 1.
    a.memRead(kHiresOn);
    a.memRead(kTextOff);
    a.memRead(kPage1);

    // Beam down to the split line, then flip to TEXT mid-frame. On real
    // hardware everything below that line is text for the rest of the frame.
    a.advanceCycles(kSplitCycle);
    a.memRead(kTextOn);

    // Run out the frame so the recording journal is PUBLISHED. Publication is
    // what the snapshot captures; an unpublished frame is still in flight.
    a.advanceCycles(Gen2VideoScanner::kCyclesPerFrame);

    const std::vector<Gen2VideoScanner::Event> eventsA = a.gen2PublishedVideoEvents();
    CHECK(!eventsA.empty(), "a mid-frame flip must reach the published journal");
    bool sawTextFlip = false;
    for (const auto& e : eventsA)
        if (e.kind == Gen2VideoScanner::EventKind::TextMode && e.value) sawTextFlip = true;
    CHECK(sawTextFlip, "the TEXT_ON flip is in the journal");

    // -----------------------------------------------------------------
    // §2 The journal survives the snapshot, event for event.
    // -----------------------------------------------------------------
    M6502 cpuA(&a);
    const std::vector<uint8_t> blob = a.saveSnapshotToBuffer(&cpuA);
    CHECK(!blob.empty(), "snapshot produced nothing");

    Memory b;
    bringUpDeterministic(b);
    std::string err;
    CHECK(b.loadSnapshotFromBuffer(blob, err, nullptr), err.empty() ? "restore failed" : err.c_str());

    const std::vector<Gen2VideoScanner::Event> eventsB = b.gen2PublishedVideoEvents();
    CHECK(eventsB.size() == eventsA.size(), "the journal came back a different length");
    if (eventsB.size() == eventsA.size()) {
        for (size_t i = 0; i < eventsA.size(); ++i) {
            CHECK(eventsA[i].emuCycle == eventsB[i].emuCycle, "event cycle changed");
            CHECK(eventsA[i].kind     == eventsB[i].kind,     "event kind changed");
            CHECK(eventsA[i].value    == eventsB[i].value,    "event value changed");
        }
    }
    // The events are deltas — without the frame-start state they mean nothing.
    const auto fsA = a.gen2PublishedFrameStartState();
    const auto fsB = b.gen2PublishedFrameStartState();
    CHECK(fsA.textMode  == fsB.textMode  && fsA.mixedMode == fsB.mixedMode &&
          fsA.page2     == fsB.page2     && fsA.hiRes     == fsB.hiRes,
          "the frame-start state must ride along with the events");

    // -----------------------------------------------------------------
    // §3 THE PROOF — the restored machine renders the same picture.
    // -----------------------------------------------------------------
    GraphicsCard cardA, cardB;
    const std::vector<uint32_t> pixA = renderFrom(cardA, a, eventsA);
    const std::vector<uint32_t> pixB = renderFrom(cardB, b, eventsB);
    CHECK(pixA.size() == pixB.size() && !pixA.empty(), "no pixels rendered");
    CHECK(std::memcmp(pixA.data(), pixB.data(), sizeof(uint32_t) * pixA.size()) == 0,
          "the restored frame does not match the original — the beam did not replay");

    // -----------------------------------------------------------------
    // §4 THE CONTROL — the journal is what makes the difference.
    //
    // Render the SAME restored machine with the journal thrown away, as a
    // snapshot that dropped GEN2VID would leave it. The picture must change.
    // Without this check, §3 would pass against a snapshot that lost the
    // journal entirely, and this whole test would be decoration.
    // -----------------------------------------------------------------
    {
        GraphicsCard cardNoJournal;
        const std::vector<uint32_t> pixNone =
            renderFrom(cardNoJournal, b, std::vector<Gen2VideoScanner::Event>{});
        CHECK(pixNone.size() == pixA.size(), "control render produced a different size");
        CHECK(std::memcmp(pixA.data(), pixNone.data(),
                          sizeof(uint32_t) * pixA.size()) != 0,
              "dropping the journal changed nothing — this test cannot fail, "
              "so it proves nothing about the journal");

        // And say WHERE it differs. The split line is DERIVED from the journal
        // rather than assumed: the beam position of the TEXT_ON event is the
        // whole point of the journal, so asserting against a hardcoded line
        // would be asserting against my arithmetic instead of the machine's.
        int splitLine = -1;
        for (const auto& e : eventsA) {
            if (e.kind != Gen2VideoScanner::EventKind::TextMode || !e.value) continue;
            splitLine = GraphicsCard::frameCycleToPos(e.emuCycle).scanline;
        }
        CHECK(splitLine >= 0, "the TEXT_ON event has a beam position");
        if (splitLine > 0 && splitLine < kH - 1) {
            // Above the flip the journal replay and the flat end-state render
            // disagree (one is still HGR, the other already TEXT) — which is
            // exactly the band the journal restores.
            const bool aboveDiff = std::memcmp(pixA.data(), pixNone.data(),
                                               sizeof(uint32_t) * kW * splitLine) != 0;
            CHECK(aboveDiff,
                  "above the flip, the journal shows the pre-flip mode and the "
                  "journal-less render does not — that band IS what is restored");
            // Below the flip both are in the post-flip mode, so they agree.
            const bool belowSame = std::memcmp(pixA.data() + (splitLine + 1) * kW,
                                               pixNone.data() + (splitLine + 1) * kW,
                                               sizeof(uint32_t) * kW * (kH - splitLine - 1)) == 0;
            CHECK(belowSame, "below the flip both renders are in the same mode");
        }
    }

    if (failures) {
        std::fprintf(stderr, "gen2_journal_snapshot_smoke: %d failures\n", failures);
        return 1;
    }
    std::printf("gen2_journal_snapshot_smoke: OK (%zu events round-tripped, "
                "restored frame pixel-identical)\n", eventsA.size());
    return 0;
}
