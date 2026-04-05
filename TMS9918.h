// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// TMS9918 Video Display Processor emulation
// Implements the P-LAB Apple-1 Graphic Card
// https://p-l4b.github.io/graphic/

#ifndef TMS9918_H
#define TMS9918_H

#include <array>
#include <cstdint>
#include "imgui.h"

class TMS9918
{
public:
    static constexpr int kScreenWidth  = 256;
    static constexpr int kScreenHeight = 192;
    static constexpr int kVramSize     = 0x4000; // 16 KB

    struct Snapshot {
        std::array<uint8_t, 0x4000> vram{};
        std::array<uint8_t, 8> regs{};
        uint8_t statusReg = 0;
    };

    TMS9918();

    // I/O interface (called from Memory::memRead / memWrite)
    void     writeData(uint8_t value);    // $CC00 write
    uint8_t  readData();                  // $CC00 read
    void     writeControl(uint8_t value); // $CC01 write
    uint8_t  readControl();               // $CC01 read

    // Cycle counting — generates frame flag (~60 Hz)
    void advanceCycles(int cycles);

    void reset();

    void copySnapshot(Snapshot& out) const;

    // Render from snapshot data (called on UI thread)
    static void render(ImDrawList* drawList, ImVec2 origin, float pixelScale,
                       const Snapshot& snap);

    static const ImU32 kPalette[16];

private:
    // Display mode helpers (operate on snapshot)
    static void renderGraphicsI  (ImDrawList* dl, ImVec2 org, float ps, const Snapshot& s, ImU32 backdrop);
    static void renderGraphicsII (ImDrawList* dl, ImVec2 org, float ps, const Snapshot& s, ImU32 backdrop);
    static void renderText       (ImDrawList* dl, ImVec2 org, float ps, const Snapshot& s, ImU32 backdrop);
    static void renderMulticolor (ImDrawList* dl, ImVec2 org, float ps, const Snapshot& s, ImU32 backdrop);
    static void renderSprites    (ImDrawList* dl, ImVec2 org, float ps, const Snapshot& s);

    static inline void drawPixel(ImDrawList* dl, float x, float y, float ps, ImU32 col)
    {
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + ps, y + ps), col);
    }

private:
    std::array<uint8_t, 0x4000> vram{};
    std::array<uint8_t, 8>      regs{};
    uint8_t statusReg       = 0;
    uint8_t controlLatch    = 0;
    bool    latchIsSecond   = false;  // true = next write is the 2nd byte
    uint16_t vramAddr       = 0;
    uint8_t readAheadBuffer = 0;
    int frameCycleCounter   = 0;

    static constexpr int kCyclesPerFrame = 16667; // ~60 Hz at 1 MHz
};

#endif // TMS9918_H
