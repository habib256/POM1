// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// RewindBuffer — a bounded, delta-encoded ring of emulation snapshots that
// backs the "state rewind" timeline (microM8-style scrubbing through recent
// history).
//
// Design (MVP):
//   * The buffer works purely on POM1 *snapshot blobs* — the exact byte
//     layout produced by Memory::saveSnapshotToBuffer(). It never touches
//     card internals, so every peripheral's existing serialize/deserialize
//     path is reused verbatim and the buffer can be unit-tested in isolation.
//   * Frames are stored as either a KEYFRAME (a full blob) or a DELTA against
//     the previous frame. Deltas diff section-by-section (matched by the
//     8-byte section name); large sections (RAM, VRAM) are sub-diffed in
//     256-byte chunks, so a typical 1-frame delta is a few KB instead of the
//     ~80 KB full snapshot. That is what stretches a fixed memory budget into
//     minutes of history.
//   * Keyframes anchor *segments*: a keyframe followed by the deltas that
//     depend on it. Eviction drops whole leading segments, so a kept frame
//     is always reconstructable. A fresh keyframe is forced whenever the
//     section set/order changes (card plug/unplug, reset) so a delta only
//     ever has to line up against a structurally identical predecessor.
//
// Threading: RewindBuffer has no internal locking. EmulationController owns
// the instance and only ever touches it under stateMutex (capture from the
// emulation slice, reconstruct/seek from the UI thread routed through the
// controller). Keep it that way — see the mutex-order note in
// EmulationController.h.

#ifndef POM1_REWIND_BUFFER_H
#define POM1_REWIND_BUFFER_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace pom1 {

class RewindBuffer {
public:
    // Default ~128 MB budget — with 256-byte chunk deltas this is many
    // minutes of history on a normal session. Tunable via setMemoryBudget().
    static constexpr std::size_t kDefaultMemoryBudgetBytes = 128u * 1024u * 1024u;
    // Force a fresh keyframe at least this often so a seek replays a bounded
    // number of deltas and eviction granularity stays reasonable.
    static constexpr std::size_t kKeyframeInterval = 60;
    // Sections at or above this payload size are diffed in 256-byte chunks
    // (RAM ~64 KB, TMS9918 VRAM ~16 KB). Smaller sections store whole.
    static constexpr std::size_t kChunkDeltaThreshold = 1024;
    static constexpr std::size_t kChunkSize = 256;

    RewindBuffer() = default;

    void setMemoryBudget(std::size_t bytes) { memoryBudget = bytes ? bytes : 1; evictToBudget(); }
    std::size_t memoryBudgetBytes() const { return memoryBudget; }

    // Append `blob` (a full snapshot from Memory::saveSnapshotToBuffer) as the
    // newest frame. Stored as a keyframe or a delta automatically. Empty or
    // malformed blobs are ignored. Evicts oldest segments past the budget.
    void capture(const std::vector<uint8_t>& blob);

    // Number of frames currently retained (0..). Position indices into the
    // reconstruct()/the UI slider run [0, frameCount).
    std::size_t frameCount() const { return frames.size(); }
    bool empty() const { return frames.empty(); }

    // Rebuild the full snapshot blob for frame `pos`. Returns empty if out of
    // range. The result is byte-identical to the blob originally captured for
    // that frame and can be fed straight to Memory::loadSnapshotFromBuffer.
    std::vector<uint8_t> reconstruct(std::size_t pos) const;

    // Drop every frame after `pos` (used when the user resumes live emulation
    // from a rewound point — the discarded future is overwritten by new
    // capture). After this, frameCount() == pos+1.
    void truncateAfter(std::size_t pos);

    void clear();

    // Accounting / diagnostics for the UI.
    std::size_t storedBytes() const { return storedBytes_; }
    std::size_t keyframeCount() const;

private:
    struct SectionRef { std::string name; std::size_t offset; std::size_t length; };

    // One section's contribution to a delta frame.
    struct SectionDelta {
        std::string name;
        uint32_t    length = 0;            // payload length in the rebuilt blob
        uint8_t     kind   = 0;            // 0=COPY 1=FULL 2=CHUNKED
        std::vector<uint8_t> full;         // kind==FULL: entire payload
        // kind==CHUNKED: (chunkIndex, bytes) for each changed 256-byte chunk
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> chunks;
    };

    struct Frame {
        bool keyframe = false;
        std::vector<uint8_t>      blob;    // keyframe only: full snapshot
        std::vector<uint8_t>      header;  // delta only: 16-byte snapshot header
        std::vector<SectionDelta> deltas;  // delta only: per-section, cur order
        std::size_t bytes = 0;             // accounted contribution to budget
    };

    static bool parseSections(const std::vector<uint8_t>& blob,
                              std::vector<SectionRef>& out);
    // Build a delta of `cur` against `prev`. Returns false when a delta is
    // impossible or not worthwhile (caller then stores a keyframe).
    static bool buildDelta(const std::vector<uint8_t>& prev,
                           const std::vector<uint8_t>& cur,
                           Frame& outFrame);
    static std::vector<uint8_t> applyDelta(const std::vector<uint8_t>& prev,
                                           const Frame& delta);
    static std::size_t frameBytes(const Frame& f);

    void pushKeyframe(const std::vector<uint8_t>& blob);
    void evictToBudget();
    void recomputeTailState();

    std::deque<Frame>    frames;
    std::vector<uint8_t> lastBlob;             // reconstruction of the newest frame
    std::size_t          framesSinceKeyframe = 0;
    std::size_t          storedBytes_ = 0;
    std::size_t          memoryBudget = kDefaultMemoryBudgetBytes;
};

} // namespace pom1

#endif // POM1_REWIND_BUFFER_H
