#include "GraphicsCard.h"
#include "imgui.h"
#include <array>
#include <cstring>

namespace {

// 7 pixels (uint32 RGBA) per (colParity, byte) combination → 14 KB total.
// colParity selects which absolute screenX-parity the byte starts on, which
// in turn selects violet/blue (even) vs green/orange (odd) for an isolated
// lit pixel. Inter-byte white-bleed is handled separately at the 39 seams.
using HgrPixelRow = std::array<uint32_t, 7>;
using HgrPixelTable = std::array<HgrPixelRow, 512>;

// NTSC artifact colors (Apple II HIRES palette).
constexpr uint32_t kBlack  = IM_COL32(0, 0, 0, 255);
constexpr uint32_t kWhite  = IM_COL32(255, 255, 255, 255);
constexpr uint32_t kViolet = IM_COL32(148, 33, 246, 255);   // group 1, even screenX
constexpr uint32_t kGreen  = IM_COL32(20, 245, 60, 255);    // group 1, odd screenX
constexpr uint32_t kBlue   = IM_COL32(20, 207, 253, 255);   // group 2, even screenX
constexpr uint32_t kOrange = IM_COL32(255, 106, 60, 255);   // group 2, odd screenX

// Resolve one pixel assuming the byte is isolated: bit 0 has no left
// neighbour (would-be byte-1 bit 6 is treated as off), bit 6 has no right
// neighbour (would-be byte+1 bit 0 is treated as off). The seam pass in
// rasterizeLine corrects the two affected pixels when the actual neighbour
// is on.
constexpr uint32_t computeIsolatedPixel(int byte, int bit, int colParity)
{
    const bool on = (byte & (1 << bit)) != 0;
    if (!on) return kBlack;

    const bool prevOn = (bit > 0) && ((byte & (1 << (bit - 1))) != 0);
    const bool nextOn = (bit < 6) && ((byte & (1 << (bit + 1))) != 0);
    if (prevOn || nextOn) return kWhite;

    const bool group2 = (byte & 0x80) != 0;
    const bool even = ((colParity + bit) & 1) == 0;
    if (!group2) return even ? kViolet : kGreen;
    return even ? kBlue : kOrange;
}

const HgrPixelTable& hgrPixelTable()
{
    static const HgrPixelTable table = []{
        HgrPixelTable t{};
        for (int parity = 0; parity < 2; ++parity) {
            for (int byte = 0; byte < 256; ++byte) {
                for (int bit = 0; bit < 7; ++bit) {
                    t[(parity << 8) | byte][bit] =
                        computeIsolatedPixel(byte, bit, parity);
                }
            }
        }
        return t;
    }();
    return table;
}

} // namespace

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

void GraphicsCard::rasterizeLine(int y, const quint8* memory)
{
    const uint16_t lineAddr = scanlineAddress(y);
    uint32_t* row = rawPixelBuf.data() + static_cast<size_t>(y) * kHiresWidth;
    const auto& table = hgrPixelTable();

    for (int col = 0; col < 40; ++col) {
        const quint8 byte = memory[lineAddr + col];
        const int parity = col & 1;
        const HgrPixelRow& pix = table[(parity << 8) | byte];
        std::memcpy(row + col * 7, pix.data(), sizeof(HgrPixelRow));
    }

    // White-bleed across the 39 inter-byte seams: when bit 6 of the current
    // byte and bit 0 of the next byte are both lit, both pixels turn white.
    // The LUT was built assuming no external neighbour, so the only case it
    // gets wrong is precisely this both-on seam — patch it here.
    for (int col = 0; col < 39; ++col) {
        const quint8 cur = memory[lineAddr + col];
        const quint8 nxt = memory[lineAddr + col + 1];
        if ((cur & 0x40) && (nxt & 0x01)) {
            uint32_t* seam = row + col * 7 + 6;
            seam[0] = kWhite;
            seam[1] = kWhite;
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
    if (anyChanged) applyGlow();
    return anyChanged;
}

void GraphicsCard::applyGlow()
{
    // Horizontal-only additive glow. Each lit lateral neighbour contributes
    // kGlowHNum/kGlowHDen of its colour into the current black pixel,
    // summed and clamped per channel. 9/20 per neighbour → a single-edge
    // halo at 45 % brightness, and a black pixel sandwiched between two
    // identical lits is filled to 90 % of the source colour.
    constexpr int kGlowHNum = 9;
    constexpr int kGlowHDen = 20;

    for (int y = 0; y < kHiresHeight; ++y) {
        const uint32_t* rowCur = rawPixelBuf.data() + static_cast<size_t>(y) * kHiresWidth;
        uint32_t* outRow = pixelBuf.data() + static_cast<size_t>(y) * kHiresWidth;

        for (int x = 0; x < kHiresWidth; ++x) {
            const uint32_t c = rowCur[x];
            if ((c & 0x00FFFFFFu) != 0) {
                outRow[x] = c;
                continue;
            }
            const uint32_t L = (x > 0)               ? rowCur[x - 1] : 0u;
            const uint32_t R = (x + 1 < kHiresWidth) ? rowCur[x + 1] : 0u;

            const int sr = static_cast<int>(L & 0xFFu)         + static_cast<int>(R & 0xFFu);
            const int sg = static_cast<int>((L >> 8) & 0xFFu)  + static_cast<int>((R >> 8) & 0xFFu);
            const int sb = static_cast<int>((L >> 16) & 0xFFu) + static_cast<int>((R >> 16) & 0xFFu);

            int r = (sr * kGlowHNum) / kGlowHDen;
            int g = (sg * kGlowHNum) / kGlowHDen;
            int b = (sb * kGlowHNum) / kGlowHDen;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            outRow[x] = IM_COL32(r, g, b, 255);
        }
    }
}
