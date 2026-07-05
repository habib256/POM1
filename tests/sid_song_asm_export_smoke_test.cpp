// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// sid_song_asm_export_smoke — pins the SID tracker data layer: SongModel edit
// ops, noteName, and formatSongAsm/parseSongAsm round-tripping in exactly the
// dev/lib/sid table format sid_player.asm plays. Pure text logic, no emulator.

#include "sidtrack/SidSongAsmExport.h"
#include "sidtrack/SidVoice.h"
#include "sidtrack/SongModel.h"

#include <cassert>
#include <cstdio>
#include <string>

using namespace sidtrack;

int main() {
    // --- SidVoice: noteFreq byte-identical to the shipped sid_notes.inc -------
    // Oracle values read out of dev/lib/sid/sid_notes.inc (lo,hi per note).
    assert(noteFreq(0)  == 0x010C);   // C0  -> lo $0C hi $01
    assert(noteFreq(12) == 0x0218);   // C1  -> lo $18 hi $02
    assert(noteFreq(57) == 0x1C32);   // A4  -> lo $32 hi $1C  (440 Hz)
    assert(noteFreq(95) == 0xFD2F);   // B7  -> lo $2F hi $FD
    assert(noteFreq(200) == noteFreq(95));  // clamped

    // --- SidVoice: note-on register sequence --------------------------------
    {
        Instrument inst;                       // defaults
        auto on = noteOnRegisters(57, WAVE_TRI, inst);
        // last write is CR with the gate bit set on top of the waveform
        assert(on.back().first == REG_V1_CR);
        assert(on.back().second == (WAVE_TRI | SID_GATE));
        // frequency split matches noteFreq(A4) = $1C32
        bool sawLo = false, sawHi = false;
        for (auto& [r, v] : on) {
            if (r == REG_V1_FREQLO) { assert(v == 0x32); sawLo = true; }
            if (r == REG_V1_FREQHI) { assert(v == 0x1C); sawHi = true; }
            if (r == REG_VOLUME)    assert((v & 0xF0) == 0);   // lo nibble only
        }
        assert(sawLo && sawHi);

        auto off = noteOffRegisters(WAVE_TRI);
        assert(off.size() == 1 && off[0].first == REG_V1_CR);
        assert((off[0].second & SID_GATE) == 0);               // gate cleared
        assert(silenceRegisters()[0].second == 0x00);
    }

    // --- noteName ------------------------------------------------------------
    assert(noteName(0)  == "C0");
    assert(noteName(57) == "A4");        // 57 = A4 (sid_notes.inc)
    assert(noteName(60) == "C5");
    assert(noteName(NOTE_OFF) == "---");
    assert(noteName(NOTE_TIE) == "===");

    // --- model edit ops + frames-terminator clamp ---------------------------
    {
        SongModel m;
        m.setName("tune");
        m.addRow({57, WAVE_TRI, 3});
        m.addRow({60, WAVE_PULSE, 2});
        assert(m.size() == 2);
        assert(m.totalFrames() == 5);

        m.addRow({0, 0, 0});             // frames 0 would be a terminator
        assert(m.at(2).frames == 1);     // clamped up
        m.removeRow(2);
        assert(m.size() == 2);
    }

    // --- format → parse round-trip (incl. OFF + TIE) ------------------------
    {
        SongModel m;
        m.setName("Chip Tune #1");                 // exercises the sanitizer
        m.addRow({57, WAVE_TRI,   4});
        m.addRow({NOTE_TIE, 0,    2});             // sustain
        m.addRow({NOTE_OFF, 0,    2});             // rest
        m.addRow({64, WAVE_PULSE, 4});

        const std::string asmText = formatSongAsm(m);
        assert(asmText.find("song_chip_tune_1:") != std::string::npos);
        assert(asmText.find("$00, $00, $00") != std::string::npos);   // terminator

        auto parsed = parseSongAsm(asmText);
        assert(parsed.size() == 1);
        assert(parsed[0].name == "chip_tune_1");
        assert(parsed[0].rows.size() == 4);
        assert((parsed[0].rows == m.rows()));      // byte-identical rows
    }

    // --- sanitizeName --------------------------------------------------------
    assert(sanitizeName("Chip Tune #1") == "chip_tune_1");
    assert(sanitizeName("42") == "song");
    assert(sanitizeName("") == "song");

    // --- parse a hand-written table (cross-check the shipped row format) -----
    {
        const char* song =
            "song_demo:\n"
            "        .byte $39, $10, $03   ; A4\n"
            "        .byte $FE, $00, $02   ; ---\n"
            "        .byte $FF, $00, $01   ; ===\n"
            "        .byte $00, $00, $00   ; end\n";
        auto parsed = parseSongAsm(song);
        assert(parsed.size() == 1 && parsed[0].name == "demo");
        assert(parsed[0].rows.size() == 3);
        assert(parsed[0].rows[0].note == 0x39 && parsed[0].rows[0].frames == 3);
        assert(parsed[0].rows[1].note == NOTE_OFF);
        assert(parsed[0].rows[2].note == NOTE_TIE);
    }

    std::printf("sid_song_asm_export_smoke: OK\n");
    return 0;
}
