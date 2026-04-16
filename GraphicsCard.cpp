#include "GraphicsCard.h"
#include <algorithm>
#include <cstring>

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
    invalidateNext = true;
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
    // Plain memcmp against the per-line 40-byte cache from the previous frame.
    // resolveColor() only inspects neighbours within the same row, so a row's
    // pixels are fully determined by its 40 framebuffer bytes — a byte-for-
    // byte compare is both correct and the cheapest diff available (the
    // compiler + libc vectorise 40-byte memcmp to a couple of SIMD loads).
    const bool forceAll = invalidateNext;
    invalidateNext = false;
    bool anyChanged = false;
    for (int y = 0; y < kHiresHeight; ++y) {
        const uint16_t lineAddr = scanlineAddress(y);
        const quint8* src = memory + lineAddr;
        if (forceAll || std::memcmp(src, lineCopy[y].data(), 40) != 0) {
            std::memcpy(lineCopy[y].data(), src, 40);
            rasterizeLine(y, memory);
            anyChanged = true;
        }
    }
    return anyChanged;
}
