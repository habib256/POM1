#include "GraphicsCard.h"

#include <algorithm>
#include <array>
#include <cstring>

// ─── MAME palette + artifact LUT ──────────────────────────────────────────
//
// Palette verbatim from MAME `apple2video.cpp::apple2_palette[]` — the
// reference sRGB values calibrated against real Apple II hardware. The
// artefact LUT resolves to a 4-bit palette index, indexed into this table
// for the final RGBA pixel. The same 16 entries drive the LORES blocks.
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

constexpr uint32_t kBlack = makeRgba(0x00, 0x00, 0x00);
constexpr uint32_t kWhite = makeRgba(0xff, 0xff, 0xff);

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

// Verbatim from MAME `apple2video.cpp` `artifact_color_lut[1]` — the
// composite_color_mode=1 **medium-color** variant (the GEN2's reference
// render; 4 bytes differ from the canonical row 0: indices 6, 48, 79, 121 —
// 0110000-class patterns map to a mid-tone instead of black/white, giving
// cleaner mid-tones at the cost of slightly uglier 40-col text). Each byte
// packs four 4-bit lo-res palette indices, one per NTSC sub-cycle phase;
// `rotl4b` selects which.
constexpr uint8_t kArtifactColorLut[128] = {
    0x00,0x00,0x00,0x00,0x88,0x00,0xcc,0x00,0x11,0x11,0x55,0x11,0x99,0x99,0xdd,0xff,
    0x22,0x22,0x66,0x66,0xaa,0xaa,0xee,0xee,0x33,0x33,0x33,0x33,0xbb,0xbb,0xff,0xff,
    0x00,0x00,0x44,0x44,0xcc,0xcc,0xcc,0xcc,0x55,0x55,0x55,0x55,0x99,0x99,0xdd,0xff,
    0x66,0x22,0x66,0x66,0xee,0xaa,0xee,0xee,0x77,0x77,0x77,0x77,0xff,0xff,0xff,0xff,
    0x00,0x00,0x00,0x00,0x88,0x88,0x88,0x88,0x11,0x11,0x55,0x11,0x99,0x99,0xdd,0x99,
    0x00,0x22,0x66,0x66,0xaa,0xaa,0xaa,0xaa,0x33,0x33,0x33,0x33,0xbb,0xbb,0xff,0xff,
    0x00,0x00,0x44,0x44,0xcc,0xcc,0xcc,0xcc,0x11,0x11,0x55,0x55,0x99,0x99,0xdd,0xdd,
    0x00,0x22,0x66,0x66,0xee,0xaa,0xee,0xee,0xff,0x33,0xff,0x77,0xff,0xff,0xff,0xff,
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

// Per-mode phosphor tint factors (linear scale per channel). Applied after
// the pixel is desaturated to luma. Values mirror Screen_ImGui's phosphor
// palette so the GEN2 mono modes match the text screen visually.
struct PhosphorTint { float r, g, b; };
constexpr PhosphorTint kPhosphorTints[4] = {
    {1.0f, 1.0f, 1.0f},  // Colour — unused (post-process bypassed)
    {0.15f, 1.00f, 0.20f}, // Green  — P1 phosphor
    {1.00f, 0.65f, 0.20f}, // Amber  — P3 phosphor
    {0.95f, 0.95f, 0.95f}, // Mono   — paper-white
};

inline uint32_t applyPhosphorTint(uint32_t rgba, GraphicsCard::MonitorMode m)
{
    if (m == GraphicsCard::MonitorMode::Colour) return rgba;
    const uint32_t r  = (rgba & 0xFFu);
    const uint32_t g  = (rgba >> 8)  & 0xFFu;
    const uint32_t bl = (rgba >> 16) & 0xFFu;
    // Rec.601 luma — same coefficient set Screen_ImGui uses for charmap.
    const float luma = (0.299f * r + 0.587f * g + 0.114f * bl);
    const PhosphorTint& t = kPhosphorTints[static_cast<int>(m)];
    auto clamp8 = [](float v) -> uint32_t {
        if (v < 0.0f) return 0u;
        if (v > 255.0f) return 255u;
        return static_cast<uint32_t>(v);
    };
    const uint32_t nr  = clamp8(luma * t.r);
    const uint32_t ng  = clamp8(luma * t.g);
    const uint32_t nbl = clamp8(luma * t.b);
    return (uint32_t(0xFFu) << 24) | (nbl << 16) | (ng << 8) | nr;
}

inline uint32_t lerpRgba(uint32_t newPix, uint32_t prevPix, float persistence)
{
    if (persistence <= 0.0f) return newPix;
    if (persistence >= 1.0f) return prevPix;
    const float a = persistence;
    const float b = 1.0f - a;
    auto mix = [&](uint32_t channelNew, uint32_t channelPrev) -> uint32_t {
        const float v = b * channelNew + a * channelPrev;
        return v < 0.0f ? 0u : (v > 255.0f ? 255u : static_cast<uint32_t>(v));
    };
    const uint32_t r  = mix(newPix & 0xFFu,       prevPix & 0xFFu);
    const uint32_t g  = mix((newPix >> 8)  & 0xFFu, (prevPix >> 8)  & 0xFFu);
    const uint32_t bl = mix((newPix >> 16) & 0xFFu, (prevPix >> 16) & 0xFFu);
    return (uint32_t(0xFFu) << 24) | (bl << 16) | (g << 8) | r;
}

// ─── Built-in 5×7 text font (port of POM2 Apple2Display) ──────────────────
//
// Bernie's release card carries a 2716 char-gen EPROM with the full-ASCII
// IIe-style encoding; no dump is published yet, so the GEN2 TEXT mode uses
// the same built-in 5×7 monospaced font POM2 falls back to without a char
// ROM. Packed 8 bytes per glyph (top→bottom), bits 0-4 = pixel pattern,
// MSB-left: bit 4 is the leftmost dot. Printable range $20-$7F; lowercase
// inherits the uppercase glyphs (matches the original Apple II look).
const uint8_t kAscii5x7[96 * 8] = {
    // 0x20 ' '
    0,0,0,0,0,0,0,0,
    // 0x21 '!'
    0x04,0x04,0x04,0x04,0x04,0x00,0x04,0x00,
    // 0x22 '"'
    0x0A,0x0A,0x0A,0,0,0,0,0,
    // 0x23 '#'
    0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A,0,
    // 0x24 '$'
    0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04,0,
    // 0x25 '%'
    0x19,0x19,0x02,0x04,0x08,0x13,0x13,0,
    // 0x26 '&'
    0x08,0x14,0x14,0x08,0x15,0x12,0x0D,0,
    // 0x27 '\''
    0x04,0x04,0x08,0,0,0,0,0,
    // 0x28 '('
    0x02,0x04,0x08,0x08,0x08,0x04,0x02,0,
    // 0x29 ')'
    0x08,0x04,0x02,0x02,0x02,0x04,0x08,0,
    // 0x2A '*'
    0x00,0x04,0x15,0x0E,0x15,0x04,0x00,0,
    // 0x2B '+'
    0x00,0x04,0x04,0x1F,0x04,0x04,0x00,0,
    // 0x2C ','
    0,0,0,0,0,0x04,0x04,0x08,
    // 0x2D '-'
    0x00,0x00,0x00,0x1F,0x00,0x00,0x00,0,
    // 0x2E '.'
    0,0,0,0,0,0x0C,0x0C,0,
    // 0x2F '/'
    0x01,0x01,0x02,0x04,0x08,0x10,0x10,0,
    // 0x30 '0'
    0x0E,0x11,0x13,0x15,0x19,0x11,0x0E,0,
    // 0x31 '1'
    0x04,0x0C,0x04,0x04,0x04,0x04,0x0E,0,
    // 0x32 '2'
    0x0E,0x11,0x01,0x02,0x04,0x08,0x1F,0,
    // 0x33 '3'
    0x0E,0x11,0x01,0x06,0x01,0x11,0x0E,0,
    // 0x34 '4'
    0x02,0x06,0x0A,0x12,0x1F,0x02,0x02,0,
    // 0x35 '5'
    0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E,0,
    // 0x36 '6'
    0x06,0x08,0x10,0x1E,0x11,0x11,0x0E,0,
    // 0x37 '7'
    0x1F,0x01,0x02,0x04,0x08,0x08,0x08,0,
    // 0x38 '8'
    0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E,0,
    // 0x39 '9'
    0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C,0,
    // 0x3A ':'
    0,0,0x0C,0x0C,0,0x0C,0x0C,0,
    // 0x3B ';'
    0,0,0x0C,0x0C,0,0x0C,0x04,0x08,
    // 0x3C '<'
    0x02,0x04,0x08,0x10,0x08,0x04,0x02,0,
    // 0x3D '='
    0,0,0x1F,0,0x1F,0,0,0,
    // 0x3E '>'
    0x08,0x04,0x02,0x01,0x02,0x04,0x08,0,
    // 0x3F '?'
    0x0E,0x11,0x01,0x02,0x04,0x00,0x04,0,
    // 0x40 '@'
    0x0E,0x11,0x01,0x0D,0x15,0x15,0x0E,0,
    // 0x41 'A'
    0x0E,0x11,0x11,0x11,0x1F,0x11,0x11,0,
    // 0x42 'B'
    0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E,0,
    // 0x43 'C'
    0x0E,0x11,0x10,0x10,0x10,0x11,0x0E,0,
    // 0x44 'D'
    0x1C,0x12,0x11,0x11,0x11,0x12,0x1C,0,
    // 0x45 'E'
    0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F,0,
    // 0x46 'F'
    0x1F,0x10,0x10,0x1E,0x10,0x10,0x10,0,
    // 0x47 'G'
    0x0E,0x11,0x10,0x17,0x11,0x11,0x0F,0,
    // 0x48 'H'
    0x11,0x11,0x11,0x1F,0x11,0x11,0x11,0,
    // 0x49 'I'
    0x0E,0x04,0x04,0x04,0x04,0x04,0x0E,0,
    // 0x4A 'J'
    0x07,0x02,0x02,0x02,0x02,0x12,0x0C,0,
    // 0x4B 'K'
    0x11,0x12,0x14,0x18,0x14,0x12,0x11,0,
    // 0x4C 'L'
    0x10,0x10,0x10,0x10,0x10,0x10,0x1F,0,
    // 0x4D 'M'
    0x11,0x1B,0x15,0x15,0x11,0x11,0x11,0,
    // 0x4E 'N'
    0x11,0x11,0x19,0x15,0x13,0x11,0x11,0,
    // 0x4F 'O'
    0x0E,0x11,0x11,0x11,0x11,0x11,0x0E,0,
    // 0x50 'P'
    0x1E,0x11,0x11,0x1E,0x10,0x10,0x10,0,
    // 0x51 'Q'
    0x0E,0x11,0x11,0x11,0x15,0x12,0x0D,0,
    // 0x52 'R'
    0x1E,0x11,0x11,0x1E,0x14,0x12,0x11,0,
    // 0x53 'S'
    0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E,0,
    // 0x54 'T'
    0x1F,0x04,0x04,0x04,0x04,0x04,0x04,0,
    // 0x55 'U'
    0x11,0x11,0x11,0x11,0x11,0x11,0x0E,0,
    // 0x56 'V'
    0x11,0x11,0x11,0x11,0x11,0x0A,0x04,0,
    // 0x57 'W'
    0x11,0x11,0x11,0x15,0x15,0x15,0x0A,0,
    // 0x58 'X'
    0x11,0x11,0x0A,0x04,0x0A,0x11,0x11,0,
    // 0x59 'Y'
    0x11,0x11,0x11,0x0A,0x04,0x04,0x04,0,
    // 0x5A 'Z'
    0x1F,0x01,0x02,0x04,0x08,0x10,0x1F,0,
    // 0x5B '['
    0x0E,0x08,0x08,0x08,0x08,0x08,0x0E,0,
    // 0x5C '\\'
    0x10,0x10,0x08,0x04,0x02,0x01,0x01,0,
    // 0x5D ']'
    0x0E,0x02,0x02,0x02,0x02,0x02,0x0E,0,
    // 0x5E '^'
    0x04,0x0A,0x11,0,0,0,0,0,
    // 0x5F '_'
    0,0,0,0,0,0,0x1F,0,
    // 0x60 '`'
    0x08,0x04,0x02,0,0,0,0,0,
    // $61-$7F zero-filled: lowercase remaps to uppercase in resolveGlyph().
};

// Map a screen byte to a glyph row pattern + video attributes (Apple II
// text encoding — the GEN2 char-gen follows the same convention):
//   $00-$3F  inverse   ─ low 6 bits = char index (always inverse)
//   $40-$7F  flashing  ─ low 6 bits = char index (alternates ~2 Hz)
//   $80-$FF  normal    ─ low 7 bits = ASCII
void resolveGlyph(uint8_t screenByte, uint8_t out[8], bool& invert, bool& flash)
{
    uint8_t ascii;
    flash = false;
    if (screenByte & 0x80) {
        invert = false;
        ascii  = screenByte & 0x7F;
    } else {
        invert = true;
        flash  = (screenByte & 0x40) != 0;   // bit 6 set → FLASH attribute
        const uint8_t idx6 = screenByte & 0x3F;
        ascii = (idx6 < 0x20) ? static_cast<uint8_t>(0x40 + idx6) : idx6;
    }

    // Lowercase fallback to uppercase (no char-ROM dump for the GEN2 yet).
    if (ascii >= 0x61 && ascii <= 0x7A) ascii = static_cast<uint8_t>(ascii - 0x20);

    if (ascii >= 0x20 && ascii <= 0x7F) {
        std::memcpy(out, &kAscii5x7[(ascii - 0x20) * 8], 8);
    } else {
        const uint8_t box[8] = { 0x1F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1F, 0 };
        std::memcpy(out, box, 8);
    }
}

// Resolve a screen byte into the cell's 8 rows as 7-bit lit masks: bit `gx`
// (0 = leftmost) set ⇔ pixel gx is lit, with inverse + flash applied.
std::array<uint8_t, 8> glyphRows7(uint8_t screenByte, bool flashPhase)
{
    std::array<uint8_t, 8> rows{};
    uint8_t glyph[8];
    bool invert = false, flash = false;
    resolveGlyph(screenByte, glyph, invert, flash);
    if (flash && flashPhase) invert = !invert;
    for (int gy = 0; gy < 8; ++gy) {
        const uint8_t row8 = glyph[gy];
        uint8_t bits = 0;
        for (int gx = 0; gx < 7; ++gx) {
            bool lit = (gx >= 1 && gx <= 5) && ((row8 >> (5 - gx)) & 1);
            if (invert) lit = !lit;
            if (lit) bits |= static_cast<uint8_t>(1u << gx);
        }
        rows[gy] = bits;
    }
    return rows;
}

// Text rows [rowLo, rowHi) whose 8-scanline cells INTERSECT the band
// [scanY0, scanY1) — rounded OUTWARD, so a row straddling a beam split is
// returned for both bands; each band's painter clips its writes to its own
// scanline window (clipY0/clipY1), so the straddled row paints each
// scanline exactly once with the state active for ITS band.
int bandRows(int scanY0, int scanY1, int rowLo, int rowHi, int* outLo, int* outHi)
{
    const int lo = std::max(rowLo, scanY0 / 8);
    const int hi = std::min(rowHi, (scanY1 + 7) / 8);
    *outLo = lo;
    *outHi = hi;
    return lo < hi ? 1 : 0;
}

int bandScanlines(int scanY0, int scanY1, int lineLo, int lineHi, int* outLo, int* outHi)
{
    const int lo = std::max(lineLo, scanY0);
    const int hi = std::min(lineHi, scanY1);
    *outLo = lo;
    *outHi = hi;
    return lo < hi ? 1 : 0;
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

uint16_t GraphicsCard::hgrRowAddress(int y, bool page2)
{
    // Apple II HIRES non-linear memory layout: 192 lines split into 3
    // groups of 64, each interleaved in blocks of 8. Woz reused the row
    // counter for DRAM refresh.
    int group = y / 64;          // 0, 1, 2
    int subGroup = (y % 64) / 8; // 0-7
    int line = y % 8;            // 0-7
    return static_cast<uint16_t>(page2 ? 0x4000 : 0x2000)
         + static_cast<uint16_t>(group) * 0x28
         + static_cast<uint16_t>(subGroup) * 0x80
         + static_cast<uint16_t>(line) * 0x400;
}

uint16_t GraphicsCard::scanlineAddress(int y)
{
    return hgrRowAddress(y, false);
}

uint16_t GraphicsCard::textRowAddress(int y, bool page2)
{
    // Apple II text/lo-res row interleave: addr = base + 0x80*(y%8) +
    // 0x28*(y/8) — the DRAM-refresh dance again, text flavour.
    const uint16_t base = page2 ? 0x0800 : 0x0400;
    return static_cast<uint16_t>(base + 0x80 * (y & 7) + 0x28 * (y >> 3));
}

GraphicsCard::RasterPos GraphicsCard::frameCycleToPos(uint64_t emuCycle,
                                                      uint64_t linesPerFrame)
{
    // Verbatim POM2 Apple2Display::frameCycleToPos. VBL lines collapse to
    // kHiresHeight (192) so the segment builder ignores them (they govern
    // the NEXT frame, whose start state already includes their effect).
    const uint64_t rawLine = (emuCycle / Gen2VideoScanner::kCyclesPerLine)
                             % linesPerFrame;
    const int scanline = rawLine < static_cast<uint64_t>(kHiresHeight)
                             ? static_cast<int>(rawLine)
                             : kHiresHeight;
    // The 40-byte visible window opens at horizontal cycle 25 (the first 25
    // cycles of each scanline are horizontal blanking). A switch thrown in
    // HBL (hpos < 25) lands at byteCol 0 → it governs the whole upcoming
    // line; a switch inside the window splits the line at that byte
    // boundary. v1 is exact at the column boundary (same scope as POM2);
    // the transition cycle within a character clock is a later refinement.
    const int hpos = static_cast<int>(emuCycle % Gen2VideoScanner::kCyclesPerLine);
    const int byteCol = std::clamp(hpos - 25, 0, 40);
    return {scanline, byteCol};
}

void GraphicsCard::applyVideoEvent(DisplayState& state, EventKind kind, bool value)
{
    switch (kind) {
        case EventKind::TextMode:  state.textMode  = value; break;
        case EventKind::MixedMode: state.mixedMode = value; break;
        case EventKind::Page2:     state.page2     = value; break;
        case EventKind::HiRes:     state.hiRes     = value; break;
    }
}

void GraphicsCard::forEachBeamSegment(
    const DisplayState& frameStart,
    std::vector<Event> events,
    uint64_t linesPerFrame,
    const std::function<void(const DisplayState&, int, int, int, int)>& paint)
{
    // ── Double-buffer page flips (DROL-class) vs beam-raced page splits ──
    // A frame whose PAGE2 events all go ONE direction is a buffer flip, not
    // beam racing: the game flips, then spends the next frames redrawing the
    // page it just hid. Replaying that flip at its raster position would
    // paint the band ABOVE it from the now-hidden page — but we read RAM at
    // render time, not at beam time, so that band shows the page MID-REDRAW
    // (half-erased sprites → strong flicker; the real beam saw it pristine).
    // Apply the final page frame-wide instead. Real beam-raced effects flip
    // BOTH directions within a frame and keep exact replay. (Verbatim POM2.)
    bool pageOn = false, pageOff = false;
    for (const auto& e : events)
        if (e.kind == EventKind::Page2)
            (e.value ? pageOn : pageOff) = true;
    DisplayState start = frameStart;
    if (pageOn != pageOff) {
        start.page2 = pageOn;
        events.erase(std::remove_if(events.begin(), events.end(),
                         [](const Event& e) { return e.kind == EventKind::Page2; }),
                     events.end());
    }

    // Raster order: scanline ascending, then byte column within the line.
    // (Stable so two switches at the same beam position keep push order.)
    std::stable_sort(events.begin(), events.end(),
        [linesPerFrame](const Event& a, const Event& b) {
            const RasterPos pa = frameCycleToPos(a.emuCycle, linesPerFrame);
            const RasterPos pb = frameCycleToPos(b.emuCycle, linesPerFrame);
            if (pa.scanline != pb.scanline) return pa.scanline < pb.scanline;
            return pa.byteCol < pb.byteCol;
        });

    // Per visible scanline, the ordered list of column segments [prevEnd,
    // colEnd) and the display state active across each. Events on a scanline
    // subdivide it at their byteCol; the end-of-line state carries into the
    // next line. A scanline with no events is a single full-width [0, 40)
    // segment — the common case.
    struct Seg { int colEnd; DisplayState st; };
    std::array<std::vector<Seg>, kHiresHeight> perLine;

    DisplayState cur = start;
    size_t ei = 0;
    for (int y = 0; y < kHiresHeight; ++y) {
        std::vector<Seg> segs;
        int prevCol = 0;
        while (ei < events.size()
               && frameCycleToPos(events[ei].emuCycle, linesPerFrame).scanline == y) {
            const int col = frameCycleToPos(events[ei].emuCycle, linesPerFrame).byteCol;
            if (col > prevCol) { segs.push_back({col, cur}); prevCol = col; }
            applyVideoEvent(cur, events[ei].kind, events[ei].value);
            ++ei;
        }
        segs.push_back({40, cur});
        perLine[y] = std::move(segs);
    }

    // Merge vertically-adjacent scanlines with identical segmentation into
    // one band, then paint each band's column segments. A run of event-free
    // lines collapses to a single full-width paint.
    auto sameSegs = [](const std::vector<Seg>& a, const std::vector<Seg>& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i].colEnd != b[i].colEnd || !(a[i].st == b[i].st))
                return false;
        return true;
    };

    int bandY0 = 0;
    for (int y = 1; y <= kHiresHeight; ++y) {
        if (y == kHiresHeight || !sameSegs(perLine[y], perLine[bandY0])) {
            int col0 = 0;
            for (const auto& s : perLine[bandY0]) {
                paint(s.st, bandY0, y, col0, s.colEnd);
                col0 = s.colEnd;
            }
            bandY0 = y;
        }
    }
}

