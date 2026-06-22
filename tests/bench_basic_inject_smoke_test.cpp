// bench_basic_inject_smoke_test.cpp -- pin the DevBench "BASIC" language path.
//
// The Bench's BASIC targets (Pom1BenchHost::injectBasic, mode 4) don't compile
// anything: they cold-start the in-ROM interpreter through the WOZ Monitor and
// TYPE the listing at the prompt over the Apple-1 keyboard FIFO ($D010), then
// RUN. Because it is pure keyboard injection — no cc65, no async build — it runs
// identically on desktop and in the web (WASM) build.
//
// This test exercises that exact mechanism headlessly, at the Memory + M6502
// level injectBasic relies on:
//   * Integer BASIC  ($E000, loaded by initMemory)   -> cold start E000R
//   * Applesoft Lite ($6000, applesoft-lite-microsd) -> cold start 6000R
// For each: boot WOZ, feed "<coldstart>\r<listing>\rRUN\r" into the keyboard,
// run the CPU, and assert the program's COMPUTED output reaches the $D012
// display sink. The asserted token is the *result* of an arithmetic PRINT
// (e.g. 1000+7 -> "1007"), which never appears in the typed source — so a match
// proves the interpreter actually executed the injected program, not that the
// listing was merely echoed back.
//
// Memory.h forward-declares card types via unique_ptr; the full definitions are
// needed here for Memory's destructor to be emitted (mirrors the other
// Memory-level smoke tests).
#include "TMS9918.h"      // IWYU pragma: keep
#include "WiFiModem.h"    // IWYU pragma: keep
#include "TerminalCard.h" // IWYU pragma: keep
#include "A1IO_RTC.h"     // IWYU pragma: keep
#include "PR40Printer.h"  // IWYU pragma: keep
#include "Memory.h"
#include "M6502.h"
#include "DisplayDevice.h"

#include <cassert>
#include <cstdio>
#include <string>

namespace {

// Captures every byte the 6502 writes to the $D012 display port — i.e. exactly
// what the Apple-1 screen would show (input echo + program output).
class CaptureDisplay : public DisplayDevice {
public:
    void onChar(char c) override { text.push_back(c); }
    std::string text;
};

// Bring up WOZ, type the cold-start + listing + RUN, run until the expected
// token appears on the display (or the cycle budget is spent). Returns the
// captured display text so the caller can assert / print it.
std::string runInjectedBasic(Memory& mem, const char* coldStart,
                             const char* listing, const std::string& token)
{
    CaptureDisplay disp;
    mem.setDisplayDevice(&disp);

    M6502 cpu(&mem);
    cpu.setProgramCounter(0xFF00);   // WOZ Monitor reset entry
    cpu.start();

    // Warm up: let WOZ run its reset init and settle at the GETLN prompt before
    // we present keys — same ordering as the live path, where the keyboard queue
    // only drains onto the bus after the monitor is already polling $D011.
    cpu.run(400000);

    auto type = [&](const char* s) { for (const char* p = s; *p; ++p) mem.setKeyPressed(*p); };
    type("\r");          // fresh monitor line
    type(coldStart); type("\r");
    type(listing);       // listing lines are already '\r'-separated
    type("RUN\r");

    // Generous budget with early-exit. At the default 60 chars/s display pace a
    // few dozen echoed+printed chars cost ~1M cycles; the interpreter work adds
    // a little more. 150M is far more than enough and the early-out keeps the
    // common case fast.
    const long long kBudget = 150000000;
    const int kSlice = 100000;
    for (long long c = 0; c < kBudget; c += kSlice) {
        cpu.run(kSlice);
        if (disp.text.find(token) != std::string::npos) break;
    }
    mem.setDisplayDevice(nullptr);
    return disp.text;
}

} // namespace

