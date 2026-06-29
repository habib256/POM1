// gen2_frame_latch_smoke -- pins beam-accuracy Phase A: the GEN2 framebuffer the
// renderer reads is FROZEN ATOMICALLY at the V-blank rollover, not sampled from
// the async dirty-page mirror. This is what makes single-buffer XOR animation
// flicker-free: a program finishes its erase+redraw in V-blank (before the
// rollover), so the latched frame is always complete, and a later mid-update
// write to live RAM must NOT leak into the frame the renderer shows until the
// next rollover re-latches.
//
// (Memory::advanceCycles captures the latch at rollover; SnapshotPublisher
// overlays it on the render mirror. This test drives the Memory side directly.)

#include "Memory.h"
#include "Gen2VideoScanner.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

int main()
{
    Memory mem;
    mem.setHgrFramebufferAttached(true);

    const uint16_t base = 0x2000;                 // HGR page 1, scanline-0 bytes
    const uint64_t cpf  = Gen2VideoScanner::kCyclesPerLine
                        * Gen2VideoScanner::kLinesPerFrame;   // 65 * 262 = 17030

    // 1) A "complete" frame: stamp a known pattern, then cross a full frame so the
    //    V-blank rollover latches it. (Adding > cpf guarantees >= 1 rollover even
    //    if the cold-plug scanner phase started mid-frame.)
    for (int i = 0; i < 40; ++i) mem.memWrite(base + i, 0xAA);
    mem.advanceCycles(static_cast<int>(cpf) + 100);

    const uint8_t* latch = mem.gen2FrameLatch();  // index 0 == $2000
    for (int i = 0; i < 40; ++i) assert(latch[i] == 0xAA);   // complete frame frozen

    // 2) A "mid-update": erase those bytes in LIVE RAM without crossing a frame.
    //    The live framebuffer changes, but the renderer's latch must stay frozen
    //    on the complete frame -> no half-drawn frame is ever shown.
    for (int i = 0; i < 40; ++i) mem.memWrite(base + i, 0x00);
    for (int i = 0; i < 40; ++i) assert(mem.getMemoryPointer()[base + i] == 0x00);
    for (int i = 0; i < 40; ++i) assert(latch[i] == 0xAA);   // latch still complete

    // 3) The next V-blank rollover re-latches the new frame (so motion still flows).
    mem.advanceCycles(static_cast<int>(cpf) + 100);
    for (int i = 0; i < 40; ++i) assert(latch[i] == 0x00);

    std::printf("gen2_frame_latch_smoke: OK\n");
    return 0;
}