void GraphicsCard::renderInternalBand(const uint8_t* memory, const DisplayState& state,
                                      int scanY0, int scanY1)
{
    renderInternalSegment(memory, state, scanY0, scanY1, 0, 40);
}

void GraphicsCard::renderInternalSegment(const uint8_t* memory, const DisplayState& state,
                                         int scanY0, int scanY1, int col0, int col1)
{
    if (scanY0 >= scanY1 || col0 >= col1) return;

    // POM2 renderInternalBand's legacy 280-wide decision tree, with the
    // column window threaded in. Mixed mode paints the graphics over the
    // full band then repaints rows 20-23 as text (clipped to the band).
    int gLo = 0, gHi = 0, tLo = 0, tHi = 0;
    if (state.textMode) {
        if (bandRows(scanY0, scanY1, 0, 24, &tLo, &tHi))
            renderText(memory, state, tLo, tHi, col0, col1, scanY0, scanY1);
    } else if (state.hiRes) {
        if (bandScanlines(scanY0, scanY1, 0, kHiresHeight, &gLo, &gHi))
            renderHiRes(memory, state, gLo, gHi, col0, col1);
        if (state.mixedMode && bandRows(scanY0, scanY1, 20, 24, &tLo, &tHi))
            renderText(memory, state, tLo, tHi, col0, col1, scanY0, scanY1);
    } else {
        if (bandScanlines(scanY0, scanY1, 0, 48 * 4, &gLo, &gHi))
            renderLoRes(memory, state, gLo / 4, (gHi + 3) / 4, col0, col1, gLo, gHi);
        if (state.mixedMode && bandRows(scanY0, scanY1, 20, 24, &tLo, &tHi))
            renderText(memory, state, tLo, tHi, col0, col1, scanY0, scanY1);
    }
}

