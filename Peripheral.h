// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Peripheral — common interface for every Apple-1 expansion card POM1
// emulates (12 cards as of Apr 2026: ACI, GEN2 HGR, TMS9918, A1-SID,
// microSD, CFFA1, Juke-Box, CodeTank, Wi-Fi Modem, Terminal Card,
// A1-IO/RTC, PR-40, GT-6144).
//
// Why this exists:
//   1. **Identification at runtime** — every card answers `name()` so
//      the snapshot framework, the bus-conflict table, and any future
//      logging/instrumentation can refer to cards uniformly.
//   2. **Snapshot scaffolding** — serialize/deserialize are optional
//      hooks (default no-op) that each card overrides as the snapshot
//      framework absorbs its internal state. Keeps adoption incremental.
//   3. **Mutex labelling for debugging** — when a deadlock pops up in
//      production, `mutexLabel()` provides a human-readable identifier
//      to correlate with the documented order
//      (`stateMutex > keyMutex > snapshotMutex`).
//
// What this interface deliberately does NOT include yet:
//   - `onPlugged(Memory&)` / `onUnplugged(Memory&)` — the per-card plug
//     logic still lives on Memory (`setXxxEnabled`). Centralising that
//     to a registry-driven flow is the next layer; see TODO.md
//     "Peripheral lifecycle centralisation".
//   - 15-frame plug-deferral hook — currently in MainWindow_Presets.cpp.
//     Same reason: incremental migration.
//
// The pure-virtual surface is kept minimal (just `name()`) so adoption
// is mechanical: every card declares `: public Peripheral` and adds one
// override. Snapshot work is opt-in until each card is ready.

#ifndef POM1_PERIPHERAL_H
#define POM1_PERIPHERAL_H

#include <string_view>

namespace pom1 {

class SnapshotWriter;
class SnapshotReader;

class Peripheral {
public:
    virtual ~Peripheral() = default;

    /// Stable, human-readable identifier (e.g. "GT-6144", "A1-SID",
    /// "PR-40"). Used by the snapshot file format as a section tag and
    /// by logs / error messages. MUST stay stable across releases — the
    /// snapshot reader looks cards up by this name.
    virtual std::string_view name() const = 0;

    /// Optional debugging label for the card's internal mutex. The
    /// documented mutex order in CLAUDE.md is
    ///   stateMutex > keyMutex > snapshotMutex
    /// Card-internal mutexes (cardMutex / chipMutex) stand below those;
    /// labelling them helps correlate stack traces.
    virtual std::string_view mutexLabel() const { return "<unspecified>"; }

    /// Append the card's internal state to the snapshot. Default no-op
    /// keeps unmigrated cards safe — their slot in the snapshot stays
    /// empty and the load path skips them. Override and write a stable
    /// per-card payload as state ownership migrates to the framework.
    virtual void serialize(SnapshotWriter& /*writer*/) const {}

    /// Restore the card's internal state from a snapshot. The reader is
    /// pre-positioned at the card's payload. Default no-op pairs with
    /// the default serialize.
    virtual void deserialize(SnapshotReader& /*reader*/) {}
};

} // namespace pom1

#endif // POM1_PERIPHERAL_H
