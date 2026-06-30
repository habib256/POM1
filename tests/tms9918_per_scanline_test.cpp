// Per-scanline sprite scan + overscan collision + 59.94 Hz frame test.
//
// Pins the silicon-correct timing of statusReg bits 5/6/0..4 implemented
// in TMS9918::scanSpritesForLine (port of openMSX SpriteChecker::checkSprites1
// at line-major granularity). Cf. sketchs/doc/Programming_TMS9918.md §11/§12/§13/§21 (Bug N°4/N°5/N°6/N°10/N°11).
//
// Strict mode stays OFF for this test (the default after reset) so VDP
// register/data writes land immediately without needing cushioning
// advanceCycles. That way the frame counter only moves when WE call
// advanceCycles explicitly — giving us precise control over the simulated
// raster position.
//
// Phases:
//   A — mid-frame collision (Bug N°5)        : bit 5 must arm at the line crossing,
//                                              not 16 ms later at VBlank.
//   B — 5S raster split  (Bug N°10)          : bit 6 + low-5 bits must arm at the
//                                              5-sprite scanline (Y=95, line 95).
//   C — last sprite scanned (Bug N°6)        : bits 0..4 reflect the last SAT
//                                              entry the line-walker reached.
//   D — overscan collision (Bug N°4)         : two early-clock sprites at X=10
//                                              (real X=-22) collide off-screen.
//   E — 59.94 Hz NTSC (Bug N°11)             : kCyclesPerFrame == 17073.

#include "TMS9918.h"
#include "CpuClock.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

void mustBeTrue(bool cond, const char* msg)
{
    if (!cond) {
        std::fprintf(stderr, "ASSERT FAILED: %s\n", msg);
        std::exit(1);
    }
}

// VDP control 2-byte sequence: data byte, then 0x80 | regnum.
void writeReg(TMS9918& vdp, uint8_t regNum, uint8_t value)
{
    vdp.writeControl(value);
    vdp.writeControl((uint8_t)(0x80 | (regNum & 0x07)));
}

// Set VRAM write address (2-byte command 0x40 | high).
void setWriteAddress(TMS9918& vdp, uint16_t addr)
{
    vdp.writeControl((uint8_t)(addr & 0xFF));
    vdp.writeControl((uint8_t)(0x40 | ((addr >> 8) & 0x3F)));
}

// Configure VDP: Mode 0 (Graphic I), display ON, 16K, sprites 8x8.
// SAT base = $1B00 (R5 = $36).
void initVdp(TMS9918& vdp)
{
    writeReg(vdp, 1, 0xC0);                       // R1 = display ON, 16K
    writeReg(vdp, 5, 0x36);                       // R5 → SAT base = $1B00
    writeReg(vdp, 6, 0x00);                       // R6 → sprite pattern base = $0000
}

void pokeSAT(TMS9918& vdp, int idx, uint8_t y, uint8_t x, uint8_t name, uint8_t color)
{
    setWriteAddress(vdp, (uint16_t)(0x1B00 + idx * 4));
    vdp.writeData(y);
    vdp.writeData(x);
    vdp.writeData(name);
    vdp.writeData(color);
}

void fillSpritePatternFF(TMS9918& vdp)
{
    setWriteAddress(vdp, 0x0000);
    for (int i = 0; i < 8; ++i) vdp.writeData(0xFF);
}

uint8_t readStatus(TMS9918& vdp) { return vdp.readControl(); }

// Cycles needed to reach scanline N from frameCycleCounter = 0.
// scanline = floor(frameCycle * 262 / kCyclesPerFrame) → frameCycle to be at
// scanline N is at least (N+1) * kCyclesPerFrame / 262 (we want >= line N+1
// so the per-line scan has processed line N).
int cyclesToBePastLine(int line)
{
    return (line + 1) * POM1_CPU_CYCLES_PER_FRAME_1X_60HZ / 262 + 5;
}

} // namespace