void GraphicsCard::renderText(const uint8_t* memory, const DisplayState& state,
                              int firstRow, int lastRow, int col0, int col1,
                              int clipY0, int clipY1)
{
    col0 = std::max(0, col0);
    col1 = std::min(40, col1);
    if (col0 >= col1) return;

    // GEN2 TEXT is B&W (Bernie's spec sheet: "TEXT 40×25, B&W"); 24 rows of
    // 8 scanlines fill the 192-line frame like the Apple II. Flash phase
    // toggles every kFlashHalfPeriodFrames frames (~2 Hz at 60 fps).
    const bool flashPhase = (frameCounter / kFlashHalfPeriodFrames) & 1u;

    for (int row = firstRow; row < lastRow; ++row) {
        const uint16_t rowAddr = textRowAddress(row, state.page2);
        for (int col = col0; col < col1; ++col) {
            const uint8_t src = memory[rowAddr + col];
            const int cellX = col * 7;
            const int cellY = row * 8;

            const auto rows = glyphRows7(src, flashPhase);
            for (int gy = 0; gy < 8; ++gy) {
                const int y = cellY + gy;
                if (y < clipY0 || y >= clipY1) continue;   // beam-split clip
                uint32_t* outRow = pixelBuf.data()
                    + static_cast<size_t>(y) * kHiresWidth;
                for (int gx = 0; gx < 7; ++gx)
                    outRow[cellX + gx] = ((rows[gy] >> gx) & 1u) ? kWhite : kBlack;
            }
        }
    }
}

