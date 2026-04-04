#include "GraphicsCard.h"

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

void GraphicsCard::render(ImDrawList* drawList, ImVec2 origin, float pixelScale,
                          const quint8* memory) const
{
    for (int y = 0; y < kHiresHeight; ++y) {
        const uint16_t lineAddr = scanlineAddress(y);
        const float py = origin.y + y * pixelScale;

        int screenX = 0; // pixel column 0-279
        for (int col = 0; col < 40; ++col) {
            const quint8 byte = memory[lineAddr + col];
            const bool group2 = (byte & 0x80) != 0;

            for (int bit = 0; bit < 7; ++bit) {
                const bool on = (byte & (1 << bit)) != 0;

                // Determine color using NTSC artifact rules
                ImU32 color;
                if (!on) {
                    color = kBlack;
                } else {
                    // Check adjacent pixels for white detection
                    bool prevOn = false;
                    bool nextOn = false;

                    if (screenX > 0) {
                        // Previous pixel: either in this byte or previous byte
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
                        color = kWhite;
                    } else {
                        // Isolated pixel: color depends on even/odd column and group
                        bool even = (screenX % 2) == 0;
                        if (!group2) {
                            color = even ? kViolet : kGreen;
                        } else {
                            color = even ? kBlue : kOrange;
                        }
                    }
                }

                if (color != kBlack) {
                    const float px = origin.x + screenX * pixelScale;
                    drawList->AddRectFilled(
                        ImVec2(px, py),
                        ImVec2(px + pixelScale, py + pixelScale),
                        color);
                }

                ++screenX;
            }
        }
    }
}
