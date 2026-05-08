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
#include <cstdio>
#include <cstddef>
#include <string_view>
#include <unordered_map>
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

    // TMS9918 NTSC visible region with R7-coloured borders. openMSX-aligned
    // dimensions (~24 lines top + 24 bottom = 48 vertical border ; 32 px
    // left + 32 right = 64 horizontal border). The active 256×192 image
    // sits centred inside the 320×240 frame. R7 lower 4 bits paints the
    // border (cf. dev/SILICONBUGS.md, "border colour" overscan rendering).
    static constexpr int kBorderTop    = 24;
    static constexpr int kBorderBottom = 24;
    static constexpr int kBorderLeft   = 32;
    static constexpr int kBorderRight  = 32;
    static constexpr int kFullWidth    = kScreenWidth  + kBorderLeft + kBorderRight;  // 320
    static constexpr int kFullHeight   = kScreenHeight + kBorderTop  + kBorderBottom; // 240

    struct Snapshot {
        std::array<uint8_t, 0x4000> vram{};
        std::array<uint8_t, 8> regs{};
        uint8_t statusReg = 0;
        bool siliconStrictMode = false;
        // Persistent 320×240 RGBA framebuffer rendered progressively at the
        // emulation-thread side (per scanline crossing). The UI reads this
        // verbatim — no further per-snapshot rendering is required. Mid-frame
        // R7 / R1 / VRAM changes affect only the lines drawn *after* the
        // change, matching silicon's progressive raster behaviour.
        std::array<uint32_t, 320 * 240> framebuffer{};
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
    void resetDroppedWriteCount();

    // Drop diagnostics — fine-grained per-PC histogram + per-port + per-mode
    // breakdown. Reset on `setSiliconStrictMode()` and `resetDroppedWriteCount()`
    // so a "test run" can be bounded by a strict-mode toggle. Exposed read-only;
    // dump via `dumpDropDiagnostics()` for human-readable triage of the offending
    // STA sites and modes.
    enum SlotTableId : uint8_t {
        kSlotTableScreenOff = 0,
        kSlotTableGfx12     = 1,
        kSlotTableGfx3      = 2,
        kSlotTableText      = 3,
        kSlotTableCount     = 4,
    };
    struct DropDiagnostics {
        uint64_t total       = 0;          // == droppedWrites
        uint64_t writeData   = 0;          // $CC00 writes dropped
        uint64_t writeCtrl   = 0;          // $CC01 writes dropped
        uint64_t byTable[kSlotTableCount] = {0,0,0,0};
        uint64_t inActive    = 0;          // dropped during active display (frameCycle < kActiveDisplayCycles)
        uint64_t inVBlank    = 0;          // dropped during VBlank (frameCycle >= kActiveDisplayCycles)
        // PC histogram. STA $CC00 is 3 bytes — captured PC = STA addr + 3.
        // The disassembly site is at PC-3 (or PC-2 for STA absX/absY = 3 bytes
        // too, or PC-2 for STA $CC00,X via abs,X also 3 bytes). Always look at
        // PC-3 first, then walk back if the opcode at PC-3 is not an STA.
        std::unordered_map<uint16_t, uint64_t> byPc;
    };
    const DropDiagnostics& dropDiagnostics() const { return dropStats; }
    // Pretty-print the diagnostics to a stream (stderr by default).
    // Format: header + table breakdown + port breakdown + top-N PC histogram.
    void dumpDropDiagnostics(std::FILE* out = stderr, int topN = 16) const;

    // /INT line state. The TMS9918 silicon pulls /INT low when R1 bit 5
    // (IRQ enable) is set AND status bit 7 (F frame flag) is sticky;
    // reading $CC01 clears F → /INT self-de-asserts on the next CPU tick.
    //
    // **P-LAB original card does NOT wire /INT to /IRQ** — the standard
    // Apple-1 usage (and the one the Nippur72 libs target) is **polling
    // via $CC01 bit 7**. POM1 therefore keeps the IRQ wiring disabled by
    // default (`irqStrapped == false`); irqAsserted() always returns
    // false in that case, even if a program were to set R1.5. An FPGA
    // strap that bridges /INT to /IRQ (community mod, not P-LAB stock)
    // is modelled by setting `irqStrapped = true` — useful when running
    // MSX-derived code that expects an IRQ-on-VBlank handler.
    // Cf. dev/SILICONBUGS.md Bug N°2.
    bool irqAsserted() const {
        if (!irqStrapped) return false;
        return (regs[1] & 0x20) && (statusReg & 0x80);
    }
    void setIrqStrapped(bool on) { irqStrapped = on; }
    bool isIrqStrapped() const   { return irqStrapped; }

    // Caller's program counter at the moment of the next VDP access.
    // Memory::memWrite snapshots cpuForIrq->getProgramCounter() into here
    // right before invoking writeData/writeControl/readData/readControl,
    // so the drop trace can name the offending STA/LDA site directly
    // (e.g. "drop at PC=$5A04" instead of grepping the source for the
    // matching value). Note: PC is sampled mid-instruction — for a
    // 3-byte `STA $CC00` the captured PC = (STA addr + 3), so the user
    // looks for the STA at PC-3 in the disassembly.
    void setLastAccessPc(uint16_t pc) { lastAccessPc = pc; }

    void reset();

    void copySnapshot(Snapshot& out);

    // Render from snapshot into a 256×192 RGBA pixel buffer (kScreenWidth × kScreenHeight).
    // IM_COL32 byte order matches GL_RGBA / GL_UNSIGNED_BYTE, so the buffer can be uploaded
    // directly to an OpenGL texture for nearest-neighbour display at arbitrary window sizes.
    static void renderToBuffer(uint32_t* pixels, const Snapshot& snap);

    // Render with R7-coloured borders into a 320×240 RGBA buffer. The 256×192
    // active image is centred at offset (kBorderLeft, kBorderTop). Outside
    // the active rect, every pixel is painted with kPalette[regs[7] & 0x0F]
    // — the silicon's overscan/border colour. Used by the Hardware →
    // TMS9918 panel so users can see colours that touch only R7 (typical
    // demo "rainbow line" effects use the border bands as a separate
    // canvas from the main playfield).
    static void renderToBufferWithBorder(uint32_t* pixels, const Snapshot& snap);

    static const ImU32 kPalette[16];

    // Bug N°8 detector — public so unit tests can verify it. Returns
    // true when 2 or more of M1/M2/M3 are simultaneously set (the
    // hybrid-mode case where the BG playfield render is bypassed).
    static bool isIllegalModeRegs(const uint8_t* regs);

    // Bug N°8 cloning trigger (meisei vdp.c:592 condition). Returns
    // true when the chip's sprite-cloning hack actually fires:
    // M3 (R0 bit 1) set AND R4 bits 0-1 not both set, on TI/NMOS
    // silicon. Independent of `isIllegalModeRegs` — clone ghosts
    // appear in legal Mode II setups too (e.g. MSX1 demos that set
    // R4=$00 to mirror the pattern table). Toshiba clones and
    // Yamaha V9938+ have factory-fixed addressing and never clone;
    // POM1 emulates "TI silicon" hard-coded (no dispatch on chip kind).
    static bool isCloningActive(const uint8_t* regs);

private:
    // Display mode helpers — write into pixel buffer
    static void renderGraphicsI  (uint32_t* pixels, const Snapshot& s, ImU32 backdrop);
    static void renderGraphicsII (uint32_t* pixels, const Snapshot& s, ImU32 backdrop);
    static void renderText       (uint32_t* pixels, const Snapshot& s, ImU32 backdrop);
    static void renderMulticolor (uint32_t* pixels, const Snapshot& s, ImU32 backdrop);
    static void renderSprites    (uint32_t* pixels, const Snapshot& s);

    // Per-scanline progressive-raster helpers. Read state directly from
    // `vram` and `regs` (LIVE), so mid-frame register / VRAM changes only
    // affect lines crossed *after* the change — silicon-correct rasterisation
    // (cf. dev/SILICONBUGS.md "rainbow demo" use cases).
    void renderActiveLine(int line);
    void paintLeftRightBorderForActiveLine(int line);
    void paintTopBorder();
    void paintBottomBorder();
    // Per-line raw renderers — operate on raw vram + regs pointers so they
    // can be reused between the live progressive path and the legacy
    // snapshot-based renderToBuffer.
    static void renderGfxILineRaw       (int line, uint32_t* lineBuf,
                                         const uint8_t* vram, const uint8_t* regs,
                                         uint16_t vramMask, ImU32 backdrop);
    static void renderGfxIILineRaw      (int line, uint32_t* lineBuf,
                                         const uint8_t* vram, const uint8_t* regs,
                                         uint16_t vramMask, ImU32 backdrop);
    static void renderTextLineRaw       (int line, uint32_t* lineBuf,
                                         const uint8_t* vram, const uint8_t* regs,
                                         uint16_t vramMask, ImU32 backdrop);
    static void renderMulticolorLineRaw (int line, uint32_t* lineBuf,
                                         const uint8_t* vram, const uint8_t* regs,
                                         uint16_t vramMask, ImU32 backdrop);
    static void renderSpritesLineRaw    (int line, uint32_t* lineBuf,
                                         const uint8_t* vram, const uint8_t* regs,
                                         uint16_t vramMask);
    // Bug N°8 (sprite cloning under illegal mode combinations) — when the
    // M1/M2/M3 mode bits are forced into a hybrid combo (≥ 2 set), TMS9918A
    // NMOS silicon produces "ghost clones" of the SAT sprites at polluted
    // Y positions in the top 64 lines (R3 bits 5-6 + R6 bits 0-2 leak into
    // the Y-fetch address). Demos like Alankomaat (Bandwagon, MSX) use this
    // intentionally to display impossible sprite counts. Modelled here as a
    // simple ghost render at Y = (slot*8) mod 64 — recognisable cascade of
    // copies for the first ~8 slots.
    static void renderCloneSpritesLineRaw(int line, uint32_t* lineBuf,
                                          const uint8_t* vram, const uint8_t* regs,
                                          uint16_t vramMask);

    // Per-scanline sprite scan that updates statusReg sticky bits 5 (collision),
    // 6 (5S) and 0..4 (SAT index of the latched 5th sprite — or, when 5S is
    // not set, the index of the last sprite the chip walked). Bit-pattern
    // based (color=0 sprites still collide). Invoked from advanceCycles
    // every time the simulated raster crosses an active scanline boundary,
    // so collision/5S latch silicon-correctly mid-frame (cf. dev/SILICONBUGS.md
    // Bug N°5 / N°6 / N°10). Source-of-truth: openMSX SpriteChecker::checkSprites1.
    void scanSpritesForLine(int line);

    // Legacy 1×/frame scan kept as a VBlank-edge fallback for the case where
    // display was blanked the entire active period (no per-line work happened).
    // Idempotent — repeating the call returns the same statusReg.
    static void scanSpritesForStatus(const uint8_t* vram, const uint8_t* regs,
                                     bool strict, uint8_t& statusOut);
    static uint16_t vramMaskForRegs(const uint8_t* regs, bool strict);
    uint16_t liveVramMask() const;

    // Silicon-strict timing: port verbatim of openMSX VDPAccessSlots.cc
    // (TMS9918/MSX1 branch). Each scanline holds 1368 VDP ticks; the active
    // table publishes the absolute tick positions where the chip grants the
    // CPU a VRAM slot. A new CPU access drains for `pendingDrainCycles` 6502
    // cycles before the chip is free again — a fresh access during that
    // window is silently overwritten (silicon's `tooFastCallback`-equivalent
    // is the dropped-write counter exposed via droppedWriteCount()).
    //
    // Time conversion: 1 line = 63.7 µs = 1368 ticks (NTSC TMS9918, openMSX
    // `TICKS_PER_LINE`). 1 cycle 6502 ≈ 21 ticks at 1.022727 MHz. Position in
    // the line is derived from frameCycleCounter (no separate counter).
    // Tiny pointer+size view into a slot-tick array. C++17 stand-in for the
    // C++20 `std::span<const int16_t>` openMSX uses; semantics match (data + size,
    // forward-iterable, back() = last element).
    struct SlotSpan {
        const int16_t* data;
        std::size_t    size;
        const int16_t* begin() const { return data; }
        const int16_t* end()   const { return data + size; }
        int16_t        back()  const { return data[size - 1]; }
    };

    bool canAcceptAccess() const;
    void noteAcceptedAccess();
    SlotSpan selectSlotTable() const;
    SlotTableId activeSlotTableId() const;
    // Record a dropped access into dropStats and emit a stderr trace line.
    // `port` is 'D' for $CC00 (writeData), 'C' for $CC01 (writeControl).
    // `value` is the byte the program tried to write. Bumps droppedWrites and
    // updates the PC/port/table/active-vs-vblank histograms.
    void noteDroppedAccess(char port, uint8_t value);

private:
    std::array<uint8_t, 0x4000> vram{};
    std::array<uint8_t, 8>      regs{};
    uint8_t statusReg       = 0;
    uint8_t controlLatch    = 0;
    bool    latchIsSecond   = false;  // true = next write is the 2nd byte
    uint16_t vramAddr       = 0;
    uint8_t readAheadBuffer = 0;
    int frameCycleCounter   = 0;
    // 6502 cycles before the in-flight $CC00/$CC01 access has drained from
    // the VDP latch. 0 = chip free, accepts a fresh access. >0 = a previous
    // access is still being serviced; another byte arriving now is dropped
    // (matches openMSX `pendingCpuAccess` + `tooFastCallback` default off).
    int  pendingDrainCycles = 0;

    // Per-scanline sprite-scan progress. -1 = nothing scanned this frame;
    // 0..192 = highest scanline covered (192 means VBlank reached). Reset
    // at frame rollover. Drives the per-line invocation of scanSpritesForLine.
    int  lastScanlineProcessed = -1;

    // Persistent 320×240 RGBA framebuffer rendered line-by-line as the
    // emulated raster crosses scanlines (silicon-correct progressive
    // raster). The Snapshot publisher copies this verbatim into Snapshot
    // ::framebuffer so the UI thread renders without any per-snapshot work.
    std::array<uint32_t, 320 * 240> framebuffer{};
    // Tracks the last scanline rendered into framebuffer this frame.
    // Same semantics as lastScanlineProcessed but for the renderer.
    int lastScanlineRendered = -1;
    bool siliconStrictMode  = false;
    // P-LAB original card leaves /INT floating; community FPGA mods can strap
    // it to /IRQ. Default = false (matches stock P-LAB → polling-only via
    // $CC01). Set true via setIrqStrapped() for FPGA-strap emulation.
    bool irqStrapped        = false;
    uint64_t droppedWrites  = 0;      // cumulative count of VDP writes dropped by siliconStrictMode
    int      droppedWriteTraceCount = 0; // first N drops trace to stderr (debug)
    DropDiagnostics dropStats{};      // per-PC / per-port / per-table histograms
    uint16_t lastAccessPc   = 0;      // CPU PC at the most recent VDP-port access (set by Memory)
    bool snapshotDirty = true;        // skip the 16 KB VRAM + regs copy when nothing changed since last publish

    // Slot-table model constants (openMSX-aligned).
    // TICKS_PER_LINE = 1368 NTSC (openMSX VDP::TICKS_PER_LINE).
    // D28 = 28-tick prep delay between CPU-side request and chip latch
    // (Nouspikel "2 µs préparation chip", openMSX `Delta::D28`).
    // 21 = round(1368 / 65) ≈ VDP ticks per 6502 cycle at 1.022727 MHz.
    static constexpr int kVdpTicksPerLine    = 1368;
    static constexpr int kVdpTicksPerCpuCycle = 21;
    static constexpr int kVdpDeltaD28Ticks   = 28;

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
