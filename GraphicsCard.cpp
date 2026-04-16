#include "GraphicsCard.h"
#include <algorithm>

GraphicsCard::GraphicsCard()
{
    invalidate();
}

void GraphicsCard::invalidate()
{
    // Mark every line dirty so the next rasterize pass repaints the whole
    // buffer. The pixel buffer itself is left zeroed (default-constructed),
    // matching the "card just powered on, framebuffer is whatever junk
    // happens to be in $2000" semantics — the next call will overwrite it.
    lineHash.fill(0xFFFFFFFFu);
}

uint16_t GraphicsCard::scanlineAddress(int y)
{
    // Apple II HIRES non-linear memory layout:
    // The 192 lines are split into 3 groups of 64 lines.
    // Within each group, lines are interleaved in blocks of 8.
    int group = y / 64;          // 0, 1, 2
    int subGroup = (y % 64) / 8; // 0-7
    int line = y % 8;            // 0-7
    return kHiresBase
         + static_cast<uint16_t>(group) * 0x28
         + static_cast<uint16_t>(subGroup) * 0x80
         + static_cast<uint16_t>(line) * 0x400;
}

ImU32 GraphicsCard::resolveColor(const quint8* memory, uint16_t lineAddr,
                                 int col, quint8 byte, int bit, int screenX, bool group2)
{
    bool prevOn = false;
    bool nextOn = false;

    if (screenX > 0) {
        if (bit > 0) {
            prevOn = (byte & (1 << (bit - 1))) != 0;
        } else if (col > 0) {
            prevOn = (memory[lineAddr + col - 1] & (1 << 6)) != 0;
        }
    }
    if (screenX < kHiresWidth - 1) {
        if (bit < 6) {
            nextOn = (byte & (1 << (bit + 1))) != 0;
        } else if (col < 39) {
            nextOn = (memory[lineAddr + col + 1] & 1) != 0;
        }
    }

    if (prevOn || nextOn) {
        return kWhite;
    }
    bool even = (screenX % 2) == 0;
    if (!group2) {
        return even ? kViolet : kGreen;
    }
    return even ? kBlue : kOrange;
}

void GraphicsCard::rasterizeLine(int y, const quint8* memory)
{
    const uint16_t lineAddr = scanlineAddress(y);
    uint32_t* row = pixelBuf.data() + static_cast<size_t>(y) * kHiresWidth;

    int screenX = 0;
    for (int col = 0; col < 40; ++col) {
        const quint8 byte = memory[lineAddr + col];
        const bool group2 = (byte & 0x80) != 0;

        for (int bit = 0; bit < 7; ++bit) {
            const bool on = (byte & (1 << bit)) != 0;
            if (on) {
                row[screenX] = resolveColor(memory, lineAddr, col, byte, bit, screenX, group2);
            } else {
                row[screenX] = kBlack;
            }
            ++screenX;
        }
    }
}

bool GraphicsCard::rasterizeToBuffer(const quint8* memory)
{
    bool anyChanged = false;
    for (int y = 0; y < kHiresHeight; ++y) {
        // FNV-1a over the 40 framebuffer bytes for this scanline.
        // We also fold in the previous and next scanline's first/last bytes
        // because resolveColor() looks at lineAddr ± 1 for artifact-colour
        // continuation; without that, a write to col 39 of line N could
        // leave line N's hash unchanged even though col 0 of line N+1 (no,
        // resolveColor only looks within the same line — neighbours are
        // *within* the row). Single-row hash is sufficient.
        const uint16_t lineAddr = scanlineAddress(y);
        const quint8* src = memory + lineAddr;
        uint32_t h = 0x811C9DC5u; // FNV-1a offset basis
        for (int i = 0; i < 40; ++i) {
            h ^= src[i];
            h *= 0x01000193u;
        }

        if (h != lineHash[y]) {
            lineHash[y] = h;
            rasterizeLine(y, memory);
            anyChanged = true;
        }
    }
    return anyChanged;
}
