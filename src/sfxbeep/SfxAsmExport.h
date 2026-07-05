// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// 1-bit beeper SFX ↔ ca65 assembly — the pure text logic behind the beeper SFX
// editor's "Export ASM" and the round-trip loader. One format, two directions,
// one source of truth: formatSfxAsm writes exactly the `sfx_<name>:` +
// `.byte $period, $length` + `.byte $00, $00` terminator table that
// dev/lib/beep/beep_sfx_bank.inc ships and dev/lib/beep/beep_sfx.asm plays, and
// parseSfxAsm reads it straight back — so an exported SFX round-trips. It is
// also byte-for-byte what tools/emit_beep_sfx.py emits, so C++ and Python agree.
// No GL/ImGui/emulator dependency; ports verbatim to POM2. Mirror of
// tmssprite/TmsSpriteAsmExport. Pinned by sfx_asm_export_smoke.

#ifndef SFXBEEP_SFX_ASM_EXPORT_H
#define SFXBEEP_SFX_ASM_EXPORT_H

#include <string>
#include <vector>

#include "sfxbeep/SfxModel.h"

namespace sfxbeep {

// Sanitize a user-typed name into a ca65-safe [a-z0-9_] identifier (used as the
// `sfx_<name>` label). Empty / all-invalid input falls back to "sfx".
std::string sanitizeName(const std::string& raw);

// Model → ca65 table text (a self-contained `.inc` fragment, no .export). The
// label is `sfx_<sanitize(name)>`. Always ends with the `.byte $00,$00`
// terminator beep_sfx.asm's walk requires.
std::string formatSfxAsm(const SfxModel& model);

// One SFX parsed back out of an asm/inc fragment.
struct ParsedSfx {
    std::string name;              // label with the leading "sfx_" stripped
    std::vector<Step> steps;       // terminator NOT included
};

// Parse every `sfx_<name>:` table in `text` back to steps. Tolerant of comments
// (`;`), blank lines and `$`/decimal byte forms; stops each table at its 0-length
// terminator. The inverse of formatSfxAsm for any table it produced.
std::vector<ParsedSfx> parseSfxAsm(const std::string& text);

}  // namespace sfxbeep

#endif  // SFXBEEP_SFX_ASM_EXPORT_H
