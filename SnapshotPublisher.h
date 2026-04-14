// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// SnapshotPublisher — single-producer / single-consumer slot for
// EmulationSnapshot. The emulation thread calls publish() while holding the
// mutex that protects Memory & M6502 (EmulationController::stateMutex).
// publish() then serializes the slot update on its own snapshotMutex, so any
// UI-thread copyTo() observes a fully-written snapshot.

#ifndef SNAPSHOTPUBLISHER_H
#define SNAPSHOTPUBLISHER_H

#include <mutex>

#include "EmulationSnapshot.h"

class Memory;
class M6502;

class SnapshotPublisher
{
public:
    /// Pre: caller holds the mutex protecting `mem` and `cpu`.
    /// Acquires snapshotMutex internally; safe for concurrent copyTo().
    /// `mem` is non-const because peripheral copySnapshot() clears internal
    /// dirty flags (e.g. TMS9918 skips its 16 KB VRAM copy when clean).
    void publish(Memory& mem, const M6502& cpu, bool cpuRunning);

    /// Thread-safe: takes only snapshotMutex. Called from the UI thread.
    void copyTo(EmulationSnapshot& out) const;

private:
    mutable std::mutex snapshotMutex;
    EmulationSnapshot latestSnapshot;
};

#endif // SNAPSHOTPUBLISHER_H
