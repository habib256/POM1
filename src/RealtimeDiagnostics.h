#ifndef POM1_REALTIME_DIAGNOSTICS_H
#define POM1_REALTIME_DIAGNOSTICS_H

#include <atomic>
#include <cstdint>

namespace pom1 {

#if !defined(NDEBUG)
#define POM1_REALTIME_DIAGNOSTICS 1
#else
#define POM1_REALTIME_DIAGNOSTICS 0
#endif

struct RealtimeDiagnostics {
    uint64_t stateLockAcquisitions = 0;
    uint64_t maxStateWaitNs = 0;
    uint64_t maxStateHoldNs = 0;
    uint64_t audioCallbacks = 0;
    uint64_t maxAudioCallbackNs = 0;
    uint64_t sidUnderruns = 0;
    uint64_t sidOverflows = 0;
    uint64_t cassetteUnderruns = 0;
    uint64_t cassetteOverflows = 0;
};

#if POM1_REALTIME_DIAGNOSTICS
inline void updateAtomicMaximum(std::atomic<uint64_t>& target, uint64_t value)
{
    uint64_t current = target.load(std::memory_order_relaxed);
    while (current < value &&
           !target.compare_exchange_weak(current, value,
                                         std::memory_order_relaxed)) {}
}
#endif

} // namespace pom1

#endif
