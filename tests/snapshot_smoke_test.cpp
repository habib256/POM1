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
#include "M6502.h"
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

    // ── CPU round-trip. Plant a tiny program at $0300 that mutates A/X/Y
    //    to known sentinel values, then step the CPU through it so the
    //    saved registers differ from any plausible reset state.
    //
    //    $0300: A9 42      LDA #$42
    //    $0302: A2 33      LDX #$33
    //    $0304: A0 77      LDY #$77
    //    $0306: EA         NOP
    mem.memWrite(0x0300, 0xA9); mem.memWrite(0x0301, 0x42);
    mem.memWrite(0x0302, 0xA2); mem.memWrite(0x0303, 0x33);
    mem.memWrite(0x0304, 0xA0); mem.memWrite(0x0305, 0x77);
    mem.memWrite(0x0306, 0xEA);

    M6502 cpu(&mem);
    cpu.setProgramCounter(0x0300);
    cpu.start();
    for (int i = 0; i < 4; ++i) cpu.step();
    cpu.stop();
    const uint8_t  expectedA  = cpu.getAccumulator();
    const uint8_t  expectedX  = cpu.getXRegister();
    const uint8_t  expectedY  = cpu.getYRegister();
    const uint8_t  expectedSR = cpu.getStatusRegister();
    const uint8_t  expectedSP = cpu.getStackPointer();
    const uint16_t expectedPC = cpu.getProgramCounter();
    assert(expectedA == 0x42);
    assert(expectedX == 0x33);
    assert(expectedY == 0x77);
    assert(expectedPC == 0x0307);

    // ── Save
    auto path = makeTempPath("roundtrip");
    std::string err;
    if (!mem.saveSnapshot(path.string(), err, &cpu)) {
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

    // ── Fresh Memory + CPU; neither RAM nor registers should match yet.
    Memory mem2;
    M6502 cpu2(&mem2);
    bool anyDiff = false;
    for (size_t i = 0; i < baseline.size() && !anyDiff; ++i) {
        if (mem2.memRead(static_cast<uint16_t>(0x0200 + i)) != baseline[i])
            anyDiff = true;
    }
    assert(anyDiff && "fresh Memory must differ from snapshotted RAM");
    assert(!mem2.isPR40Enabled());
    assert(!mem2.isGT6144Enabled());
    assert(cpu2.getProgramCounter() != expectedPC ||
           cpu2.getAccumulator()    != expectedA  ||
           cpu2.getXRegister()      != expectedX  ||
           cpu2.getYRegister()      != expectedY);

    // ── Load snapshot into the fresh instance
    if (!mem2.loadSnapshot(path.string(), err, &cpu2)) {
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

    // ── CPU registers must round-trip exactly
    if (cpu2.getAccumulator()    != expectedA  ||
        cpu2.getXRegister()      != expectedX  ||
        cpu2.getYRegister()      != expectedY  ||
        cpu2.getStatusRegister() != expectedSR ||
        cpu2.getStackPointer()   != expectedSP ||
        cpu2.getProgramCounter() != expectedPC) {
        std::fprintf(stderr,
            "CPU mismatch: A=%02X/%02X X=%02X/%02X Y=%02X/%02X "
            "SR=%02X/%02X SP=%02X/%02X PC=%04X/%04X\n",
            cpu2.getAccumulator(),    expectedA,
            cpu2.getXRegister(),      expectedX,
            cpu2.getYRegister(),      expectedY,
            cpu2.getStatusRegister(), expectedSR,
            cpu2.getStackPointer(),   expectedSP,
            cpu2.getProgramCounter(), expectedPC);
        return 1;
    }

    // ── Memory-only round-trip path (no CPU pointer): the "CPU" section
    //    written above must be skipped cleanly by a reader that doesn't
    //    care about CPU state.
    Memory mem3;
    if (!mem3.loadSnapshot(path.string(), err)) {
        std::fprintf(stderr, "loadSnapshot (memory-only) failed: %s\n", err.c_str());
        return 1;
    }
    assert(mem3.isPR40Enabled());
    assert(mem3.isGT6144Enabled());

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::printf("snapshot round-trip OK (%zu RAM bytes, 2 cards, CPU regs)\n",
                baseline.size());
    return 0;
}
