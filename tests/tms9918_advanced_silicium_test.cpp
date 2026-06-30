// tms9918_advanced_silicium — couverture directe sur l'API TMS9918.
//
// Each ctest assertion below probes a specific silicon behaviour the
// emulator claims to model, without depending on the SilTest 6502 binary.
// These tests catch regressions in the implementation faster than a
// full POM1+SilTest cycle, and lock the documented semantics in code.
//
// What's covered:
//   1. Bug N°8 — isIllegalModeRegs detector matches the documented
//      "two or more of M1/M2/M3 set" rule.
//   2. Bug N°5 — scanSpritesForLine STOPS at the first SAT[i].y == $D0
//      (terminator) and ignores all later entries.
//   3. Bug N°5/9 — color-0 sprite is invisible BUT counted in the
//      scanline 5S overflow detection (Nouspikel).
//   4. Sprite Y=$FF wraparound — y_raw > $D0 ⇒ line = y_raw - 256 + 1.
//      Two opaque sprites at Y=$FF and Y=$00 must collide on overlap.
//   5. Bug N°4 — collision range is the VISIBLE area [0, kScreenWidth)
//      ONLY; overscan-border overlaps do NOT collide (openMSX/meisei).
//      Real-silicon ground truth still unconfirmed (Test E).
//   6. Bug N°6 — bits 0..4 of statusReg = SAT index of last sprite
//      walked when bit 6 (5S) is NOT latched.

#include "TMS9918.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

void must(bool cond, const char* msg)
{
    if (!cond) {
        std::fprintf(stderr, "ASSERT FAIL: %s\n", msg);
        std::exit(1);
    }
}

// Helper: write a byte to VRAM via the chip's data port at a known
// address (avoids the silicon-strict timing windows by toggling strict
// off for the setup phase).
void vramWrite(TMS9918& vdp, uint16_t addr, uint8_t v)
{
    vdp.writeControl((uint8_t)(addr & 0xFF));
    vdp.writeControl((uint8_t)(0x40 | ((addr >> 8) & 0x3F)));
    vdp.writeData(v);
}

// Helper: read a byte from VRAM (sets read addr, then reads the
// pre-fetched first byte).
uint8_t vramRead(TMS9918& vdp, uint16_t addr)
{
    vdp.writeControl((uint8_t)(addr & 0xFF));
    vdp.writeControl((uint8_t)((addr >> 8) & 0x3F));
    return vdp.readData();
}

// Helper: write a SAT entry (Y, X, name, color) at SAT base $1B00.
void writeSAT(TMS9918& vdp, int slot, uint8_t y, uint8_t x, uint8_t name, uint8_t col)
{
    const uint16_t addr = 0x1B00 + slot * 4;
    vramWrite(vdp, addr,     y);
    vramWrite(vdp, addr + 1, x);
    vramWrite(vdp, addr + 2, name);
    vramWrite(vdp, addr + 3, col);
}

// Helper: write a register via the 2-byte CTRL protocol.
void writeReg(TMS9918& vdp, uint8_t regNum, uint8_t value)
{
    vdp.writeControl(value);
    vdp.writeControl((uint8_t)(0x80 | (regNum & 0x07)));
}

// Helper: configure standard Mode 0 + display ON + SAT @ $1B00 +
// sprite pattern @ $0000.
void initMode0(TMS9918& vdp)
{
    writeReg(vdp, 0, 0x00);          // R0 = 0 (Mode 0)
    writeReg(vdp, 1, 0xC0);          // R1 = display ON, 16K, no IRQ
    writeReg(vdp, 5, 0x36);          // R5 = SAT @ $1B00
    writeReg(vdp, 6, 0x00);          // R6 = sprite pattern @ $0000
    writeReg(vdp, 7, 0x01);          // R7 = backdrop colour 1 (black)
}

// Helper: drive the chip into the VBlank of a single frame — far enough that
// the per-scanline scan walked every active line and F is raised, but NOT past
// the frame rollover (which would reset the per-frame 5S flag before the caller
// reads it). 15000 sits inside the NTSC VBlank window [~12505, 17062).
void tickFrame(TMS9918& vdp)
{
    vdp.advanceCycles(15000);
}

// Helper: clear status sticky bits, then advance one frame, then
// snapshot the status (without clearing it again — caller decides).
uint8_t scanAndSnapshot(TMS9918& vdp)
{
    (void)vdp.readControl();          // clear sticky F (bit 7) + collision (bit 5);
                                      // 5S (bit 6) is re-derived by the scan below
    tickFrame(vdp);
    return vdp.readControl();         // returns post-scan status
}

} // namespace

