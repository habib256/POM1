// TMS9918A sprite-engine status-flag smoke test.
//
// Self-contained: exercises TMS9918 alone (no Memory, no peripheral bus).
// Pins three sticky status-register behaviors documented by the TI datasheet
// and BiFi's MSX TMS9918A reference, none of which were implemented before
// this test landed:
//
//   - Status bit 6 ($40) — fifth-sprite-on-scanline overflow flag.
//     Set when any scanline contains >4 sprites AND F (bit 7) is still
//     clear (TMS9918A datasheet: "5th sprite detection is only active when
//     F flag is zero"; openMSX SpriteChecker: `(status & 0xC0) == 0`).
//     Bits 0..4 latch the SAT index of the fifth sprite. Sticky until a
//     status read (pinned by T9/T10).
//   - Status bit 5 ($20) — sprite-sprite collision. Set on any opaque
//     pattern-bit overlap, even when one (or both) sprites have color = 0
//     (a real TMS9918A collides on pattern bits, not on rendered color).
//     Sticky until read.
//   - Reading $CC01 latch-clears F (bit 7), 5S (bit 6) AND C (bit 5) — the
//     ~0xE0 mask. TI datasheet §2.2, Nouspikel ("the first 3 bits are
//     automatically reset when the register is read") and openMSX
//     (readStatusReg case 0 → statusReg0 & 0x1F) all agree. Bits 0..4
//     survive the read.
//
// Each test runs in a fresh TMS9918 to avoid sticky-flag bleed.

#include "TMS9918.h"

#include <cassert>
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

// VRAM write-address set + streamed bytes through the data port.
void vramWrite(TMS9918& vdp, uint16_t addr, const uint8_t* data, size_t n)
{
    vdp.writeControl((uint8_t)(addr & 0xFF));
    vdp.writeControl((uint8_t)(0x40 | ((addr >> 8) & 0x3F))); // $40xx = write address
    for (size_t i = 0; i < n; i++) vdp.writeData(data[i]);
}

void writeReg(TMS9918& vdp, uint8_t reg, uint8_t value)
{
    vdp.writeControl(value);
    vdp.writeControl((uint8_t)(0x80 | (reg & 0x07)));
}

// Strict-aware helpers must advance enough cycles to clear the worst-case
// access window (24c hardened in Mode I + sprites). 44c covers it with margin.
// In the openMSX-faithful deferred model a $CC00/$CC01 access is scheduled to
// the next VRAM slot and only executes on a later advanceCycles() — so each
// helper advances BEFORE (clear any prior pending) AND AFTER (fire the deferred
// slot so the access actually lands before the next operation reads VRAM).
void strictWriteControl(TMS9918& vdp, uint8_t value)
{
    vdp.advanceCycles(44);
    vdp.writeControl(value);
    vdp.advanceCycles(44);
}

void strictWriteData(TMS9918& vdp, uint8_t value)
{
    vdp.advanceCycles(44);
    vdp.writeData(value);
    vdp.advanceCycles(44);
}

void strictSetWriteAddress(TMS9918& vdp, uint16_t addr)
{
    strictWriteControl(vdp, (uint8_t)(addr & 0xFF));
    strictWriteControl(vdp, (uint8_t)(0x40 | ((addr >> 8) & 0x3F)));
}

void strictSetReadAddress(TMS9918& vdp, uint16_t addr)
{
    strictWriteControl(vdp, (uint8_t)(addr & 0xFF));
    strictWriteControl(vdp, (uint8_t)((addr >> 8) & 0x3F));
}

// Default test layout:
//   R1 = $C0   display on, 16K, 8x8 sprites, no magnify, no IRQ
//   R5 = $06   SAT base = $0300 (6 << 7)
//   R6 = $00   sprite pattern base = $0000
// Pattern at name=1 ($0008..$000F) is solid 0xFF.
void initVdp(TMS9918& vdp)
{
    vdp.reset();
    writeReg(vdp, 1, 0xC0);
    writeReg(vdp, 5, 0x06);
    writeReg(vdp, 6, 0x00);
    uint8_t solid[8] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    vramWrite(vdp, 0x0008, solid, 8); // sprite name 1 → all-on pattern
}

// One-shot SAT entry write.
void pokeSAT(TMS9918& vdp, int idx, uint8_t y, uint8_t x, uint8_t name, uint8_t color)
{
    uint8_t buf[4] = { y, x, name, color };
    vramWrite(vdp, (uint16_t)(0x0300 + idx * 4), buf, 4);
}

