// lock_order_smoke — proves src/LockOrder.h actually fires.
//
// A lock-order checker that never trips is worse than no checker: it reads as
// coverage while providing none. So this pins both directions —
//
//   §1  the legal nesting (state > keyboard > snapshot) is accepted, in every
//       partial combination the real code uses;
//   §2  each illegal nesting ABORTS, verified by forking and reaping SIGABRT.
//
// §2 is the half that matters. It is skipped on Windows (no fork) and in any
// build with NDEBUG, where the checks compile out by design — the test asserts
// that fact instead, so "checks disabled" can never masquerade as "checks pass".

#include <cassert>
#include <cstdio>

#include "LockOrder.h"

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#define POM1_HAVE_FORK 1
#else
#define POM1_HAVE_FORK 0
#endif

using pom1::LockRank;
using pom1::RankedMutex;

namespace {

RankedMutex<LockRank::State>    stateM;
RankedMutex<LockRank::Keyboard> keyM;
RankedMutex<LockRank::Snapshot> snapM;

#if POM1_HAVE_FORK && POM1_LOCK_ORDER_CHECKS
// Runs `fn` in a child process; returns true iff the child died on SIGABRT
// (i.e. an assert fired). stderr is silenced so a passing run stays readable.
template <typename F>
bool abortsInChild(F fn)
{
    fflush(nullptr);
    pid_t pid = fork();
    assert(pid >= 0 && "fork failed");
    if (pid == 0) {
        if (freopen("/dev/null", "w", stderr) == nullptr) _exit(90);
        fn();
        _exit(0);            // no abort → the checker missed the inversion
    }
    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}
#endif

} // namespace

int main()
{
    // ── §1 — every legal order the emulator actually takes ───────────────────
    // Full nest, outermost first. This is the emulation thread's slice:
    // stateMutex held across drainTo() (keyMutex) and publish() (snapshotMutex).
    {
        std::lock_guard<decltype(stateM)> a(stateM);
        {
            std::lock_guard<decltype(keyM)> b(keyM);
        }
        {
            std::lock_guard<decltype(snapM)> c(snapM);
        }
    }

    // Each lock alone — the UI thread's copyTo() takes snapshotMutex with
    // nothing else held, and queueKey() takes keyMutex the same way.
    { std::lock_guard<decltype(stateM)> a(stateM); }
    { std::lock_guard<decltype(keyM)>   b(keyM);   }
    { std::lock_guard<decltype(snapM)>  c(snapM);  }

    // Skipping a rank is legal: strictly-decreasing, not consecutive.
    {
        std::lock_guard<decltype(stateM)> a(stateM);
        std::lock_guard<decltype(snapM)>  c(snapM);
    }

    // Truly nested, all three at once.
    {
        std::lock_guard<decltype(stateM)> a(stateM);
        std::lock_guard<decltype(keyM)>   b(keyM);
        std::lock_guard<decltype(snapM)>  c(snapM);
    }

    // Releasing must have unwound the thread's stack completely; if it did not,
    // the very next legal acquisition below would trip.
    { std::lock_guard<decltype(stateM)> a(stateM); }

#if !POM1_LOCK_ORDER_CHECKS
    // A build with NDEBUG compiles the checks out — say so out loud rather than
    // reporting a green run that verified nothing.
    printf("lock_order_smoke: checks compiled out (NDEBUG) — §1 only\n");
    return 0;
#elif !POM1_HAVE_FORK
    printf("lock_order_smoke: no fork() on this platform — §1 only\n");
    return 0;
#else
    // ── §2 — every inversion must abort ──────────────────────────────────────
    // keyMutex held, then stateMutex: the exact deadlock the header warns about
    // (the emulation thread owns state and waits on key; the UI thread the
    // reverse).
    assert(abortsInChild([] {
        std::lock_guard<decltype(keyM)>   b(keyM);
        std::lock_guard<decltype(stateM)> a(stateM);
    }) && "keyboard-then-state inversion was NOT caught");

    // snapshotMutex is innermost: nothing may be taken while it is held.
    assert(abortsInChild([] {
        std::lock_guard<decltype(snapM)>  c(snapM);
        std::lock_guard<decltype(stateM)> a(stateM);
    }) && "snapshot-then-state inversion was NOT caught");

    assert(abortsInChild([] {
        std::lock_guard<decltype(snapM)> c(snapM);
        std::lock_guard<decltype(keyM)>  b(keyM);
    }) && "snapshot-then-keyboard inversion was NOT caught");

    // Equal rank is an inversion too: two locks of the same rank have no
    // defined order between them, so nesting them is exactly as unsafe.
    static RankedMutex<LockRank::Keyboard> keyM2;
    assert(abortsInChild([] {
        std::lock_guard<decltype(keyM)>  b(keyM);
        std::lock_guard<decltype(keyM2)> b2(keyM2);
    }) && "same-rank nesting was NOT caught");

    // Control: the legal order must NOT abort. Without this, a checker that
    // aborted on everything would pass all four assertions above.
    assert(!abortsInChild([] {
        std::lock_guard<decltype(stateM)> a(stateM);
        std::lock_guard<decltype(keyM)>   b(keyM);
        std::lock_guard<decltype(snapM)>  c(snapM);
    }) && "the DOCUMENTED order aborted — the checker is inverted");

    printf("lock_order_smoke: OK (4 inversions caught, legal order accepted)\n");
    return 0;
#endif
}
