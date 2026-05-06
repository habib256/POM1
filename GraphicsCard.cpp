#include "GraphicsCard.h"

#include <array>
#include <cstring>

// ─── MAME palette + artifact LUT ──────────────────────────────────────────
//
// Palette verbatim from MAME `apple2video.cpp::apple2_palette[]` — the
// reference sRGB values calibrated against real Apple II hardware. The
// artefact LUT resolves to a 4-bit palette index, indexed into this table
// for the final RGBA pixel.
namespace {

constexpr uint32_t makeRgba(uint8_t r, uint8_t g, uint8_t b)
{
    // GL_RGBA + GL_UNSIGNED_BYTE on little-endian = 0xAABBGGRR in memory.
    return (uint32_t(0xFFu) << 24)
         | (static_cast<uint32_t>(b) << 16)
         | (static_cast<uint32_t>(g) << 8)
         |  static_cast<uint32_t>(r);
}

constexpr uint32_t kApple2Palette[16] = {
    makeRgba(0x00, 0x00, 0x00), //  0 Black
    makeRgba(0xa7, 0x0b, 0x40), //  1 Dark Red
    makeRgba(0x40, 0x1c, 0xf7), //  2 Dark Blue
    makeRgba(0xe6, 0x28, 0xff), //  3 Purple
    makeRgba(0x00, 0x74, 0x40), //  4 Dark Green
    makeRgba(0x80, 0x80, 0x80), //  5 Dark Gray
    makeRgba(0x19, 0x90, 0xff), //  6 Medium Blue
    makeRgba(0xbf, 0x9c, 0xff), //  7 Light Blue
    makeRgba(0x40, 0x63, 0x00), //  8 Brown
    makeRgba(0xe6, 0x6f, 0x00), //  9 Orange
    makeRgba(0x80, 0x80, 0x80), // 10 Light Gray
    makeRgba(0xff, 0x8b, 0xbf), // 11 Pink
    makeRgba(0x19, 0xd7, 0x00), // 12 Light Green
    makeRgba(0xbf, 0xe3, 0x08), // 13 Yellow
    makeRgba(0x58, 0xf4, 0xbf), // 14 Aquamarine
    makeRgba(0xff, 0xff, 0xff), // 15 White
};

constexpr int kStreamLen = 560;   // 280 visible color clocks × 2 sub-pixels

// Bit doubler. `kBitDoubler[i]` is the 14-bit word obtained by replacing
// each of the 7 low bits of i with a doubled (b, b) pair (LSB = leftmost
// sub-pixel, matching the HGR convention where bit 0 is the leftmost
// pixel of the byte).
constexpr std::array<uint16_t, 128> makeBitDoubler()
{
    std::array<uint16_t, 128> t{};
    for (unsigned i = 1; i < 128; ++i)
        t[i] = static_cast<uint16_t>(t[i >> 1] * 4 + (i & 1) * 3);
    return t;
}
constexpr std::array<uint16_t, 128> kBitDoubler = makeBitDoubler();

// Verbatim from MAME `apple2video.cpp` `artifact_color_lut[0]` (composite
// /NTSC variant). Each byte packs four 4-bit lo-res palette indices, one
// per NTSC sub-cycle phase; `rotl4b` selects which.
constexpr uint8_t kArtifactColorLut[128] = {
    0x00,0x00,0x00,0x00,0x88,0x00,0x00,0x00,0x11,0x11,0x55,0x11,0x99,0x99,0xdd,0xff,
    0x22,0x22,0x66,0x66,0xaa,0xaa,0xee,0xee,0x33,0x33,0x33,0x33,0xbb,0xbb,0xff,0xff,
    0x00,0x00,0x44,0x44,0xcc,0xcc,0xcc,0xcc,0x55,0x55,0x55,0x55,0x99,0x99,0xdd,0xff,
    0x00,0x22,0x66,0x66,0xee,0xaa,0xee,0xee,0x77,0x77,0x77,0x77,0xff,0xff,0xff,0xff,
    0x00,0x00,0x00,0x00,0x88,0x88,0x88,0x88,0x11,0x11,0x55,0x11,0x99,0x99,0xdd,0xff,
    0x00,0x22,0x66,0x66,0xaa,0xaa,0xaa,0xaa,0x33,0x33,0x33,0x33,0xbb,0xbb,0xff,0xff,
    0x00,0x00,0x44,0x44,0xcc,0xcc,0xcc,0xcc,0x11,0x11,0x55,0x55,0x99,0x99,0xdd,0xdd,
    0x00,0x22,0x66,0x66,0xee,0xaa,0xee,0xee,0xff,0xff,0xff,0x77,0xff,0xff,0xff,0xff,
};

// rotl4b(n, count) — extract the 4-bit nibble of `n` at logical position
// `count` (mod 4). Maps to the NTSC phase rotation MAME uses.
constexpr unsigned rotl4b(unsigned n, unsigned count)
{
    return (n >> ((-static_cast<int>(count)) & 3)) & 0x0fu;
}

// Decode 40 HGR bytes into a 40-element array of 14-bit doubled words,
// applying the half-dot delay when the source byte's MSB is set.
inline void buildHgrWordRow(const uint8_t* ram, uint16_t rowAddr,
                            uint16_t (&words)[40])
{
    unsigned last_output_bit = 0;
    for (int col = 0; col < 40; ++col) {
        const uint8_t b = ram[rowAddr + col];
        uint16_t word = kBitDoubler[b & 0x7Fu];
        if (b & 0x80u) {
            word = static_cast<uint16_t>(((word << 1) | last_output_bit) & 0x3FFFu);
        }
        words[col] = word;
        last_output_bit = (word >> 13) & 1u;
    }
}

// Pair-average two RGBA pixels (chroma-bandwidth downsample).
inline uint32_t avgRgb(uint32_t a, uint32_t b)
{
    const uint32_t r  = ((a & 0xFFu)         + (b & 0xFFu))         >> 1;
    const uint32_t g  = (((a >> 8)  & 0xFFu) + ((b >> 8)  & 0xFFu)) >> 1;
    const uint32_t bl = (((a >> 16) & 0xFFu) + ((b >> 16) & 0xFFu)) >> 1;
    return (uint32_t(0xFFu) << 24) | (bl << 16) | (g << 8) | r;
}

} // namespace

