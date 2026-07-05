// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// sfx_asm_export_smoke — pins the beeper SFX data layer: the SfxModel edit ops,
// and formatSfxAsm/parseSfxAsm round-tripping in exactly the dev/lib/beep table
// format that beep_sfx.asm plays. No emulator needed (pure text logic).

#include "sfxbeep/SfxAsmExport.h"
#include "sfxbeep/SfxModel.h"
#include "sfxbeep/SfxPulse.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace sfxbeep;

int main() {
    // --- SfxPulse: SFX steps -> square-wave (cycles, level) segments ---------
    {
        // one 2-half-cycle tone at period 0x40 -> two segments, level toggles.
        auto p = sfxToPulses({{0x40, 2}});
        assert(p.size() == 2);
        const uint32_t hc = halfCycleCycles(0x40);       // 5*0x40 + 16
        assert(hc == 5u * 0x40u + 16u);
        assert(p[0].first == hc && p[1].first == hc);
        assert(p[0].second == true && p[1].second == false);   // starts low->toggles high,low

        // a rest (period 0) holds the current level for one coalesced segment.
        auto r = sfxToPulses({{0x40, 1}, {0x00, 2}, {0x20, 1}});
        assert(r.size() == 3);                            // tone, rest, tone
        assert(r[0].second == true);                      // first toggle -> high
        assert(r[1].first == halfCycleCycles(0) * 2u);    // rest = 2 half-cycles wide
        assert(r[1].second == true);                      // rest holds the level (no toggle)
        assert(r[2].second == false);                     // next tone resumes toggling
    }

    // --- model edit ops + terminator-clamp ----------------------------------
    {
        SfxModel m;
        m.setName("coin");
        m.addStep({0x60, 0x18});
        m.addStep({0x40, 0x28});
        assert(m.size() == 2);
        assert(m.totalHalfCycles() == 0x18u + 0x28u);

        // length 0 is the table terminator — the model must never hold it.
        m.addStep({0x10, 0x00});
        assert(m.at(2).length == 1);          // clamped up
        m.setStep(2, {0x10, 0x00});
        assert(m.at(2).length == 1);          // clamped on setStep too

        m.removeStep(2);
        assert(m.size() == 2);

        m.insertStep(1, {0x00, 0x04});        // a REST (period 0) in the middle
        assert(m.size() == 3 && m.at(1).period == 0x00 && m.at(1).length == 0x04);
    }

    // --- format → parse round-trip (incl. a rest) ---------------------------
    {
        SfxModel m;
        m.setName("Laser Zap!");                 // exercises the sanitizer
        m.addStep({0x18, 0x06});
        m.addStep({0x00, 0x04});                 // rest
        m.addStep({0x40, 0x06});

        const std::string asmText = formatSfxAsm(m);
        assert(asmText.find("sfx_laser_zap:") != std::string::npos);
        assert(asmText.find("$00, $00") != std::string::npos);   // terminator present

        auto parsed = parseSfxAsm(asmText);
        assert(parsed.size() == 1);
        assert(parsed[0].name == "laser_zap");
        assert(parsed[0].steps.size() == 3);     // terminator NOT counted
        assert((parsed[0].steps == m.steps()));  // byte-identical steps
    }

    // --- sanitizeName edge cases --------------------------------------------
    assert(sanitizeName("My SFX!") == "my_sfx");
    assert(sanitizeName("123abc") == "abc");     // leading digits stripped
    assert(sanitizeName("") == "sfx");
    assert(sanitizeName("!!!") == "sfx");

    // --- parse the shipped bank format (cross-check vs beep_sfx_bank.inc) ----
    {
        const char* bank =
            "; coin\n"
            "sfx_coin:\n"
            "        .byte $60, $18          ; step 0\n"
            "        .byte $40, $28          ; step 1\n"
            "        .byte $00, $00          ; end\n"
            "sfx_hit:\n"
            "        .byte $70, $20\n"
            "        .byte $00, $04          ; rest (period 0, length 4)\n"
            "        .byte $90, $30\n"
            "        .byte $00, $00\n";
        auto parsed = parseSfxAsm(bank);
        assert(parsed.size() == 2);
        assert(parsed[0].name == "coin"  && parsed[0].steps.size() == 2);
        assert(parsed[1].name == "hit"   && parsed[1].steps.size() == 3);
        // the rest step survived (length 4, not mistaken for a terminator):
        assert(parsed[1].steps[1].period == 0x00 && parsed[1].steps[1].length == 0x04);
    }

    std::printf("sfx_asm_export_smoke: OK\n");
    return 0;
}
