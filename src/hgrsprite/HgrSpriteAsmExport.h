// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// HGR sprite ↔ ca65 assembly — the pure text logic behind the HGR Sprite
// editor's "Export ASM" and the host's dev-sprite-library loader. One format,
// two directions, one source of truth: formatSpriteAsm writes exactly the
// `; slot … -- <name>` + `<name>_pat:` + `.byte $xx, …` catalogue format that
// dev/lib/gen2/sprites ships, and parseSpritesAsm is THE parser
// Pom1HgrPaintHost::devSprites feeds those files through — so an exported
// sprite is guaranteed to round-trip (pinned by sprite_asm_export_smoke).
// No GL/ImGui/emulator dependency; ports verbatim to POM2. Mirror of
// tmssprite/TmsSpriteAsmExport (row-major wBytes×hRows here vs native 8/32 B
// pattern slots there).

#ifndef HGRSPRITE_ASM_EXPORT_H
#define HGRSPRITE_ASM_EXPORT_H

#include <cstdint>
#include <string>
#include <vector>

namespace hgrsprite {

// One parsed `.byte` block: slot-comment/label name + raw sprite bytes.
struct AsmSprite {
    std::string name;
    std::vector<uint8_t> bytes;
};

// Sanitize a user-typed sprite name into a ca65-safe [a-z0-9_] identifier:
// lower-cased, runs of other characters collapse to one '_', leading/trailing
// '_' padding stripped, a digit-leading result gets a '_' prefix, and an empty
// result falls back to "sprite".
std::string sanitizeAsmName(const std::string& raw);

// Format a row-major wBytes×hRows HGR sprite (bit 0 = leftmost pixel) as a
// ca65 snippet in the dev-catalogue format: a `; slot 01/01 -- <name>` header
// comment, a `<name>_pat:` label, and hRows `.byte` lines of wBytes values.
// `note`, when non-empty, is emitted as one extra `; ` comment line before the
// slot header — the ×2 export uses it to record the chosen colour + the
// parity/blit-mode contract. It is documentation only: the doubled bytes already
// carry the colour, so parseSpritesAsm ignores the note and the round-trip holds.
std::string formatSpriteAsm(const std::string& name, int wBytes, int hRows,
                            const uint8_t* bytes, const std::string& note = "");

// Parse catalogue text: every bare ca65 label starting a run of `.byte` lines
// totalling at least `minBytes` values becomes one sprite (truncated to exactly
// minBytes); its name comes from the most recent `; slot … -- name` comment, or
// the label minus a trailing "_pat". Inline comments are stripped; a base label
// with no bytes (e.g. `<cat>_hgr_data:`) is flushed away by the next label.
// minBytes = 48 for the bundled 3-byte × 16-row GEN2 HGR library.
std::vector<AsmSprite> parseSpritesAsm(const std::string& text, size_t minBytes);

} // namespace hgrsprite

#endif // HGRSPRITE_ASM_EXPORT_H
