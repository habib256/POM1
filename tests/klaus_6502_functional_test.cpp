// Klaus Dormann's 6502 functional test — smoke test for POM1's M6502 core.
//
// The test is a single 64 KB image that exercises every official 6502
// instruction + addressing mode, verifies all flag behaviours, and ends
// by JMP-ing to itself at a known "success" address. Any earlier trap
// (JMP-to-self at a different address) means the emulator diverged from
// the expected behaviour on that instruction.
//
// Reference: https://github.com/Klaus2m5/6502_65C02_functional_tests
//
// The binary is downloaded by CMake at configure time and the path is
// passed as argv[1]. Loads at $0000, test entry is at $0400. The reset
// vector in the image already points at $0400.

#include "M6502.h"
#include "Memory.h"
// Memory holds unique_ptrs to these peripheral classes (forward-declared in
// Memory.h). The destructor of a local Memory instance needs the complete
// types, so include them here.
#include "A1IO_RTC.h"
#include "CFFA1.h"
#include "MicroSD.h"
#include "SID.h"
#include "TMS9918.h"
#include "TerminalCard.h"
#include "WiFiModem.h"
#include "PR40Printer.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

// Success address for the reference 6502_functional_test.bin published in
// bin_files/ (commit 9cfcedd…, SHA256 fa12bfc7…). If the binary is ever
// reassembled from source with different segment addresses, update this.
constexpr uint16_t kSuccessAddress = 0x3469;
constexpr uint16_t kTestEntry      = 0x0400;
constexpr size_t   kImageSize      = 0x10000;

// Safety cap: the test runs ~96 million 6502 cycles. At native host speed
// (2 ns/cycle for a warm interpreter) that's a couple of seconds. Cap at
// 200 million to avoid hanging CI on a buggy build.
constexpr long kMaxCycles = 200'000'000L;

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
            "usage: test_klaus_6502 <path-to-6502_functional_test.bin>\n");
        return 2;
    }

    std::ifstream f(argv[1], std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    std::vector<uint8_t> image((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
    if (image.size() != kImageSize) {
        std::fprintf(stderr,
            "unexpected image size: %zu bytes (expected %zu)\n",
            image.size(), kImageSize);
        return 2;
    }

    // Memory in flat-RAM test mode: no PeripheralBus, no PIA aliasing, no
    // ROM write protection — every byte of the 64 KB image is addressable
    // as plain RAM, exactly as Klaus's test assumes.
    Memory memory;
    memory.setTestMode(true);
    memory.setWriteInRom(true);  // defensive — testMode already bypasses the check
    std::memcpy(memory.getMemoryPointerMutable(), image.data(), kImageSize);

    M6502 cpu(&memory);
    // The reset vector in the image points at $0400 but we set PC manually
    // to avoid relying on Memory::configureResetVectors (which test mode
    // bypasses anyway).
    cpu.setProgramCounter(kTestEntry);
    cpu.start();

    const auto t0 = std::chrono::steady_clock::now();
    long cycles_total = 0;
    uint16_t last_pc = cpu.getProgramCounter();
    long stuck_for = 0;

    // We step instruction-by-instruction so we can detect "PC didn't move"
    // the moment it happens. Klaus's traps are all `JMP *` — one step and
    // PC stays. Any forward progress resets the stuck counter.
    //
    // Running step() in batches via run(N) would be faster but loses the
    // trap-detection granularity; a legitimate tight loop inside the test
    // could transiently match the "PC unchanged" criterion if we only
    // checked the start and end of a large batch.
    while (cycles_total < kMaxCycles) {
        cpu.step();
        cycles_total += 1;           // step count, not cycles — close enough for the cap
        const uint16_t pc = cpu.getProgramCounter();
        if (pc == last_pc) {
            // JMP-to-self style trap. One iteration is enough (JMP doesn't
            // branch conditionally) but we double-check to guard against
            // BEQ/BNE patterns that look transient — +1 extra step is free.
            if (++stuck_for >= 2) {
                break;
            }
        } else {
            stuck_for = 0;
            last_pc = pc;
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double seconds =
        std::chrono::duration<double>(t1 - t0).count();

    const uint16_t final_pc = cpu.getProgramCounter();
    std::printf("Klaus 6502 functional test: "
                "ended at $%04X after %ld steps (%.2f s)\n",
                final_pc, cycles_total, seconds);

    if (cycles_total >= kMaxCycles) {
        std::fprintf(stderr,
            "TIMEOUT — test did not terminate within %ld steps. "
            "Last PC: $%04X\n",
            kMaxCycles, final_pc);
        return 1;
    }

    if (final_pc != kSuccessAddress) {
        std::fprintf(stderr,
            "FAIL — trapped at $%04X (expected success at $%04X)\n",
            final_pc, kSuccessAddress);
        return 1;
    }

    std::printf("OK\n");
    return 0;
}