int main()
{
    // ---- Integer BASIC ($E000, cold start E000R) -------------------------
    // 1000+7 -> "1007": the result is absent from the typed source, so finding
    // it on the display proves Integer BASIC RAN the injected program.
    {
        Memory mem;
        mem.initMemory();   // loads basic.rom @ $E000 + WozMonitor @ $FF00
        const char* listing =
            "10 PRINT 1000+7\r"
            "20 END\r";
        const std::string out = runInjectedBasic(mem, "E000R", listing, "1007");
        if (out.find("1007") == std::string::npos) {
            std::fprintf(stderr,
                "Integer BASIC inject FAILED: '1007' not printed.\n--- display ---\n%s\n",
                out.c_str());
            return 1;
        }
        std::printf("integer-basic inject: OK (E000R + listing + RUN -> 1007)\n");
    }

    // ---- Applesoft Lite ($6000, cold start 6000R) ------------------------
    // 100/8 -> "12.5": floating-point division, result absent from the source.
    {
        Memory mem;
        mem.initMemory();   // loads sdcard.rom (SD OS @ $8000) + WozMonitor
        mem.setWriteInRom(true);
        const int rc = mem.loadApplesoftLiteSDCard();   // applesoft-lite-microsd @ $6000
        mem.setWriteInRom(false);
        if (rc != 0) {
            std::fprintf(stderr,
                "applesoft-lite-microsd.rom not loadable (rc=%d) — cannot test Applesoft.\n", rc);
            return 1;
        }
        const char* listing =
            "10 PRINT 100/8\r"
            "20 END\r";
        const std::string out = runInjectedBasic(mem, "6000R", listing, "12.5");
        if (out.find("12.5") == std::string::npos) {
            std::fprintf(stderr,
                "Applesoft Lite inject FAILED: '12.5' not printed.\n--- display ---\n%s\n",
                out.c_str());
            return 1;
        }
        std::printf("applesoft-lite inject: OK (6000R + listing + RUN -> 12.5)\n");
    }

    // ---- Root-cause pin: WHY the microSD Applesoft injection relaxes OOR ----
    // Applesoft Lite lives at $6000-$7FFF. On the 8 KB microSD preset
    // (presetRamKB=8) the out-of-range window is $1000..$7FFF, and the preset
    // arms silicon/OOR-strict mode (non-Fantasy), so CPU reads of $6000 return
    // $FF even though the ROM byte is present. 6000R then jumps into $FF garbage
    // and falls back to WozMon — the live bug. injectBasic() therefore relaxes the
    // microSD preset to a permissive 64 KB view (presetRamKB=64 -> isOorAddress
    // always false -> $6000 readable) for the Applesoft run. This pins both halves:
    // $6000 reads $FF under 8K+strict, and the real ROM byte under the 64 KB view.
    {
        Memory mem;
        mem.initMemory();
        mem.setWriteInRom(true);
        if (mem.loadApplesoftLiteSDCard() != 0) { std::fprintf(stderr, "applesoft ROM load failed\n"); return 1; }
        mem.setWriteInRom(false);
        const uint8_t rawByte = mem.getMemoryPointer()[0x6000];   // raw ROM byte (bypasses OOR)

        // Strict 8 KB machine (microSD preset profile): $6000 reads back $FF.
        mem.setPresetRamKB(8);
        mem.setOutOfRangeStrictMode(true);
        const uint8_t strictRead = mem.memRead(0x6000);

        // Permissive 64 KB Fantasy machine: $6000 reads the real ROM byte.
        mem.setPresetRamKB(64);
        mem.setOutOfRangeStrictMode(false);
        const uint8_t fantasyRead = mem.memRead(0x6000);

        if (rawByte == 0xFF) {
            std::fprintf(stderr, "unexpected: applesoft ROM byte at $6000 is $FF (load failed?)\n");
            return 1;
        }
        if (strictRead != 0xFF) {
            std::fprintf(stderr,
                "OOR pin FAILED: $6000 under 8K+strict read $%02X (expected $FF). The OOR\n"
                "trap that breaks Applesoft on the microSD preset has changed — re-check\n"
                "the Applesoft target's preset choice in Pom1BenchHost.\n", strictRead);
            return 1;
        }
        if (fantasyRead != rawByte) {
            std::fprintf(stderr,
                "OOR pin FAILED: $6000 under 64K Fantasy read $%02X (expected ROM byte $%02X)\n",
                fantasyRead, rawByte);
            return 1;
        }
        std::printf("oor-pin: OK ($6000 ROM=$%02X; 8K+strict reads $FF; 64K Fantasy reads $%02X)\n",
                    rawByte, fantasyRead);
    }

    std::printf("bench_basic_inject_smoke: OK\n");
    return 0;
}
