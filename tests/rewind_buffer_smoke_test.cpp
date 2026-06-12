// State-rewind ring buffer smoke test.
//
// Exercises pom1::RewindBuffer — the delta-encoded snapshot ring that backs
// the rewind timeline — at two levels:
//
//   1. Pure byte-format level (no Memory): synthetic snapshot blobs built
//      with the in-memory SnapshotWriter. Asserts that every captured frame
//      reconstructs byte-for-byte, that reconstruction is exact across a
//      forced keyframe boundary (delta replay), that COPY / FULL / CHUNKED
//      section deltas all round-trip, that budget eviction drops whole
//      leading segments while keeping the rest reconstructable, and that
//      truncateAfter() rewrites the future cleanly.
//
//   2. End-to-end through Memory: capture real snapshots via
//      Memory::saveSnapshotToBuffer, reconstruct a past frame, and load it
//      into a fresh Memory via loadSnapshotFromBuffer — proving the rewind
//      path reuses the production serialize/deserialize format intact.

#include "TMS9918.h"
#include "WiFiModem.h"
#include "TerminalCard.h"
#include "A1IO_RTC.h"
#include "PR40Printer.h"
#include "GT6144.h"
#include "JukeBox.h"
#include "CodeTank.h"
#include "CFFA1.h"
#include "MicroSD.h"
#include "SID.h"
#include "Memory.h"
#include "M6502.h"
#include "SnapshotIO.h"
#include "RewindBuffer.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// Build a synthetic snapshot blob with a chunk-deltable "MEM" section (>=
// kChunkDeltaThreshold so it goes through the 256-byte chunk path) and a
// small "TAG" section (always FULL when it changes).
std::vector<uint8_t> makeBlob(const std::vector<uint8_t>& ram, uint32_t tag)
{
    pom1::SnapshotWriter w;  // in-memory
    auto h = w.beginSection("MEM");
    w.writeBytes(ram.data(), ram.size());
    w.endSection(h);
    auto h2 = w.beginSection("TAG");
    w.writeU32(tag);
    w.endSection(h2);
    return w.takeBuffer();
}

int failures = 0;
void check(bool cond, const char* what)
{
    if (!cond) { std::printf("  FAIL: %s\n", what); ++failures; }
}

void testFormatLevel()
{
    std::printf("[rewind] synthetic blob delta round-trip\n");

    constexpr std::size_t kRam = 4096;  // > kChunkDeltaThreshold
    pom1::RewindBuffer rb;

    std::vector<std::vector<uint8_t>> originals;
    std::vector<uint8_t> ram(kRam, 0x00);

    // Capture enough frames to cross at least one forced keyframe boundary.
    const std::size_t frameCount = pom1::RewindBuffer::kKeyframeInterval * 2 + 5;
    for (std::size_t i = 0; i < frameCount; ++i) {
        if (i % 7 != 0) {
            // Mutate a few bytes → CHUNKED delta on MEM.
            ram[(i * 37) % kRam]  = static_cast<uint8_t>(i);
            ram[(i * 101) % kRam] = static_cast<uint8_t>(i * 3 + 1);
        }
        // Every 7th frame leaves MEM untouched but bumps TAG → MEM COPY + TAG FULL.
        std::vector<uint8_t> blob = makeBlob(ram, static_cast<uint32_t>(i * 1000 + 1));
        rb.capture(blob);
        originals.push_back(std::move(blob));
    }

    check(rb.frameCount() == frameCount, "all frames retained (budget ample)");
    check(rb.keyframeCount() >= 2, "at least two keyframes (boundary crossed)");

    bool allExact = true;
    for (std::size_t i = 0; i < frameCount; ++i)
        if (rb.reconstruct(i) != originals[i]) { allExact = false; break; }
    check(allExact, "every frame reconstructs byte-for-byte");

    // truncateAfter: drop the future, confirm tail is reconstructable, and
    // that capture resumes cleanly from the new tail.
    const std::size_t cut = frameCount / 2;
    rb.truncateAfter(cut);
    check(rb.frameCount() == cut + 1, "truncateAfter trims to cut+1 frames");
    check(rb.reconstruct(cut) == originals[cut], "tail frame still exact after truncate");

    ram[0] = 0xEE;
    std::vector<uint8_t> resumed = makeBlob(ram, 0xABCDEF);
    rb.capture(resumed);
    check(rb.frameCount() == cut + 2, "capture after truncate appends");
    check(rb.reconstruct(rb.frameCount() - 1) == resumed, "resumed frame exact");
}

void testEviction()
{
    std::printf("[rewind] budget eviction keeps recent frames reconstructable\n");

    constexpr std::size_t kRam = 4096;
    pom1::RewindBuffer rb;
    rb.setMemoryBudget(40 * 1024);  // small — forces eviction of old segments

    std::vector<std::vector<uint8_t>> originals;
    std::vector<uint8_t> ram(kRam, 0x11);

    const std::size_t frameCount = pom1::RewindBuffer::kKeyframeInterval * 4;
    for (std::size_t i = 0; i < frameCount; ++i) {
        ram[(i * 17) % kRam] = static_cast<uint8_t>(i + 7);
        std::vector<uint8_t> blob = makeBlob(ram, static_cast<uint32_t>(i + 1));
        rb.capture(blob);
        originals.push_back(std::move(blob));
    }

    check(rb.frameCount() < frameCount, "eviction dropped some frames");
    check(rb.storedBytes() <= rb.memoryBudgetBytes() || rb.keyframeCount() == 1,
          "stored bytes within budget (or one indivisible segment)");
    // The newest frame must always match the last original.
    check(rb.reconstruct(rb.frameCount() - 1) == originals.back(),
          "newest frame exact after eviction");
    // The oldest retained frame must reconstruct (it is a keyframe).
    check(!rb.reconstruct(0).empty(), "oldest retained frame reconstructs");
}

void testMemoryRoundTrip()
{
    std::printf("[rewind] end-to-end through Memory save/load buffer\n");

    Memory mem;
    mem.memWrite(0x0300, 0xAA);
    std::vector<uint8_t> blob0 = mem.saveSnapshotToBuffer(nullptr);
    check(!blob0.empty(), "saveSnapshotToBuffer produced bytes");

    mem.memWrite(0x0300, 0x55);
    std::vector<uint8_t> blob1 = mem.saveSnapshotToBuffer(nullptr);

    pom1::RewindBuffer rb;
    rb.capture(blob0);
    rb.capture(blob1);
    check(rb.frameCount() == 2, "two frames captured from Memory");

    // Reconstruct the older frame and load it into a fresh Memory.
    {
        Memory mem2;
        std::string err;
        bool ok = mem2.loadSnapshotFromBuffer(rb.reconstruct(0), err, nullptr);
        check(ok, "reconstructed frame 0 loads into fresh Memory");
        check(mem2.memRead(0x0300) == 0xAA, "frame 0 restored RAM byte (0xAA)");
    }
    {
        Memory mem3;
        std::string err;
        bool ok = mem3.loadSnapshotFromBuffer(rb.reconstruct(1), err, nullptr);
        check(ok, "reconstructed frame 1 loads into fresh Memory");
        check(mem3.memRead(0x0300) == 0x55, "frame 1 restored RAM byte (0x55)");
    }
}

} // namespace

int main()
{
    testFormatLevel();
    testEviction();
    testMemoryRoundTrip();

    if (failures == 0) {
        std::printf("rewind_buffer_smoke: PASS\n");
        return 0;
    }
    std::printf("rewind_buffer_smoke: FAIL (%d checks)\n", failures);
    return 1;
}