void GraphicsCard::renderLoRes(const uint8_t* memory, const DisplayState& state,
                               int firstRow, int lastRow, int col0, int col1,
                               int clipY0, int clipY1)
{
    col0 = std::max(0, col0);
    col1 = std::min(40, col1);
    if (col0 >= col1) return;
    // Lo-res draws 40 columns × 48 rows of 7×4 colour blocks in the text
    // page. Each text byte stores TWO blocks: low nibble is the upper
    // block, high nibble the lower one. (Port of POM2 renderLoRes.)
    for (int blockRow = firstRow; blockRow < lastRow; ++blockRow) {
        const int textRow = blockRow / 2;
        const bool upperHalf = (blockRow % 2 == 0);
        const uint16_t rowAddr = textRowAddress(textRow, state.page2);
        for (int col = col0; col < col1; ++col) {
            const uint8_t b = memory[rowAddr + col];
            const uint8_t nibble = upperHalf ? (b & 0x0F) : (b >> 4);
            const uint32_t rgb = kApple2Palette[nibble];
            const int x0 = col * 7;
            const int y0 = blockRow * 4;
            for (int dy = 0; dy < 4; ++dy) {
                const int y = y0 + dy;
                if (y < clipY0 || y >= clipY1) continue;   // beam-split clip
                uint32_t* outRow = pixelBuf.data()
                    + static_cast<size_t>(y) * kHiresWidth;
                for (int dx = 0; dx < 7; ++dx)
                    outRow[x0 + dx] = rgb;
            }
        }
    }
}