// Drive enough cycles to complete the active-display sprite scan and enter
// VBlank without rolling into the next frame's active region. 15000 sits in
// the NTSC VBlank window [~12505, 17062): full scan done, F raised, 5S + its
// index stable for the read that follows. (Status bits are sticky-until-read
// on silicon — nothing resets at frame rollover — but staying inside one
// frame keeps each test's timeline easy to reason about.)
void tickFrame(TMS9918& vdp)
{
    vdp.advanceCycles(15000);
}

// Fine-grained frame advance for the sticky-flag phases (T9/T10): 18000
// cycles in 1000-cycle steps, mimicking the per-instruction granularity the
// real emulation loop uses. A single oversized advanceCycles() that spans
// the frame rollover scans the next frame's lines on a LATER call — after
// that frame's F-flag edge — which inverts the scan-before-F ordering the
// F-gated 5S latch depends on. Stepped advance preserves silicon ordering.
void tickFrameStepped(TMS9918& vdp)
{
    for (int i = 0; i < 18; ++i) vdp.advanceCycles(1000);
}

// Read status without disturbing latch state for subsequent control writes
// in the same test (writeControl() clears latchIsSecond on its own; readControl
// also resets it, so this is safe).
uint8_t readStatus(TMS9918& vdp) { return vdp.readControl(); }

} // namespace

