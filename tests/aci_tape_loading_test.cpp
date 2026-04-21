// ACI tape loading smoke test.
//
// End-to-end: feed the real cassettes/APPLE50TH.ogg through CassetteDevice,
// invoke the Woz ACI ROM at $C100 exactly the way the user does on the
// real machine (`C100R` then `0280.0FFFR<CR>`), and assert that the first
// three bytes of the APPLE50TH demo payload (`A9 FF 48` — LDA #$FF / PHA)
// land at $0280 (the demo's natural load address — see tapeinfo.txt).
//
// This is THE regression gate for any change to:
//   - CassetteDevice's pulse extraction (pcmToDurations, zero-crossing
//     threshold, kTapeFileTimebaseHz vs POM1_CPU_CLOCK_HZ)
//   - Memory's ACI ROM load (kAciRom) or $C081/$D010/$D011 wiring
//   - M6502 instruction timing (CPY/INX/branch — the READBIT loop is
//     cycle-sensitive)
//   - Any peripheral bus ordering that could shadow $C000-$C0FF
//
// argv[1] = absolute path to the .ogg cassette (passed from CMake).

#include "Memory.h"
// Memory holds unique_ptrs to these (forward-declared in Memory.h); destructor
// needs the complete types.
#include "A1IO_RTC.h"
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
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

namespace {
// Captures everything written to $D012 so we can see the ACI's prompts and
// echoes. Strips bit 7 to render readable ASCII.
class DisplayCapture : public DisplayDevice {
public:
    void onChar(char c) override {
        char ascii = static_cast<char>(c & 0x7F);
        if (ascii == 0x0D) ascii = '\n';
        captured.push_back(ascii);
    }
    std::string captured;
};
} // namespace

namespace {

// Queue every character of `cmd` into the Apple-1 keyboard. Real hardware
// types one char at a time with strobe; setKeyPressed / keyBuffer already
// models that. The trailing CR ($0D + bit-7 = $8D when read via $D010) is
// what unblocks the ACI ROM's `CMP #$8D / BEQ parse-buffer` step.
void queueWozmonCommand(Memory& mem, const char* cmd)
{
    for (const char* p = cmd; *p; ++p) {
        mem.setKeyPressed(*p);
    }
    mem.setKeyPressed('\r');  // Apple-1 Return -> $8D
}

int countNonZero(const uint8_t* data, int len)
{
    int n = 0;
    for (int i = 0; i < len; ++i) if (data[i] != 0) ++n;
    return n;
}

void dumpHex(const uint8_t* data, int len)
{
    for (int i = 0; i < len; ++i) {
        std::printf("%02X ", data[i]);
        if ((i & 15) == 15) std::puts("");
    }
    if (len & 15) std::puts("");
}

} // namespace

