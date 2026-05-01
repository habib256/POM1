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
#include <string_view>
#include "CpuClock.h"
#include "Peripheral.h"
#include "imgui.h"

class TMS9918 : public pom1::Peripheral
{
public:
    std::string_view name() const override { return "TMS9918"; }

    static constexpr int kScreenWidth  = 256;
    static constexpr int kScreenHeight = 192;
    static constexpr int kVramSize     = 0x4000; // 16 KB

    struct Snapshot {
        std::array<uint8_t, 0x4000> vram{};
        std::array<uint8_t, 8> regs{};
        uint8_t statusReg = 0;
        bool siliconStrictMode = false;
    };

    TMS9918();

    // I/O interface (called from Memory::memRead / memWrite)
    void     writeData(uint8_t value);    // $CC00 write
    uint8_t  readData();                  // $CC00 read
    void     writeControl(uint8_t value); // $CC01 write
    uint8_t  readControl();               // $CC01 read

    // Cycle counting — generates frame flag (~60 Hz)
    void advanceCycles(int cycles);

    // Silicon-accuracy checks used by real-hardware presets. When enabled,
    // R1 bit 7 selects 4K/16K VRAM addressing and CPU accesses to $CC00/$CC01
    // must leave enough 6502 cycles for the VDP's internal access window.
    void setSiliconStrictMode(bool enabled);
    bool isSiliconStrictMode() const { return siliconStrictMode; }

    // Cumulative count of VDP writes (data + control port) dropped because
    // siliconStrictMode was on and `canAcceptAccess()` rejected the byte.
    // Exposed in the status bar next to the STRICT/FANTASY tag so users can
    // see at a glance how often a program violates the access window.
    uint64_t droppedWriteCount() const { return droppedWrites; }
    void resetDroppedWriteCount() { droppedWrites = 0; }

    void reset();

    void copySnapshot(Snapshot& out);

    // Render from snapshot into a 256×192 RGBA pixel buffer (kScreenWidth × kScreenHeight).
    // IM_COL32 byte order matches GL_RGBA / GL_UNSIGNED_BYTE, so the buffer can be uploaded
    // directly to an OpenGL texture for nearest-neighbour display at arbitrary window sizes.
    static void renderToBuffer(uint32_t* pixels, const Snapshot& snap);

    static const ImU32 kPalette[16];

private:
    // Display mode helpers — write into pixel buffer
    static void renderGraphicsI  (uint32_t* pixels, const Snapshot& s, ImU32 backdrop);
    static void renderGraphicsII (uint32_t* pixels, const Snapshot& s, ImU32 backdrop);
    static void renderText       (uint32_t* pixels, const Snapshot& s, ImU32 backdrop);
    static void renderMulticolor (uint32_t* pixels, const Snapshot& s, ImU32 backdrop);
    static void renderSprites    (uint32_t* pixels, const Snapshot& s);

    // Per-VBLANK sprite scan that updates statusReg sticky bits 5 (collision)
    // and 6 (5S) + the 5S sprite SAT index in low 5 bits. Bit-pattern based
    // (color=0 sprites still collide). Live VRAM, called from advanceCycles.
    static void scanSpritesForStatus(const uint8_t* vram, const uint8_t* regs,
                                     bool strict, uint8_t& statusOut);
    static uint16_t vramMaskForRegs(const uint8_t* regs, bool strict);
    uint16_t liveVramMask() const;
    int requiredAccessCycles() const;
    bool canAcceptAccess() const;
    void noteAcceptedAccess();

private:
    std::array<uint8_t, 0x4000> vram{};
    std::array<uint8_t, 8>      regs{};
    uint8_t statusReg       = 0;
    uint8_t controlLatch    = 0;
    bool    latchIsSecond   = false;  // true = next write is the 2nd byte
    uint16_t vramAddr       = 0;
    uint8_t readAheadBuffer = 0;
    int frameCycleCounter   = 0;
    int cyclesSinceIoAccess = 1000000;
    bool siliconStrictMode  = false;
    uint64_t droppedWrites  = 0;      // cumulative count of VDP writes dropped by siliconStrictMode
    bool snapshotDirty = true;        // skip the 16 KB VRAM + regs copy when nothing changed since last publish

    static constexpr int kCyclesPerFrame = POM1_CPU_CYCLES_PER_FRAME_1X_60HZ;
    // Active display covers 192 of the 262 NTSC scanlines; the remaining 70
    // are VBlank. Sized in CPU cycles so requiredAccessCycles() can decide
    // whether the beam is currently scanning visible pixels (window = strict)
    // or in VBlank (window = relaxed). Driven by frameCycleCounter, NOT by
    // statusReg bit 7 — that flag is sticky-until-readControl, so software
    // that never polls $CC01 (e.g. Galaga) leaves it set indefinitely and
    // would otherwise free-pass every cycle as "VBlank".
    static constexpr int kActiveDisplayCycles = (kCyclesPerFrame * 192) / 262;
};

#endif // TMS9918_H
