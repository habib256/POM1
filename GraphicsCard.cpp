#include "GraphicsCard.h"
#include <algorithm>

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
                                 int col, quint8 byte, int bit, int screenX, bool group2) const
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

void GraphicsCard::render(ImDrawList* drawList, ImVec2 origin, float pixelScale,
                          const quint8* memory) const
{
    // Glow parameters
    const float glowAlpha = 0.18f;
    const float glowPadX = std::max(1.5f, pixelScale * 0.9f);
    const float glowPadY = std::max(1.5f, pixelScale * 0.9f);
    const float rounding = std::max(0.5f, pixelScale * 0.22f);

    // First pass: draw glow behind all lit pixels
    for (int y = 0; y < kHiresHeight; ++y) {
        const uint16_t lineAddr = scanlineAddress(y);
        const float py = origin.y + y * pixelScale;

        int screenX = 0;
        for (int col = 0; col < 40; ++col) {
            const quint8 byte = memory[lineAddr + col];
            const bool group2 = (byte & 0x80) != 0;

            for (int bit = 0; bit < 7; ++bit) {
                const bool on = (byte & (1 << bit)) != 0;

                if (on) {
                    ImU32 color = resolveColor(memory, lineAddr, col, byte, bit, screenX, group2);
                    ImVec4 colorF = ImGui::ColorConvertU32ToFloat4(color);
                    colorF.w *= glowAlpha;
                    ImU32 glowColor = ImGui::ColorConvertFloat4ToU32(colorF);

                    const float px = origin.x + screenX * pixelScale;
                    drawList->AddRectFilled(
                        ImVec2(px - glowPadX, py - glowPadY),
                        ImVec2(px + pixelScale + glowPadX, py + pixelScale + glowPadY),
                        glowColor, rounding + glowPadY);
                }

                ++screenX;
            }
        }
    }

    // Second pass: draw solid pixels on top
    for (int y = 0; y < kHiresHeight; ++y) {
        const uint16_t lineAddr = scanlineAddress(y);
        const float py = origin.y + y * pixelScale;

        int screenX = 0;
        for (int col = 0; col < 40; ++col) {
            const quint8 byte = memory[lineAddr + col];
            const bool group2 = (byte & 0x80) != 0;

            for (int bit = 0; bit < 7; ++bit) {
                const bool on = (byte & (1 << bit)) != 0;

                if (on) {
                    ImU32 color = resolveColor(memory, lineAddr, col, byte, bit, screenX, group2);
                    const float px = origin.x + screenX * pixelScale;
                    drawList->AddRectFilled(
                        ImVec2(px, py),
                        ImVec2(px + pixelScale, py + pixelScale),
                        color, rounding);
                }

                ++screenX;
            }
        }
    }
}
