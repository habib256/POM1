// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// SWTPC PR-40 Alphanumeric Printer (1976) — passive sniffing peripheral.
//
// Steve Jobs, "Interfacing the Apple Computer", Interface Age, Oct. 1976:
// the PR-40 is wired directly to the Apple 1 PIA 6820 Port B (which drives
// the video display at $D012). Data Accepted (active low, J4-2) is routed
// through a free NAND gate (IC15) and a DPDT switch to PB7. While the
// printer is in its ~0.8 s mechanical cycle, PB7 stays high and the Woz
// Monitor's BIT $D012 / BMI tight loop at $FFEF stalls the CPU naturally.
//
// POM1 models this by:
//   1. A third sniffer on $D012 writes (the first two being
//      DisplayDevice::onChar and TerminalCard::onDisplayWrite).
//   2. A 40-char FIFO that triggers mechanical print on either a CR ($0D)
//      or FIFO-full — matching the real hardware spec.
//   3. An 818,182-cycle mechanical countdown (~0.8 s at 1.022727 MHz),
//      decremented from Memory::advanceCycles() so stall duration tracks
//      the emulated CPU clock.
//   4. A 3-position switch mode — Off / Mixed / PrintOnly — that selects
//      which signal feeds PB7 of the PIA. See Memory::memRead(0xD012) for
//      the busy-OR merge expressing the DPDT wiring.

#ifndef PR40PRINTER_H
#define PR40PRINTER_H

#include "CpuClock.h"
#include "Peripheral.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

class PR40Printer : public pom1::Peripheral
{
public:
    std::string_view name() const override { return "PR-40"; }
    std::string_view mutexLabel() const override { return "PR40Printer::cardMutex"; }

    /// Position of the DPDT switch Steve Jobs hot-wired to PB7 of the PIA.
    ///   Off       : printer disconnected; writes to $D012 ignored here,
    ///               only the video's displayBusyCycles drives PB7.
    ///   Mixed     : Jobs' 2-position mod (Oct. 1976). PB7 reflects the OR of
    ///               video busy and printer busy — the CPU stalls for either.
    ///   PrintOnly : community 3-position mod. PB7 reflects printer busy
    ///               alone, isolated from the video's 60 Hz /RDA, so the CPU
    ///               can flood the PR-40's FIFO at up to 1 MHz.
    enum class SwitchMode { Off, Mixed, PrintOnly };

    /// PR-40 hardware line buffer is 40 characters.
    static constexpr int kFifoCapacity = 40;

    /// ~0.8 s mechanical print cycle at 1.022727 MHz CPU clock.
    /// POM1_CPU_CLOCK_HZ * 4 / 5 ≈ 818 182 cycles.
    static constexpr int kMechCycleCpu = POM1_CPU_CLOCK_HZ * 4 / 5;

    PR40Printer();
    void reset();

    // ---- Emulation-thread entry points (serialised via Memory's flow) ----
    /// $D012 write sniff. `rawValue` is the unmasked byte (bit 7 may be set
    /// by programs — the real PR-40 treats bit 7 as don't-care, we strip it).
    void onDisplayWrite(uint8_t rawValue);
    /// Pumped once per CPU slice by Memory::advanceCycles(cycles).
    void advanceCycles(int cycles);

    // ---- Thread-safe accessors used by Memory::memRead's busy-OR merge ----
    bool isMechBusy() const;
    SwitchMode getMode() const;

    // ---- UI-thread API ----
    struct Snapshot {
        SwitchMode mode = SwitchMode::Mixed;
        int  fifoLevel = 0;
        bool busy = false;
        int  mechCyclesRemaining = 0;
        int  charactersPrinted = 0;
        int  linesPrinted = 0;
        int  pagesTornOff = 0;
        // Full paper roll since the last tearOffPage(), newest last. The
        // last entry may be the in-progress `currentLine` if it's non-empty.
        // Size is unbounded by design — the UI uses ImGuiListClipper to keep
        // rendering O(visible) on long sessions.
        std::vector<std::string> recentLines;
    };
    void copySnapshot(Snapshot& out) const;

    void setMode(SwitchMode m);
    bool savePaperRoll(const std::string& path, std::string& error) const;
    /// Clear the paper roll. If `dumpedContents` is non-null, the current
    /// roll is copied to it first (so the UI can offer "tear off + save").
    void tearOffPage(std::string* dumpedContents = nullptr);

private:
    mutable std::mutex cardMutex;
    SwitchMode mode = SwitchMode::Mixed;

    uint8_t fifo[kFifoCapacity]{};
    int     fifoLevel = 0;

    int     mechCyclesRemaining = 0;

    std::vector<std::string> paperLines;   // full roll since last tearOff
    std::string              currentLine;  // in-progress line (< 40 chars)

    int charactersPrinted = 0;
    int linesPrinted = 0;
    int pagesTornOff = 0;

    // Flush the FIFO + currentLine into paperLines and arm the mechanical
    // cycle. Caller must already hold cardMutex.
    void flushLineLocked();
};

#endif // PR40PRINTER_H
