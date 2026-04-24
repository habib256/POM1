// SWTPC GT-6144 Graphic Terminal — state-machine smoke test.
//
// Self-contained: exercises GT6144 alone (no Memory, no peripheral core),
// plus the PeripheralBus dispatch around $D00A to confirm the card is
// write-only on the bus. Pins the 4-phase FSM + control-opcode aliases
// documented in GT6144.h:
//   - X/Y commit produces a pixel at the correct bit position.
//   - Control opcodes 224-255 decode only on bits 0-2; bits 3-4 are
//     don't-cares (240 / 224 / 232 / 248 all mean "INVERTED SCREEN").
//   - Inversion and blanking affect renderToBuffer only — the framebuffer
//     bits stay put (matches the analog video-stage XOR on real hardware).
//   - Power-on contents are random (Intel 2102 bistable flip-flops), so
//     two fresh cards should not have byte-identical framebuffers.

#include "GT6144.h"
#include "PeripheralBus.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

namespace {

bool pixel(const GT6144::Snapshot& s, int x, int y) {
    const int index = y * (GT6144::kWidth / 8) + (x >> 3);
    const uint8_t mask = static_cast<uint8_t>(0x80 >> (x & 7));
    return (s.framebuffer[index] & mask) != 0;
}

} // namespace

int main() {
    // --- 1. Bus registration: write-only at $D00A, read falls through. ---
    {
        GT6144 gt;
        PeripheralBus bus;
        auto h = bus.registerHandle("GT6144", {0xD00A, 0xD00A}, /*priority*/ 0,
            /*onRead=*/ {},
            [&](uint16_t /*a*/, uint8_t v) { gt.writeCommand(v); });
        bus.setEnabled(h, true);

        uint8_t v = 0;
        assert(!bus.tryRead(0xD00A, v));        // write-only: no onRead
        assert( bus.tryWrite(0xD00A, 0xF1));    // NORMAL SCREEN
        assert(!bus.tryRead(0xD00A, v));        // still write-only

        bus.setEnabled(h, false);
        assert(!bus.tryWrite(0xD00A, 0xF1));    // disabled: falls through
    }

    // --- 2. Draw a white pixel at (26, 22). Research §6.2 example. ---
    {
        GT6144 gt;
        // Clear neighbours first (random power-on SRAM may have them set).
        gt.writeCommand(25);   // latch X=25, mode=OFF
        gt.writeCommand(150);  // 128 + 22 → commit Y=22
        gt.writeCommand(27);   // latch X=27, mode=OFF
        gt.writeCommand(150);  // 128 + 22 → commit Y=22
        // Now draw the target pixel.
        gt.writeCommand(90);   // 64 + 26 → latch X=26, mode=ON
        gt.writeCommand(150);  // 128 + 22 → commit Y=22

        GT6144::Snapshot s;
        gt.copySnapshot(s);
        assert(pixel(s, 26, 22));
        // Neighbours must remain off — ON at X=26 must not bleed.
        assert(!pixel(s, 25, 22));
        assert(!pixel(s, 27, 22));
    }

    // --- 3. OFF latch clears a previously-set pixel. ---
    {
        GT6144 gt;
        // Paint (10, 22) white, then clear it via the OFF branch.
        gt.writeCommand(10 + 64);
        gt.writeCommand(128 + 22);
        {
            GT6144::Snapshot s; gt.copySnapshot(s);
            assert(pixel(s, 10, 22));
        }
        gt.writeCommand(10);       // latch X=10, mode=OFF
        gt.writeCommand(128 + 22); // commit Y=22 with OFF
        {
            GT6144::Snapshot s; gt.copySnapshot(s);
            assert(!pixel(s, 10, 22));
        }
    }

    // --- 4. INVERTED SCREEN does NOT touch the framebuffer. ---
    {
        GT6144 gt;
        gt.writeCommand(5 + 64);    // latch X=5, ON
        gt.writeCommand(128 + 40);  // commit Y=40
        GT6144::Snapshot before; gt.copySnapshot(before);
        assert(pixel(before, 5, 40));
        assert(!before.inverted);

        gt.writeCommand(240);       // INVERTED SCREEN
        GT6144::Snapshot after; gt.copySnapshot(after);
        assert(after.inverted);
        // Framebuffer must be byte-for-byte identical (research §4.3).
        assert(before.framebuffer == after.framebuffer);

        gt.writeCommand(241);       // NORMAL SCREEN
        GT6144::Snapshot restored; gt.copySnapshot(restored);
        assert(!restored.inverted);
        assert(before.framebuffer == restored.framebuffer);
    }

    // --- 5. Bits 3-4 are don't-cares: 224 / 232 / 248 behave like 240. ---
    for (uint8_t alias : { uint8_t{224}, uint8_t{232}, uint8_t{248}, uint8_t{240} }) {
        GT6144 gt;
        gt.writeCommand(241);       // ensure NORMAL
        GT6144::Snapshot s1; gt.copySnapshot(s1);
        assert(!s1.inverted);
        gt.writeCommand(alias);     // should flip to INVERTED
        GT6144::Snapshot s2; gt.copySnapshot(s2);
        assert(s2.inverted);
    }

    // --- 6. BLANKED GRAPHICS toggles only the render-stage flag. ---
    {
        GT6144 gt;
        gt.writeCommand(245);       // BLANKED
        GT6144::Snapshot s; gt.copySnapshot(s);
        assert(s.blanked);
        gt.writeCommand(244);       // ENABLE GRAPHICS (unblank)
        gt.copySnapshot(s);
        assert(!s.blanked);
    }

    // --- 7. renderToBuffer: blanked → solid black; inverted swaps palette. ---
    {
        GT6144 gt;
        // Clear neighbour (1, 0) first — random power-on SRAM may have it lit.
        gt.writeCommand(1);        // latch X=1, mode=OFF
        gt.writeCommand(128 + 0);  // commit Y=0
        // Paint (0, 0) white.
        gt.writeCommand(0 + 64);
        gt.writeCommand(128 + 0);
        GT6144::Snapshot s; gt.copySnapshot(s);

        uint32_t buf[GT6144::kWidth * GT6144::kHeight];
        GT6144::renderToBuffer(buf, s);
        assert(buf[0] == 0xFFFFFFFFu);          // white pixel
        assert(buf[1] == 0xFF000000u);          // black neighbour

        s.inverted = true;
        GT6144::renderToBuffer(buf, s);
        assert(buf[0] == 0xFF000000u);          // lit pixel now black
        assert(buf[1] == 0xFFFFFFFFu);          // background now white

        s.inverted = false;
        s.blanked  = true;
        GT6144::renderToBuffer(buf, s);
        assert(buf[0] == 0xFF000000u);          // blanked: all black
        assert(buf[GT6144::kWidth * GT6144::kHeight - 1] == 0xFF000000u);
    }

    // --- 8. Power-on noise is non-trivially different between two cards. ---
    // With 768 bytes seeded from std::random_device + std::mt19937, the
    // probability of two independent cards producing identical framebuffers
    // is 1/256^768 — negligible. A false positive here means the RNG was
    // seeded deterministically, which would defeat the visible-at-boot
    // SRAM entropy the test is guarding.
    {
        GT6144 a, b;
        GT6144::Snapshot sa, sb; a.copySnapshot(sa); b.copySnapshot(sb);
        assert(sa.framebuffer != sb.framebuffer);
    }

    std::printf("GT6144 smoke test: all assertions passed.\n");
    return 0;
}