void GraphicsCard::renderHiRes(const uint8_t* memory, const DisplayState& state,
                               int firstScanline, int lastScanline,
                               int col0, int col1)
{
    // Column window in framebuffer pixels (each byte = 7 px). The scanline
    // is always decoded in full so the NTSC artifact sliding window keeps
    // its neighbour-byte context across the split; only the write-back is
    // clipped to [px0, px1) — identical to POM2 renderHiRes.
    const int px0 = std::clamp(col0, 0, 40) * 7;
    const int px1 = std::clamp(col1, 0, 40) * 7;
    if (px0 >= px1) return;
    for (int y = firstScanline; y < lastScanline; ++y) {
        rasterizeHgrLine(y, memory, hgrRowAddress(y, state.page2), px0, px1);
    }
}

void GraphicsCard::rasterizeHgrLine(int y, const uint8_t* memory, uint16_t rowAddr,
                                    int px0, int px1)
{
    // MAME-style 7-bit sliding-window decode. ContextBits = 3 leaves the
    // centre sub-pixel at bit 3 of the window, with 3 bits of left context
    // (the tail of the previous byte) and 3 bits of right context (the
    // head of the next byte) on either side.
    constexpr int kContextBits = 3;

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
    // the 7 MHz pixel grid. Write-back clipped to the segment's window.
    uint32_t* outRow = pixelBuf.data() + static_cast<size_t>(y) * kHiresWidth;
    for (int x = px0; x < px1; ++x) {
        outRow[x] = avgRgb(subPixels[2 * x], subPixels[2 * x + 1]);
    }
}

