// ACI tape saving (+ round-trip) smoke test.
//
// End-to-end gate for the SAVE half of the cassette path:
//
//   1. Plant a known 64-byte pattern at $0300-$033F.
//   2. Drive the Woz ACI ROM's WRITE routine (`C100R` then `0300.033FW<CR>`)
//      — every access to $C000-$C0FF flips the output flip-flop, which
//      CassetteDevice::toggleOutput() captures into recordedDurations.
//   3. Dump the capture to both `.aci` and `.wav` via saveTape().
//   4. Re-open each saved file in a FRESH Memory + M6502, drive the
//      READ routine the same way aci_tape_loading_test does, and assert
//      the original bytes round-trip byte-for-byte.
//
// Regression gate for any change to:
//   - CassetteDevice::toggleOutput() (the WRITE-side pulse recorder)
//   - saveAciTape / saveWavTape (file format, timebase)
//   - The round-trip invariant kTapeFileTimebaseHz == POM1_CPU_CLOCK_HZ
//     — if either the save or the load path drifts off CPU cycles, the
//     `.wav` round-trip will fail first because the 1-bit / 0-bit half-
//     periods stop straddling the Woz READBIT threshold.
//   - Memory's $C000-$C0FF write sniffer at Memory.cpp:841 (blanket
//     toggleOutput on any write in that range except $C081)
//
// Shares no code with aci_tape_loading_test.cpp on purpose — the two are
// independent regression gates. If READ regresses we still want SAVE's
// round-trip half to be exercisable by tweaking argv.

#include "Memory.h"
// Memory holds unique_ptrs to these (forward-declared in Memory.h); destructor
// needs the complete types.
#include "A1IO_RTC.h"
#include "CassetteDevice.h"
#include "CFFA1.h"
#include "CpuClock.h"
#include "DisplayDevice.h"
#include "M6502.h"
#include "MicroSD.h"
#include "SID.h"
#include "TMS9918.h"
#include "TerminalCard.h"
#include "WiFiModem.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

class DisplayCapture : public DisplayDevice {
public:
    void onChar(char c) override {
        char ascii = static_cast<char>(c & 0x7F);
        if (ascii == 0x0D) ascii = '\n';
        captured.push_back(ascii);
    }
    std::string captured;
};

void queueWozmonCommand(Memory& mem, const char* cmd)
{
    for (const char* p = cmd; *p; ++p) {
        mem.setKeyPressed(*p);
    }
    mem.setKeyPressed('\r');  // Apple-1 Return -> $8D
}

// Puts the CPU at Wozmon's GETLINE entry point and runs the ACI ROM
// until either `done(cyclesConsumed)` returns true or the cycle budget
// runs out. Returns the number of cycles actually consumed.
template <typename DoneFn>
int64_t runAciDriver(M6502& cpu, int64_t cycleBudget, DoneFn done)
{
    constexpr int kCycleSlice = 50000;
    int64_t cyclesConsumed = 0;
    cpu.hardReset();
    cpu.setProgramCounter(0xFF1F);   // Wozmon GETLINE — parses the queued line
    cpu.start();
    while (cyclesConsumed < cycleBudget) {
        cyclesConsumed += cpu.run(kCycleSlice);
        if (done(cyclesConsumed)) break;
    }
    return cyclesConsumed;
}

