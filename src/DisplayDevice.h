#ifndef DISPLAYDEVICE_H
#define DISPLAYDEVICE_H

/// Sink for Apple 1 display writes at $D012 (PIA 6821 output port).
///
/// Memory::memWrite forwards each display byte (already masked to the low
/// 7 bits) to the currently-attached `DisplayDevice`, or drops it if none
/// is attached. Screen_ImGui implements this interface by queueing the
/// char into the 40×24 text buffer; a test harness can inject a fake that
/// captures bytes into a vector for assertion, and future peripherals can
/// tee output (e.g. Terminal Card mirror) without modifying Memory.
///
/// Replaces the previous `void (*)(char)` C-style callback, which forced
/// the implementation to live inside a free/static function and routed
/// through a hidden singleton atomic — preventing any form of dependency
/// injection.
namespace pom1 { class SnapshotWriter; class SnapshotReader; }

class DisplayDevice
{
public:
    virtual ~DisplayDevice() = default;

    /// Called from the emulation thread under `Memory::memWrite`. The
    /// byte is already masked with `& 0x7F` (Apple 1 dropped bit 7 on
    /// its output lines). Implementations must be fast and non-blocking;
    /// heavy work belongs on the consumer thread.
    virtual void onChar(char c) = 0;

    /// Optional snapshot hooks (default no-op) so the visible display state
    /// rides along in rewind / save-state. Without this, restoring memory
    /// leaves the on-screen text at the live frame — the Apple-1 text grid has
    /// no backing RAM, it lives in the display device. Screen_ImGui overrides.
    virtual void serialize(pom1::SnapshotWriter&) const {}
    virtual void deserialize(pom1::SnapshotReader&) {}

    /// Blank the display immediately (no power-on pattern, no banner). Used
    /// when a fresh program is about to run and should not inherit the
    /// previous session's text — DevBench compile-and-run, Load Memory.
    virtual void clear() {}

    /// Cold-boot presentation: garbage screen → auto-clear → welcome banner,
    /// as a real Apple-1 shows when power is applied. Used by hard reset.
    virtual void resetDisplay() {}
};

/// Attach point for the emulation core. EmulationController drives the display
/// exclusively through this interface — it deliberately does NOT know about
/// Screen_ImGui, which would drag imgui.h (and the whole UI layer) into the
/// core's include graph and break the "core is UI-free" invariant the rest of
/// the architecture keeps.


#endif // DISPLAYDEVICE_H
