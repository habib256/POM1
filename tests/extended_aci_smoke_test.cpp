// Uncle Bernie's Extended ACI ($C500-$C5FF) smoke test.
//
// Two halves, both load-bearing:
//
//   PART A — the card. $C500-$C5FF maps only when the extended page is
//   plugged, it is write-protected while plugged and plain RAM while not,
//   and the daughterboard cascade holds in both directions (plugging the
//   extended page pulls the ACI in; unplugging the ACI pushes it out).
//   Guards against the page being mapped over a Fantasy machine's RAM, or
//   surviving as a zombie ROM after the cassette card is pulled.
//
//   PART B — the firmware, end to end. Plays Uncle Bernie's own
//   `cassettes/codebrk.aiff` (synthesised by his ACIace tool, EXTENDED
//   format: 8-byte from/to headers, equal addresses = autostart) through
//   the real pulse-extraction path, types exactly what the operator types
//   on real hardware —
//
//       C500R <return>
//       RX RX <return>
//
//   — and asserts the Codebreaker game loads AND autostarts, by watching
//   for its banner on $D012. Nothing here is injected: the bytes can only
//   appear by way of AIFF decode -> zero-crossing pulses -> $C081 -> the
//   extended firmware relocated into page 1.
//
// So this one test covers the AIFF reader (miniaudio has no AIFF backend),
// the $C500 mapping, the extended firmware's self-relocation into the
// stack page, and its checksum/header handling.
//
// argv[1] = absolute path to codebrk.aiff (passed from CMake).

#include "Memory.h"
// Memory holds unique_ptrs to these (forward-declared in Memory.h); the
// destructor needs the complete types.
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
#include "PR40Printer.h"

#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// Captures everything the machine prints to $D012, bit 7 stripped, so the
// test can assert on the loaded game's banner.
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
    for (const char* p = cmd; *p; ++p) mem.setKeyPressed(*p);
    mem.setKeyPressed('\r');   // Apple-1 Return -> $8D
}

bool containsIgnoreCase(const std::string& hay, const char* needle)
{
    const size_t n = std::strlen(needle);
    if (n == 0 || hay.size() < n) return false;
    for (size_t i = 0; i + n <= hay.size(); ++i) {
        size_t j = 0;
        while (j < n &&
               std::toupper(static_cast<unsigned char>(hay[i + j])) ==
               std::toupper(static_cast<unsigned char>(needle[j]))) {
            ++j;
        }
        if (j == n) return true;
    }
    return false;
}

