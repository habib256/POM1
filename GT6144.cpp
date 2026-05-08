// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// See GT6144.h for protocol and hardware notes.

#include "GT6144.h"
#include "SnapshotIO.h"

#include <random>

GT6144::GT6144()
{
    reset();
}

void GT6144::reset()
{
    std::lock_guard<std::mutex> lock(cardMutex);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& byte : framebuffer) byte = static_cast<uint8_t>(dist(gen));
    inverted = false;
    blanked  = false;
    latchedX    = 0;
    latchedMode = 0;
    awaitY      = false;
}

void GT6144::writeCommand(uint8_t byte)
{
    std::lock_guard<std::mutex> lock(cardMutex);

    if (byte >= 224) {
        // Control opcode. Bits 5-7 mark "command"; bits 3-4 are don't-cares
        // (so 224/232/240/248 all alias). The 3-bit sub-opcode is bits 0-2.
        switch (byte & 0x07) {
            case 0: inverted = true;  break;  // INVERTED SCREEN
            case 1: inverted = false; break;  // NORMAL  SCREEN
            case 2: /* DISABLE CT-1024 — no-op on Apple-1 */ break;
            case 3: /* ENABLE  CT-1024 — no-op on Apple-1 */ break;
            case 4: blanked  = false; break;  // ENABLE GRAPHICS (unblank)
            case 5: blanked  = true;  break;  // BLANKED GRAPHICS
            case 6: /* NOT USED — hardware no-op */          break;
            case 7: inverted = false; break;  // NORMAL SCREEN (alias)
        }
        return;
    }

    if (byte < 64) {
        latchedX    = byte;
        latchedMode = 0;       // OFF / black
        awaitY      = true;
        return;
    }

    if (byte < 128) {
        latchedX    = static_cast<uint8_t>(byte - 64);
        latchedMode = 1;       // ON / white
        awaitY      = true;
        return;
    }

    // byte < 224 here: Y-commit phase.
    if (!awaitY) return;
    const uint8_t y = static_cast<uint8_t>(byte - 128);
    if (y >= kHeight) return;
    if (latchedX >= kWidth) return;

    const int index = y * (kWidth / 8) + (latchedX >> 3);
    const uint8_t mask = static_cast<uint8_t>(0x80 >> (latchedX & 7));
    if (latchedMode) framebuffer[index] |=  mask;
    else             framebuffer[index] &= ~mask;
}

void GT6144::copySnapshot(Snapshot& out) const
{
    std::lock_guard<std::mutex> lock(cardMutex);
    out.framebuffer = framebuffer;
    out.inverted    = inverted;
    out.blanked     = blanked;
    out.latchedX    = latchedX;
    out.latchedMode = latchedMode;
    out.awaitY      = awaitY;
}

void GT6144::serialize(pom1::SnapshotWriter& w) const
{
    std::lock_guard<std::mutex> lock(cardMutex);
    w.writeBytes(framebuffer.data(), framebuffer.size());
    w.writeU8(inverted ? 1 : 0);
    w.writeU8(blanked  ? 1 : 0);
    w.writeU8(latchedX);
    w.writeU8(latchedMode);
    w.writeU8(awaitY ? 1 : 0);
}

void GT6144::deserialize(pom1::SnapshotReader& r)
{
    std::lock_guard<std::mutex> lock(cardMutex);
    r.readBytes(framebuffer.data(), framebuffer.size());
    inverted    = r.readU8() != 0;
    blanked     = r.readU8() != 0;
    latchedX    = r.readU8();
    latchedMode = r.readU8();
    awaitY      = r.readU8() != 0;
}

void GT6144::renderToBuffer(uint32_t* out, const Snapshot& snap)
{
    // IM_COL32 byte order on little-endian: 0xAABBGGRR. Matches GL_RGBA +
    // GL_UNSIGNED_BYTE the same way TMS9918::renderToBuffer does.
    constexpr uint32_t kWhite = 0xFFFFFFFFu;
    constexpr uint32_t kBlack = 0xFF000000u;

    if (snap.blanked) {
        for (int i = 0; i < kWidth * kHeight; ++i) out[i] = kBlack;
        return;
    }

    const uint32_t litColor   = snap.inverted ? kBlack : kWhite;
    const uint32_t unlitColor = snap.inverted ? kWhite : kBlack;

    for (int y = 0; y < kHeight; ++y) {
        const int rowBase = y * (kWidth / 8);
        for (int xByte = 0; xByte < kWidth / 8; ++xByte) {
            const uint8_t byte = snap.framebuffer[rowBase + xByte];
            for (int bit = 0; bit < 8; ++bit) {
                const bool lit = (byte & (0x80 >> bit)) != 0;
                out[y * kWidth + xByte * 8 + bit] = lit ? litColor : unlitColor;
            }
        }
    }
}