void GraphicsCard::rasterizeLine(int y, const uint8_t* memory)
{
    rasterizeHgrLine(y, memory, scanlineAddress(y), 0, kHiresWidth);
}

void GraphicsCard::postProcessLine(int y)
{
    if (monitorMode == MonitorMode::Colour && phosphorPersistence <= 0.0f) return;
    uint32_t* outRow = pixelBuf.data() + static_cast<size_t>(y) * kHiresWidth;
    uint32_t* prevRow = prevPixelBuf.data() + static_cast<size_t>(y) * kHiresWidth;
    for (int x = 0; x < kHiresWidth; ++x) {
        uint32_t pix = applyPhosphorTint(outRow[x], monitorMode);
        if (phosphorPersistence > 0.0f) {
            pix = lerpRgba(pix, prevRow[x], phosphorPersistence);
        }
        outRow[x] = pix;
    }
}

bool GraphicsCard::render(const uint8_t* memory,
                          const DisplayState& endState,
                          const DisplayState& frameStart,
                          const std::vector<Event>& events,
                          uint64_t linesPerFrame)
{
    ++frameCounter;   // FLASH text attribute phase

    // Fast path: no beam events and the latch sits at the classic
    // GRAPHICS+HIRES+PAGE1 state every pre-Phase-2 HGR program uses — run
    // the original per-scanline-diffed rasteriser unchanged.
    const bool legacyHgr = !endState.textMode && endState.hiRes
                        && !endState.mixedMode && !endState.page2;
    if (events.empty() && legacyHgr) {
        return rasterizeToBuffer(memory);
    }

    // Full repaint (non-HGR mode, page 2, or beam-raced splits). Poison the
    // fast-path line cache so a later return to plain HGR repaints from
    // scratch instead of trusting a cache the full path didn't maintain.
    invalidateNext = true;
    if (phosphorPersistence > 0.0f) {
        std::memcpy(prevPixelBuf.data(), pixelBuf.data(),
                    sizeof(uint32_t) * kHiresWidth * kHiresHeight);
    }
    if (events.empty()) {
        renderInternalBand(memory, endState, 0, kHiresHeight);
    } else {
        forEachBeamSegment(frameStart, events, linesPerFrame,
            [&](const DisplayState& st, int y0, int y1, int c0, int c1) {
                renderInternalSegment(memory, st, y0, y1, c0, c1);
            });
    }
    for (int y = 0; y < kHiresHeight; ++y) {
        postProcessLine(y);
    }
    return true;
}

