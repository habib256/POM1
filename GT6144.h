// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// SWTPC GT-6144 Graphic Terminal (Southwest Technical Products, 1976)
//
// The first commercial graphics board available for the Apple-1: $98.50,
// standalone 64 x 96 monochrome framebuffer on 6 x Intel 2102 SRAM chips,
// write-only I/O port at $D00A on the Apple-1 bus (PIA chip-select A3
// decoding). Originally designed for the SWTPC 6800; community-adapted to
// the 6502 via a second PIA 6820.
//
// Protocol — 4-phase state machine on the single write-only port:
//   byte < 64        : latch X = byte,      pixel OFF (black)
//   64  <= byte <128 : latch X = byte - 64, pixel ON  (white)
//   128 <= byte <224 : commit Y = byte - 128 using the latched X + state
//   224 <= byte      : control opcode; (byte & 0x07) selects:
//                        0 INVERTED SCREEN    (240/224/232/248)
//                        1 NORMAL  SCREEN     (241/225/233/249, also 7/231/...)
//                        2 DISABLE CT-1024    (no-op on Apple-1 standalone)
//                        3 ENABLE  CT-1024    (no-op on Apple-1 standalone)
//                        4 ENABLE  GRAPHICS   (unblank)
//                        5 BLANKED GRAPHICS   (solid-black at video stage)
//                        6 NOT USED           (true hardware no-op)
//                        7 NORMAL  SCREEN     (redundant alias)
// Bits 3-4 are don't-cares: 224/232/240/248 all map to INVERTED SCREEN.
//
// Inversion and blanking modify the analog video pipeline, not the SRAM —
// the framebuffer bits stay put across mode toggles. POM1 mirrors that at
// the render stage, applying `inverted` / `blanked` in renderToBuffer()
// instead of rewriting the framebuffer.
//
// Power-on state: Intel 2102 SRAM cells are bistable flip-flops; they
// settle to arbitrary 0/1 values on power-up — visible as a "random
// rectangles" pattern on the real hardware, which the period's test
// programs explicitly clear before drawing. reset() seeds the framebuffer
// with std::mt19937(std::random_device) to reproduce that behaviour.

#ifndef GT6144_H
#define GT6144_H

#include "Peripheral.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <string_view>

class GT6144 : public pom1::Peripheral
{
public:
    std::string_view name() const override { return "GT-6144"; }
    std::string_view mutexLabel() const override { return "GT6144::cardMutex"; }

    static constexpr int kWidth  = 64;
    static constexpr int kHeight = 96;
    static constexpr int kFramebufferBytes = (kWidth * kHeight) / 8;  // 768
    static constexpr uint16_t kIoPort = 0xD00A;

    GT6144();

    // Reseed the framebuffer with pseudo-random bits (bistable SRAM model)
    // and clear the FSM latches + video-stage flags (inverted/blanked).
    // Also invoked from Memory::setGT6144Enabled(true) so replugging the
    // card produces fresh power-on noise.
    void reset();

    // Write a byte to the I/O port. Dispatches through the 4-phase state
    // machine described at the top of the file.
    void writeCommand(uint8_t byte);

    struct Snapshot {
        std::array<uint8_t, kFramebufferBytes> framebuffer{};
        bool inverted = false;
        bool blanked  = false;
        uint8_t latchedX    = 0;
        uint8_t latchedMode = 0;   // 0 = OFF, 1 = ON
        bool    awaitY      = false;
    };

    void copySnapshot(Snapshot& out) const;

    // Fill `out` (kWidth x kHeight RGBA pixels, IM_COL32 byte order, same
    // convention as TMS9918::renderToBuffer) from a previously captured
    // snapshot. Applies inversion + blanking at render time — the framebuffer
    // bits in `snap` are never altered.
    static void renderToBuffer(uint32_t* out, const Snapshot& snap);

private:
    mutable std::mutex cardMutex;
    std::array<uint8_t, kFramebufferBytes> framebuffer{};
    bool inverted = false;
    bool blanked  = false;
    uint8_t latchedX    = 0;
    uint8_t latchedMode = 0;
    bool    awaitY      = false;
};

#endif // GT6144_H
