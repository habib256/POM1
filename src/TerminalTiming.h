// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// TerminalTiming.h — how long the Apple-1's display stays BUSY after a write.
//
// Pure: <cstdint> only. No Memory, no ImGui, no GLFW — same seam rule as
// Apple1KeyMap / LayoutDecisions / ShortcutTable.
//
// WHAT THIS IS ABOUT. Every Apple-1 program that prints goes through the Woz
// Monitor's ECHO, which spins on `BIT $D012 / BMI` until PB7 clears. That poll
// loop is the single most-executed piece of timing in the machine, so how PB7
// is modelled decides how a printing program's cycles fall.
//
// POM1 has always used a FIXED DELAY: a $D012 write arms a countdown of
// CPU_HZ/60 cycles and PB7 clears when it expires. That gives the right AVERAGE
// — the Apple-1 terminal displays 60 characters per second — and it is what
// every shipped program and golden image has been validated against.
//
// It is not, however, how Woz's terminal works. There is no framebuffer: the
// text lives in a recirculating shift register scanned continuously at the
// video rate, and a character is only latched when the scan reaches the cursor.
// PB7 therefore stays busy until the NEXT PASS OF THE SCAN, not for a fixed
// interval measured from the write. The observable difference is the PHASE: a
// write just before the scan reaches the cursor is answered almost immediately,
// one just after waits nearly a whole field.
//
// Both models are here, `FixedDelay` is the default, and nothing changes unless
// a caller asks for `FieldSync`.
//
// WHAT IS AND IS NOT ESTABLISHED — read this before trusting FieldSync:
//
//   * ESTABLISHED (doc/GEN2_RELEASE.md, Q4, resolved with Uncle Bernie): the
//     Apple-1 video chain runs 65 CPU cycles per scanline and 262 lines per
//     60 Hz field = 17 030 cycles. POM1's fixed delay uses CPU_HZ/60 = 17 045,
//     a derivation rather than the raster period. The 15-cycle gap is real but
//     has never mattered, because nothing was phase-locked to it.
//   * ESTABLISHED (arithmetic, pinned by §3 below): under FieldSync, two
//     consecutive writes are exactly one field apart WHATEVER the phase — so
//     the documented 60 characters per second survives the change. Only the
//     distribution of the wait moves.
//   * A MODEL, NOT A MEASUREMENT: that the latch point is the field boundary.
//     On real hardware it is wherever the cursor sits, which walks down the
//     screen as text is printed, so the true busy interval carries a
//     cursor-position term this does not. Confirming it needs a scope on a real
//     terminal section, or Woz's own timing notes; neither is in this tree.
//     That is exactly why FieldSync is opt-in.

#ifndef POM1_TERMINAL_TIMING_H
#define POM1_TERMINAL_TIMING_H

#include <cstdint>

namespace pom1::terminal {

/// CPU cycles in one 60 Hz video field: 65 cycles/scanline × 262 lines.
/// From doc/GEN2_RELEASE.md Q4 — the same 14.31818 MHz ÷ 14 chain the CPU runs
/// on, so this is the machine's raster period rather than a derived 1/60 s.
inline constexpr int kFieldCycles = 65 * 262;   // 17030

/// How PB7 (the $D012 busy bit) is driven.
enum class BusyModel {
    /// A fixed countdown armed by the write. POM1's historical behaviour and
    /// the default: every shipped program and golden image is validated on it.
    FixedDelay,
    /// Busy until the scan next reaches the latch point — the phase-locked
    /// model described above. Opt-in.
    FieldSync,
};

/// Advance a free-running field phase. Kept here rather than inline at the call
/// site so the wrap is written once and can be tested on its own.
inline constexpr int advanceFieldPhase(int phase, int cycles)
{
    if (cycles <= 0) return phase;
    long long p = static_cast<long long>(phase) + cycles;
    if (p >= kFieldCycles) p %= kFieldCycles;
    return static_cast<int>(p);
}

/// Cycles PB7 stays set after a $D012 write.
///
/// `fieldPhase` is where the write lands inside the current field, [0, kFieldCycles).
/// `fixedDelay` is the caller's historical countdown, used by FixedDelay only.
///
/// FieldSync never returns 0: a write that lands exactly on the boundary has
/// just missed the latch and waits a full field. A busy of zero would let a
/// tight ECHO loop print without ever spinning, which no Apple-1 does.
inline constexpr int busyCyclesAfterWrite(BusyModel model, int fieldPhase, int fixedDelay)
{
    if (model == BusyModel::FixedDelay)
        return fixedDelay > 0 ? fixedDelay : 0;

    if (fieldPhase < 0) fieldPhase = 0;
    if (fieldPhase >= kFieldCycles) fieldPhase %= kFieldCycles;
    return kFieldCycles - fieldPhase;
}

} // namespace pom1::terminal

#endif // POM1_TERMINAL_TIMING_H
