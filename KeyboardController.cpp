// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "KeyboardController.h"
#include "Memory.h"

void KeyboardController::queueKey(char key)
{
    std::lock_guard<std::mutex> lock(keyMutex);
    queuedKeys.push(key);
}

void KeyboardController::drainTo(Memory& mem)
{
    // Swap-out pattern: release keyMutex before touching `mem`, so the UI
    // thread can keep queuing keys without waiting on the emulation slice.
    std::queue<char> localKeys;
    {
        std::lock_guard<std::mutex> lock(keyMutex);
        std::swap(localKeys, queuedKeys);
    }
    while (!localKeys.empty()) {
        mem.setKeyPressed(localKeys.front());
        localKeys.pop();
    }
}