int main()
{
    // -----------------------------------------------------------------
    // T1 — fifth-sprite flag + SAT index in low 5 bits
    //
    // 5 sprites all on Y=50 (top edge raw 49 since top = yRaw + 1).
    // Names 1..5, colors non-zero. Terminator at SAT[5].
    // Expected: bit 6 set; low 5 bits == 4 (the SAT index of the dropped
    // 5th sprite — sprites 0..3 are kept, sprite 4 overflows).
    // -----------------------------------------------------------------
    {
        TMS9918 vdp;
        initVdp(vdp);
        for (int i = 0; i < 5; i++) {
            pokeSAT(vdp, i, /*y=*/49, /*x=*/(uint8_t)(i * 16), /*name=*/1, /*color=*/0x0F);
        }
        pokeSAT(vdp, 5, 0xD0, 0, 0, 0); // terminator
        tickFrame(vdp);
        uint8_t s = readStatus(vdp);
        mustBeTrue((s & 0x40) != 0, "T1: fifth-sprite flag (bit 6) should be set");
        mustBeTrue((s & 0x1F) == 4,  "T1: 5S index should be SAT idx 4 (5th sprite)");
        // Bit 5 must NOT be set — sprites are spaced 16 apart and 8x8 wide, no overlap.
        mustBeTrue((s & 0x20) == 0,  "T1: collision should not be set (no overlap)");
    }

    // -----------------------------------------------------------------
    // T2 — sprite-sprite collision basic
    //
    // Two 8x8 sprites at Y=50, X=50 and X=52, both name=1 (solid),
    // both color non-zero. Pixels 52..57 overlap.
    // -----------------------------------------------------------------
    {
        TMS9918 vdp;
        initVdp(vdp);
        pokeSAT(vdp, 0, 49, 50, 1, 0x0F);
        pokeSAT(vdp, 1, 49, 52, 1, 0x0F);
        pokeSAT(vdp, 2, 0xD0, 0, 0, 0);
        tickFrame(vdp);
        uint8_t s = readStatus(vdp);
        mustBeTrue((s & 0x20) != 0, "T2: collision flag (bit 5) should be set");
        mustBeTrue((s & 0x40) == 0, "T2: 5S flag should not be set (only 2 sprites)");
    }

    // -----------------------------------------------------------------
    // T3 — collision when one sprite has color 0
    //
    // The TMS9918A collides on pattern bits regardless of color. A
    // color=0 sprite is visually transparent but its pattern bits still
    // count for collision detection.
    // -----------------------------------------------------------------
    {
        TMS9918 vdp;
        initVdp(vdp);
        pokeSAT(vdp, 0, 49, 50, 1, 0x00); // color 0 — transparent
        pokeSAT(vdp, 1, 49, 50, 1, 0x0F); // visible, same X/Y
        pokeSAT(vdp, 2, 0xD0, 0, 0, 0);
        tickFrame(vdp);
        uint8_t s = readStatus(vdp);
        mustBeTrue((s & 0x20) != 0,
                   "T3: collision should be set even when one sprite has color=0");
    }

    // -----------------------------------------------------------------
    // T4 — sticky behaviour: collision flag survives further frames,
    // and is only cleared by readControl().
    // -----------------------------------------------------------------
    {
        TMS9918 vdp;
        initVdp(vdp);
        pokeSAT(vdp, 0, 49, 50, 1, 0x0F);
        pokeSAT(vdp, 1, 49, 52, 1, 0x0F);
        pokeSAT(vdp, 2, 0xD0, 0, 0, 0);
        tickFrame(vdp);
        // Now move the sprites apart so this frame would NOT trigger collision,
        // but the flag must persist from the previous frame.
        pokeSAT(vdp, 1, 49, 200, 1, 0x0F);
        tickFrame(vdp);
        tickFrame(vdp);
        uint8_t s1 = readStatus(vdp);
        mustBeTrue((s1 & 0x20) != 0, "T4: collision flag should still be sticky");
        uint8_t s2 = readStatus(vdp);
        mustBeTrue((s2 & 0x20) == 0, "T4: collision flag must clear after read");
    }

    // -----------------------------------------------------------------
    // T5 — disjoint Y bands → no collision
    // -----------------------------------------------------------------
    {
        TMS9918 vdp;
        initVdp(vdp);
        pokeSAT(vdp, 0, 49,  50, 1, 0x0F); // Y top = 50, band 50..57
        pokeSAT(vdp, 1, 79,  50, 1, 0x0F); // Y top = 80, band 80..87 — disjoint
        pokeSAT(vdp, 2, 0xD0, 0, 0, 0);
        tickFrame(vdp);
        uint8_t s = readStatus(vdp);
        mustBeTrue((s & 0x20) == 0, "T5: disjoint Y bands must not collide");
    }

    // -----------------------------------------------------------------
    // T6 — display blank (R1 bit 6 = 0) suppresses the sprite scan.
    // No flags should be set even with 5 overlapping sprites.
    // -----------------------------------------------------------------
    {
        TMS9918 vdp;
        initVdp(vdp);
        writeReg(vdp, 1, 0x80); // display off (bit 6 cleared)
        for (int i = 0; i < 5; i++)
            pokeSAT(vdp, i, 49, 50, 1, 0x0F);
        pokeSAT(vdp, 5, 0xD0, 0, 0, 0);
        tickFrame(vdp);
        uint8_t s = readStatus(vdp);
        mustBeTrue((s & 0x60) == 0,
                   "T6: blanked display must not raise collision or 5S flags");
        // Bit 7 (frame interrupt) is still set regardless of blanking.
        mustBeTrue((s & 0x80) != 0, "T6: VBlank flag still raised when blanked");
    }

    // -----------------------------------------------------------------
    // T7 — silicon strict 4K VRAM mode. With R1 bit 7 clear, addresses
    // above $0FFF mirror into the first 4 KB.
    // -----------------------------------------------------------------
    {
        TMS9918 vdp;
        vdp.reset();
        vdp.setSiliconStrictMode(true);
        strictSetWriteAddress(vdp, 0x1800);
        strictWriteData(vdp, 0xA5);
        strictSetReadAddress(vdp, 0x0800);
        vdp.advanceCycles(44);
        mustBeTrue(vdp.readData() == 0xA5,
                   "T7: strict 4K mode should mirror $1800 writes to $0800");
    }

    // -----------------------------------------------------------------
    // T8 — silicon strict timing, openMSX-faithful deferred model. A CPU VRAM
    // write is scheduled to the next access slot; two writes too fast (no gap
    // between them) collapse to ONE cell holding the NEWER byte (newest-wins),
    // and the pointer advances only once — exactly as openMSX with
    // allowTooFastAccess=off. Properly-spaced writes each land in sequence.
    // -----------------------------------------------------------------
    {
        TMS9918 vdp;
        vdp.reset();
        vdp.setSiliconStrictMode(true);
        strictWriteControl(vdp, 0xC0); // R1 = display on + 16K
        strictWriteControl(vdp, 0x81);
        strictSetWriteAddress(vdp, 0x0000);
        strictWriteData(vdp, 0x11);          // lands at $0000
        strictWriteData(vdp, 0x33);          // lands at $0001
        vdp.writeData(0x44);                 // schedules at $0002
        vdp.writeData(0x55);                 // too fast: overwrites the pending byte
        vdp.advanceCycles(44);               // slot fires -> $0002 = $55 (newest)
        strictSetReadAddress(vdp, 0x0000);
        vdp.advanceCycles(44);
        mustBeTrue(vdp.readData() == 0x11, "T8: spaced write @ $0000 landed");
        vdp.advanceCycles(44);
        mustBeTrue(vdp.readData() == 0x33, "T8: spaced write @ $0001 landed");
        vdp.advanceCycles(44);
        mustBeTrue(vdp.readData() == 0x55, "T8: too-fast pair -> newest ($55) wins, pointer advanced once");
    }

    // -----------------------------------------------------------------
    // T9 — a status read latch-clears F (bit 7), 5S (bit 6) AND collision
    // (bit 5); the index bits 0..4 are NOT cleared. TI datasheet §2.2 ("The
    // 5S bit is cleared to 0 after the status register is read"), Nouspikel,
    // and openMSX (statusReg0 & 0x1F on read) all agree. Regression guard
    // for the silicon-fidelity restore of the ~0xE0 read-clear mask.
    // -----------------------------------------------------------------
    {
        TMS9918 vdp;
        initVdp(vdp);
        for (int i = 0; i < 5; i++)
            pokeSAT(vdp, i, 49, (uint8_t)(i * 16), 1, 0x0F);
        pokeSAT(vdp, 5, 0xD0, 0, 0, 0);
        tickFrame(vdp);                       // lands in VBlank, full scan done
        uint8_t s1 = readStatus(vdp);
        mustBeTrue((s1 & 0x40) != 0, "T9: 5S set after the frame scan");
        mustBeTrue((s1 & 0x1F) == 4, "T9: 5S index = SAT idx 4");
        // Second read, same frame: 5S must be GONE (cleared by the first
        // read), while the index bits keep their last latched value.
        uint8_t s2 = readStatus(vdp);
        mustBeTrue((s2 & 0x40) == 0, "T9: 5S cleared by the status read (F+5S+C clear)");
        mustBeTrue((s2 & 0x1F) == 4, "T9: index bits 0..4 survive the read");
        // Next frame re-latches 5S: the read above cleared F, so the sprite
        // comparator is re-armed ((status & 0xC0) == 0 holds again).
        tickFrameStepped(vdp);                // fine-grained roll into the next frame
        uint8_t s3 = readStatus(vdp);
        mustBeTrue((s3 & 0x40) != 0, "T9: 5S re-latches on the next scanned frame");
    }

    // -----------------------------------------------------------------
    // T10 — F-gated 5S latch (TMS9918A datasheet: "5th sprite detection is
    // only active when F flag is zero"; openMSX `(status & 0xC0) == 0`).
    // Frame 0 scans with only 4 sprites → no 5S; F rises at VBlank and is
    // NOT read. A 5th sprite is then added: frame 1's scan must NOT latch
    // 5S because F is still pending. After a status read clears F, the
    // following frame latches 5S normally.
    // -----------------------------------------------------------------
    {
        TMS9918 vdp;
        initVdp(vdp);
        for (int i = 0; i < 4; i++)
            pokeSAT(vdp, i, 49, (uint8_t)(i * 16), 1, 0x0F);
        pokeSAT(vdp, 4, 0xD0, 0, 0, 0);
        tickFrameStepped(vdp);                // frame 0: 4 sprites, F rises, unread
        pokeSAT(vdp, 4, 49, 64, 1, 0x0F);     // 5th sprite appears
        pokeSAT(vdp, 5, 0xD0, 0, 0, 0);
        tickFrameStepped(vdp);                // frame 1 scanned with F still set
        uint8_t s1 = readStatus(vdp);
        mustBeTrue((s1 & 0x80) != 0, "T10: F pending from the unread frames");
        mustBeTrue((s1 & 0x40) == 0, "T10: 5S NOT latched while F was set (F-gated)");
        tickFrameStepped(vdp);                // F cleared by the read → re-armed
        uint8_t s2 = readStatus(vdp);
        mustBeTrue((s2 & 0x40) != 0, "T10: 5S latches again once F was consumed");
        mustBeTrue((s2 & 0x1F) == 4, "T10: 5S index = SAT idx 4");
    }

    std::printf("tms9918_sprite_status: all 10 tests passed\n");
    return 0;
}
