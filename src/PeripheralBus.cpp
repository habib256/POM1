// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "PeripheralBus.h"

#include <algorithm>
#include <cassert>
#include <utility>

PeripheralBus::Handle PeripheralBus::registerHandle(std::string name, Range range,
                                                    int priority,
                                                    ReadFn onRead, WriteFn onWrite)
{
    assert(entries.size() < static_cast<size_t>(kMaxEntries) &&
           "PeripheralBus::EntryMask only supports 32 entries; widen the type.");
    // Capture the insertion index for the new entry BEFORE sorting. After
    // sortEntries() the vector is reordered by priority, so `entries.back()`
    // is NOT guaranteed to be the one we just pushed — higher-priority
    // entries (e.g. Juke-Box at priority 20) get moved to the front, leaving
    // some unrelated priority-0 entry at the back. Returning that stranger's
    // insertionIndex means setEnabled/isEnabled later operate on the wrong
    // handle, and the entry we actually registered stays enabled by default
    // (shadowing the address range it claims).
    const Handle newHandle = nextInsertionIndex++;
    entries.push_back(Entry{
        std::move(name),
        range,
        priority,
        newHandle,
        true,
        std::move(onRead),
        std::move(onWrite),
    });
    sortEntries();
    rebuildPageMask();
    return newHandle;
}

void PeripheralBus::setEnabled(Handle handle, bool enabled)
{
    for (auto& e : entries) {
        if (e.insertionIndex == handle) {
            if (e.enabled != enabled) {
                e.enabled = enabled;
                rebuildPageMask();
            }
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

bool PeripheralBus::tryReadSlow(uint16_t address, uint8_t& valueOut, EntryMask mask) const
{
    // Iterate only the entries flagged in the page bitmap, in priority order
    // (entries are pre-sorted, so lower index = higher priority).
    while (mask) {
        // Portable count-trailing-zeros (loop is fine here — at most a handful
        // of iterations per call, and the bus has ≤ 16 entries).
        int idx = 0;
        EntryMask probe = mask;
        while ((probe & 1u) == 0) { probe >>= 1; ++idx; }
        mask &= static_cast<EntryMask>(mask - 1);

        const Entry& e = entries[static_cast<size_t>(idx)];
        if (!e.enabled) continue;
        if (!e.range.contains(address)) continue;
        if (e.onRead) {
            valueOut = e.onRead(address);
            return true;
        }
        // Write-only entry (e.g. CFFA1's register window): this entry can't
        // service the read, so keep scanning any LOWER-priority entry that
        // overlaps this address before falling through to raw RAM. (`continue`,
        // not `return false` — the latter would let a high-priority write-only
        // card mask a lower-priority reader at the same address.)
        continue;
    }
    return false;
}

bool PeripheralBus::tryWriteSlow(uint16_t address, uint8_t value, EntryMask mask) const
{
    while (mask) {
        int idx = 0;
        EntryMask probe = mask;
        while ((probe & 1u) == 0) { probe >>= 1; ++idx; }
        mask &= static_cast<EntryMask>(mask - 1);

        const Entry& e = entries[static_cast<size_t>(idx)];
        if (!e.enabled) continue;
        if (!e.range.contains(address)) continue;
        if (e.onWrite) {
            e.onWrite(address, value);
            return true;
        }
        // No write handler on this entry → keep scanning any LOWER-priority
        // entry that overlaps this address before falling through to the caller
        // (raw RAM, ROM-protection, or whatever logic Memory has). Callers that
        // want to BLOCK writes (e.g. ROM windows) must register an explicit
        // no-op write handler rather than leaving onWrite empty. (`continue`,
        // not `return false` — the latter would let a high-priority read-only
        // card mask a lower-priority writer at the same address.)
        continue;
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

void PeripheralBus::rebuildPageMask()
{
    pageMask.fill(0);
    for (size_t idx = 0; idx < entries.size(); ++idx) {
        const Entry& e = entries[idx];
        // Disabled entries stay in the map: setEnabled() flips the flag and
        // rebuilds, which is rare. The runtime `if (!e.enabled) continue;`
        // inside the slow path keeps semantics identical to a full scan.
        const int firstPage = e.range.low >> 8;
        const int lastPage  = e.range.high >> 8;
        const EntryMask bit = static_cast<EntryMask>(1u << idx);
        for (int p = firstPage; p <= lastPage; ++p) {
            pageMask[static_cast<size_t>(p)] |= bit;
        }
    }
}