int main()
{
    int n = 0;

    // --------------------------------------------------------------------
    // Test 1 — Bug N°8 isIllegalModeRegs detector.
    //
    // Valid combos (≤ 1 of M1/M2/M3 set) → false.
    // Illegal combos (≥ 2 set) → true.
    // --------------------------------------------------------------------
    {
        uint8_t r[8] = {0};
        // R0 bit 1 = M3, R1 bit 4 = M1, R1 bit 3 = M2.
        r[0] = 0x00; r[1] = 0x00; must(!TMS9918::isIllegalModeRegs(r), "isIllegal: all clear"); ++n;
        r[0] = 0x02; r[1] = 0x00; must(!TMS9918::isIllegalModeRegs(r), "isIllegal: M3 only"); ++n;
        r[0] = 0x00; r[1] = 0x10; must(!TMS9918::isIllegalModeRegs(r), "isIllegal: M1 only"); ++n;
        r[0] = 0x00; r[1] = 0x08; must(!TMS9918::isIllegalModeRegs(r), "isIllegal: M2 only"); ++n;
        r[0] = 0x02; r[1] = 0x10; must( TMS9918::isIllegalModeRegs(r), "isIllegal: M1+M3"); ++n;
        r[0] = 0x00; r[1] = 0x18; must( TMS9918::isIllegalModeRegs(r), "isIllegal: M1+M2"); ++n;
        r[0] = 0x02; r[1] = 0x08; must( TMS9918::isIllegalModeRegs(r), "isIllegal: M2+M3"); ++n;
        r[0] = 0x02; r[1] = 0x18; must( TMS9918::isIllegalModeRegs(r), "isIllegal: M1+M2+M3"); ++n;
    }

    // --------------------------------------------------------------------
    // Test 2 — $D0 mid-SAT stops scan (terminator semantics).
    //
    // Place 4 visible sprites at Y=50, then a $D0 terminator at SAT[4],
    // then 5 MORE sprites at Y=50 (slots 5..9 — would be 9 total
    // sprites at Y=50 → 5S overflow IF silicon scanned them all).
    // Silicon stops at $D0 → only 4 sprites scanned → bit 6 = 0.
    // --------------------------------------------------------------------
    {
        TMS9918 vdp;
        vdp.reset();
        initMode0(vdp);
        // 4 visible sprites
        for (int i = 0; i < 4; ++i)
            writeSAT(vdp, i, /*Y=*/49, /*X=*/(uint8_t)(i * 16), 0, 0x0F);
        // Terminator at slot 4
        writeSAT(vdp, 4, 0xD0, 0, 0, 0);
        // 5 more sprites at slots 5..9 (would overflow if scanned)
        for (int i = 5; i < 10; ++i)
            writeSAT(vdp, i, /*Y=*/49, /*X=*/(uint8_t)(i * 16), 0, 0x0F);
        const uint8_t s = scanAndSnapshot(vdp);
        must((s & 0x40) == 0, "T2: $D0 terminator stops scan, no 5S latched"); ++n;
        must((s & 0x1F) == 4, "T2: bits 0..4 = 4 (terminator slot)"); ++n;
    }

    // --------------------------------------------------------------------
    // Test 3 — color-0 sprite COUNTS in 5S overflow.
    //
    // 5 sprites at Y=50, slots 0..3 with color=$0F (visible) and slot 4
    // with color=$00 (invisible). Silicon: 5S latches because all 5
    // sprites are scanned (color is ignored for scan-count purposes).
    // Bits 0..4 = 4 (the color-0 sprite at slot 4).
    // --------------------------------------------------------------------
    {
        TMS9918 vdp;
        vdp.reset();
        initMode0(vdp);
        for (int i = 0; i < 4; ++i)
            writeSAT(vdp, i, 49, (uint8_t)(i * 16), 0, 0x0F);
        writeSAT(vdp, 4, 49, 64, 0, 0x00);  // color 0 — transparent
        writeSAT(vdp, 5, 0xD0, 0, 0, 0);
        const uint8_t s = scanAndSnapshot(vdp);
        must((s & 0x40) != 0, "T3: 5S latched even though slot 4 is color-0"); ++n;
        must((s & 0x1F) == 4, "T3: bits 0..4 = 4 (color-0 sprite at slot 4)"); ++n;
    }

    // --------------------------------------------------------------------
    // Test 4 — Sprite Y wraparound (Y=$FF ⇒ line 0).
    //
    // Sprite SAT byte 0 with raw value $FF means y = 0 (silicon convention
    // y_raw - 256 + 1). Two sprites at Y=$FF and Y=$00 (= line 1) at
    // same X with $FF pattern overlap on lines 1..7 → collision.
    // --------------------------------------------------------------------
    {
        TMS9918 vdp;
        vdp.reset();
        initMode0(vdp);
        // Fill sprite pattern 0 with $FF
        for (uint16_t a = 0; a < 8; ++a) vramWrite(vdp, a, 0xFF);
        // Sprites
        writeSAT(vdp, 0, 0xFF, 80, 0, 0x0F);   // line 0..7
        writeSAT(vdp, 1, 0x00, 80, 0, 0x0F);   // line 1..8
        writeSAT(vdp, 2, 0xD0, 0, 0, 0);
        const uint8_t s = scanAndSnapshot(vdp);
        must((s & 0x20) != 0, "T4: collision detected for Y=$FF + Y=$00 overlap"); ++n;
    }

    // --------------------------------------------------------------------
    // Test 5 — Collision range visible-only [0, kScreenWidth) per
    //          openMSX SpriteChecker.cc:187-191 + meisei vdp.c:587-589.
    //
    // Two early-clock sprites (color bit 7 = 1, X=10 → real X=-22),
    // Y=49, opaque pattern. Both sprites span pixels x=-22..-15, ENTIRELY
    // in the left-border / overscan area. Per openMSX (silicon source of
    // truth), sprite pixels in the border do NOT contribute to collision
    // detection — only visible [0, 256) pixels. So even though the two
    // sprites overlap exactly, NO collision should be latched.
    //
    // (Was inverted before mai 2026 — the test pinned the Nouspikel
    // [-32, 288) range, contradicting openMSX. POM1 now matches
    // openMSX; the test is updated to assert the new contract.)
    // --------------------------------------------------------------------
    {
        TMS9918 vdp;
        vdp.reset();
        initMode0(vdp);
        for (uint16_t a = 0; a < 8; ++a) vramWrite(vdp, a, 0xFF);
        writeSAT(vdp, 0, 49, 10, 0, 0x8F);  // early-clock + color 15
        writeSAT(vdp, 1, 49, 10, 0, 0x8F);
        writeSAT(vdp, 2, 0xD0, 0, 0, 0);
        const uint8_t s = scanAndSnapshot(vdp);
        must((s & 0x20) == 0, "T5: NO collision in overscan zone (real X=-22) — visible-only per openMSX"); ++n;
    }

    // --------------------------------------------------------------------
    // Test 6 — Bug N°6 last-sprite-walked semantics.
    //
    // 4 visible sprites + terminator at slot 4. No overflow → bit 6 = 0,
    // bits 0..4 = 4 (the SAT index of the terminator entry, which is
    // the last one the chip walked before stopping).
    // --------------------------------------------------------------------
    {
        TMS9918 vdp;
        vdp.reset();
        initMode0(vdp);
        for (int i = 0; i < 4; ++i)
            writeSAT(vdp, i, 49, (uint8_t)(i * 16), 0, 0x0F);
        writeSAT(vdp, 4, 0xD0, 0, 0, 0);
        const uint8_t s = scanAndSnapshot(vdp);
        must((s & 0x40) == 0, "T6: no 5S overflow with 4 visible sprites"); ++n;
        must((s & 0x1F) == 4, "T6: bits 0..4 = 4 (last sprite walked = terminator)"); ++n;
    }

    // --------------------------------------------------------------------
    // Test 7 — 5S latch FIRST occurrence (Nouspikel).
    //
    // 5 sprites at Y=50 (slots 0..4), 5 MORE sprites at Y=100 (slots
    // 5..9). Silicon raster scans top→bottom: first 5S overflow hits
    // at line 50. Bit 6 latches with bits 0..4 = 4. The Y=100 group
    // ALSO produces 5S, but the latch is sticky → bits 0..4 stay = 4.
    // --------------------------------------------------------------------
    {
        TMS9918 vdp;
        vdp.reset();
        initMode0(vdp);
        for (int i = 0; i < 5; ++i)
            writeSAT(vdp, i, 49, (uint8_t)(i * 16), 0, 0x0F);
        for (int i = 5; i < 10; ++i)
            writeSAT(vdp, i, 99, (uint8_t)((i - 5) * 16), 0, 0x0F);
        writeSAT(vdp, 10, 0xD0, 0, 0, 0);
        const uint8_t s = scanAndSnapshot(vdp);
        must((s & 0x40) != 0, "T7: bit 6 latched on first 5S occurrence"); ++n;
        must((s & 0x1F) == 4, "T7: bits 0..4 = 4 (FIRST 5S sprite, at slot 4)"); ++n;
    }

    // --------------------------------------------------------------------
    // Test 8 — Status sticky-on-read clears bits 5/6/7 (Nouspikel).
    //
    // After a collision frame, the FIRST status read returns the
    // bits set; the SECOND read returns them cleared.
    // --------------------------------------------------------------------
    {
        TMS9918 vdp;
        vdp.reset();
        initMode0(vdp);
        for (uint16_t a = 0; a < 8; ++a) vramWrite(vdp, a, 0xFF);
        writeSAT(vdp, 0, 49, 80, 0, 0x0F);
        writeSAT(vdp, 1, 49, 80, 0, 0x0F);
        writeSAT(vdp, 2, 0xD0, 0, 0, 0);
        (void)vdp.readControl();          // clear stale
        tickFrame(vdp);
        const uint8_t s1 = vdp.readControl();
        const uint8_t s2 = vdp.readControl();
        must((s1 & 0x20) != 0, "T8: 1st read shows collision bit 5"); ++n;
        must((s2 & 0x20) == 0, "T8: 2nd read has bit 5 cleared (sticky-on-read)"); ++n;
    }

    std::printf("tms9918_advanced_silicium: all %d assertions passed\n", n);
    return 0;
}
