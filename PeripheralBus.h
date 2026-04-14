// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// PeripheralBus — dispatches Apple-1 memory-mapped I/O accesses to registered
// peripherals. Used by Memory to keep memRead/memWrite oblivious to specific
// devices (SID, TMS9918, CFFA1, etc.).
//
// Usage:
//   bus.registerHandle("sid", {0xC800, 0xCFFF}, /*priority*/ 0, sidRead, sidWrite);
//   bus.registerHandle("tms9918", {0xCC00, 0xCC01}, /*priority*/ 10, ...);
//   // TMS9918 wins the two overlapping addresses because its priority is higher.
//
// Callers flip devices on/off via setEnabled(handle, bool). Disabled entries
// are skipped by the dispatch. Ordering is stable: when two entries have the
// same priority, the one registered first is consulted first.

#ifndef PERIPHERALBUS_H
#define PERIPHERALBUS_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class PeripheralBus
{
public:
    using ReadFn  = std::function<uint8_t(uint16_t)>;
    using WriteFn = std::function<void(uint16_t, uint8_t)>;

    struct Range {
        uint16_t low;
        uint16_t high;  // inclusive
        bool contains(uint16_t a) const { return a >= low && a <= high; }
    };

    using Handle = int;

    /// Register a peripheral. `onRead`/`onWrite` may be empty (std::function{})
    /// to mark the range as read-only or write-only respectively. Higher
    /// `priority` wins on overlaps (ties resolved by registration order).
    /// Returns a handle to enable/disable later.
    Handle registerHandle(std::string name, Range range, int priority,
                          ReadFn onRead, WriteFn onWrite);

    /// Enable/disable a peripheral without re-registering.
    void setEnabled(Handle handle, bool enabled);
    bool isEnabled(Handle handle) const;

    /// Fast-path dispatch. Returns true when a peripheral handled the access.
    /// A read-only entry returns false for writes (and vice versa); the caller
    /// is free to treat that as "pass-through" or to block it (Memory treats
    /// it as "consumed" for a read-only ROM range to match the original
    /// inline behaviour — see how CFFA1 is registered in Memory::ctor).
    bool tryRead(uint16_t address, uint8_t& valueOut) const;
    bool tryWrite(uint16_t address, uint8_t value) const;

private:
    struct Entry {
        std::string name;
        Range range;
        int priority;
        int insertionIndex;   // ties-break by registration order
        bool enabled;
        ReadFn onRead;
        WriteFn onWrite;
    };

    // Entries are kept sorted so dispatch is a single linear scan that stops
    // on the first enabled match. The list is small (≤ 10 entries), so a
    // fancier structure (page map, interval tree) would not pay off.
    std::vector<Entry> entries;
    int nextInsertionIndex = 0;

    void sortEntries();
};

#endif // PERIPHERALBUS_H
