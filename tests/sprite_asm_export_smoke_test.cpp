// Sprite ASM export round-trip smoke test — pins the "Export ASM" feature of
// both sprite editors: hgrsprite/tmssprite::formatSpriteAsm must emit exactly
// the ca65 dev-catalogue format that parseSpritesAsm — the parser behind
// Pom1HgrPaintHost::devSprites / Pom1TmsPaintHost::devSprites — reads back
// (name + byte-for-byte payload). Also pins the [a-z0-9_] name sanitizer and
// the parser against a hand-written catalogue-style file (multiple sprites,
// inline comments, base label, `; slot … -- name` naming, label fallback).
// No GL/ImGui/emulator dependency.
//
// assert() failures abort with a stderr trace + non-zero exit — enough for ctest.

#include "HgrSpriteAsmExport.h"
#include "TmsSpriteAsmExport.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

int main()
{
    // ── HGR: catalogue geometry (3 bytes × 16 rows = 48 B) round-trips ───────
    {
        std::vector<uint8_t> b(48);
        for (size_t i = 0; i < b.size(); ++i)
            b[i] = static_cast<uint8_t>((i * 37 + 11) & 0xFF);   // exercise all bit patterns
        const std::string text = hgrsprite::formatSpriteAsm("wolf", 3, 16, b.data());
        const auto got = hgrsprite::parseSpritesAsm(text, 48);
        assert(got.size() == 1);
        assert(got[0].name == "wolf");
        assert(got[0].bytes == b);
    }

    // ── HGR: non-catalogue geometry (2 bytes × 8 rows = 16 B) round-trips ────
    {
        std::vector<uint8_t> b(16);
        for (size_t i = 0; i < b.size(); ++i)
            b[i] = static_cast<uint8_t>(0x80 | (i * 9));         // palette bit set
        const std::string text = hgrsprite::formatSpriteAsm("tiny", 2, 8, b.data());
        const auto got = hgrsprite::parseSpritesAsm(text, 16);
        assert(got.size() == 1);
        assert(got[0].name == "tiny");
        assert(got[0].bytes == b);
    }

    // ── HGR ×2: doubled single-colour block (6×32 = 192 B) with a colour note ─
    //    round-trips — the note is documentation only and must not perturb the
    //    name (from the slot comment) or the byte payload.
    {
        std::vector<uint8_t> b(192);
        for (size_t i = 0; i < b.size(); ++i)
            b[i] = static_cast<uint8_t>(((i * 53 + 7) & 0x7F) | ((i & 3) ? 0x80 : 0));
        const std::string text = hgrsprite::formatSpriteAsm(
            "wolf_x2", 6, 32, b.data(),
            "x2 colour = Violet; doubled colour-clock, place at even x, blit SET");
        const auto got = hgrsprite::parseSpritesAsm(text, 192);
        assert(got.size() == 1);
        assert(got[0].name == "wolf_x2");
        assert(got[0].bytes == b);
    }

    // ── TMS: 16×16 (32 B native quadrant stream) + 8×8 (8 B) round-trip ──────
    {
        std::vector<uint8_t> b(32);
        for (size_t i = 0; i < b.size(); ++i)
            b[i] = static_cast<uint8_t>(255 - i * 5);
        const std::string text = tmssprite::formatSpriteAsm("knight", 32, b.data());
        const auto got = tmssprite::parseSpritesAsm(text, 32);
        assert(got.size() == 1);
        assert(got[0].name == "knight");
        assert(got[0].bytes == b);
    }
    {
        const uint8_t b[8] = { 0x18, 0x3C, 0x7E, 0xFF, 0xFF, 0x7E, 0x3C, 0x18 };
        const std::string text = tmssprite::formatSpriteAsm("gem", 8, b);
        const auto got = tmssprite::parseSpritesAsm(text, 8);
        assert(got.size() == 1);
        assert(got[0].name == "gem");
        assert(got[0].bytes == std::vector<uint8_t>(b, b + 8));
    }

    // ── Name sanitizer: ca65-safe [a-z0-9_] identifiers ──────────────────────
    assert(hgrsprite::sanitizeAsmName("My Sprite-1!") == "my_sprite_1");
    assert(hgrsprite::sanitizeAsmName("") == "sprite");            // empty fallback
    assert(hgrsprite::sanitizeAsmName("---") == "sprite");         // all-junk fallback
    assert(hgrsprite::sanitizeAsmName("9lives") == "_9lives");     // no digit-leading label
    assert(tmssprite::sanitizeAsmName("Brick Wall (v2)") == "brick_wall_v2");

    // ── Parser vs a catalogue-style file: base label, slot comments, inline
    //    comments, label-name fallback (strip "_pat"), short blocks dropped ───
    {
        const char* file =
            "; ============================================================\n"
            ".export a_pat, b_pat\n"
            ".segment \"CODE\"\n"
            "\n"
            "demo_hgr_data:\n"                        // base label, no bytes → dropped
            "; slot 01/02 of \"Demo\" row -- alpha (renamed from a)\n"
            "a_pat:\n"
            "        .byte $01, $02, $03, $04   ; rows 0..1\n"
            "        .byte $05, $06, $07, $08\n"
            "b_pat:\n"                                // no slot comment → label fallback
            "        .byte $11, $12, $13, $14\n"
            "        .byte $15, $16, $17, $18\n"
            "stub_pat:\n"                             // too short → dropped
            "        .byte $FF\n";
        const auto got = hgrsprite::parseSpritesAsm(file, 8);
        assert(got.size() == 2);
        assert(got[0].name == "alpha");               // "(renamed …)" stripped
        assert(got[0].bytes == std::vector<uint8_t>({1, 2, 3, 4, 5, 6, 7, 8}));
        assert(got[1].name == "b");                   // "_pat" stripped
        assert(got[1].bytes[0] == 0x11 && got[1].bytes[7] == 0x18);
    }

    std::printf("sprite_asm_export_smoke: OK\n");
    return 0;
}