int main()
{
    int assertions = 0;

    // ----------------------------------------------------------------------
    // Phase A — Mid-frame collision (Bug N°5).
    // 2 fully-overlapping sprites at Y=80 (raw 79; silicon adds +1 to yRaw).
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp;
        initVdp(vdp);
        fillSpritePatternFF(vdp);
        pokeSAT(vdp, 0, /*y=*/79, /*x=*/100, /*name=*/0, /*color=*/0x0F);
        pokeSAT(vdp, 1, /*y=*/79, /*x=*/100, /*name=*/0, /*color=*/0x0F);
        pokeSAT(vdp, 2, 0xD0, 0, 0, 0);

        // frameCycleCounter is still 0 — no advanceCycles consumed yet.
        // Advance to ~line 30, well before the collision line 80.
        vdp.advanceCycles(cyclesToBePastLine(30));
        const uint8_t s30 = readStatus(vdp);
        mustBeTrue((s30 & 0x20) == 0,
                   "PhaseA: at line ~30, collision bit 5 must NOT yet be armed"); ++assertions;

        // readStatus cleared bits 5/6/7. Continue past line 80; bit 5 must arm.
        vdp.advanceCycles(cyclesToBePastLine(85) - cyclesToBePastLine(30));
        const uint8_t s85 = readStatus(vdp);
        mustBeTrue((s85 & 0x20) != 0,
                   "PhaseA: past line 80, bit 5 (collision) must be armed"); ++assertions;
    }

    // ----------------------------------------------------------------------
    // Phase B — 5S raster split (Bug N°10).
    // 5 sprites at line ~95 (yRaw 94). Bit 6 must arm + low 5 bits = 4
    // when the raster crosses the offending line.
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp;
        initVdp(vdp);
        for (int i = 0; i < 5; ++i) {
            pokeSAT(vdp, i, /*y=*/94, /*x=*/(uint8_t)(i * 16), /*name=*/0, /*color=*/0x0F);
        }
        pokeSAT(vdp, 5, 0xD0, 0, 0, 0);

        // At ~line 50, before the 5-sprite scanline 95, 5S not yet armed.
        vdp.advanceCycles(cyclesToBePastLine(50));
        const uint8_t s50 = readStatus(vdp);
        mustBeTrue((s50 & 0x40) == 0,
                   "PhaseB: at line ~50, 5S bit 6 must NOT yet be armed"); ++assertions;

        // Past line 95: bit 6 + low 5 bits = 4 (the 5th sprite's SAT index).
        vdp.advanceCycles(cyclesToBePastLine(110) - cyclesToBePastLine(50));
        const uint8_t s110 = readStatus(vdp);
        mustBeTrue((s110 & 0x40) != 0,
                   "PhaseB: past line 95, 5S bit 6 must be armed"); ++assertions;
        mustBeTrue((s110 & 0x1F) == 4,
                   "PhaseB: low 5 bits = 4 (5th sprite's SAT index)"); ++assertions;
    }

    // ----------------------------------------------------------------------
    // Phase C — Last sprite scanned (Bug N°6).
    // 4 sprites + terminator at SAT[4]. After a full frame, no overflow:
    // bit 6 = 0, bits 0..4 reflect the index of the last SAT entry walked
    // (the terminator at SAT[4]).
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp;
        initVdp(vdp);
        for (int i = 0; i < 4; ++i) {
            pokeSAT(vdp, i, /*y=*/49, /*x=*/(uint8_t)(i * 32), /*name=*/0, /*color=*/0x0F);
        }
        pokeSAT(vdp, 4, 0xD0, 0, 0, 0);

        // Step a full frame so all 192 lines are scanned.
        vdp.advanceCycles(POM1_CPU_CYCLES_PER_FRAME_1X_60HZ + 100);
        const uint8_t s = readStatus(vdp);
        mustBeTrue((s & 0x40) == 0, "PhaseC: no overflow, bit 6 = 0"); ++assertions;
        mustBeTrue((s & 0x1F) == 4,
                   "PhaseC: bits 0..4 = 4 (SAT index of the Y=$D0 terminator)"); ++assertions;
    }

    // ----------------------------------------------------------------------
    // Phase D — Visible-only collision (Bug N°4 corrected mai 2026).
    // 2 fully-opaque sprites in early-clock (color bit 7 set) at X=10 →
    // real X=-22. Both at Y=50, fully overlapping in the LEFT BORDER.
    // Per openMSX (SpriteChecker.cc:187-191), collision detection is
    // restricted to visible [0, 256). Border pixels do NOT trigger
    // collision even when sprites overlap there. So NO collision should
    // be latched.
    // (Was inverted pre-mai 2026 — pinned the Nouspikel range but
    // contradicted openMSX. Updated when POM1 aligned to openMSX.)
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp;
        initVdp(vdp);
        fillSpritePatternFF(vdp);
        pokeSAT(vdp, 0, /*y=*/49, /*x=*/10, /*name=*/0, /*color=*/0x8F);
        pokeSAT(vdp, 1, /*y=*/49, /*x=*/10, /*name=*/0, /*color=*/0x8F);
        pokeSAT(vdp, 2, 0xD0, 0, 0, 0);

        vdp.advanceCycles(POM1_CPU_CYCLES_PER_FRAME_1X_60HZ + 100);
        const uint8_t s = readStatus(vdp);
        mustBeTrue((s & 0x20) == 0,
                   "PhaseD: NO collision for early-clock sprites at real X=-22 (border-only, visible-only per openMSX)"); ++assertions;
    }

    // ----------------------------------------------------------------------
    // Phase E — 59.94 Hz NTSC frame budget (Bug N°11).
    // 1.022727 MHz / 59.94005994 Hz = 1022727 × 1001 / 60000 ≈ 17062.49
    // → 17062 cycles/frame (rounded to nearest). 17045 was the legacy
    // 60 Hz round value; the new value is silicon-faithful for NTSC.
    // ----------------------------------------------------------------------
    {
        mustBeTrue(POM1_CPU_CYCLES_PER_FRAME_1X_60HZ == 17062,
                   "PhaseE: kCyclesPerFrame matches 59.94 Hz NTSC (= 17062)"); ++assertions;
    }

    // ----------------------------------------------------------------------
    // Phase F — Progressive rasterisation: mid-frame R7 change.
    //
    // Set R7 = $0F (white), advance to ~line 50, change R7 = $0A (yellow),
    // advance to end of frame. The framebuffer's L/R border bands should
    // reflect:
    //   - rows [kBorderTop, kBorderTop+50) → R7=$0F (white) at the time
    //     of those lines' rendering.
    //   - rows [kBorderTop+100, ...)        → R7=$0A (yellow) since R7 was
    //     changed before those lines were rendered.
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp;
        initVdp(vdp);
        writeReg(vdp, 7, 0x0F);                   // backdrop = white
        // Advance past line 0 to begin rendering with R7=$0F.
        vdp.advanceCycles(cyclesToBePastLine(50));
        writeReg(vdp, 7, 0x0A);                   // mid-frame change → yellow
        vdp.advanceCycles(cyclesToBePastLine(150) - cyclesToBePastLine(50));
        // Finish frame.
        vdp.advanceCycles(POM1_CPU_CYCLES_PER_FRAME_1X_60HZ);

        TMS9918::Snapshot snap;
        vdp.copySnapshot(snap);

        // Border-left pixel at line 25 (active line ~1) should be white.
        const int kBorderTop  = TMS9918::kBorderTop;
        const int kFullWidth  = TMS9918::kFullWidth;
        const uint32_t earlyLR  = snap.framebuffer[(kBorderTop + 5) * kFullWidth + 4];
        const uint32_t lateLR   = snap.framebuffer[(kBorderTop + 150) * kFullWidth + 4];
        const ImU32 white  = TMS9918::kPalette[0x0F];
        const ImU32 yellow = TMS9918::kPalette[0x0A];
        mustBeTrue(earlyLR == white,
                   "PhaseF: early line border = R7 at-line-render time (white)"); ++assertions;
        mustBeTrue(lateLR == yellow,
                   "PhaseF: late line border = R7 after mid-frame change (yellow)"); ++assertions;
    }

    // ----------------------------------------------------------------------
    // Phase G — Sub-scanline split (Étape 0, beam/CPU sync). WITHIN a single
    // scanline, change R7 mid-line via renderBeamCatchUp (the exact idiom the
    // Memory MMIO hook uses before a register write): the LEFT border of that
    // line must keep the OLD R7, the RIGHT border the NEW R7 — a split that is
    // impossible with line-major rendering. Lines fully before the change stay
    // old; lines fully after stay new.
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp;
        initVdp(vdp);
        writeReg(vdp, 7, 0x0F);                        // backdrop = white
        // Put the beam mid-line 80: frameCycleCounter≈5240 → active x≈98.
        vdp.advanceCycles(5240);
        vdp.renderBeamCatchUp(0);                      // commit line 80 left half + L border WHITE
        writeReg(vdp, 7, 0x0A);                        // mid-SCANLINE change → yellow
        vdp.advanceCycles(POM1_CPU_CYCLES_PER_FRAME_1X_60HZ);  // finish: line 80 right half + R border YELLOW

        TMS9918::Snapshot snap;
        vdp.copySnapshot(snap);

        const int   kBorderTop = TMS9918::kBorderTop;
        const int   kFullWidth = TMS9918::kFullWidth;
        const ImU32 white  = TMS9918::kPalette[0x0F];
        const ImU32 yellow = TMS9918::kPalette[0x0A];

        const int splitRow = kBorderTop + 80;
        const uint32_t splitLeft  = snap.framebuffer[splitRow * kFullWidth + 4];                // L border
        const uint32_t splitRight = snap.framebuffer[splitRow * kFullWidth + (kFullWidth - 4)]; // R border
        mustBeTrue(splitLeft  == white,
                   "PhaseG: split line L-border = R7 before the mid-scanline change (white)"); ++assertions;
        mustBeTrue(splitRight == yellow,
                   "PhaseG: split line R-border = R7 after the mid-scanline change (yellow)"); ++assertions;
        mustBeTrue(splitLeft != splitRight,
                   "PhaseG: same scanline carries two backdrop colours (sub-line split)"); ++assertions;

        // A line fully before the change stays white on both sides.
        const int earlyRow = kBorderTop + 40;
        mustBeTrue(snap.framebuffer[earlyRow * kFullWidth + 4] == white &&
                   snap.framebuffer[earlyRow * kFullWidth + (kFullWidth - 4)] == white,
                   "PhaseG: line before the change is uniformly white"); ++assertions;
        // A line fully after the change stays yellow on both sides.
        const int lateRow = kBorderTop + 150;
        mustBeTrue(snap.framebuffer[lateRow * kFullWidth + 4] == yellow &&
                   snap.framebuffer[lateRow * kFullWidth + (kFullWidth - 4)] == yellow,
                   "PhaseG: line after the change is uniformly yellow"); ++assertions;
    }

    // ----------------------------------------------------------------------
    // Phase H — Read-time sprite-scan sync (Étape 1). The 5S overflow flag must
    // reflect the beam's scanline at the EXACT read cycle, not merely as of the
    // last advanceCycles. 5 sprites sit on line 100. We stop the beam at ~line
    // 96 (scan hasn't reached 100 → 5S clear), then push the scan past line 100
    // purely via syncSpriteScanToBeam's in-flight offset — WITHOUT another
    // advanceCycles — and 5S must latch. This is what makes the 5S raster-split
    // poll loop cycle-precise.
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp;
        initVdp(vdp);
        fillSpritePatternFF(vdp);
        for (int i = 0; i < 5; ++i)
            pokeSAT(vdp, i, /*y=*/99, /*x=*/(uint8_t)(i * 16), /*name=*/0, /*color=*/0x0F);
        pokeSAT(vdp, 5, 0xD0, 0, 0, 0);                // terminator

        // Beam at ~line 96: the per-line scan has NOT reached line 100 yet.
        vdp.advanceCycles(cyclesToBePastLine(96));
        const uint8_t before = vdp.readControl();      // direct read — no sync
        mustBeTrue((before & 0x40) == 0,
                   "PhaseH: 5S clear while the beam is still above the sprite line"); ++assertions;

        // Read-time catch-up: an in-flight offset of ~400 cycles (~6 lines)
        // pushes the effective beam past line 100 → the scan latches 5S, with
        // no further advanceCycles. (Memory's $CC01 read hook does exactly this
        // with cpu->getCurrentInstructionCycles().)
        vdp.syncSpriteScanToBeam(400);
        const uint8_t after = vdp.readControl();
        mustBeTrue((after & 0x40) != 0,
                   "PhaseH: 5S latched by read-time beam sync (cycle-precise raster split)"); ++assertions;
        mustBeTrue((after & 0x1F) == 4,
                   "PhaseH: 5S index = SAT idx 4 (the 5th sprite on the line)"); ++assertions;
    }

    // ----------------------------------------------------------------------
    // Phase I — Seamless mode split (Étape 2). A mid-scanline MODE change (here
    // Graphics I → Text, which disables the sprite engine) must be LATCHED by
    // the chip until the line ends: the line on which the write lands keeps its
    // old mode for its whole width, the NEXT line gets the new mode. We place a
    // sprite on line 80, switch to text mode mid-line 80, and check the sprite
    // still shows on line 80 (latched) but is gone on line 81 (text mode).
    // ----------------------------------------------------------------------
    {
        TMS9918 vdp;
        initVdp(vdp);                                  // Graphics I, display ON
        writeReg(vdp, 7, 0x04);                        // backdrop = dark blue
        fillSpritePatternFF(vdp);                      // sprite name 0 = solid
        pokeSAT(vdp, 0, /*y=*/79, /*x=*/110, /*name=*/0, /*color=*/0x0F);  // line 80, white
        pokeSAT(vdp, 1, 0xD0, 0, 0, 0);                // terminator

        vdp.advanceCycles(5240);                       // beam mid-line 80 (x≈98)
        vdp.renderBeamCatchUp(0);                      // commit line 80 left half — latches mode 0
        writeReg(vdp, 1, 0xF0);                        // mid-SCANLINE switch to Text mode (M1)
        vdp.advanceCycles(POM1_CPU_CYCLES_PER_FRAME_1X_60HZ);  // finish the frame

        static uint32_t active[256 * 192];
        vdp.copyActiveFramebuffer(active);
        const ImU32 white = TMS9918::kPalette[0x0F];
        mustBeTrue(active[80 * 256 + 113] == white,
                   "PhaseI: sprite still drawn on the split line — mode change latched to line end"); ++assertions;
        mustBeTrue(active[81 * 256 + 113] != white,
                   "PhaseI: next line is Text mode — sprite engine off (seamless split applied)"); ++assertions;
    }

    std::printf("tms9918_per_scanline: all %d assertions passed\n", assertions);
    return 0;
}
