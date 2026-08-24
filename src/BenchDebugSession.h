// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// BenchDebugSession — the DevBench's source-level debugging STATE MACHINE:
// which line table is live, which source line we armed the machine's single
// CPU breakpoint through, and what should happen on a toggle / a rebuild.
//
// PURE, and that is the whole point. This logic used to sit inside
// Pom1BenchHost, which needs a live MainWindow — so ctest could not reach it,
// and six of the ten defects found while hardening this feature lived exactly
// there, each verified by reading only. It follows the seam CLAUDE.md already
// prescribes for MainWindow (Apple1KeyMap / FullscreenExpand / WindowGeometry):
// *anything that is a decision rather than a draw call belongs on this side*.
//
// The machine's breakpoint is NOT owned here. POM1 has exactly one CPU
// breakpoint, shared with the Debug window, so every query takes the machine's
// current (has, address) as arguments and every answer is an INTENTION the
// caller carries out. That is what makes "is this breakpoint still ours?"
// answerable without an emulator.

#ifndef POM1_BENCH_DEBUG_SESSION_H
#define POM1_BENCH_DEBUG_SESSION_H

#include "DbgFile.h"

#include <cstdint>
#include <utility>

namespace pom1 {

class BenchDebugSession {
public:
    /// The machine's single CPU breakpoint as it stands right now.
    struct MachineBp {
        bool armed = false;
        uint16_t address = 0;
    };

    // ── Line table lifetime ─────────────────────────────────────────────
    /// A build produced a usable table. Replaces whatever was live.
    void adopt(DbgLineInfo info) { info_ = std::move(info); }
    /// The MACHINE no longer runs the program the table described (preset
    /// applied, interpreter injected). Forgets everything, the pending
    /// re-arm included — resurrecting a breakpoint into a different program
    /// would poke an address that now means something else.
    void invalidate() { info_ = {}; armedLine_ = -1; pendingRearm_ = -1; }
    bool hasLineInfo() const { return info_.ok; }
    const DbgLineInfo& lineInfo() const { return info_; }

    /// 1-based source line for `pc`, or -1 when the PC is outside the built
    /// program (or no table is live).
    int lineForPc(uint16_t pc) const
    {
        return info_.ok ? info_.lineForAddr(pc) : -1;
    }

    // ── Toggle ──────────────────────────────────────────────────────────
    enum class Toggle {
        Armed,    // arm `address`; the marker moves to `line`
        Cleared,  // clear the machine breakpoint; no marker
        NoCode,   // nothing at or after the clicked line — machine untouched
    };
    struct ToggleResult {
        Toggle kind = Toggle::NoCode;
        uint16_t address = 0;
        int line = -1;        // the SNAPPED line (Armed only)
    };

    /// Decide what a click on `cursorLine` (1-based) means, and record it.
    /// Snaps forward past comment/blank/data lines. A second toggle on the
    /// line we armed clears it — but only while the machine breakpoint is
    /// still the one we set: if the Debug window re-armed it elsewhere, this
    /// arms ours instead of clearing a stranger's.
    ///
    /// The three outcomes are DISTINCT on purpose. The pre-extraction host
    /// returned -1 for both "cleared" and "no code", leaving the caller to
    /// re-derive which had happened from a before/after comparison.
    ToggleResult toggle(int cursorLine, MachineBp machine);

    // ── Marker ──────────────────────────────────────────────────────────
    /// Line to draw the gutter marker on, or -1. Answers -1 whenever the
    /// machine's breakpoint is not the one we armed (cleared or re-armed
    /// elsewhere from the Debug window) — a marker that does not describe the
    /// machine is worse than no marker.
    int markerLine(MachineBp machine) const;

    // ── Rebuild ─────────────────────────────────────────────────────────
    /// A build is starting: drop the table (its addresses are about to move)
    /// and REMEMBER the armed line so a later rearm() can restore it.
    /// Returns that line — whose machine breakpoint the caller must clear —
    /// or -1 when there is nothing of ours to carry: no armed line, or the
    /// machine breakpoint belongs to someone else, which is left strictly
    /// alone.
    ///
    /// The memory is deliberately a MEMBER and not the caller's local: a
    /// build has a dozen ways to fail before it ever links (a typo is the
    /// commonest event in development), and each one used to lose the
    /// breakpoint for good — the wipe had happened, the re-arm never ran.
    /// The intent now survives failed builds and is honoured by the next one
    /// that succeeds; only invalidate() (the machine itself changing) drops
    /// it.
    int beginRebuild(MachineBp machine);

    struct RearmResult {
        bool ok = false;
        uint16_t address = 0;
        int line = -1;        // possibly snapped to a different line
    };
    /// Re-arm the line remembered by beginRebuild() against the freshly
    /// adopted table, snapping forward like a click does. CONSUMES the
    /// memory either way: a fresh table settles the question, so a line that
    /// no longer resolves leaves the breakpoint down rather than trailing a
    /// pending intent forever. Call only after adopt() — a build that failed
    /// must NOT call it, which is exactly how the intent survives.
    RearmResult rearm();
    /// True while a beginRebuild() is still waiting for its rearm() — i.e. a
    /// build wiped a breakpoint that has not been restored yet.
    bool rearmPending() const { return pendingRearm_ >= 0; }

private:
    DbgLineInfo info_;
    int armedLine_ = -1;      // 1-based snapped line we armed, -1 = none
    int pendingRearm_ = -1;   // line a build owes us a re-arm for, -1 = none
};

} // namespace pom1

#endif // POM1_BENCH_DEBUG_SESSION_H
