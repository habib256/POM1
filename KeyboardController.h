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

    /// Pre: caller holds the mutex protecting `mem` (typically
    /// EmulationController::stateMutex). Drains the pending queue into
    /// Memory::setKeyPressed(). keyMutex is released before touching `mem`.
    void drainTo(Memory& mem);

private:
    std::mutex keyMutex;
    std::queue<char> queuedKeys;
};

#endif // KEYBOARDCONTROLLER_H
