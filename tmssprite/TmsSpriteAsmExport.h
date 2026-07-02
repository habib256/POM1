// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// TMS9918 sprite ↔ ca65 assembly — the pure text logic behind the TMS9918
// Sprite editor's "Export ASM" and the host's dev-sprite-library loader. One
// format, two directions, one source of truth: formatSpriteAsm writes exactly
// the `; slot … -- <name>` + `<name>_pat:` + `.byte $xx, …` catalogue format
// that dev/lib/tms9918/sprites_*.asm ships, and parseSpritesAsm is THE parser
// Pom1TmsPaintHost::devSprites feeds those files through — so an exported
// sprite is guaranteed to round-trip (pinned by sprite_asm_export_smoke).
// No GL/ImGui/emulator dependency; ports verbatim to POM2. Mirror of
// hgrsprite/HgrSpriteAsmExport (native 8 B / 32 B pattern-slot stream here vs
// row-major wBytes×hRows there).

#ifndef TMSSPRITE_ASM_EXPORT_H
#define TMSSPRITE_ASM_EXPORT_H

#include <cstdint>
#include <string>
#include <vector>

namespace tmssprite {

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

// Format an nBytes-long sprite pattern (8 = one 8×8 slot, 32 = a 16×16 sprite
// in native TMS quadrant order — the $3800+pat*8 stream as stored in VRAM) as
// a ca65 snippet in the dev-catalogue format: a `; slot 01/01 -- <name>`
// header comment, a `<name>_pat:` label, and `.byte` lines of 8 values each.
std::string formatSpriteAsm(const std::string& name, int nBytes,
                            const uint8_t* bytes);

// Parse catalogue text: every bare ca65 label starting a run of `.byte` lines
// totalling at least `minBytes` values becomes one sprite (truncated to exactly
// minBytes); its name comes from the most recent `; slot … -- name` comment, or
// the label minus a trailing "_pat". Inline comments are stripped. minBytes =
// 32 for the bundled native 16×16 SCROLL-O-SPRITES library.
std::vector<AsmSprite> parseSpritesAsm(const std::string& text, size_t minBytes);

} // namespace tmssprite

#endif // TMSSPRITE_ASM_EXPORT_H