// ─── GraphicsCard ─────────────────────────────────────────────────────────

GraphicsCard::GraphicsCard()
{
    invalidate();
}

void GraphicsCard::invalidate()
{
    invalidateNext = true;
}

uint16_t GraphicsCard::scanlineAddress(int y)
{
    // Apple II HIRES non-linear memory layout: 192 lines split into 3
    // groups of 64, each interleaved in blocks of 8. Woz reused the row
    // counter for DRAM refresh.
    int group = y / 64;          // 0, 1, 2
    int subGroup = (y % 64) / 8; // 0-7
    int line = y % 8;            // 0-7
    return kHiresBase
         + static_cast<uint16_t>(group) * 0x28
         + static_cast<uint16_t>(subGroup) * 0x80
         + static_cast<uint16_t>(line) * 0x400;
}

void GraphicsCard::rasterizeLine(int y, const uint8_t* memory)
{
    // MAME-style 7-bit sliding-window decode. ContextBits = 3 leaves the
    // centre sub-pixel at bit 3 of the window, with 3 bits of left context
    // (the tail of the previous byte) and 3 bits of right context (the
    // head of the next byte) on either side.
    constexpr int kContextBits = 3;
    const uint16_t rowAddr = scanlineAddress(y);

    uint16_t words[40];
    buildHgrWordRow(memory, rowAddr, words);

    uint32_t subPixels[kStreamLen];

    // `w` accumulates up to (3 + 14 + 14) = 31 bits — fits a uint32_t.
    // Each iteration consumes one bit (`>>= 1`).
    uint32_t w = static_cast<uint32_t>(words[0]) << kContextBits;
    for (int col = 0; col < 40; ++col) {
        if (col + 1 < 40) {
            w |= static_cast<uint32_t>(words[col + 1])
                 << (14 + kContextBits);
        }
        for (int b = 0; b < 14; ++b) {
            const int absX = col * 14 + b;
            const uint8_t lutEntry = kArtifactColorLut[w & 0x7Fu];
            const unsigned loresIdx = rotl4b(lutEntry, static_cast<unsigned>(absX));
            subPixels[absX] = kApple2Palette[loresIdx];
            w >>= 1;
        }
    }

    // Downsample 560 → 280 by pair averaging — the chroma-bandwidth limit
    // a real CRT applies. Without it the 14 MHz bit pattern aliases against
    // the 7 MHz pixel grid.
    uint32_t* outRow = pixelBuf.data() + static_cast<size_t>(y) * kHiresWidth;
    for (int x = 0; x < kHiresWidth; ++x) {
        outRow[x] = avgRgb(subPixels[2 * x], subPixels[2 * x + 1]);
    }
}

bool GraphicsCard::rasterizeToBuffer(const uint8_t* memory)
{
    // Plain memcmp against the per-line 40-byte cache from the previous
    // frame. The MAME decode is deterministic in those 40 bytes (the
    // sliding window stays inside the row), so a byte-for-byte compare is
    // both correct and the cheapest diff available.
    const bool forceAll = invalidateNext;
    invalidateNext = false;
    bool anyChanged = false;
    for (int y = 0; y < kHiresHeight; ++y) {
        const uint16_t lineAddr = scanlineAddress(y);
        const uint8_t* src = memory + lineAddr;
        if (forceAll || std::memcmp(src, lineCopy[y].data(), 40) != 0) {
            std::memcpy(lineCopy[y].data(), src, 40);
            rasterizeLine(y, memory);
            anyChanged = true;
        }
    }
    return anyChanged;
}
