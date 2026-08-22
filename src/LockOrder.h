// LockOrder.h -- runtime enforcement of POM1's documented mutex order.
//
// POM1 has exactly three locks, and one rule about them:
//
//     stateMutex  >  keyboard.keyMutex  >  publisher.snapshotMutex
//
// That rule was written down in eight places (all four EmulationController
// TUs, KeyboardController.h, SnapshotPublisher.h, Peripheral.h, CLAUDE.md) and
// checked in none of them. Repeating an invariant is how you show it matters;
// it is not how you keep it true. A comment does not fail the build when
// someone contradicts it, and a lock-order inversion does not fail the build
// either -- it deadlocks, once, on a machine that is not yours.
//
// So the ranks below turn the comment into an assertion. Each mutex carries a
// rank; a thread may only take a lock strictly LOWER than the lowest rank it
// already holds. Taking nothing-then-anything is always fine, which is why the
// UI thread can call copyTo() (snapshot only) without going through state.
//
// Cost: a thread_local array push/pop per lock, and only in builds where
// asserts are live. Under NDEBUG -- every shipped binary -- RankedMutex is a
// std::mutex with a rank tag and the checking code is not compiled at all.
// The test binaries build with -UNDEBUG (see tests/CMakeLists.txt), so the
// suite exercises the checks; the emulator you ship does not pay for them.

#ifndef POM1_LOCK_ORDER_H
#define POM1_LOCK_ORDER_H

#include <mutex>

#if !defined(NDEBUG)
#define POM1_LOCK_ORDER_CHECKS 1
#else
#define POM1_LOCK_ORDER_CHECKS 0
#endif

#if POM1_LOCK_ORDER_CHECKS
#include <cassert>
#include <cstddef>
#endif

namespace pom1 {

/// Higher rank = taken FIRST. The values are the documented order, read left
/// to right: stateMutex outermost, snapshotMutex innermost. Leave gaps so a
/// fourth lock can slot in between two existing ones without renumbering.
enum class LockRank : int {
    Snapshot = 10,   ///< SnapshotPublisher::snapshotMutex -- innermost
    Keyboard = 20,   ///< KeyboardController::keyMutex
    State    = 30,   ///< EmulationController::stateMutex -- outermost
};

#if POM1_LOCK_ORDER_CHECKS

namespace lockorder {

/// Ranks currently held by THIS thread, innermost last. Fixed capacity: three
/// locks exist and nothing nests deeper, so a stack array keeps the hot path
/// allocation-free (and usable from the audio callback, where allocating would
/// be a bug of its own).
struct HeldStack {
    static constexpr std::size_t kMax = 8;
    int    ranks[kMax] = {};
    std::size_t depth  = 0;
};

inline HeldStack& held()
{
    thread_local HeldStack s;
    return s;
}

/// Called before acquiring. Fires when the new lock is not strictly inside
/// everything this thread already holds -- i.e. the inversion that deadlocks.
inline void willAcquire(LockRank rank)
{
    HeldStack& s = held();
    if (s.depth > 0) {
        // Only the innermost held rank matters: the stack is already known to
        // be strictly decreasing, so it is the smallest one.
        assert(static_cast<int>(rank) < s.ranks[s.depth - 1] &&
               "POM1 lock-order violation: see the rank table in LockOrder.h. "
               "The order is stateMutex > keyMutex > snapshotMutex; taking a "
               "lock at or outside the innermost one you already hold is the "
               "inversion that deadlocks the emulation thread.");
    }
    assert(s.depth < HeldStack::kMax && "lock nesting deeper than POM1 has locks");
}

inline void didAcquire(LockRank rank)
{
    HeldStack& s = held();
    if (s.depth < HeldStack::kMax) s.ranks[s.depth++] = static_cast<int>(rank);
}

inline void willRelease(LockRank rank)
{
    HeldStack& s = held();
    // Locks are released innermost-first in every RAII path; a mismatch here
    // means someone unlocked out of order, which is worth knowing about too.
    assert(s.depth > 0 && "releasing a lock this thread does not hold");
    assert(s.ranks[s.depth - 1] == static_cast<int>(rank) &&
           "POM1 lock released out of order (not the innermost one held)");
    if (s.depth > 0) --s.depth;
}

} // namespace lockorder

#endif // POM1_LOCK_ORDER_CHECKS

/// std::mutex plus a rank. BasicLockable, so every existing
/// `std::lock_guard<...>` / `std::unique_lock<...>` site keeps working; only
/// the declared type of the member changes.
template <LockRank Rank>
class RankedMutex {
public:
    void lock()
    {
#if POM1_LOCK_ORDER_CHECKS
        lockorder::willAcquire(Rank);
        mtx_.lock();
        lockorder::didAcquire(Rank);
#else
        mtx_.lock();
#endif
    }

    void unlock()
    {
#if POM1_LOCK_ORDER_CHECKS
        lockorder::willRelease(Rank);
#endif
        mtx_.unlock();
    }

    bool try_lock()
    {
#if POM1_LOCK_ORDER_CHECKS
        lockorder::willAcquire(Rank);
        if (!mtx_.try_lock()) return false;
        lockorder::didAcquire(Rank);
        return true;
#else
        return mtx_.try_lock();
#endif
    }

private:
    std::mutex mtx_;
};

} // namespace pom1

#endif // POM1_LOCK_ORDER_H
