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

#include <array>
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
    // Returned by registerHandle when the bus is full (EntryMask exhausted).
    // setEnabled/isEnabled treat it as a no-op / disabled.
    static constexpr Handle kInvalidHandle = -1;

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
    ///
    /// Hot-path: a 256-entry page map (indexed by `address >> 8`) carries a
    /// bitmask of which `entries[]` overlap that page. Pages with no
    /// peripheral (the vast majority of the 64 KB address space) bypass the
    /// scan entirely. Inlined into the header so memRead/memWrite get the
    /// fast bypass without a function call.
    bool tryRead(uint16_t address, uint8_t& valueOut) const {
        EntryMask mask = pageMask[address >> 8];
        if (!mask) return false;
        return tryReadSlow(address, valueOut, mask);
    }
    bool tryWrite(uint16_t address, uint8_t value) const {
        EntryMask mask = pageMask[address >> 8];
        if (!mask) return false;
        return tryWriteSlow(address, value, mask);
    }

private:
    // Bitmap of which entries overlap a given page. Four bytes covers up to
    // 32 peripherals; the bus currently has ~16 (the GEN2 $C2xx soft-switch
    // window filled the old 16-entry uint16_t). The assert in registerHandle
    // guards against overflow if future cards push past 32.
    using EntryMask = uint32_t;
    static constexpr int kMaxEntries = 32;

    struct Entry {
        std::string name;
        Range range;
        int priority;
        int insertionIndex;   // ties-break by registration order
        bool enabled;
        ReadFn onRead;
        WriteFn onWrite;
    };

    // Entries are kept sorted by priority. The page map is rebuilt whenever
    // the entry layout changes (registerHandle/setEnabled).
    std::vector<Entry> entries;
    std::array<EntryMask, 256> pageMask{};
    int nextInsertionIndex = 0;

    void sortEntries();
    void rebuildPageMask();
    bool tryReadSlow(uint16_t address, uint8_t& valueOut, EntryMask mask) const;
    bool tryWriteSlow(uint16_t address, uint8_t value, EntryMask mask) const;
};

#endif // PERIPHERALBUS_H
