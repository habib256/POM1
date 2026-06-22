// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// KeyboardController — thread-safe queue of pending Apple-1 keypresses.
// The UI thread pushes via queueKey(); the emulation thread drains via
// drainTo(Memory&) once per slice. drainTo() releases its internal keyMutex
// *before* calling Memory::setKeyPressed() (swap-out pattern) so the UI
// thread can keep queuing keys while the emulation thread updates the PIA.

#ifndef KEYBOARDCONTROLLER_H
#define KEYBOARDCONTROLLER_H

#include <mutex>
#include <queue>

class Memory;

class KeyboardController
{
public:
    /// Thread-safe: takes only its own keyMutex. Safe to call from the UI thread.
    void queueKey(char key);

    /// True if keystrokes are still waiting to be drained into Memory. Thread-safe
    /// (takes only keyMutex). Used to detect when an injected listing has been typed.
    bool hasQueuedKeys();

    /// Drop every pending keystroke. Thread-safe (takes only keyMutex). Called on
    /// reset so stale keys from a prior run can't corrupt the next cold-start.
    void clear();

    /// Pre: caller holds the mutex protecting `mem` (typically
    /// EmulationController::stateMutex). Drains the pending queue into
    /// Memory::setKeyPressed(). keyMutex is released before touching `mem`.
    void drainTo(Memory& mem);

private:
    std::mutex keyMutex;
    std::queue<char> queuedKeys;
};

#endif // KEYBOARDCONTROLLER_H