bool GraphicsCard::rasterizeToBuffer(const uint8_t* memory)
{
    // Plain memcmp against the per-line 40-byte cache from the previous
    // frame. The MAME decode is deterministic in those 40 bytes (the
    // sliding window stays inside the row), so a byte-for-byte compare is
    // both correct and the cheapest diff available.
    //
    // Persistence > 0 bypasses the diff: even idle frames must lerp toward
    // the prev buffer or trails would freeze.
    const bool persistenceOn = phosphorPersistence > 0.0f;
    const bool forceAll = invalidateNext || persistenceOn;
    invalidateNext = false;
    bool anyChanged = false;
    // Snapshot last frame's pixels for lerp before rewriting them.
    if (persistenceOn) {
        std::memcpy(prevPixelBuf.data(), pixelBuf.data(),
                    sizeof(uint32_t) * kHiresWidth * kHiresHeight);
    }
    for (int y = 0; y < kHiresHeight; ++y) {
        const uint16_t lineAddr = scanlineAddress(y);
        const uint8_t* src = memory + lineAddr;
        const bool ramChanged = std::memcmp(src, lineCopy[y].data(), 40) != 0;
        if (forceAll || ramChanged) {
            std::memcpy(lineCopy[y].data(), src, 40);
            rasterizeLine(y, memory);
            postProcessLine(y);
            anyChanged = true;
        }
    }
    return anyChanged;
}