int runPartA()
{
    Memory memory;

    // Fresh machine: no cassette card, no extended page, and $C500 is
    // ordinary RAM the CPU may scribble on.
    assert(!memory.isACIEnabled());
    assert(!memory.isExtendedACIEnabled());
    memory.memWrite(0xC500, 0x5A);
    if (memory.memRead(0xC500) != 0x5A) {
        std::fprintf(stderr,
            "FAIL: $C500 is not writable RAM with the extended page unplugged "
            "(read back $%02X) — the write-protect must be flag-gated.\n",
            memory.memRead(0xC500));
        return 1;
    }
    std::printf("A OK: $C500 is plain RAM while the extended page is out\n");

    // Plug the extended page. Daughterboard rule: it cascade-plugs the ACI,
    // because the page physically lives in the cassette card's PROM pair.
    memory.setExtendedACIEnabled(true);
    if (!memory.isExtendedACIEnabled() || !memory.isACIEnabled()) {
        std::fprintf(stderr,
            "FAIL: setExtendedACIEnabled(true) left xaci=%d aci=%d — expected "
            "1/1 (the extended page must cascade-plug the ACI).\n",
            memory.isExtendedACIEnabled() ? 1 : 0,
            memory.isACIEnabled() ? 1 : 0);
        return 1;
    }
    std::printf("A OK: plugging the extended page cascade-plugged the ACI\n");

    // The page must actually be Uncle Bernie's firmware. $C500 opens with
    // LDX #$FF / TXS / INX — it re-points the stack before relocating the
    // Woz ROM into page 1, and that entry sequence is what `C500R` runs.
    const uint8_t entry[4] = { memory.memRead(0xC500), memory.memRead(0xC501),
                               memory.memRead(0xC502), memory.memRead(0xC503) };
    if (entry[0] != 0xA2 || entry[1] != 0xFF || entry[2] != 0x9A || entry[3] != 0xE8) {
        std::fprintf(stderr,
            "FAIL: $C500 = %02X %02X %02X %02X, expected A2 FF 9A E8 "
            "(LDX #$FF / TXS / INX) — wrong or missing roms/XACI.rom.\n",
            entry[0], entry[1], entry[2], entry[3]);
        return 1;
    }
    // $C506 is the high byte of `LDA $C100,X`: the extended page reaches into
    // the STOCK ACI ROM to relocate it. If this ever stops pointing at $C1,
    // the two PROM halves have drifted apart.
    if (memory.memRead(0xC506) != 0xC1) {
        std::fprintf(stderr,
            "FAIL: $C506 = $%02X, expected $C1 — the relocation source must "
            "still be the stock ACI ROM at $C100.\n", memory.memRead(0xC506));
        return 1;
    }
    std::printf("A OK: $C500 carries the extended firmware (relocator intact)\n");

    // Write-protected while plugged — it is a PROM.
    const uint8_t before = memory.memRead(0xC540);
    memory.memWrite(0xC540, static_cast<uint8_t>(~before));
    if (memory.memRead(0xC540) != before) {
        std::fprintf(stderr,
            "FAIL: $C540 accepted a write while the extended page is plugged "
            "($%02X -> $%02X) — PROM must be read-only.\n",
            before, memory.memRead(0xC540));
        return 1;
    }
    std::printf("A OK: the extended page is write-protected while plugged\n");

    // Reverse cascade: pulling the cassette card takes its second PROM page
    // with it, and hands $C500 back to RAM.
    memory.setACIEnabled(false);
    if (memory.isExtendedACIEnabled()) {
        std::fprintf(stderr,
            "FAIL: unplugging the ACI left the extended page plugged — a page "
            "whose code JSRs into $C100 would run against a zeroed ROM.\n");
        return 1;
    }
    memory.memWrite(0xC500, 0xA5);
    if (memory.memRead(0xC500) != 0xA5) {
        std::fprintf(stderr,
            "FAIL: $C500 stayed write-protected after the cascade unplug.\n");
        return 1;
    }
    std::printf("A OK: unplugging the ACI cascade-unplugged the extended page\n");
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <codebrk.aiff>\n", argv[0]);
        return 2;
    }
    const std::string tapePath = argv[1];

    if (int rc = runPartA(); rc != 0) return rc;

    // ---------------- PART B: load the real extended-format tape ----------
    Memory memory;
    M6502  cpu(&memory);

    DisplayCapture display;
    memory.setDisplayDevice(&display);

    memory.setExtendedACIEnabled(true);   // cascade-plugs the ACI
    assert(memory.isACIEnabled());

    CassetteDevice& tape = memory.getCassetteDevice();
    if (!tape.loadTape(tapePath)) {
        std::fprintf(stderr, "FAIL: loadTape(%s): %s\n",
                     tapePath.c_str(), tape.getLastError().c_str());
        return 3;
    }
    // .aiff must take the pulse path unconditionally (it is ACIace DATA, not
    // deck music) — audio-stream mode would carry no readable transitions.
    if (tape.isAudioStreamMode()) {
        std::fprintf(stderr,
            "FAIL: .aiff loaded as an audio stream — extended tapes must use "
            "the pulse path so $C081 sees transitions.\n");
        return 1;
    }
    const size_t transitions = tape.getLoadedTransitionCount();
    std::printf("B: decoded %zu transitions from %s\n",
                transitions, tapePath.c_str());
    if (transitions < 1000) {
        std::fprintf(stderr,
            "FAIL: AIFF decode produced only %zu transitions — the big-endian "
            "PCM reader is broken.\n", transitions);
        return 1;
    }

    // What the DECK tells the operator to type. The playback path below
    // proves the tape loads; this proves the UI sends the user to the
    // commands that actually do it. Both used to be wrong for this tape: the
    // ARMED banner hardcoded "C100R" — the one entry that does NOT read an
    // extended tape — and the cassette label appended a bare "R" to an entry
    // that was already a full command, printing "Type C500R then RX RXR".
    // The keystrokes queued a few lines down are the ground truth these two
    // strings must agree with.
    {
        const std::string& info = tape.getLoadInfo();
        if (info.find("C500R") == std::string::npos) {
            std::fprintf(stderr,
                "FAIL: cassettes/tapeinfo.txt no longer describes %s as an "
                "extended-format tape (loadInfo = \"%s\").\n",
                tapePath.c_str(), info.c_str());
            return 1;
        }
        const std::string label = CassetteDevice::tapeLabelCommand(info);
        const std::string armed = CassetteDevice::tapeArmingCommand(info);
        if (label != info || armed != info) {
            std::fprintf(stderr,
                "FAIL: the deck would misdescribe an extended tape.\n"
                "  label  = \"%s\"\n  banner = \"%s\"\n"
                "  both must be the entry sequence verbatim: \"%s\"\n",
                label.c_str(), armed.c_str(), info.c_str());
            return 1;
        }
        // ...and the range-style tapes must keep their old behaviour: the
        // range is typed first, C100R is what starts the read.
        if (CassetteDevice::tapeLabelCommand("E000.EFFF") != "E000.EFFFR" ||
            CassetteDevice::tapeArmingCommand("E000.EFFF") != "C100R") {
            std::fprintf(stderr,
                "FAIL: a bare load range no longer gets the Woz Monitor's "
                "run suffix / the stock C100R arming command.\n");
            return 1;
        }
        std::printf("B: deck says \"Type %s\" and waits for \"%s\"\n",
                    label.c_str(), armed.c_str());
    }

    tape.playTape();   // B6: arms; the first $C081 read starts it

    // Exactly the operator's keystrokes on real hardware.
    queueWozmonCommand(memory, "C500R");
    queueWozmonCommand(memory, "RX RX");

    cpu.hardReset();
    cpu.setProgramCounter(0xFF1F);   // Wozmon GETLINE
    cpu.start();

    // codebrk.aiff is ~24 s of tape; 200 M cycles is ~195 s of emulated
    // wallclock, generous even with a long leader.
    constexpr int kCycleSlice = 50000;
    constexpr int64_t kCycleBudget = 200'000'000LL;
    int64_t cyclesConsumed = 0;

    bool sawExtendedPage = false;   // CPU reached $C5xx
    bool sawRelocated = false;      // CPU reached the page-1 copy
    bool banner = false;

    while (cyclesConsumed < kCycleBudget) {
        cyclesConsumed += cpu.run(kCycleSlice);

        const uint16_t pc = cpu.getProgramCounter();
        if (!sawExtendedPage && pc >= 0xC500 && pc <= 0xC5FF) {
            sawExtendedPage = true;
            std::printf("  [cycle %lld] entered the extended page at $%04X\n",
                        static_cast<long long>(cyclesConsumed), pc);
        }
        if (!sawRelocated && pc >= 0x0100 && pc <= 0x01FF) {
            sawRelocated = true;
            std::printf("  [cycle %lld] running the relocated ACI at $%04X "
                        "(SP=$%02X)\n",
                        static_cast<long long>(cyclesConsumed), pc,
                        cpu.getStackPointer());
        }
        // Autostart: the header's equal from/to addresses make the firmware
        // JMP ($0024) into the game, which prints its banner.
        if (containsIgnoreCase(display.captured, "CODEBREAKER")) {
            banner = true;
            break;
        }
    }

    std::printf("B: ran %lld cycles, PC=$%04X\n",
                static_cast<long long>(cyclesConsumed), cpu.getProgramCounter());
    std::printf("---- $D012 capture ----\n%s\n-----------------------\n",
                display.captured.c_str());

    if (!sawExtendedPage) {
        std::fprintf(stderr,
            "FAIL: the CPU never reached $C5xx — `C500R` did not enter the "
            "extended page (Wozmon R parse, or the page is not mapped).\n");
        return 1;
    }
    if (!sawRelocated) {
        std::fprintf(stderr,
            "FAIL: the CPU never ran the page-1 copy — the extended page's "
            "self-relocation of $C100-$C1FF into $0100 did not happen.\n");
        return 1;
    }
    if (!banner) {
        std::fprintf(stderr,
            "FAIL: Codebreaker never announced itself on $D012 — the extended "
            "format load (`RX RX`) did not complete or did not autostart.\n");
        return 1;
    }

    std::printf("PASS: extended ACI loaded and autostarted codebrk.aiff "
                "(%lld cycles)\n", static_cast<long long>(cyclesConsumed));
    return 0;
}
