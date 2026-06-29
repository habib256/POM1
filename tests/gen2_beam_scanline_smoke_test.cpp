// gen2_beam_scanline_smoke -- pins beam-accuracy Phase B: the GEN2 framebuffer is
// latched PER SCANLINE at the cycle the beam crosses each line, so a program that
// changes the framebuffer mid-frame renders per-line-correct (each line shows RAM
// as of its own beam time), exactly like real hardware racing the beam.
//
// We fill the page, advance the beam halfway down the frame, change the WHOLE page,
// then advance to the bottom. The lines the beam already passed must keep the OLD
// content; the lines it scanned after the change must show the NEW content.

#include "Memory.h"
#include "Gen2VideoScanner.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

// Apple II HGR interleave offset of scanline y within an 8 KB page.
static int rowOff(int y) { return ((y & 7) << 10) | (((y >> 3) & 7) << 7) | ((y >> 6) * 40); }

int main()
{
    Memory mem;
    mem.setGen2RandomScannerPhase(false);   // deterministic: beam starts at line 0
    mem.setGen2RandomDramNoise(false);       // zeroed framebuffer (no cold-plug noise)
    mem.setHgrFramebufferAttached(true);

    const int LPC = static_cast<int>(Gen2VideoScanner::kCyclesPerLine);  // 65

    // Fill page 1 with 0xAA, then scan the beam down to mid-line-100. Lines 0..99
    // are latched while RAM still reads 0xAA.
    for (int a = 0x2000; a < 0x4000; ++a) mem.memWrite(static_cast<uint16_t>(a), 0xAA);
    mem.advanceCycles(100 * LPC + 32);

    // Mid-frame content change: rewrite the whole page to 0xBB.
    for (int a = 0x2000; a < 0x4000; ++a) mem.memWrite(static_cast<uint16_t>(a), 0xBB);

    // Scan to the bottom of the frame. Lines 100..191 are latched as 0xBB.
    mem.advanceCycles(162 * LPC);

    const uint8_t* latch = mem.gen2FrameLatch();   // index 0 == $2000 (page 1)
    assert(latch[rowOff(50)]  == 0xAA);            // beam passed line 50 BEFORE the change
    assert(latch[rowOff(150)] == 0xBB);            // beam passed line 150 AFTER the change
    assert(latch[rowOff(50)] != latch[rowOff(150)]); // a true mid-frame raster split

    std::printf("gen2_beam_scanline_smoke: OK\n");
    return 0;
}
