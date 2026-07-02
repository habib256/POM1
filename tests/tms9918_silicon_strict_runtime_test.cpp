// TMS9918 Silicon Strict (siliconStrictMode) runtime test — openMSX slot-table model.
//
// Pins the silicon-strict drop behaviour after the port from min-distance threshold
// to openMSX's `slotsMsx1*` slot-table model (cf. sketchs/doc/Programming_TMS9918.md §17 Bug N°1 and
// the comment block in TMS9918.cpp above the slot tables).
//
// What's covered:
//   Phase A  tolerant mode (strict=false)            — bursts always land
//   Phase B  Gfx12 pure slot-table gating            — 4c drops / 8c lands (no floor)
//   Phase C  unpadded 4c burst in Mode I+sprites     — partial drops (mixed)
//   Phase D  text-mode tight loop @ 5c gap           — accept (Text table dense)
//   Phase E  multicolor @ 6c gap                     — accept (Gfx3 worst ~4.2c)
//   Phase F  display blanked flood @ 2c              — accept (ScreenOff slot/8t)
//   Phase G  VBlank flood, display ON @ 2c           — accept (VBlank → ScreenOff)
//   Phase H  strict toggle non-destructive (no reset)
//   Phase I  slot-table phasing (different starting frameCycleCounter → different
//            drain → silicon-correct phase dependency)
//   Phase J  drop diagnostics: per-PC + per-port + per-table aggregation
//   Phase K  newest-wins collision (openMSX deferred-overwrite source of truth)
//
// References:
//   - TMS9918.cpp `slotsMsx1*` arrays (verbatim from openMSX VDPAccessSlots.cc)
//   - sketchs/doc/Programming_TMS9918.md §17 "Bug N°1 — VRAM access timing (slot-table model)"

#include "TMS9918.h"

#include <cstdint>
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

// Cushion = 30c. openMSX worst-case Gfx12 = D28+128 ticks = 156 ticks ≈ 7.5c;
// 30c covers all modes with a 4× margin. The cushion is applied BEFORE and
// AFTER each cushioned call so callers can chain them and finish with a
// drained chip (pendingDrainCycles == 0) — without that post-drain, a
// `runWriteLoop` immediately following a `cushionedSetWriteAddress` would
// spuriously drop on its first iteration.
constexpr int kCushion = 30;

void cushionedWriteControl(TMS9918& vdp, uint8_t value)
{
    vdp.advanceCycles(kCushion);
    vdp.writeControl(value);
    vdp.advanceCycles(kCushion);              // drain post-write so caller is fresh
}

void cushionedSetWriteAddress(TMS9918& vdp, uint16_t addr)
{
    cushionedWriteControl(vdp, (uint8_t)(addr & 0xFF));
    cushionedWriteControl(vdp, (uint8_t)(0x40 | ((addr >> 8) & 0x3F)));
}

void cushionedSetReadAddress(TMS9918& vdp, uint16_t addr)
{
    cushionedWriteControl(vdp, (uint8_t)(addr & 0xFF));
    cushionedWriteControl(vdp, (uint8_t)((addr >> 8) & 0x3F));
}

uint8_t cushionedReadVramAt(TMS9918& vdp, uint16_t addr)
{
    cushionedSetReadAddress(vdp, addr);
    vdp.advanceCycles(kCushion);
    return vdp.readData();
}

// Set R1 = value via two-byte $CC01 sequence (data + 0x81 register select).
void setReg1(TMS9918& vdp, uint8_t value)
{
    cushionedWriteControl(vdp, value);
    cushionedWriteControl(vdp, 0x81);
}

// Configure VDP in Mode 0 (Graphic I) with display on. Active slot table = Gfx12.
void initGfxMode(TMS9918& vdp)
{
    setReg1(vdp, 0xC0);                       // 16K, display ON, no sprite mag, no IRQ
}

// Configure VDP in Mode 1 (Text). Active slot table = Text.
void initTextMode(TMS9918& vdp)
{
    setReg1(vdp, 0xD0);                       // 16K, display ON, M1=1
}

// Configure VDP in Mode 3 (Multicolor). Active slot table = Gfx3.
void initMulticolorMode(TMS9918& vdp)
{
    setReg1(vdp, 0xC8);                       // 16K, display ON, M2=1
}

// Configure VDP with display blanked (R1.6 = 0). Active slot table = ScreenOff.
void initBlankedMode(TMS9918& vdp)
{
    setReg1(vdp, 0x80);                       // 16K, display OFF
}

