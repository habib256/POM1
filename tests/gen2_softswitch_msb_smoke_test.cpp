// GEN2 release soft switches ($C250-$C257) + HST0 MSB blank flag.
//
// Phase 2 of the GEN2 beam-racing back-port (TODO.md). Pins Bernie's
// confirmed spec (doc/GEN2_RELEASE_questions.md, all questions RESOLVED
// 2026-06-12):
//
//   * Reads TOGGLE the addressed switch and return HST0 in D7 (low 7 bits
//     are floating-bus noise — masked off here).
//   * Writes are ignored: the latch doesn't move and the byte never lands
//     in RAM (blocked, not pass-through).
//   * Decode SEL = $Cxxx & !A11 & A9 & A4 — mirrors across $C2/$C3/$C6/$C7xx
//     wherever A4 = 1; $C4xx/$C5xx (A9=0) and A4=0 offsets stay plain RAM.
//   * HST0 = 1 while H/V-blank, 0 in live scan, with a 0 notch during the
//     3-cycle color burst (hcnt 13-15) — even inside VBL (ordering is
//     load-bearing in Bernie's reference model).
//   * Apple-1 RESET never touches the latch; the ACI coexists ($C0xx
//     untouched by the GEN2 decode).
//   * Soft-switch flips are journaled and published at every video-frame
//     rollover for the beam-raced renderer.
//
// Needs the full Memory core (PeripheralBus dispatch); run from the repo
// root so Memory's ctor finds roms/.

// Memory.h forward-declares the peripherals it owns by unique_ptr; the
// concrete headers must come first so ~Memory instantiates cleanly here
// (same pattern as memory_dualram_smoke_test.cpp).
#include "A1IO_RTC.h"
#include "PR40Printer.h"
#include "TMS9918.h"
#include "TerminalCard.h"
#include "WiFiModem.h"
#include "Memory.h"
#include "Gen2VideoScanner.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace {

int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { \
    std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

using DS = Gen2VideoScanner::DisplayState;

// Advance the scanner to an absolute frame position (test starts at cycle 0
// after the cold plug resets the counter). Chunked: advanceCycles takes int.
void advanceTo(Memory& mem, uint64_t& cur, uint64_t target)
{
    while (cur < target) {
        const uint64_t step = std::min<uint64_t>(target - cur, 6000);
        mem.advanceCycles(static_cast<int>(step));
        cur += step;
    }
}

} // namespace