int main(int argc, char** argv)
{
    // Default: APPLE50TH.ogg into $0280-$0FFF (the demo's natural load
    // range — see cassettes/tapeinfo.txt), expect the first 3 bytes of the
    // demo (`A9 FF 48` = LDA #$FF / PHA). The destination is plain Apple 1
    // RAM, the test memsets it to zero before the ACI READ, and the
    // signature comes from the tape's pulse stream — so the bytes can
    // only appear via the full pulse-extraction → ACI ROM pipeline.
    //
    // Optional override: argv[2] = load-from hex, argv[3] = load-to hex,
    //                    argv[4] = three-byte expected signature in hex
    //                              ("A9FF48") or empty to just assert
    //                              that *something* loaded.
    if (argc < 2) {
        std::fprintf(stderr,
            "usage: %s <tape.ogg|wav> [load_from_hex] [load_to_hex] [signature_hex]\n"
            "  defaults: load_from=0280  load_to=0FFF  signature=A9FF48\n",
            argv[0]);
        return 2;
    }
    const std::string tapePath = argv[1];
    const uint16_t loadFrom = (argc > 2) ? static_cast<uint16_t>(std::stoul(argv[2], nullptr, 16)) : 0x0280;
    const uint16_t loadTo   = (argc > 3) ? static_cast<uint16_t>(std::stoul(argv[3], nullptr, 16)) : 0x0FFF;
    // Signature = 0..3 expected bytes (printed as 6-hex-digit string with no
    // separator, e.g. "A9FF48"). Empty disables the strict check.
    const std::string sig = (argc > 4) ? std::string(argv[4]) : std::string("A9FF48");
    uint8_t sigBytes[3] = {0, 0, 0};
    int sigLen = 0;
    for (size_t i = 0; i + 1 < sig.size() && sigLen < 3; i += 2) {
        sigBytes[sigLen++] = static_cast<uint8_t>(std::stoul(sig.substr(i, 2), nullptr, 16));
    }
    std::printf("Loading %s into $%04X-$%04X (sig=%s, %d bytes)\n",
                tapePath.c_str(), loadFrom, loadTo, sig.c_str(), sigLen);

    Memory memory;
    M6502  cpu(&memory);

    DisplayCapture display;
    memory.setDisplayDevice(&display);

    // ---- B1 regression: loadTape() with ACI off enters AUDIO STREAM ----
    // Contract (post-fix): loading an audio file (wav/ogg/mp3/flac) while
    // the ACI is unplugged succeeds in audio-stream mode so the cassette
    // deck acts as a simple audio player. The zombie-state hazard (a
    // stream-mode tape still loaded when the ACI gets plugged back in
    // would make the ROM poll $C081 forever on a flat input) is guarded
    // at the setACIEnabled → setAciActive boundary: plugging the ACI
    // evicts any stream-mode tape. Verify both halves here.
    //
    // .aci is explicit pulse data and loadAciTape() runs regardless of
    // aciActive, so we hardcode cassettes/APPLE50TH.ogg instead of whatever
    // argv[1] is — lets the rest of this test take a `.aci` path as
    // input for other round-trip checks without breaking the B1 gate.
    {
        CassetteDevice& t = memory.getCassetteDevice();
        assert(!memory.isACIEnabled() && "fresh Memory should have ACI off");
        const std::string b1Path = "cassettes/APPLE50TH.ogg";
        if (!t.loadTape(b1Path)) {
            std::fprintf(stderr,
                "FAIL: loadTape(.ogg) returned false with ACI off — "
                "audio-stream fallback is broken: %s\n",
                t.getLastError().c_str());
            return 1;
        }
        if (!t.isAudioStreamMode() || !t.hasLoadedTape()) {
            std::fprintf(stderr,
                "FAIL: loadTape(.ogg) succeeded but isAudioStreamMode=%d "
                "hasLoadedTape=%d — expected true/true.\n",
                t.isAudioStreamMode() ? 1 : 0,
                t.hasLoadedTape() ? 1 : 0);
            return 1;
        }
        std::printf("B1 OK: loadTape entered audio-stream mode with ACI off\n");

        // Zombie-state guard: plugging the ACI must evict the stream tape.
        memory.setACIEnabled(true);
        if (t.hasLoadedTape() || t.isAudioStreamMode()) {
            std::fprintf(stderr,
                "FAIL: setACIEnabled(true) did not evict the stream tape — "
                "ACI ROM would poll $C081 forever on a flat input.\n");
            return 1;
        }
        std::printf("B1 OK: setACIEnabled(true) evicted the stream tape\n");
        memory.setACIEnabled(false);
    }

    // Plug the ACI (loads roms/ACI.rom at $C100 + enables $C000/$C081 bus
    // handlers) and mark the cassette device as "ACI active" so the
    // subsequent loadTape() picks the pulse-extraction path instead of
    // falling through to audio-stream mode.
    memory.setACIEnabled(true);
    assert(memory.isACIEnabled());

    // Zero the target window so a successful load is unambiguously detectable.
    // Memory::initMemory() pre-fills $E000-$EFFF from roms/basic.rom, which
    // would otherwise mask a completely broken ACI load path when the
    // default target region is used.
    uint8_t* mem = memory.getMemoryPointerMutable();
    std::memset(mem + loadFrom, 0x00, (loadTo - loadFrom) + 1);

    // Load the tape. loadTape() chooses loadMiniaudioTape (OGG decoder + pulse
    // extraction) because aciActive==true was latched by setACIEnabled above.
    CassetteDevice& tape = memory.getCassetteDevice();
    if (!tape.loadTape(tapePath)) {
        std::fprintf(stderr, "loadTape failed: %s\n", tape.getLastError().c_str());
        return 3;
    }
    const size_t loadedTransitions = tape.getLoadedTransitionCount();
    std::printf("loaded %zu transitions from %s\n",
                loadedTransitions, tapePath.c_str());
    assert(loadedTransitions > 1000 && "pulse extraction produced almost nothing");

    // Arm the tape. Under B6 (play-on-first-read), playTape() doesn't
    // activate playback — it arms the deck. The first LDA $C081 issued
    // by the ACI ROM flips the armed bit to active inside
    // readTapeInput(); from there advancePlayback consumes pulses on
    // every Memory::advanceCycles call, so the tape streams beneath
    // the CPU without burning anything while Wozmon waits for input.
    tape.playTape();
    assert(tape.hasLoadedTape());
    assert(!tape.isPlaybackActive() && "B6: PLAY must arm, not activate");
    // Arming must be externally observable so UI code can distinguish
    // "armed, about to play" from "EOF, finished" — both leave
    // isPlaybackActive() == false but only the former should keep the
    // UI's transport latched at Playing (CassetteDeck_ImGui skips its
    // auto-return-to-Stopped guard when cassettePlaybackArmed is true).
    assert(tape.isPlaybackArmed() && "B6: PLAY must leave the deck armed");

    // Prime the keyboard buffer with the Wozmon/ACI command sequence. The
    // ACI ROM reads $D010/$D011 in a tight poll loop; each setKeyPressed
    // call adds one character to the queue that memRead drains one-by-one
    // as the ROM consumes them.
    //
    // C100R jumps to the ACI ROM via Wozmon's R command.
    // <from>.<to>R tells the ACI to READ the tape and store bytes at
    //              [from, to]. No leading "0:" — that's Wozmon store syntax
    //              the ACI parser doesn't understand.
    queueWozmonCommand(memory, "C100R");
    char aciCmd[32];
    std::snprintf(aciCmd, sizeof(aciCmd), "%04X.%04XR", loadFrom, loadTo);
    queueWozmonCommand(memory, aciCmd);

    // Drop the CPU at Wozmon's GETLINE entry point ($FF1F). The CR from
    // the first queueWozmonCommand will close the line, Wozmon will parse
    // "C100R" and JMP to the ACI ROM. From there the second line drives
    // the ACI READ.
    cpu.hardReset();
    cpu.setProgramCounter(0xFF1F);
    cpu.start();

    // Budget: APPLE50TH.ogg is ~40 s of tape at nominal 1200 baud for
    // ~3.5 KB of payload. A 200 M cycle budget = ~200 s of emulated
    // wallclock — enough even if the capture has a long pre-roll leader
    // or if the extractor slightly under-resolves transitions.
    constexpr int kCycleSlice = 50000;
    constexpr int64_t kCycleBudget = 200'000'000LL;
    int64_t cyclesConsumed = 0;

    // Track progress — if no bytes land in $E000-$EFFF for 50 M cycles after
    // some signs of tape activity, bail out early with a diagnostic.
    int lastNonZero = 0;
    int64_t cyclesSinceProgress = 0;

    // PC trace counters — buckets the CPU's location across the run so we
    // can tell where the time is being spent (Wozmon stuck waiting? ACI
    // ROM looping? user RAM?).
    int64_t pcWozmon = 0, pcAci = 0, pcOther = 0;
    // Fine-grained ACI bucket: count slices spent at each $C1xx instruction
    // so we can pinpoint the exact loop the CPU is stuck in.
    int64_t pcAciByte[256] = {0};
    // Track once when READ is first entered ($C18D) and once when the
    // ACI restarts back to $C100 (would prove the parser-to-READ
    // hand-off is misfiring).
    bool readEntered = false;
    int64_t restartCount = 0;
    uint16_t prevPc = 0;

    const int loadSize = (loadTo - loadFrom) + 1;

    while (cyclesConsumed < kCycleBudget) {
        const int actual = cpu.run(kCycleSlice);
        cyclesConsumed += actual;

        const uint16_t pc = cpu.getProgramCounter();
        if (pc >= 0xC100 && pc <= 0xC1FF) {
            pcAci += actual;
            pcAciByte[pc & 0xFF] += actual;
        } else if (pc >= 0xFF00) {
            pcWozmon += actual;
        } else {
            pcOther += actual;
        }
        // First time we hit READ (anywhere in $C18D..$C1EF), log it.
        if (!readEntered && pc >= 0xC18D && pc <= 0xC1EF) {
            readEntered = true;
            std::printf("  [cycle %lld] READ entered at PC=$%04X (A=$%02X "
                        "X=$%02X Y=$%02X SP=$%02X $24=$%02X $25=$%02X "
                        "$26=$%02X $27=$%02X $28=$%02X)\n",
                        static_cast<long long>(cyclesConsumed), pc,
                        cpu.getAccumulator(), cpu.getXRegister(),
                        cpu.getYRegister(), cpu.getStackPointer(),
                        mem[0x24], mem[0x25], mem[0x26], mem[0x27], mem[0x28]);
        }
        // Detect restart: PC was deep in ACI, now back at $C100.
        if (prevPc >= 0xC110 && prevPc <= 0xC1FF && pc <= 0xC110) {
            ++restartCount;
            if (restartCount <= 3) {
                std::printf("  [cycle %lld] ACI RESTART: PC %04X -> %04X "
                            "(A=$%02X X=$%02X Y=$%02X SP=$%02X $24/25=%02X%02X "
                            "$26/27=%02X%02X $28=$%02X)\n",
                            static_cast<long long>(cyclesConsumed),
                            prevPc, pc,
                            cpu.getAccumulator(), cpu.getXRegister(),
                            cpu.getYRegister(), cpu.getStackPointer(),
                            mem[0x25], mem[0x24], mem[0x27], mem[0x26], mem[0x28]);
            }
        }
        prevPc = pc;

        const int nz = countNonZero(mem + loadFrom, loadSize);
        if (nz != lastNonZero) {
            std::printf("  [cycle %lld] %d bytes landed in $%04X-$%04X (PC=$%04X)\n",
                        static_cast<long long>(cyclesConsumed), nz,
                        loadFrom, loadTo, pc);
            lastNonZero = nz;
            cyclesSinceProgress = 0;
        } else {
            cyclesSinceProgress += actual;
        }

        // Early stop if signature matches AND we've seen reasonable
        // progress — no need to wait for the full tape tail.
        if (sigLen > 0 && nz >= sigLen) {
            bool match = true;
            for (int i = 0; i < sigLen; ++i) {
                if (mem[loadFrom + i] != sigBytes[i]) { match = false; break; }
            }
            if (match) {
                std::printf("  [cycle %lld] signature present, early stop\n",
                            static_cast<long long>(cyclesConsumed));
                break;
            }
        }
    }

    std::printf("=== Display capture ===\n%s\n=== End capture ===\n",
                display.captured.c_str());
    std::printf("Keyboard still ready: %s, last queued char would be '%c' (raw 0x%02X)\n",
                memory.isKeyReady() ? "yes" : "no",
                memory.isKeyReady() ? memory.getLastKey() : '?',
                memory.isKeyReady() ? static_cast<unsigned>(memory.getLastKey()) : 0u);

    std::printf("PC distribution: Wozmon $FFxx=%lld  ACI $C1xx=%lld  other=%lld\n",
                static_cast<long long>(pcWozmon),
                static_cast<long long>(pcAci),
                static_cast<long long>(pcOther));

    // Top 8 hot spots inside the ACI ROM — each entry is "PC: cycles spent"
    // sampled at slice boundaries. Helps identify the exact loop the CPU
    // is stuck in (kbd wait at $C10D, or READBIT at $C1B6, etc.).
    std::puts("ACI ROM hot spots (PC: cycles, sampled at slice boundaries):");
    for (int top = 0; top < 8; ++top) {
        int   bestI = -1;
        int64_t bestV = 0;
        for (int i = 0; i < 256; ++i) {
            if (pcAciByte[i] > bestV) { bestV = pcAciByte[i]; bestI = i; }
        }
        if (bestI < 0 || bestV == 0) break;
        std::printf("  $C1%02X: %lld cycles\n", bestI, static_cast<long long>(bestV));
        pcAciByte[bestI] = 0;
    }

    std::printf("\nFinal state after %lld cycles:\n",
                static_cast<long long>(cyclesConsumed));
    std::printf("  tape playback active: %s\n",
                tape.isPlaybackActive() ? "yes" : "no (tape ended)");
    std::printf("  non-zero bytes in $%04X-$%04X: %d / %d\n",
                loadFrom, loadTo, lastNonZero, loadSize);
    std::printf("  first 32 bytes at $%04X: ", loadFrom);
    dumpHex(mem + loadFrom, std::min(32, loadSize));

    // Explicit exits instead of assert(): CMAKE_BUILD_TYPE=Release sets
    // NDEBUG and silently compiles asserts to nothing, which would make
    // this test pass vacuously with 0 bytes loaded.
    if (lastNonZero == 0) {
        std::fprintf(stderr,
            "FAIL: ACI never wrote a single byte to $%04X-$%04X. "
            "Either no pulses reach $C081, the ACI ROM never runs, "
            "or writes are blocked somewhere in the dispatch path.\n",
            loadFrom, loadTo);
        return 1;
    }
    if (sigLen > 0) {
        bool match = true;
        for (int i = 0; i < sigLen; ++i) {
            if (mem[loadFrom + i] != sigBytes[i]) { match = false; break; }
        }
        if (!match) {
            std::fprintf(stderr,
                "FAIL: bytes written but signature %s is not at $%04X. "
                "Got %02X %02X %02X. Bit-framing or threshold is off.\n",
                sig.c_str(), loadFrom,
                mem[loadFrom], mem[loadFrom + 1], mem[loadFrom + 2]);
            return 1;
        }
    }

    // ---- B3 regression: playTape() on an exhausted tape must rewind ----
    // Before the fix, once playbackIndex reached loadedDurations.size(),
    // a fresh playTape() would arm the tape but the first $C081 read
    // would find the index out-of-range and immediately fall off the end
    // again — silently stuck at EOF until a manual rewindTape().
    //
    // Under B6 (play-on-first-read), advanceCycles alone doesn't consume
    // anything — the tape must be polled via readTapeInput() to
    // activate. We also have to poll more often than the 500 ms
    // leader-rewind guard fires, otherwise the very next readTapeInput
    // after a long quiet slice snaps playbackIndex back to 0 and we
    // never reach EOF. 250k cycles ≈ 244 ms at 1.022 MHz — safely under
    // the guard, and a 100 M cycle budget easily spans the remaining
    // tape (~76 k transitions ≈ 76 M cycles).
    auto driveToEof = [&]() {
        for (int i = 0; i < 400; ++i) {
            (void)tape.readTapeInput();
            tape.advanceCycles(250'000);
            if (!tape.isPlaybackActive() && !tape.isRewinding()) {
                return true;
            }
        }
        return false;
    };
    if (!driveToEof()) {
        std::fprintf(stderr,
            "FAIL (B3 precondition): couldn't exhaust the tape — "
            "advanceCycles is not consuming durations as expected.\n");
        return 1;
    }
    tape.playTape();
    // B6: PLAY arms; first poll activates.
    if (tape.isPlaybackActive()) {
        std::fprintf(stderr,
            "FAIL (B6): playTape() activated immediately — should arm only.\n");
        return 1;
    }
    (void)tape.readTapeInput();  // arm→active transition
    if (!tape.isPlaybackActive()) {
        std::fprintf(stderr,
            "FAIL (B3): readTapeInput() after playTape on exhausted tape "
            "did not re-activate playback. The stopped-deck rewind is missing.\n");
        return 1;
    }
    // Consume a small slice; without the B3 rewind, the out-of-range
    // index would flip playbackActive=false on the next advancePlayback.
    tape.advanceCycles(100'000);
    if (!tape.isPlaybackActive()) {
        std::fprintf(stderr,
            "FAIL (B3): playTape() on exhausted tape activated briefly "
            "then died — the out-of-range playbackIndex was not rewound.\n");
        return 1;
    }
    std::puts("B3 OK: playTape() on exhausted tape rewound and resumed.");

    // ---- B7 regression: rewindTape() on mid-tape enters progressive REW ----
    // After PLAY re-armed the tape, consume a chunk so playbackIndex > 0,
    // then call rewindTape(). Expected: isRewinding() flips true AND
    // playback halts, but the index is NOT instantly 0 — advancing a
    // small slice must keep isRewinding() true until enough emulated
    // time has passed. Eventually REW completes, isRewinding() goes
    // false, tape is armed at the leader again.
    tape.advanceCycles(5'000'000);   // consume pulses (active=true from B3 above)
    if (!tape.isPlaybackActive()) {
        std::fprintf(stderr,
            "FAIL (B7 precondition): tape exhausted again too fast — "
            "can't test mid-tape REW.\n");
        return 1;
    }
    tape.rewindTape();
    if (!tape.isRewinding()) {
        std::fprintf(stderr,
            "FAIL (B7): rewindTape() on mid-tape didn't enter rewinding "
            "state — progressive REW is missing.\n");
        return 1;
    }
    if (tape.isPlaybackActive()) {
        std::fprintf(stderr,
            "FAIL (B7): REW engaged but playbackActive is still true — "
            "playback should halt while winding back.\n");
        return 1;
    }
    // Let REW complete. A 30 s tape winds back at ~20× play speed →
    // ~1.5 s of emulated time ≈ 1.5 M cycles. Budget 50 M cycles to be
    // safe even if the tape is longer or the slice granularity adds
    // overhead.
    for (int i = 0; i < 5 && tape.isRewinding(); ++i) {
        tape.advanceCycles(50'000'000);
    }
    if (tape.isRewinding()) {
        std::fprintf(stderr,
            "FAIL (B7): REW did not complete after 250 M cycles — "
            "advanceRewind is stuck.\n");
        return 1;
    }
    std::puts("B7 OK: rewindTape() walked the tape back progressively "
              "and completed.");

    std::puts("ACI tape loading smoke test: OK");
    return 0;
}
