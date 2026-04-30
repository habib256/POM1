// SID audio production smoke test — self-contained, no framework.
// Covers two layers:
//   (1) Direct: writeRegister / advanceCycles on a bare pom1::SID instance.
//   (2) Through Memory's PeripheralBus: memWrite at $C800+ must reach the
//       SID via the bus dispatch, and advancing cycles via Memory must
//       produce samples the same way.
// Regressions in either layer would silence SID audio in the app.

#include "Memory.h"
// Memory holds unique_ptrs to these (forward-declared in Memory.h); destructor
// needs the complete types.
#include "A1IO_RTC.h"
#include "CFFA1.h"
#include "MicroSD.h"
#include "SID.h"
#include "TMS9918.h"
#include "TerminalCard.h"
#include "WiFiModem.h"
#include "PR40Printer.h"
#include "CpuClock.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

struct VoiceProbe {
    int nonZero = 0;
    float peak  = 0.0f;
};

VoiceProbe probe(const std::vector<float>& buf)
{
    VoiceProbe r;
    for (float s : buf) {
        if (s != 0.0f) ++r.nonZero;
        if (std::fabs(s) > r.peak) r.peak = std::fabs(s);
    }
    return r;
}

template <typename Writer>
void writeVoice1Triangle(Writer writer)
{
    const uint32_t freq = static_cast<uint32_t>(440.0 * 16777216.0 / POM1_CPU_CLOCK_HZ);
    writer(0x00, freq & 0xFF);         // FREQ LO
    writer(0x01, (freq >> 8) & 0xFF);  // FREQ HI
    writer(0x05, 0x00);                // AD envelope: instant attack, no decay
    writer(0x06, 0xF0);                // SR envelope: full sustain, no release
    writer(0x18, 0x0F);                // Volume = max, no filters
    writer(0x04, 0x11);                // Voice 1 control: triangle + GATE on
}

} // namespace

int main()
{
    // --- Layer 1: direct SID --------------------------------------------------
    {
        pom1::SID sid(44100);
        writeVoice1Triangle([&](uint8_t r, uint8_t v) { sid.writeRegister(r, v); });

        const int totalCycles = POM1_CPU_CLOCK_HZ / 10;  // ~100 ms
        const int slice = 6000;
        for (int c = 0; c < totalCycles; c += slice) {
            sid.advanceCycles(std::min(slice, totalCycles - c));
        }

        std::vector<float> buf(8192, 0.0f);
        sid.fillAudioBuffer(buf.data(), static_cast<int>(buf.size()));
        VoiceProbe p = probe(buf);
        std::printf("[direct] non-zero=%d/%zu peak=%.4f\n", p.nonZero, buf.size(), p.peak);
        assert(p.nonZero > 100 && "direct SID produced no audio samples");
        assert(p.peak > 0.01f    && "direct SID samples near zero");
    }

    // --- Layer 2: via Memory's peripheral bus ---------------------------------
    {
        Memory mem;
        mem.setSIDEnabled(true);
        assert(mem.isSIDEnabled());

        writeVoice1Triangle([&](uint8_t r, uint8_t v) {
            mem.memWrite(static_cast<uint16_t>(0xC800 + r), v);
        });

        const int totalCycles = POM1_CPU_CLOCK_HZ / 10;
        const int slice = 6000;
        for (int c = 0; c < totalCycles; c += slice) {
            mem.advanceCycles(std::min(slice, totalCycles - c));
        }

        std::vector<float> buf(8192, 0.0f);
        mem.getSID().fillAudioBuffer(buf.data(), static_cast<int>(buf.size()));
        VoiceProbe p = probe(buf);
        std::printf("[bus]    non-zero=%d/%zu peak=%.4f\n", p.nonZero, buf.size(), p.peak);
        assert(p.nonZero > 100 && "SID via bus produced no audio samples "
                                  "— PeripheralBus dispatch broken?");
        assert(p.peak > 0.01f    && "SID via bus samples near zero "
                                    "— writes not reaching the chip?");
    }

    // --- Layer 3: SID audio survives a hardReset cycle ------------------------
    // Mimics what EmulationController::hardReset() does around the SID:
    //   setSIDEnabled(false) → resetMemory + initMemory → setSIDEnabled(true)
    // The test plays audio through the bus, performs the reset cycle, then
    // plays again and asserts the second pass still produces samples —
    // catches regressions where the SID gets stranded off the audio mixer
    // or where the bus handler isn't re-armed properly after hardReset.
    {
        Memory mem;
        mem.setSIDEnabled(true);
        assert(mem.isSIDEnabled());

        auto playAndProbe = [&](const char* label) {
            writeVoice1Triangle([&](uint8_t r, uint8_t v) {
                mem.memWrite(static_cast<uint16_t>(0xC800 + r), v);
            });
            const int totalCycles = POM1_CPU_CLOCK_HZ / 10;
            const int slice = 6000;
            for (int c = 0; c < totalCycles; c += slice) {
                mem.advanceCycles(std::min(slice, totalCycles - c));
            }
            std::vector<float> buf(8192, 0.0f);
            mem.getSID().fillAudioBuffer(buf.data(), static_cast<int>(buf.size()));
            VoiceProbe p = probe(buf);
            std::printf("[reset-%s] non-zero=%d/%zu peak=%.4f\n",
                        label, p.nonZero, buf.size(), p.peak);
            return p;
        };

        VoiceProbe before = playAndProbe("pre");
        assert(before.nonZero > 100 && "SID silent before reset");
        assert(before.peak > 0.01f);

        // Simulate EmulationController::hardReset()'s SID fence:
        mem.setSIDEnabled(false);        // unplug from audio mixer + bus
        mem.resetMemory();                // wipes RAM, resetChip
        mem.initMemory();                 // reloads ROMs, resetChip
        mem.setSIDEnabled(true);          // re-plug on audio mixer + bus
        assert(mem.isSIDEnabled());

        VoiceProbe after = playAndProbe("post");
        assert(after.nonZero > 100 && "SID silent after hardReset — bus or "
                                       "audio-source reattachment broken");
        assert(after.peak > 0.01f   && "SID samples near zero after hardReset");
    }

    std::puts("SID audio smoke test: OK");
    return 0;
}