// Reset stats but keep mode + strict flag untouched. Returns total drops since
// last reset window.
struct DropWindow {
    TMS9918* vdp;
    uint64_t baseline;
    DropWindow(TMS9918& v) : vdp(&v), baseline(v.dropDiagnostics().total) {}
    uint64_t drops() const { return vdp->dropDiagnostics().total - baseline; }
};

// Run a tight loop of N writeData(value) separated by `gapCycles` of advanceCycles.
// Returns drops accumulated during the loop.
uint64_t runWriteLoop(TMS9918& vdp, int n, int gapCycles, uint8_t firstValue = 0xA0)
{
    DropWindow w(vdp);
    for (int i = 0; i < n; ++i) {
        vdp.writeData(static_cast<uint8_t>(firstValue + i));
        if (i + 1 < n) vdp.advanceCycles(gapCycles);
    }
    return w.drops();
}

} // namespace

int main()
{
    int assertions = 0;

    // ----------------------------------------------------------------------
    // Phase A — tolerant mode (default after reset). Spam 4 bytes back-to-back.
    // All four MUST land regardless of model.
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp; vdp.reset();
        vdp.setSiliconStrictMode(false);
        initGfxMode(vdp);
        cushionedSetWriteAddress(vdp, 0x1000);
        vdp.writeData(0xA1);
        vdp.writeData(0xA2);
        vdp.writeData(0xA3);
        vdp.writeData(0xA4);

        mustBeTrue(cushionedReadVramAt(vdp, 0x1000) == 0xA1, "PhaseA: byte 0 lands"); ++assertions;
        mustBeTrue(cushionedReadVramAt(vdp, 0x1001) == 0xA2, "PhaseA: byte 1 lands"); ++assertions;
        mustBeTrue(cushionedReadVramAt(vdp, 0x1002) == 0xA3, "PhaseA: byte 2 lands"); ++assertions;
        mustBeTrue(cushionedReadVramAt(vdp, 0x1003) == 0xA4, "PhaseA: byte 3 lands"); ++assertions;
        mustBeTrue(vdp.dropDiagnostics().total == 0, "PhaseA: zero drops in tolerant mode"); ++assertions;
    }

    // ----------------------------------------------------------------------
    // Phase B — Gfx12 active-display gating, PURE openMSX slot-table timing
    // (no artificial floor — POM1 now follows openMSX exactly: D28 prep + the
    // slotsMsx1Gfx12 access slots decide everything). At this start position a
    // 4c burst out-paces the slots and drops; an 8c burst clears the worst
    // Gfx12 slot gap and lands. The lib pad helper gives 18c (22c STA->STA),
    // far above any Gfx12 slot gap. Gfx12-scoped: Text/Multicolor stay dense
    // (Phases D/E below accept their tighter loops).
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp; vdp.reset();
        initGfxMode(vdp);
        vdp.setSiliconStrictMode(true);
        cushionedSetWriteAddress(vdp, 0x1100);
        const uint64_t drops = runWriteLoop(vdp, 8, 4, 0xB0);
        mustBeTrue(drops > 0, "PhaseB: 8 STA at 4c gap in Gfx12 — below the slot spacing, must drop"); ++assertions;
    }
    {
        // 8c gap clears the worst Gfx12 access-slot gap at this phase — writes
        // must land. This is the openMSX-faithful boundary (the removed POM1
        // 9c floor used to push it one cycle higher).
        TMS9918 vdp; vdp.reset();
        initGfxMode(vdp);
        vdp.setSiliconStrictMode(true);
        cushionedSetWriteAddress(vdp, 0x1100);
        const uint64_t drops = runWriteLoop(vdp, 8, 8, 0xB0);
        mustBeTrue(drops == 0, "PhaseB2: 8 STA at 8c gap in Gfx12 — above the slot gap, must accept all"); ++assertions;
    }

    // ----------------------------------------------------------------------
    // Phase C — a raw unpadded 4c burst in Mode I+sprites must produce *some*
    // drops: 4c is below the 9c Gfx12 floor, so a 4c-only loop cannot sustain.
    // This is NOT how the shipped games write — TMS_Galaga (and the rest of the
    // lib) pad every back-to-back VDP store with tms9918_pad18 (22c STA->STA),
    // which clears the floor with margin (Phase I pins the 18c-gap case). Phase C
    // just pins that the floor still catches a genuinely under-padded stream.
    // Loose assertion (drop count > 0 and < N) — the slot pattern is bursty, so
    // a precise count would over-pin the model.
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp; vdp.reset();
        initGfxMode(vdp);
        vdp.setSiliconStrictMode(true);
        cushionedSetWriteAddress(vdp, 0x1200);
        const int n = 40;
        const uint64_t drops = runWriteLoop(vdp, n, 4, 0xC0);
        mustBeTrue(drops > 0,
                   "PhaseC: 40 STA at 4c gap (unpadded burst) MUST drop at least once"); ++assertions;
        mustBeTrue(drops < (uint64_t)n,
                   "PhaseC: bursty silicon model — not every byte should drop"); ++assertions;
        mustBeTrue(vdp.dropDiagnostics().byTable[TMS9918::kSlotTableGfx12] == drops,
                   "PhaseC: drops attributed to Gfx12 table"); ++assertions;
        mustBeTrue(vdp.dropDiagnostics().writeData == drops,
                   "PhaseC: drops attributed to data port"); ++assertions;
    }

    // ----------------------------------------------------------------------
    // Phase D — text mode tight loop. Text table has dense slots (~8 tick gap
    // for the first half of the line). 5c gap (≈ 105 ticks) is comfortably
    // above the worst Text gap, so all writes must land.
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp; vdp.reset();
        initTextMode(vdp);
        vdp.setSiliconStrictMode(true);
        cushionedSetWriteAddress(vdp, 0x1300);
        const uint64_t drops = runWriteLoop(vdp, 16, 5, 0xD0);
        mustBeTrue(drops == 0, "PhaseD: 16 STA at 5c gap in text mode must accept all"); ++assertions;
    }

    // ----------------------------------------------------------------------
    // Phase E — multicolor 6c gap. Gfx3 worst-case slot gap is 88 ticks ≈ 4.2c
    // → 6c (≈ 126 ticks) is silicon-safe.
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp; vdp.reset();
        initMulticolorMode(vdp);
        vdp.setSiliconStrictMode(true);
        cushionedSetWriteAddress(vdp, 0x1400);
        const uint64_t drops = runWriteLoop(vdp, 16, 6, 0xE0);
        mustBeTrue(drops == 0, "PhaseE: 16 STA at 6c gap in multicolor must accept all"); ++assertions;
    }

    // ----------------------------------------------------------------------
    // Phase F — display blanked flood. R1.6=0 selects ScreenOff table, slots
    // every 8 ticks (~0.4c) for the bulk of the line. The minimum drain
    // observable is ceil(28-tick D28 / 21) = 2c (chip prep dominates), so
    // 2c gap is the silicon-tight floor for blanked-flood — the documented
    // cold-boot init-VRAM idiom (R1.6=0; massive STA loop; R1.6=1).
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp; vdp.reset();
        initBlankedMode(vdp);
        vdp.setSiliconStrictMode(true);
        cushionedSetWriteAddress(vdp, 0x1500);
        const uint64_t drops = runWriteLoop(vdp, 256, 2, 0x00);
        mustBeTrue(drops == 0, "PhaseF: 256 STA at 2c gap blanked flood must accept all"); ++assertions;
    }

    // ----------------------------------------------------------------------
    // Phase G — VBlank flood with display ON. POM1 deliberately diverges
    // from openMSX's stock `getTab` here: during vertical retrace the chip
    // doesn't pixel-scan, so CPU bandwidth is silicon-correctly FREE even
    // with display ON. This matches the documented init-VRAM-in-VBlank
    // idiom (BIT $CC01 / BPL / massive STA loop). Pins the relaxation
    // implemented in TMS9918::selectSlotTable when frameCycle >=
    // kActiveDisplayCycles.
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp; vdp.reset();
        initGfxMode(vdp);                     // display ON, Mode I
        vdp.advanceCycles(13000);             // push beyond kActiveDisplayCycles
        vdp.setSiliconStrictMode(true);
        cushionedSetWriteAddress(vdp, 0x1600);
        const int n = 64;
        const uint64_t drops = runWriteLoop(vdp, n, 2, 0xF0);
        mustBeTrue(drops == 0,
                   "PhaseG: 64 STA @2c in VBlank-with-display-ON must accept all "
                   "(VBlank relaxes to ScreenOff — silicon free-bandwidth idiom)"); ++assertions;
    }

    // ----------------------------------------------------------------------
    // Phase H — strict toggle non-destructive. Toggle on→off should restore
    // tolerant behaviour without a chip reset. The Hardware menu / CLI
    // override depends on this for live A/B against silicon (Galaga workflow).
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp; vdp.reset();
        initGfxMode(vdp);
        vdp.setSiliconStrictMode(true);
        cushionedSetWriteAddress(vdp, 0x1700);
        runWriteLoop(vdp, 8, 4, 0x10);        // some drops expected
        const uint64_t dropsStrict = vdp.dropDiagnostics().total;
        mustBeTrue(dropsStrict > 0, "PhaseH: strict run produced drops"); ++assertions;

        vdp.setSiliconStrictMode(false);      // toggle off → counters reset, mode tolerant
        mustBeTrue(vdp.dropDiagnostics().total == 0, "PhaseH: toggle to false resets stats"); ++assertions;

        cushionedSetWriteAddress(vdp, 0x1800);
        const uint64_t dropsTolerant = runWriteLoop(vdp, 8, 4, 0x20);
        mustBeTrue(dropsTolerant == 0, "PhaseH: tolerant mode after toggle accepts all"); ++assertions;

        // VRAM consistency check: the tolerant write must have placed all 8 bytes.
        for (int i = 0; i < 8; ++i) {
            const uint8_t expect = (uint8_t)(0x20 + i);
            const uint8_t got    = cushionedReadVramAt(vdp, (uint16_t)(0x1800 + i));
            mustBeTrue(got == expect, "PhaseH: tolerant byte landed in VRAM"); ++assertions;
        }
    }

    // ----------------------------------------------------------------------
    // Phase I — the lib pad18 cushion (18c gap) is always safe regardless of
    // phase. Above the 9c Gfx12 floor, so it covers any starting linePos.
    // Two runs with shifted starting frameCycleCounter must both accept all.
    // Pins that the silicon floor leaves the documented pad contract valid.
    // ----------------------------------------------------------------------
    {
        for (int phaseShift : {0, 137, 421, 1009}) {
            TMS9918 vdp; vdp.reset();
            initGfxMode(vdp);
            vdp.advanceCycles(phaseShift);    // shift line position
            vdp.setSiliconStrictMode(true);
            cushionedSetWriteAddress(vdp, (uint16_t)(0x1900 + phaseShift));
            const uint64_t drops = runWriteLoop(vdp, 8, 18, 0xA0);
            mustBeTrue(drops == 0,
                       "PhaseI: 18c pad18 gap is silicon-safe at any starting phase"); ++assertions;
        }
    }

    // ----------------------------------------------------------------------
    // Phase J — drop diagnostics aggregation + SILICON control-port
    // semantics (re-pinned juillet 2026). On real TMS9918A silicon the
    // control port is an internal latch: register/address writes are NEVER
    // dropped however fast they arrive (openMSX applies them immediately
    // too), and the byte flip-flop therefore can never desync from the
    // hardware's. Only data-port traffic rides the VRAM slot machinery.
    // (The old POM1 "Barrier" gating dropped gated control bytes — a
    // dropped FIRST byte desynced the flip-flop versus silicon, making
    // every later control pair decode differently from the real chip.)
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp; vdp.reset();
        initGfxMode(vdp);
        vdp.setSiliconStrictMode(true);
        cushionedSetWriteAddress(vdp, 0x1B00);

        // Distinct "PCs" via setLastAccessPc — emulates Memory's
        // mid-instruction PC stamp before each VDP access.
        for (int i = 0; i < 8; ++i) {
            vdp.setLastAccessPc(0x5000);
            vdp.writeData((uint8_t)i);
            vdp.advanceCycles(2);             // tight enough to drop
        }
        // Back-to-back $CC01 register writes with ZERO breathing room:
        // silicon latches every byte. Write R7 = $0B via a 2c-apart pair,
        // then verify the register really changed (never dropped, flip-flop
        // in phase).
        vdp.setLastAccessPc(0x6000);
        vdp.writeControl(0x0B);               // 1st byte: value
        vdp.advanceCycles(2);
        vdp.writeControl(0x87);               // 2nd byte: write R7
        vdp.advanceCycles(2);

        const auto& d = vdp.dropDiagnostics();
        mustBeTrue(d.total == d.writeData + d.readData + d.writeCtrl,
                   "PhaseJ: total = writeData + readData + writeCtrl"); ++assertions;
        mustBeTrue(d.writeData > 0,
                   "PhaseJ: data-port drops recorded"); ++assertions;
        mustBeTrue(d.writeCtrl == 0,
                   "PhaseJ: control-port writes NEVER drop (silicon: internal latch)"); ++assertions;
        mustBeTrue(vdp.regsData()[7] == 0x0B,
                   "PhaseJ: the 2c-apart register write landed (R7 = $0B)"); ++assertions;
        mustBeTrue(d.byPc.count(0x5000) > 0 && d.byPc.count(0x6000) == 0,
                   "PhaseJ: byPc histogram has the data site only"); ++assertions;
        mustBeTrue(d.byTable[TMS9918::kSlotTableGfx12] == d.total,
                   "PhaseJ: all drops in Gfx12 table"); ++assertions;
        mustBeTrue(d.inActive + d.inVBlank == d.total,
                   "PhaseJ: active+vblank partitioning sums to total"); ++assertions;

        // Smoke-test dump (silence by writing to /dev/null-equivalent).
        std::FILE* sink = std::tmpfile();
        if (sink) {
            vdp.dumpDropDiagnostics(sink, 4);
            std::fclose(sink);
        }
    }

    // ----------------------------------------------------------------------
    // Phase K — NEWEST-WINS collision semantics (openMSX source of truth).
    // openMSX (allowTooFastAccess=off): a too-fast access OVERWRITES the still-
    // pending request with the newer data (cpuVramData is shared) and the
    // single scheduled slot fires once with that newer byte. So of two
    // collided writes to the same cell, the LATER value lands and the pointer
    // advances exactly once. (POM1 previously kept the OLDER byte — the old
    // "drop the new write" model. This pins the corrected behaviour.)
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp; vdp.reset();
        initGfxMode(vdp);                          // display ON, Gfx12 (gated)
        vdp.setSiliconStrictMode(true);
        cushionedSetWriteAddress(vdp, 0x1A00);
        vdp.writeData(0x11);                       // schedules at the next slot
        vdp.advanceCycles(2);                      // < slot gap -> collision
        vdp.writeData(0x22);                       // newer byte overwrites pending
        const uint64_t drops = vdp.dropDiagnostics().total;
        vdp.advanceCycles(60);                     // let the slot fire
        mustBeTrue(drops > 0, "PhaseK: the 2c-apart second write registered a too-fast event"); ++assertions;
        mustBeTrue(cushionedReadVramAt(vdp, 0x1A00) == 0x22,
                   "PhaseK: newest-wins — the LATER byte ($22) is what landed in VRAM"); ++assertions;
        mustBeTrue(cushionedReadVramAt(vdp, 0x1A01) == 0x00,
                   "PhaseK: the pointer advanced only once — $1A01 untouched"); ++assertions;
    }

    // ----------------------------------------------------------------------
    // Phase L — a too-fast DATA-PORT READ is classified as readData, not
    // writeCtrl. The detector tracks data-write / data-read / control-write
    // separately (openMSX shares cpuVramData across read+write; POM1 keeps the
    // three drop kinds distinct).
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp; vdp.reset();
        initGfxMode(vdp);
        vdp.setSiliconStrictMode(true);
        cushionedSetReadAddress(vdp, 0x1000);
        vdp.advanceCycles(44);
        (void)vdp.readData();                 // schedules a prefetch (pending)
        (void)vdp.readData();                 // too fast -> readData drop
        const auto& d = vdp.dropDiagnostics();
        mustBeTrue(d.readData > 0, "PhaseL: too-fast read counted in readData"); ++assertions;
        mustBeTrue(d.writeCtrl == 0, "PhaseL: a read drop is NOT mis-counted as a control drop"); ++assertions;
    }

    // Sanity: getter must mirror the last setter call.
    {
        TMS9918 vdp;
        vdp.setSiliconStrictMode(true);
        mustBeTrue(vdp.isSiliconStrictMode(), "isSiliconStrictMode reflects setter (true)"); ++assertions;
        vdp.setSiliconStrictMode(false);
        mustBeTrue(!vdp.isSiliconStrictMode(), "isSiliconStrictMode reflects setter (false)"); ++assertions;
    }

    std::printf("tms9918_silicon_strict_runtime: all %d assertions passed\n", assertions);
    return 0;
}
