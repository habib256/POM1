// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// SID song model — the emulator-agnostic data behind the SID tracker editor.
// A song is a list of ROWS matching the runtime table dev/lib/sid/sid_player.asm
// walks (one voice-1 sequencer for now; voices 2/3 extend the same way):
//   note   : 0..95 note index (sid_notes.inc), or NOTE_OFF ($FE, gate off/rest),
//            or NOTE_TIE ($FF, sustain the previous note untouched)
//   ctrl   : waveform mask (WAVE_TRI/SAW/PULSE/NOISE) used when note < 96
//   frames : rows-are-held-this-many ticks; 0 terminates a table (never stored)
// No ImGui / GL / emulator dependency — ports verbatim to POM2. The editor owns
// the pattern grid + undo; the host owns the live preview (poke SID registers);
// SidSongAsmExport turns this into the ca65 table (and back). Pinned by
// sid_song_asm_export_smoke.

#ifndef SIDTRACK_SONG_MODEL_H
#define SIDTRACK_SONG_MODEL_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sidtrack {

// Special note bytes (mirror sid_player.asm).
enum : uint8_t {
    NOTE_OFF = 0xFE,   // gate off — a rest
    NOTE_TIE = 0xFF,   // hold the previous note, no retrigger
};
// Waveform-mask bits (mirror sid.inc CR bits; gate is added by the runtime).
enum : uint8_t {
    WAVE_TRI   = 0x10,
    WAVE_SAW   = 0x20,
    WAVE_PULSE = 0x40,
    WAVE_NOISE = 0x80,
};

struct Row {
    uint8_t note   = 57;         // A4 by default
    uint8_t ctrl   = WAVE_TRI;   // waveform (used when note < 96)
    uint8_t frames = 8;          // duration in ticks; never 0 in a live row
};

inline bool operator==(const Row& a, const Row& b) {
    return a.note == b.note && a.ctrl == b.ctrl && a.frames == b.frames;
}
inline bool operator!=(const Row& a, const Row& b) { return !(a == b); }

// note index (0..95) → "C4"/"A#5"… ; NOTE_OFF → "---" ; NOTE_TIE → "===".
std::string noteName(uint8_t note);

class SongModel {
public:
    SongModel() = default;

    const std::string& name() const { return name_; }
    void setName(std::string n) { name_ = std::move(n); }

    std::size_t size() const { return rows_.size(); }
    bool empty() const { return rows_.empty(); }
    const std::vector<Row>& rows() const { return rows_; }
    const Row& at(std::size_t i) const { return rows_.at(i); }

    void clear() { rows_.clear(); }
    void addRow(Row r) { rows_.push_back(clampRow(r)); }
    void insertRow(std::size_t i, Row r);
    void setRow(std::size_t i, Row r);
    void removeRow(std::size_t i);

    unsigned totalFrames() const;   // sum of row durations (a length gauge)

private:
    static Row clampRow(Row r) { if (r.frames == 0) r.frames = 1; return r; }

    std::string name_ = "song";
    std::vector<Row> rows_;
};

}  // namespace sidtrack

#endif  // SIDTRACK_SONG_MODEL_H
