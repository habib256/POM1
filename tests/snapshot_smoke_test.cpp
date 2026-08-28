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
// Card-payload coverage as cards adopt serialize/deserialize:
//   - TMS9918: VRAM byte + register-7 (border colour) round-trip.
//   - A1-SID:  shadow-register file round-trip.
//   - ACI:     recorded-transition count round-trip (output-level FF flip-
//              flop is reflected in the recording buffer growth).
//   - GT-6144: latched FSM state via copySnapshot.
//   - PR-40:   switch mode via copySnapshot.
//   - Juke-Box: bank-register latch ($CA00) via copySnapshot.
//   - CodeTank: jumper position via copySnapshot.
//   - CFFA1:   ATA LBA0 register read-back through MMIO.
// What this test does NOT cover yet:
//   - in-flight cassette playback position (PR2 deliberately quiesces it
//     on load — see CassetteDevice.h header comment).
//   - libresidfp internal filter integrators / oscillator phase (engine
//     does not expose them — load re-pokes shadow regs and accepts a
//     short filter-settle transient).
//   - microSD / A1-IO/RTC fine-grained state (round-tripped but not
//     individually asserted here — covered by the lower-level card tests
//     once they get their own snapshot fixtures).
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
#include "GT6144.h"
#include "JukeBox.h"
#include "CodeTank.h"
#include "CassetteDevice.h"
#include "CFFA1.h"
#include "MicroSD.h"
#include "SID.h"
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
#include <string_view>
#include <vector>

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

    // ── Snapshot section-name invariant. writeFixedName truncates every name
    //    to kSectionNameLen (8) bytes and readSnapshotSections dispatches on
    //    that truncated form, so each serialized card's name must be unique
    //    within its first 8 bytes — and must not collide with a framework
    //    section. The A1-IO/RTC bug (9-char name, dropped on load) lived here;
    //    pin it so a future card with a colliding 8-char prefix fails loudly.
    //
    //    The card list is taken from Memory::cardSlots() — the single registry
    //    — rather than restated here. This test used to carry its own copy,
    //    which made it a FIFTH hand-synced list: a new card that its author
    //    forgot to add was exactly the case the test was supposed to catch,
    //    and it would have passed. The registry also proves 8-byte uniqueness
    //    at compile time now; this remains as the runtime cross-check that the
    //    live Peripheral::name()s match what the table claims.
    {
        std::vector<std::string_view> names;
        for (const Memory::CardSlot& s : Memory::cardSlots()) {
            if (!s.card) continue;              // flag-only row, no section
            const pom1::Peripheral* p = s.card(mem);
            assert(p && "registry row names a card the Memory does not own");
            assert(p->name() == std::string_view(s.name) &&
                   "CardSlot::name disagrees with the live Peripheral::name()");
            names.push_back(p->name());
        }
        assert(names.size() == 13 &&
               "serialized-card count changed — update the section-order pin below");
        const size_t n = names.size();
        for (size_t i = 0; i < n; ++i) {
            const std::string_view a = names[i].substr(0, pom1::kSectionNameLen);
            assert(a != "CPU" && a != "MEM" && a != "FLAGS" &&
                   a != "SCREEN" && a != "GEN2VID" &&
                   "card name collides with a reserved framework section");
            for (size_t j = i + 1; j < n; ++j) {
                if (a == names[j].substr(0, pom1::kSectionNameLen)) {
                    std::fprintf(stderr,
                        "snapshot section-name collision: '%.*s' and '%.*s' share "
                        "the same first %zu bytes — one is lost on load\n",
                        int(names[i].size()), names[i].data(),
                        int(names[j].size()), names[j].data(),
                        pom1::kSectionNameLen);
                    return 1;
                }
            }
        }
    }

    // ── Snapshot section ORDER invariant.
    //    Memory::cardSlots() row order is load-bearing: it fixes the on-disk
    //    section sequence, and it encodes real restore-ordering constraints
    //    (IEC cascades onto microSD so it must follow it; GEN2 attaches last).
    //    Reordering rows silently changes the format and can break restore,
    //    which no round-trip assertion would catch — a save and a load written
    //    by the same table always agree with each other. So pin the literal
    //    sequence against the names as they appear on disk, i.e. truncated to
    //    kSectionNameLen. This list is the format; changing it means bumping
    //    the snapshot version deliberately.
    {
        const char* kExpectedOrder[] = {
            "CPU", "MEM", "FLAGS",
            "ACI", "TMS9918", "A1-SID", "microSD", "CFFA1", "Juke-Box",
            "CodeTank", "Wi-Fi Mo", "Terminal", "A1-IO/RT", "PR-40",
            "GT-6144", "IECCard",
            "GEN2VID",
            // no "SCREEN": this fixture attaches no display device.
        };
        M6502 orderCpu(&mem);
        const std::vector<uint8_t> blob = mem.saveSnapshotToBuffer(&orderCpu);
        assert(!blob.empty() && "snapshot buffer came back empty");

        pom1::SnapshotReader rd(blob);
        assert(rd.good());
        std::vector<std::string> seen;
        std::string  sectionName;
        uint32_t     sectionLen = 0;
        while (rd.nextSection(sectionName, sectionLen)) {
            seen.push_back(sectionName);
            rd.skipCurrentSection();
        }
        const size_t expectedCount = sizeof(kExpectedOrder) / sizeof(kExpectedOrder[0]);
        if (seen.size() != expectedCount) {
            std::fprintf(stderr, "snapshot section count %zu, expected %zu:\n",
                         seen.size(), expectedCount);
            for (const std::string& s : seen) std::fprintf(stderr, "  '%s'\n", s.c_str());
            return 1;
        }
        for (size_t i = 0; i < expectedCount; ++i) {
            if (seen[i] != kExpectedOrder[i]) {
                std::fprintf(stderr,
                    "snapshot section order changed at index %zu: got '%s', "
                    "expected '%s' — this is an on-disk format change\n",
                    i, seen[i].c_str(), kExpectedOrder[i]);
                return 1;
            }
        }
    }

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

    // ── Enable a representative mix of cards that exercise per-card
    //    serialize hooks. PR-40 + GT-6144 stay enabled to pin the FLAGS
    //    section; TMS9918 + SID exercise their own card sections; the
    //    cassette deck always exists (no enable flag) so we just mutate it.
    mem.setPR40Enabled(true);
    mem.setGT6144Enabled(true);
    mem.setTMS9918Enabled(true);
    mem.setSIDEnabled(true);
    // IEC round-trip is exercised in tests/iec_snapshot_smoke_test.cpp —
    // enabling it here would cascade-disable the CFFA1 sentinel below.

    // ── TMS9918: write a sentinel into VRAM[$0100] and set R7 (border
    //    colour) to a non-default value via the standard $CC01 latch
    //    protocol (low addr, high addr | $40 = "write data").
    auto& vdp = mem.getTMS9918();
    vdp.writeControl(0x00); vdp.writeControl(0x41);  // VRAM addr = $0100, write
    vdp.writeData(0xA5);
    vdp.writeControl(0x0E); vdp.writeControl(0x87);  // R7 = $0E (white-on-black border)

    // ── SID: write distinct sentinels to a few registers (voice 1 freq
    //    low/high, control). Going through Memory's MMIO at $C800-$CFFF
    //    so the bus + chipMutex paths are exercised end-to-end.
    mem.memWrite(0xC800, 0x42);  // voice 1 freq lo
    mem.memWrite(0xC801, 0x33);  // voice 1 freq hi
    mem.memWrite(0xC804, 0x21);  // voice 1 control (gate=1, triangle=1)

    // ── Cassette: toggle the $C000 output flip-flop a few times. Each
    //    toggle records a transition; `getRecordedTransitionCount()`
    //    reflects the buffer growth round-trip below.
    // armRecording() captures the initial level + baseline cycle WITHOUT pushing
    // a sample; each subsequent toggle records one delta from the baseline. Here
    // the baseline is cycle 0 (fresh deck), and four toggles at 100/200/300/400
    // therefore yield four recorded transitions ([0,100] included). (Before the
    // cycle-0 recording fix the [0,100] interval was dropped — the arm at cycle 0
    // was silently re-taken by the first toggle — so this used to read 3.)
    auto& cassette = mem.getCassetteDevice();
    cassette.armRecording();
    for (int i = 0; i < 4; ++i) {
        cassette.advanceCycles(100);   // currentCycle must advance between toggles
        cassette.toggleOutput();       // for the transition to land in recordedDurations
    }
    const size_t expectedRecordedTransitions = cassette.getRecordedTransitionCount();
    assert(expectedRecordedTransitions == 4);

    // ── GT-6144: latch a non-default FSM mode (control opcode = blanked).
    //    Phase-3 (224..255 byte&7=5) sets blanked=true.
    mem.getGT6144().writeCommand(static_cast<uint8_t>(224 | 5));

    // ── PR-40: switch mode flip
    mem.setPR40Enabled(true);
    mem.getPR40().setMode(PR40Printer::SwitchMode::PrintOnly);

    // ── CodeTank: jumper toggle via setter (defaults to Lower16).
    //    JukeBox is mutex with CodeTank + SID (it claims $4000-$BFFF), so
    //    enabling it here would evict SID. Juke-Box's per-card serialize
    //    is exercised separately below on a fresh Memory.
    mem.setCodeTankEnabled(true);
    mem.getCodeTank().setJumper(CodeTank::Jumper::Upper16);

    // ── CFFA1: poke a sentinel into LBA0 register ($AFFB / $AFEB mirror).
    mem.setCFFA1Enabled(true);
    mem.memWrite(0xAFFB, 0x77);
    const uint8_t expectedLBA0 = mem.memRead(0xAFFB);
    assert(expectedLBA0 == 0x77);

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
    assert(mem2.isTMS9918Enabled());
    assert(mem2.isSIDEnabled());

    // ── TMS9918 internal state: VRAM byte + R7 border colour.
    {
        TMS9918::Snapshot vdpSnap;
        mem2.getTMS9918().copySnapshot(vdpSnap);
        if (vdpSnap.vram[0x0100] != 0xA5) {
            std::fprintf(stderr, "TMS9918 VRAM[$0100] mismatch: got %02X expected A5\n",
                         vdpSnap.vram[0x0100]);
            return 1;
        }
        if (vdpSnap.regs[7] != 0x0E) {
            std::fprintf(stderr, "TMS9918 R7 mismatch: got %02X expected 0E\n",
                         vdpSnap.regs[7]);
            return 1;
        }
    }

    // ── SID shadow registers
    {
        pom1::SID::Snapshot sidSnap;
        mem2.getSID().copySnapshot(sidSnap);
        if (sidSnap.regs[0x00] != 0x42 ||
            sidSnap.regs[0x01] != 0x33 ||
            sidSnap.regs[0x04] != 0x21) {
            std::fprintf(stderr,
                "SID shadow regs mismatch: got %02X %02X %02X expected 42 33 21\n",
                sidSnap.regs[0x00], sidSnap.regs[0x01], sidSnap.regs[0x04]);
            return 1;
        }
    }

    // ── Cassette recorded-transition buffer
    {
        const size_t got = mem2.getCassetteDevice().getRecordedTransitionCount();
        if (got != expectedRecordedTransitions) {
            std::fprintf(stderr,
                "ACI recorded transitions mismatch: got %zu expected %zu\n",
                got, expectedRecordedTransitions);
            return 1;
        }
    }

    // ── GT-6144 FSM latch
    {
        GT6144::Snapshot s;
        mem2.getGT6144().copySnapshot(s);
        if (!s.blanked) {
            std::fprintf(stderr, "GT-6144 'blanked' flag did not survive snapshot\n");
            return 1;
        }
    }

    // ── PR-40 switch mode
    {
        PR40Printer::Snapshot s;
        mem2.getPR40().copySnapshot(s);
        if (s.mode != PR40Printer::SwitchMode::PrintOnly) {
            std::fprintf(stderr, "PR-40 switch mode did not survive snapshot\n");
            return 1;
        }
    }

    // ── CodeTank jumper
    {
        assert(mem2.isCodeTankEnabled());
        CodeTank::Snapshot s;
        mem2.getCodeTank().copySnapshot(s);
        if (s.jumper != CodeTank::Jumper::Upper16) {
            std::fprintf(stderr, "CodeTank jumper did not survive snapshot\n");
            return 1;
        }
    }

    // ── CFFA1 LBA0 register
    {
        assert(mem2.isCFFA1Enabled());
        const uint8_t got = mem2.memRead(0xAFFB);
        if (got != 0x77) {
            std::fprintf(stderr,
                "CFFA1 LBA0 mismatch: got %02X expected 77\n", got);
            return 1;
        }
    }

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

    // ── Standalone Juke-Box round-trip. JukeBox is mutex with SID/CodeTank/
    //    CFFA1/microSD/WiFiModem (it claims $4000-$BFFF), so a fresh Memory
    //    with only JukeBox plugged keeps the test focused.
    {
        Memory memJB;
        memJB.setJukeBoxEnabled(true);
        memJB.memWrite(0xCA00, 0x05);   // page 5 latch
        auto pathJB = makeTempPath("jukebox");
        std::string errJB;
        if (!memJB.saveSnapshot(pathJB.string(), errJB)) {
            std::fprintf(stderr, "JB saveSnapshot failed: %s\n", errJB.c_str());
            return 1;
        }
        Memory memJB2;
        memJB2.setJukeBoxEnabled(true);
        if (!memJB2.loadSnapshot(pathJB.string(), errJB)) {
            std::fprintf(stderr, "JB loadSnapshot failed: %s\n", errJB.c_str());
            return 1;
        }
        JukeBox::Snapshot s;
        memJB2.getJukeBox().copySnapshot(s);
        if (s.bankRegister != 0x05) {
            std::fprintf(stderr,
                "Juke-Box bankRegister round-trip failed: got %02X expected 05\n",
                s.bankRegister);
            return 1;
        }
        std::error_code ecJB;
        std::filesystem::remove(pathJB, ecJB);
    }

    // ── A1-IO/RTC round-trip. Regression for the section-name dispatch bug:
    //    the card name "A1-IO/RTC" is 9 chars and is truncated to "A1-IO/RT"
    //    on write, so a full-name compare on load never matched and the whole
    //    section (VIA regs, analog/digital inputs, RTC offset) was silently
    //    dropped. We mutate an analog input, save, and confirm it survives.
    {
        Memory memA;
        memA.setA1IO_RTCEnabled(true);
        memA.getA1IO_RTC().setAnalogInput(2, 0xAB);
        memA.getA1IO_RTC().setDigitalInput(1, 0x00);  // override the pull-up default (1)
        auto pathA = makeTempPath("a1io");
        std::string errA;
        if (!memA.saveSnapshot(pathA.string(), errA)) {
            std::fprintf(stderr, "A1-IO saveSnapshot failed: %s\n", errA.c_str());
            return 1;
        }
        Memory memA2;
        if (!memA2.loadSnapshot(pathA.string(), errA)) {
            std::fprintf(stderr, "A1-IO loadSnapshot failed: %s\n", errA.c_str());
            return 1;
        }
        assert(memA2.isA1IO_RTCEnabled() && "A1-IO/RTC enable flag must round-trip");
        A1IO_RTC::Snapshot sA;
        memA2.getA1IO_RTC().copySnapshot(sA);
        if (sA.analogInputs[2] != 0xAB || sA.digitalInputs[1] != 0x00) {
            std::fprintf(stderr,
                "A1-IO/RTC state did not survive snapshot: analog[2]=%02X (want AB) "
                "digital[1]=%02X (want 00) — section likely skipped on load\n",
                sA.analogInputs[2], sA.digitalInputs[1]);
            return 1;
        }
        std::error_code ecA;
        std::filesystem::remove(pathA, ecA);
    }

    // ── GEN2 HGR round-trip. Regression for the missing card-attach bit: the
    //    framebuffer ($2000-$3FFF) lived in the MEM section but the attach
    //    state itself was never persisted, so a reloaded GEN2 session came
    //    back detached. Confirm attach + framebuffer byte + the 50 Hz jumper
    //    (GEN2VID section) all survive, and that loading does NOT re-seed the
    //    framebuffer with cold-plug noise.
    {
        Memory memG;
        memG.setHgrFramebufferAttached(true);   // seeds $2000-$3FFF with noise...
        memG.memWrite(0x2000, 0x5A);            // ...then we stamp a sentinel
        memG.memWrite(0x3FFF, 0xC3);
        memG.setGen2FiftyHz(true);
        auto pathG = makeTempPath("gen2");
        std::string errG;
        if (!memG.saveSnapshot(pathG.string(), errG)) {
            std::fprintf(stderr, "GEN2 saveSnapshot failed: %s\n", errG.c_str());
            return 1;
        }
        Memory memG2;   // fresh: GEN2 detached, 60 Hz
        assert(!memG2.isHgrFramebufferAttached());
        if (!memG2.loadSnapshot(pathG.string(), errG)) {
            std::fprintf(stderr, "GEN2 loadSnapshot failed: %s\n", errG.c_str());
            return 1;
        }
        if (!memG2.isHgrFramebufferAttached()) {
            std::fprintf(stderr, "GEN2 attach flag did not survive snapshot\n");
            return 1;
        }
        if (memG2.memRead(0x2000) != 0x5A || memG2.memRead(0x3FFF) != 0xC3) {
            std::fprintf(stderr,
                "GEN2 framebuffer not restored (got %02X/%02X want 5A/C3) — "
                "load may have re-seeded it with noise\n",
                memG2.memRead(0x2000), memG2.memRead(0x3FFF));
            return 1;
        }
        if (!memG2.isGen2FiftyHz()) {
            std::fprintf(stderr, "GEN2 50 Hz jumper (GEN2VID section) did not survive\n");
            return 1;
        }
        std::error_code ecG;
        std::filesystem::remove(pathG, ecG);
    }

    // ── GEN2 video journal round-trip (v5 GEN2VID extension). Soft-switch flips
    //    journaled during a frame must survive save/restore so a beam-split
    //    scene (mid-line PAGE2/TEXT flips — DROL-class double-buffering,
    //    horizontal splits) replays correctly on load instead of collapsing to
    //    the plain end-of-frame latch. Regression for the load path dropping the
    //    published journal via resetGen2VideoEventJournal().
    {
        Memory memJ;
        memJ.setHgrFramebufferAttached(true);
        memJ.memRead(0xC255);        // PAGE2 on  -> journals a Page2=1 event
        memJ.memRead(0xC251);        // TEXT  on  -> journals a TextMode=1 event
        memJ.advanceCycles(20000);   // cross a video-frame boundary -> publish
        const auto& pub = memJ.gen2PublishedVideoEvents();
        if (pub.size() != 2) {
            std::fprintf(stderr,
                "GEN2 journal: expected 2 published events, got %zu\n", pub.size());
            return 1;
        }
        auto pathJ = makeTempPath("gen2_journal");
        std::string errJ;
        if (!memJ.saveSnapshot(pathJ.string(), errJ)) {
            std::fprintf(stderr, "GEN2 journal saveSnapshot failed: %s\n", errJ.c_str());
            return 1;
        }
        Memory memJ2;
        if (!memJ2.loadSnapshot(pathJ.string(), errJ)) {
            std::fprintf(stderr, "GEN2 journal loadSnapshot failed: %s\n", errJ.c_str());
            return 1;
        }
        const auto& pub2 = memJ2.gen2PublishedVideoEvents();
        if (pub2.size() != pub.size()) {
            std::fprintf(stderr,
                "GEN2 journal did not survive snapshot (got %zu events want %zu) — "
                "load dropped the published journal\n", pub2.size(), pub.size());
            return 1;
        }
        for (size_t i = 0; i < pub.size(); ++i) {
            if (pub2[i].emuCycle != pub[i].emuCycle ||
                pub2[i].kind     != pub[i].kind ||
                pub2[i].value    != pub[i].value) {
                std::fprintf(stderr,
                    "GEN2 journal event %zu mismatch after restore\n", i);
                return 1;
            }
        }
        if (!(memJ2.gen2PublishedFrameStartState() == memJ.gen2PublishedFrameStartState())) {
            std::fprintf(stderr, "GEN2 journal frame-start state did not survive snapshot\n");
            return 1;
        }
        std::error_code ecJ;
        std::filesystem::remove(pathJ, ecJ);
    }

    // ── microSD snapshot across a CodeTank preset (regression for the
    //    MEM-then-FLAGS restore ordering bug: disable paths used to zero
    //    $4000-$7FFF / $8000-$9FFF after MEM had already restored RAM).
    {
        Memory memSD;
        memSD.setMicroSDEnabled(true);
        for (int addr = 0x6000; addr < 0x6800; ++addr) {
            memSD.memWrite(static_cast<uint16_t>(addr),
                           static_cast<uint8_t>(0xA0 + (addr & 0x0F)));
        }
        auto pathSD = makeTempPath("microsd_codetank");
        std::string errSD;
        if (!memSD.saveSnapshot(pathSD.string(), errSD)) {
            std::fprintf(stderr, "microSD saveSnapshot failed: %s\n", errSD.c_str());
            return 1;
        }
        Memory memSD2;
        memSD2.setCodeTankEnabled(true);   // different preset — TMS9918 cascades on
        assert(memSD2.isCodeTankEnabled());
        if (!memSD2.loadSnapshot(pathSD.string(), errSD)) {
            std::fprintf(stderr, "microSD loadSnapshot failed: %s\n", errSD.c_str());
            return 1;
        }
        assert(memSD2.isMicroSDEnabled());
        assert(!memSD2.isCodeTankEnabled());
        for (int addr = 0x6000; addr < 0x6800; ++addr) {
            const uint8_t expected = static_cast<uint8_t>(0xA0 + (addr & 0x0F));
            const uint8_t got = memSD2.memRead(static_cast<uint16_t>(addr));
            if (got != expected) {
                std::fprintf(stderr,
                    "microSD/CodeTank preset-cross restore: RAM[$%04X] got %02X expected %02X\n",
                    addr, got, expected);
                return 1;
            }
        }
        std::error_code ecSD;
        std::filesystem::remove(pathSD, ecSD);
    }

    // ── CFFA1: a multi-sector transfer must survive a snapshot taken while it
    //    is still in flight. The card's sector counter lives only in
    //    sectorsRemaining_ — it is not derivable from the ATA registers, since
    //    the count register is a copy that the transfer itself rewrites — and
    //    it was missing from the CFFA1 section until snapshot v6. Restoring
    //    left it at whatever the destination card held (0 on a fresh one), so
    //    the next 512-byte boundary decremented it below zero, cleared DRQ and
    //    truncated the read to the sector already in the buffer: ProDOS got a
    //    short read and $FF for every byte after it.
    //
    //    The image is built here rather than borrowed from cfcard/cfcard.po:
    //    every sector must carry a DISTINCT byte, or the truncation is
    //    invisible — a run of $FF in the real image reads the same whether the
    //    transfer continued or aborted (an abort also answers $FF), and the pin
    //    would pass against the very bug it exists for.
    {
        const auto imagePath = std::filesystem::temp_directory_path()
                             / "pom1_snap_cffa1_multisector.po";
        {
            std::ofstream img(imagePath, std::ios::binary | std::ios::trunc);
            for (int s = 0; s < 8; ++s) {
                const std::vector<char> sector(512, static_cast<char>(0x10 + s));
                img.write(sector.data(), static_cast<std::streamsize>(sector.size()));
            }
            if (!img) {
                std::fprintf(stderr, "CFFA1 multi-sector pin: cannot write %s\n",
                             imagePath.string().c_str());
                return 1;
            }
        }
        const std::string kImage = imagePath.string();
        CFFA1 straight, restored;
        if (!straight.openDiskImage(kImage) || !restored.openDiskImage(kImage)) {
            std::fprintf(stderr, "CFFA1 multi-sector pin: cannot open %s\n",
                         kImage.c_str());
            return 1;
        }
        auto startRead4 = [](CFFA1& cf) {
            cf.writeByte(0xAFEA, 4);       // sector count = 4
            cf.writeByte(0xAFEB, 0x00);    // LBA0 — sectors 0..3
            cf.writeByte(0xAFEC, 0x00);
            cf.writeByte(0xAFED, 0x00);
            cf.writeByte(0xAFEE, 0xE0);    // LBA3 / device
            cf.writeByte(0xAFEF, 0x20);    // READ SECTOR(S)
        };
        startRead4(straight);
        startRead4(restored);

        std::vector<uint8_t> want, got;
        want.reserve(4 * 512);
        got.reserve(4 * 512);
        for (int i = 0; i < 4 * 512; ++i) want.push_back(straight.readByte(0xAFE8));

        // Interrupt the second card one byte into sector 1 and round-trip it.
        for (int i = 0; i < 512 + 1; ++i) got.push_back(restored.readByte(0xAFE8));
        pom1::SnapshotWriter cfw;
        restored.serialize(cfw);
        const std::vector<uint8_t> cfBlob = cfw.takeBuffer();
        CFFA1 resumed;
        if (!resumed.openDiskImage(kImage)) {
            std::fprintf(stderr, "CFFA1 multi-sector pin: cannot reopen %s\n",
                         kImage.c_str());
            return 1;
        }
        pom1::SnapshotReader cfr(cfBlob);
        resumed.deserialize(cfr);
        while (got.size() < want.size()) got.push_back(resumed.readByte(0xAFE8));

        if (got != want) {
            size_t at = 0;
            while (at < want.size() && got[at] == want[at]) ++at;
            std::fprintf(stderr,
                "CFFA1 multi-sector transfer did not survive the snapshot: "
                "byte %zu (sector %zu) got %02X expected %02X\n",
                at, at / 512, got[at], want[at]);
            return 1;
        }
        std::error_code ecCf;
        std::filesystem::remove(imagePath, ecCf);
    }

    // ── A card section that goes bad AFTER the card has started writing ──
    //
    // The structural pre-flight (pom1::validateSnapshot) vouches for the file's
    // shape, but not for a CARD's payload — that grammar lives in the card's
    // own deserialize, and several of them restore fields before they can
    // reject what follows. PR40Printer has already written its mode, FIFO and
    // four counters by the time it validates the paper-roll line count.
    //
    // Measured before the rollback existed: forging that count left the machine
    // holding the SNAPSHOT's CPU and RAM (PC=$1234, $0300=$AA) while the caller
    // was told the load had failed — and a half-restored printer on top.
    {
        Memory m;
        M6502 c(&m);
        m.initMemory();
        m.setPR40Enabled(true);

        // State A, captured.
        m.memWrite(0x0300, 0xAA);
        c.setProgramCounter(0x1234);
        std::vector<uint8_t> snap = m.saveSnapshotToBuffer(&c);

        // Forge the PR-40 paper-roll line count. Layout after the 8-byte
        // section name + u32 length: mode(1) fifoLevel(1) fifo(40)
        // counters(4x4) currentLine(u32 len + bytes) lineCount(u32).
        size_t at = std::string::npos;
        for (size_t i = 0; i + 8 < snap.size(); ++i)
            if (std::memcmp(&snap[i], "PR-40", 5) == 0) { at = i; break; }
        if (at == std::string::npos) {
            std::fprintf(stderr, "PR-40 section missing from snapshot\n");
            return 1;
        }
        const size_t lineLenAt = at + 8 + 4 + 1 + 1 + 40 + 16;
        uint32_t lineLen = 0;
        std::memcpy(&lineLen, &snap[lineLenAt], 4);
        const uint32_t forged = 0x7FFFFFFF;
        std::memcpy(&snap[lineLenAt + 4 + lineLen], &forged, 4);

        // Move to state B, then attempt the corrupt load.
        m.memWrite(0x0300, 0x55);
        c.setProgramCounter(0xBEEF);

        std::string loadErr;
        if (m.loadSnapshotFromBuffer(snap, loadErr, &c)) {
            std::fprintf(stderr, "a forged PR-40 line count was accepted\n");
            return 1;
        }
        // The whole point: state B, entirely intact.
        if (c.getProgramCounter() != 0xBEEF || m.memRead(0x0300) != 0x55) {
            std::fprintf(stderr,
                "rejected snapshot still mutated the machine: PC=$%04X "
                "$0300=$%02X (expected $BEEF / $55)\n",
                c.getProgramCounter(), m.memRead(0x0300));
            return 1;
        }
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::printf("snapshot round-trip OK (%zu RAM bytes, CPU regs, "
                "TMS9918+SID+cassette+GT-6144+PR-40+CodeTank+CFFA1 "
                "(incl. an in-flight multi-sector transfer) + "
                "standalone Juke-Box + A1-IO/RTC + GEN2 HGR + "
                "microSD/CodeTank preset-cross; rejected loads roll back)\n",
                baseline.size());
    return 0;
}
