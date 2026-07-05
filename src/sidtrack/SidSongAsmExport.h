// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// SID song ↔ ca65 assembly — the pure text logic behind the SID tracker's
// "Export ASM" and round-trip loader. One format, two directions, one source of
// truth: formatSongAsm writes exactly the `song_<name>:` + `.byte note,ctrl,
// frames` rows + `.byte $00,$00,$00` terminator table that
// dev/lib/sid/sid_player.asm plays, and parseSongAsm reads it back. Byte-for-byte
// what tools/emit_sid_song.py emits, so C++ and Python agree. No GL/ImGui/
// emulator dependency; ports verbatim to POM2. Mirror of sfxbeep/SfxAsmExport.
// Pinned by sid_song_asm_export_smoke.

#ifndef SIDTRACK_SID_SONG_ASM_EXPORT_H
#define SIDTRACK_SID_SONG_ASM_EXPORT_H

#include <string>
#include <vector>

#include "sidtrack/SongModel.h"

namespace sidtrack {

// [a-z0-9_] identifier for the `song_<name>` label. "" / invalid → "song".
std::string sanitizeName(const std::string& raw);

// Model → ca65 table text (self-contained `.inc` fragment, no .export). Each row
// is commented with its note name (C4/A#5/---/===). Always terminated.
std::string formatSongAsm(const SongModel& model);

struct ParsedSong {
    std::string name;              // label with leading "song_" stripped
    std::vector<Row> rows;         // terminator NOT included
};

// Parse every `song_<name>:` table in `text` back to rows. Tolerant of comments,
// blank lines and $/decimal byte forms; stops each table at its 0-frames
// terminator. Inverse of formatSongAsm.
std::vector<ParsedSong> parseSongAsm(const std::string& text);

}  // namespace sidtrack

#endif  // SIDTRACK_SID_SONG_ASM_EXPORT_H