bool verifyRoundTrip(const std::string& tapePath,
                     uint16_t loadFrom, uint16_t loadTo,
                     const std::vector<uint8_t>& expected)
{
    std::printf("\n--- ROUND-TRIP read via %s ---\n", tapePath.c_str());
    Memory memory;
    M6502  cpu(&memory);
    DisplayCapture display;
    memory.setDisplayDevice(&display);
    memory.setACIEnabled(true);

    uint8_t* mem = memory.getMemoryPointerMutable();
    std::memset(mem + loadFrom, 0x00, (loadTo - loadFrom) + 1);

    CassetteDevice& tape = memory.getCassetteDevice();
    if (!tape.loadTape(tapePath)) {
        std::fprintf(stderr, "  loadTape(%s) failed: %s\n",
                     tapePath.c_str(), tape.getLastError().c_str());
        return false;
    }
    tape.playTape();

    queueWozmonCommand(memory, "C100R");
    char aciCmd[32];
    std::snprintf(aciCmd, sizeof(aciCmd), "%04X.%04XR", loadFrom, loadTo);
    queueWozmonCommand(memory, aciCmd);

    const int loadSize = (loadTo - loadFrom) + 1;
    int64_t cycles = runAciDriver(cpu, 200'000'000LL, [&](int64_t) {
        // Early-exit once the full pattern matches — no need to
        // wait for the tape tail.
        return std::memcmp(mem + loadFrom, expected.data(), loadSize) == 0;
    });

    bool ok = std::memcmp(mem + loadFrom, expected.data(), loadSize) == 0;
    std::printf("  read complete: %lld cycles, match=%s\n",
                static_cast<long long>(cycles), ok ? "yes" : "no");
    if (!ok) {
        std::printf("  expected: ");
        for (int i = 0; i < std::min(16, loadSize); ++i)
            std::printf("%02X ", expected[i]);
        std::printf("\n  got     : ");
        for (int i = 0; i < std::min(16, loadSize); ++i)
            std::printf("%02X ", mem[loadFrom + i]);
        std::printf("\n");
    }
    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    // Target range is configurable so the test can be narrowed/widened for
    // debugging. Defaults: 64 bytes at $0300-$033F — enough to exercise
    // multi-byte framing while keeping the test under ~2 s.
    const uint16_t from = (argc > 1) ? static_cast<uint16_t>(std::stoul(argv[1], nullptr, 16)) : 0x0300;
    const uint16_t to   = (argc > 2) ? static_cast<uint16_t>(std::stoul(argv[2], nullptr, 16)) : 0x033F;
    const int size = (to - from) + 1;
    std::printf("ACI save round-trip test: range $%04X-$%04X (%d bytes)\n",
                from, to, size);

    // Pattern that avoids trivial constant runs so framing bugs are visible
    // (XOR with the offset decorrelates adjacent bytes).
    std::vector<uint8_t> pattern(size);
    for (int i = 0; i < size; ++i) {
        pattern[i] = static_cast<uint8_t>((0xA5 ^ (i * 37)) & 0xFF);
    }

    // -- PHASE 1: WRITE to tape --------------------------------------------
    std::printf("\n--- PHASE 1: WRITE ---\n");
    Memory memory;
    M6502  cpu(&memory);
    DisplayCapture display;
    memory.setDisplayDevice(&display);
    memory.setACIEnabled(true);
    assert(memory.isACIEnabled());

    uint8_t* mem = memory.getMemoryPointerMutable();
    std::memcpy(mem + from, pattern.data(), size);

    CassetteDevice& tape = memory.getCassetteDevice();
    assert(!tape.hasRecordedTape() && "deck must start empty");

    queueWozmonCommand(memory, "C100R");
    char aciCmd[32];
    std::snprintf(aciCmd, sizeof(aciCmd), "%04X.%04XW", from, to);
    queueWozmonCommand(memory, aciCmd);

    // WRITE stops toggling $C000 when it hits end-address. We detect that
    // by watching recordedDurations: once it stops growing for ~5 M cycles
    // (~5 s of emulated wallclock), we consider the capture complete.
    size_t lastCount = 0;
    int64_t cyclesSinceGrowth = 0;
    const int64_t writeCycles = runAciDriver(cpu, 200'000'000LL, [&](int64_t) {
        const size_t now = tape.getRecordedTransitionCount();
        if (now != lastCount) {
            lastCount = now;
            cyclesSinceGrowth = 0;
            return false;
        }
        cyclesSinceGrowth += 50000;
        return now > 100 && cyclesSinceGrowth > 5'000'000;
    });
    std::printf("  write complete: %lld cycles, %zu transitions captured\n",
                static_cast<long long>(writeCycles),
                tape.getRecordedTransitionCount());

    if (tape.getRecordedTransitionCount() < 100) {
        std::fprintf(stderr,
            "FAIL: ACI WRITE produced only %zu transitions. Either the "
            "ACI ROM never reached the WRITE routine, or $C000-$C0FF "
            "writes aren't reaching CassetteDevice::toggleOutput().\n"
            "=== Display capture ===\n%s\n=== End ===\n",
            tape.getRecordedTransitionCount(), display.captured.c_str());
        return 1;
    }

    // -- PHASE 2: save both formats ----------------------------------------
    namespace fs = std::filesystem;
    const fs::path tmpDir = fs::temp_directory_path();
    const std::string aciPath = (tmpDir / "pom1_aci_roundtrip.aci").string();
    const std::string wavPath = (tmpDir / "pom1_aci_roundtrip.wav").string();

    if (!tape.saveTape(aciPath)) {
        std::fprintf(stderr, "FAIL: saveTape(.aci) -> %s\n",
                     tape.getLastError().c_str());
        return 1;
    }
    if (!tape.saveTape(wavPath)) {
        std::fprintf(stderr, "FAIL: saveTape(.wav) -> %s\n",
                     tape.getLastError().c_str());
        return 1;
    }
    std::printf("  saved %s (%lld bytes)\n", aciPath.c_str(),
                static_cast<long long>(fs::file_size(aciPath)));
    std::printf("  saved %s (%lld bytes)\n", wavPath.c_str(),
                static_cast<long long>(fs::file_size(wavPath)));

    // -- PHASE 3: read both back in fresh instances ------------------------
    bool okAci = verifyRoundTrip(aciPath, from, to, pattern);
    bool okWav = verifyRoundTrip(wavPath, from, to, pattern);

    // Cleanup (best-effort — leave the files on disk if somehow in use so
    // the developer can inspect them).
    std::error_code ec;
    fs::remove(aciPath, ec);
    fs::remove(wavPath, ec);

    if (!okAci) {
        std::fprintf(stderr,
            "FAIL: .aci round-trip mismatch — recordedDurations are not "
            "surviving the save/load cycle. Check saveAciTape / "
            "loadAciTape framing.\n");
        return 1;
    }
    if (!okWav) {
        std::fprintf(stderr,
            "FAIL: .wav round-trip mismatch — saveWavTape is emitting "
            "durations at the wrong sample-rate ratio, or pcmToDurations "
            "isn't re-extracting them cleanly.\n");
        return 1;
    }

    std::puts("ACI tape saving round-trip test: OK");
    return 0;
}
