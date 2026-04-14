// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "PeripheralBus.h"

#include <algorithm>
#include <utility>

PeripheralBus::Handle PeripheralBus::registerHandle(std::string name, Range range,
                                                    int priority,
                                                    ReadFn onRead, WriteFn onWrite)
{
    Handle h = static_cast<Handle>(entries.size());
    entries.push_back(Entry{
        std::move(name),
        range,
        priority,
        nextInsertionIndex++,
        true,
        std::move(onRead),
        std::move(onWrite),
    });
    sortEntries();
    // sortEntries() reorders the vector so `h` (an index into the old layout)
    // is no longer meaningful. We return the entry's stable insertionIndex
    // instead; setEnabled/isEnabled match on that.
    return entries.back().insertionIndex;
}

void PeripheralBus::setEnabled(Handle handle, bool enabled)
{
    for (auto& e : entries) {
        if (e.insertionIndex == handle) {
            e.enabled = enabled;
            return;
        }
    }
}

bool PeripheralBus::isEnabled(Handle handle) const
{
    for (const auto& e : entries) {
        if (e.insertionIndex == handle) return e.enabled;
    }
    return false;
}

bool PeripheralBus::tryRead(uint16_t address, uint8_t& valueOut) const
{
    for (const auto& e : entries) {
        if (!e.enabled) continue;
        if (!e.range.contains(address)) continue;
        if (e.onRead) {
            valueOut = e.onRead(address);
            return true;
        }
        // Write-only entry: treat the read as consumed so it falls through
        // to raw RAM below (mirrors the original "no early return" behaviour
        // for CFFA1's write-only register window).
        return false;
    }
    return false;
}

bool PeripheralBus::tryWrite(uint16_t address, uint8_t value) const
{
    for (const auto& e : entries) {
        if (!e.enabled) continue;
        if (!e.range.contains(address)) continue;
        if (e.onWrite) {
            e.onWrite(address, value);
            return true;
        }
        // No write handler → fall through to the caller (which will hit raw
        // RAM, ROM-protection, or whatever logic Memory has). Callers that
        // want to block writes (e.g. ROM windows) should register an
        // explicit no-op write handler instead of leaving onWrite empty.
        return false;
    }
    return false;
}

void PeripheralBus::sortEntries()
{
    std::stable_sort(entries.begin(), entries.end(),
        [](const Entry& a, const Entry& b) {
            if (a.priority != b.priority) return a.priority > b.priority;
            return a.insertionIndex < b.insertionIndex;
        });
}
