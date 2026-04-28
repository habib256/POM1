// Snapshot save/load smoke test.
//
// Round-trips a Memory instance through saveSnapshot → loadSnapshot and
// asserts:
//   - random RAM bytes are preserved across the round-trip;
//   - the snapshot file starts with the documented "POM1SNAP" magic +
//     version number (so a future format change forces a deliberate
//     bump rather than a silent breakage);
//   - card-enabled flags survive (we exercise PR-40 + GT-6144 because
//     those don't pull in network sockets or audio devices that would
//     complicate a pure-memory test).
//
// What this test does NOT (yet) cover — and why:
//   - per-card payloads beyond enable flags. Most cards still ship the
//     default `Peripheral::serialize()` no-op (see Peripheral.h);
//     migrating each card's internal state is the next layer of work.
//   - CPU register state. Snapshot v1 only captures Memory; CPU snapshot
//     lives behind EmulationController::saveSnapshot once that exists.
//
// Adding card-payload coverage. Once a card overrides serialize/deserialize,
// add an assertion here that mutates the card, snapshots, mutates again,
// loads, and confirms the snapshot value sticks.

// Memory.h forward-declares the cards it owns via unique_ptr to avoid
// a header avalanche; instantiating Memory in user code (like this test)
// needs the full types so the unique_ptr destructors are emitted.
#include "TMS9918.h"
#include "WiFiModem.h"
#include "TerminalCard.h"
#include "A1IO_RTC.h"
#include "PR40Printer.h"
#include "Memory.h"
#include "SnapshotIO.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace {

std::filesystem::path makeTempPath(const char* tag)
{
    auto tmp = std::filesystem::temp_directory_path()
             / (std::string("pom1_snap_") + tag + ".snap");
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    return tmp;
}

} // namespace

int main()
{
    Memory mem;

    // ── Mutate RAM with a deterministic pseudo-random pattern in the user
    //    area ($0200-$1FFF). Avoid ROM zones (BASIC at $E000+, Wozmon at
    //    $FF00+) because those are reloaded by Memory's constructor on the
    //    fresh instance we create below — comparing them would test
    //    Memory's ROM loader, not the snapshot path.
    std::mt19937 rng(42);
    for (int addr = 0x0200; addr < 0x2000; ++addr) {
        mem.memWrite(static_cast<uint16_t>(addr),
                     static_cast<uint8_t>(rng() & 0xFF));
    }

    // ── Enable two well-isolated cards. PR-40 + GT-6144 have no host
    //    side-effects (sockets, audio devices) so the test stays
    //    self-contained.
    mem.setPR40Enabled(true);
    mem.setGT6144Enabled(true);

    // ── Save
    auto path = makeTempPath("roundtrip");
    std::string err;
    if (!mem.saveSnapshot(path.string(), err)) {
        std::fprintf(stderr, "saveSnapshot failed: %s\n", err.c_str());
        return 1;
    }

    // ── Verify file starts with magic + expected version
    {
        std::ifstream in(path, std::ios::binary);
        char magic[8]{};
        in.read(magic, sizeof(magic));
        assert(std::memcmp(magic, pom1::kSnapshotMagic, sizeof(magic)) == 0);
        unsigned char ver[4]{};
        in.read(reinterpret_cast<char*>(ver), 4);
        const uint32_t v = ver[0] | (ver[1] << 8) | (ver[2] << 16) | (ver[3] << 24);
        assert(v == pom1::kSnapshotVersion);
    }

    // ── Snapshot a baseline of the user-area RAM for comparison after the
    //    fresh-Memory + load round-trip.
    std::vector<uint8_t> baseline(0x2000 - 0x0200);
    for (size_t i = 0; i < baseline.size(); ++i) {
        baseline[i] = mem.memRead(static_cast<uint16_t>(0x0200 + i));
    }

    // ── Fresh Memory; nothing should match the previous one in the user area.
    Memory mem2;
    bool anyDiff = false;
    for (size_t i = 0; i < baseline.size() && !anyDiff; ++i) {
        if (mem2.memRead(static_cast<uint16_t>(0x0200 + i)) != baseline[i])
            anyDiff = true;
    }
    assert(anyDiff && "fresh Memory must differ from snapshotted RAM");
    assert(!mem2.isPR40Enabled());
    assert(!mem2.isGT6144Enabled());

    // ── Load snapshot into the fresh instance
    if (!mem2.loadSnapshot(path.string(), err)) {
        std::fprintf(stderr, "loadSnapshot failed: %s\n", err.c_str());
        return 1;
    }

    // ── User-area RAM must round-trip exactly
    for (size_t i = 0; i < baseline.size(); ++i) {
        const uint8_t got = mem2.memRead(static_cast<uint16_t>(0x0200 + i));
        if (got != baseline[i]) {
            std::fprintf(stderr,
                "RAM mismatch at $%04X: expected %02X got %02X\n",
                int(0x0200 + i), baseline[i], got);
            return 1;
        }
    }

    // ── Card-enabled flags must round-trip
    assert(mem2.isPR40Enabled());
    assert(mem2.isGT6144Enabled());

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::printf("snapshot round-trip OK (%zu RAM bytes, 2 cards)\n", baseline.size());
    return 0;
}