int main()
{
    // ── Pure HST0 model (Bernie's hst0_state, verbatim port) ──────────────
    CHECK(Gen2VideoScanner::hst0State(0, 0)   == 1, "HBL start -> 1");
    CHECK(Gen2VideoScanner::hst0State(0, 12)  == 1, "hcnt 12 still HBL -> 1");
    CHECK(Gen2VideoScanner::hst0State(0, 13)  == 0, "burst notch hcnt 13 -> 0");
    CHECK(Gen2VideoScanner::hst0State(0, 15)  == 0, "burst notch hcnt 15 -> 0");
    CHECK(Gen2VideoScanner::hst0State(0, 16)  == 1, "hcnt 16 back in HBL -> 1");
    CHECK(Gen2VideoScanner::hst0State(0, 24)  == 1, "hcnt 24 last HBL -> 1");
    CHECK(Gen2VideoScanner::hst0State(0, 25)  == 0, "hcnt 25 live scan -> 0");
    CHECK(Gen2VideoScanner::hst0State(0, 64)  == 0, "hcnt 64 live scan -> 0");
    CHECK(Gen2VideoScanner::hst0State(191, 40) == 0, "last live line -> 0");
    CHECK(Gen2VideoScanner::hst0State(192, 40) == 1, "VBL line 192 -> 1");
    CHECK(Gen2VideoScanner::hst0State(261, 0)  == 1, "VBL line 261 -> 1");
    CHECK(Gen2VideoScanner::hst0State(200, 14) == 0,
          "burst notch wins even inside VBL (Bernie's ordering)");
    CHECK(Gen2VideoScanner::hst0State(300, 30) == 1, "50 Hz VBL line 300 -> 1");

    // ── Memory-level soft-switch behaviour ────────────────────────────────
    Memory mem;
    mem.setHgrFramebufferAttached(true);   // cold plug: scanner cycle = 0
    uint64_t cur = 0;

    // Documented cold state: GRAPHICS + HIRES + PAGE1, MIX off.
    {
        const DS& ds = mem.gen2DisplayState();
        CHECK(!ds.textMode && ds.hiRes && !ds.page2 && !ds.mixedMode,
              "cold state is GRAPHICS+HIRES+PAGE1");
    }

    // Read toggles + returns HST0 in D7. At cycle 0 we are in HBL -> D7 = 1.
    {
        const uint8_t v = mem.memRead(0xC251);   // TEXT_ON
        CHECK(mem.gen2DisplayState().textMode, "read $C251 sets TEXT");
        CHECK((v & 0x80) == 0x80, "MSB = 1 during HBL (cycle 0)");
    }

    // Write is a no-op: latch unchanged AND the byte never lands in RAM.
    {
        const uint8_t before = mem.getMemoryPointer()[0xC250];
        mem.memWrite(0xC250, 0x42);
        CHECK(mem.gen2DisplayState().textMode, "write $C250 ignored (latch)");
        CHECK(mem.getMemoryPointer()[0xC250] == before,
              "write $C250 blocked (no RAM landing)");
    }

    // Mirrors: A4=1 offsets of $C3xx/$C6xx/$C7xx decode; the same switch
    // mirrors every 8 bytes within a page ($C770 == $C250).
    mem.memRead(0xC350);                       // TEXT_OFF via $C3xx mirror
    CHECK(!mem.gen2DisplayState().textMode, "mirror $C350 clears TEXT");
    mem.memRead(0xC675);                       // PAGE_TWO via $C6xx mirror
    CHECK(mem.gen2DisplayState().page2, "mirror $C675 selects PAGE2");
    mem.memRead(0xC77C);                       // PAGE_ONE ($C77C & 7 == 4)
    CHECK(!mem.gen2DisplayState().page2, "mirror $C77C selects PAGE1");

    // Not decoded: $C4xx/$C5xx (A9=0) and A4=0 offsets are plain RAM.
    {
        mem.memWrite(0xC455, 0x5A);
        CHECK(mem.getMemoryPointer()[0xC455] == 0x5A,
              "$C455 (A9=0) falls through to RAM");
        CHECK(mem.memRead(0xC455) == 0x5A, "$C455 reads back from RAM");
        mem.memWrite(0xC243, 0xA5);            // A4=0 inside $C2xx
        CHECK(mem.getMemoryPointer()[0xC243] == 0xA5,
              "$C243 (A4=0) falls through to RAM");
        const DS ds = mem.gen2DisplayState();
        mem.memRead(0xC243);
        CHECK(ds == mem.gen2DisplayState(), "$C243 read doesn't touch latch");
    }

    // ACI coexistence: with the ACI plugged, $C25x still reaches the GEN2
    // (and only the GEN2 — switch flips, no bus clash).
    mem.setACIEnabled(true);
    mem.memRead(0xC253);                       // MIX_ON
    CHECK(mem.gen2DisplayState().mixedMode, "MIX_ON works with ACI plugged");
    mem.memRead(0xC252);                       // MIX_OFF
    mem.setACIEnabled(false);

    // ── HST0 cadence through the live scanner ─────────────────────────────
    // Move into line 0's live window (hcnt 30): MSB must read 0.
    advanceTo(mem, cur, 30);
    CHECK((mem.memRead(0xC250) & 0x80) == 0x00, "MSB = 0 in live scan");
    // Burst notch of line 1 (cycle 65 + 14).
    advanceTo(mem, cur, 65 + 14);
    CHECK((mem.memRead(0xC250) & 0x80) == 0x00, "MSB = 0 in burst notch");
    // Post-burst H-blank of line 1 (cycle 65 + 20).
    advanceTo(mem, cur, 65 + 20);
    CHECK((mem.memRead(0xC250) & 0x80) == 0x80, "MSB = 1 in HBL after burst");
    // Deep VBL (line 200, hcnt 30).
    advanceTo(mem, cur, 200 * 65 + 30);
    CHECK((mem.memRead(0xC250) & 0x80) == 0x80, "MSB = 1 in VBL");

    // ── Journal publication at the video-frame rollover ───────────────────
    // The reads above were journaled into frame 0; nothing published yet.
    CHECK(mem.gen2PublishedVideoEvents().empty(),
          "no events published before the frame rollover");
    {
        const DS preFlip = mem.gen2DisplayState();
        mem.memRead(0xC255);                   // PAGE_TWO at line 200 (VBL)
        advanceTo(mem, cur, Gen2VideoScanner::kCyclesPerFrame + 10);
        const auto& events = mem.gen2PublishedVideoEvents();
        CHECK(!events.empty(), "rollover publishes the frame's journal");
        bool sawPage2 = false;
        for (const auto& e : events) {
            if (e.kind == Gen2VideoScanner::EventKind::Page2 && e.value)
                sawPage2 = true;
        }
        CHECK(sawPage2, "published journal contains the PAGE_TWO flip");
        CHECK(!mem.gen2PublishedFrameStartState().page2,
              "published frame-start state predates the flip");
        CHECK(mem.gen2DisplayState().page2, "latch holds PAGE2 after rollover");
        (void)preFlip;
    }

    // ── RESET never touches the latch (Bernie Q8) ─────────────────────────
    mem.resetMemory();
    CHECK(mem.gen2DisplayState().page2, "resetMemory leaves the latch alone");

    // ── 50 Hz vertical jumper: line 300 exists and is VBL ─────────────────
    mem.setGen2FiftyHz(true);
    CHECK(mem.isGen2FiftyHz(), "50 Hz jumper round-trips");
    // Scanner counter is monotonic; recompute the current frame position and
    // park at line 300 of a 312-line frame.
    {
        const uint64_t cpf = Gen2VideoScanner::kCyclesPerLine
                           * Gen2VideoScanner::kLinesPerFrame50Hz;
        const uint64_t pos = cur % cpf;
        advanceTo(mem, cur, cur + (cpf - pos) + 300 * 65 + 30);
        CHECK((mem.memRead(0xC250) & 0x80) == 0x80, "50 Hz: line 300 is VBL");
    }
    mem.setGen2FiftyHz(false);

    // ── Unplugged card: the window reverts to plain RAM ───────────────────
    mem.setHgrFramebufferAttached(false);
    mem.memWrite(0xC251, 0x99);
    CHECK(mem.getMemoryPointer()[0xC251] == 0x99,
          "unplugged: $C251 is plain RAM again");

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("gen2_softswitch_msb_smoke: all checks passed\n");
    return 0;
}
