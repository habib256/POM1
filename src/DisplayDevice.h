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
class DisplayDevice
{
public:
    virtual ~DisplayDevice() = default;

    /// Called from the emulation thread under `Memory::memWrite`. The
    /// byte is already masked with `& 0x7F` (Apple 1 dropped bit 7 on
    /// its output lines). Implementations must be fast and non-blocking;
    /// heavy work belongs on the consumer thread.
    virtual void onChar(char c) = 0;
};

#endif // DISPLAYDEVICE_H
