// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "sidtrack/SongModel.h"

namespace sidtrack {

std::string noteName(uint8_t note) {
    if (note == NOTE_OFF) return "---";
    if (note == NOTE_TIE) return "===";
    if (note > 95) return "???";
    static const char* kNames[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return std::string(kNames[note % 12]) + std::to_string(note / 12);
}

void SongModel::insertRow(std::size_t i, Row r) {
    if (i > rows_.size()) i = rows_.size();
    rows_.insert(rows_.begin() + static_cast<std::ptrdiff_t>(i), clampRow(r));
}

void SongModel::setRow(std::size_t i, Row r) {
    if (i < rows_.size()) rows_[i] = clampRow(r);
}

void SongModel::removeRow(std::size_t i) {
    if (i < rows_.size())
        rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(i));
}

unsigned SongModel::totalFrames() const {
    unsigned t = 0;
    for (const Row& r : rows_) t += r.frames;
    return t;
}

}  // namespace sidtrack
