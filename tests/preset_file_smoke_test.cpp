// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// preset_file_smoke — the external machine-preset format.
//
// TODO.md asked for "des tests sur le RÉSULTAT plutôt que sur le texte
// source", and that is what every section below does: it writes a machine
// down, parses it, and asserts on the CardSet, the RAM and the BASIC that come
// out. Nothing here inspects the parser's intermediate state, so the format can
// be re-spelled without rewriting the test.
//
// Nine sections:
//   1. a minimal preset, and what the defaults are.
//   2. the version gate: absent, malformed, and NEWER than we understand.
//   3. cards, aliases, whitespace and the empty list.
//   4. dependencies are closed before validation (codetank pulls tms9918).
//   5. the bus is checked, in the mode the FILE declares.
//   6. every scalar key round-trips into the machine.
//   7. an unknown key or card is an ERROR, and a rejected preset is INERT.
//   8. diagnostics name the line, and the size cap is refused before parsing.
//   9. the card vocabulary round-trips (its agreement with `--enable` is
//      asserted in cli_dispatcher_smoke, where that parser already links).

#include "PresetFile.h"

#include <cassert>
#include <cstdio>
#include <string>

using namespace pom1;
using namespace pom1::presetfile;

namespace {

ParsedPreset parse(const std::string& body, const char* who = "test.preset")
{
    return parsePreset(body, who);
}

/// A valid header plus whatever the section is actually testing.
std::string withHeader(const std::string& body)
{
    return "pom1-preset 1\nname = T\nram = 8\n" + body;
}

bool hasErrorContaining(const ParsedPreset& p, const std::string& needle)
{
    for (const Diagnostic& d : p.diagnostics)
        if (d.severity == Diagnostic::Severity::Error &&
            d.message.find(needle) != std::string::npos)
            return true;
    return false;
}

} // namespace

int main()
{
    // ── 1. a minimal preset ──────────────────────────────────────────────
    {
        const ParsedPreset p = parse("pom1-preset 1\nname = Bare\nram = 8\n");
        assert(p.ok);
        assert(p.name == "Bare");
        assert(p.ramKB == 8);
        assert(p.cards == CardSet{});                    // no card unless asked
        assert(p.basicType == BasicType::None);
        assert(p.mode == TopologyMode::Strict);          // strict unless asked
        assert(!p.krusader);
        assert(p.diagnostics.empty());
        // Comments and blank lines are not content.
        const ParsedPreset q = parse("# a machine\n\npom1-preset 1\n\n"
                                     "# what it is\nname = Bare\nram = 8\n\n");
        assert(q.ok && q.name == "Bare");
        std::puts("  [PASS] 1. a minimal preset, and its defaults");
    }

    // ── 2. the version gate ──────────────────────────────────────────────
    {
        // Missing entirely.
        assert(!parse("name = X\nram = 8\n").ok);
        // Present but not first: every other line is refused until it is seen,
        // so a file cannot be half-read under an assumed version.
        assert(hasErrorContaining(parse("name = X\npom1-preset 1\n"),
                                  "first directive"));
        // Malformed.
        assert(!parse("pom1-preset\nname = X\nram = 8\n").ok);
        assert(!parse("pom1-preset banana\nname = X\nram = 8\n").ok);
        // NEWER than we understand is refused, never half-understood — the rule
        // the snapshot gate follows for exactly the same reason.
        const ParsedPreset future = parse("pom1-preset 2\nname = X\nram = 8\n");
        assert(!future.ok);
        assert(hasErrorContaining(future, "newer than this POM1 understands"));
        // An OLDER version stays readable; that is what versioning is for.
        assert(parse("pom1-preset 0\nname = X\nram = 8\n").ok);
        std::puts("  [PASS] 2. the version gate");
    }

    // ── 3. cards, aliases and whitespace ─────────────────────────────────
    {
        const ParsedPreset p = parse(withHeader("cards = aci, tms9918\n"));
        assert(p.ok);
        assert(p.cards.contains(CardId::Aci));
        assert(p.cards.contains(CardId::Tms9918));
        // Aliases resolve to the same machine — the RESULT is what is asserted,
        // so the two spellings are indistinguishable here by construction.
        const ParsedPreset a = parse(withHeader("cards = gen2\n"));
        const ParsedPreset b = parse(withHeader("cards = hgr\n"));
        assert(a.ok && b.ok && a.cards == b.cards);
        assert(a.cards.contains(CardId::Gen2));
        // Case and spacing are not content either.
        const ParsedPreset messy = parse(withHeader("cards =   ACI ,, TMS  ,\n"));
        assert(messy.ok);
        assert(messy.cards.contains(CardId::Aci) && messy.cards.contains(CardId::Tms9918));
        // An empty list is a machine with no cards, not an error.
        const ParsedPreset none = parse(withHeader("cards =\n"));
        assert(none.ok && none.cards == CardSet{});
        std::puts("  [PASS] 3. cards, aliases and whitespace");
    }

    // ── 4. dependencies are closed before validation ─────────────────────
    {
        // A preset naming only the daughterboard gets its host. Without this it
        // would be rejected for a conflict it never wrote down.
        const ParsedPreset p = parse(withHeader("cards = codetank\n"));
        assert(p.ok);
        assert(p.cards.contains(CardId::CodeTank));
        assert(p.cards.contains(CardId::Tms9918));
        // Same for the two other daughter relations POM1 has.
        const ParsedPreset iec = parse(withHeader("cards = iec\n"));
        assert(iec.ok && iec.cards.contains(CardId::MicroSD));
        const ParsedPreset xaci = parse(withHeader("cards = xaci\n"));
        assert(xaci.ok && xaci.cards.contains(CardId::Aci));
        std::puts("  [PASS] 4. dependencies are closed before validation");
    }

    // ── 5. the bus, in the mode the FILE declares ────────────────────────
    {
        // microSD and CFFA1 both decode $9000. Strict refuses.
        const ParsedPreset strict = parse(withHeader("cards = microsd, cffa1\n"));
        assert(!strict.ok);
        assert(hasErrorContaining(strict, "cannot share the bus"));
        // …and says how to mean it on purpose.
        assert(hasErrorContaining(strict, "mode = fantasy"));
        // Fantasy permits it: that is what the POM1 Multiplexing presets are.
        const ParsedPreset fantasy = parse(withHeader("mode = fantasy\n"
                                                      "cards = microsd, cffa1\n"));
        assert(fantasy.ok);
        assert(fantasy.cards.contains(CardId::MicroSD));
        assert(fantasy.cards.contains(CardId::Cffa1));
        assert(fantasy.mode == TopologyMode::Fantasy);
        // The mode is carried by the FILE because an external preset has no
        // PresetId, and isFantasyPreset() answers by identity.
        std::puts("  [PASS] 5. the bus is checked in the file's own mode");
    }

    // ── 6. every scalar key reaches the machine ──────────────────────────
    {
        const ParsedPreset p = parse(
            "pom1-preset 1\n"
            "name = Full House\n"
            "description = every knob at once\n"
            "ram = 48\n"
            "basic = applesoft-lite\n"
            "krusader = yes\n"
            "mode = fantasy\n"
            "jukebox-jumper = ram32\n"
            "jukebox-chip = eeprom\n"
            "codetank-jumper = upper\n"
            "codetank-rom = roms/codetank/Codetank_DEMOS.rom\n");
        assert(p.ok);
        assert(p.name == "Full House");
        assert(p.description == "every knob at once");
        assert(p.ramKB == 48);
        assert(p.basicType == BasicType::ApplesoftLite);
        assert(p.krusader);
        assert(p.mode == TopologyMode::Fantasy);
        assert(p.jukeBoxJumper == JukeBox::Jumper::RAM32_ROM16);
        assert(p.jukeBoxChipMode == JukeBox::ChipMode::EEPROM28C256);
        assert(p.codeTankJumper == CodeTank::Jumper::Upper16);
        assert(p.codeTankRomPath == "roms/codetank/Codetank_DEMOS.rom");
        // Every BASIC spelling.
        assert(parse(withHeader("basic = none\n")).basicType == BasicType::None);
        assert(parse(withHeader("basic = integer\n")).basicType == BasicType::Integer);
        assert(parse(withHeader("basic = integer-cassette\n")).basicType
               == BasicType::IntegerCassette);
        // Booleans in every accepted spelling, and a refused one.
        assert(parse(withHeader("krusader = TRUE\n")).krusader);
        assert(!parse(withHeader("krusader = off\n")).krusader);
        assert(!parse(withHeader("krusader = maybe\n")).ok);
        std::puts("  [PASS] 6. every scalar key reaches the machine");
    }

    // ── 7. strictness, and a rejected preset is inert ────────────────────
    {
        // A typo in a key must not boot a different machine than the one
        // written down, so it is an error rather than a shrug.
        const ParsedPreset badKey = parse(withHeader("cardz = aci\n"));
        assert(!badKey.ok);
        assert(hasErrorContaining(badKey, "unknown key"));
        // Same for a card name one letter out.
        const ParsedPreset badCard = parse(withHeader("cards = tms9819\n"));
        assert(!badCard.ok);
        assert(hasErrorContaining(badCard, "unknown card"));
        // A rejected preset carries NO machine — not a partial one. This is the
        // all-or-nothing rule the snapshot restore follows, and the reason a
        // caller can use `ok` alone.
        assert(badCard.cards == CardSet{});
        assert(badCard.name.empty());
        assert(badCard.ramKB == 8);
        assert(!badCard.diagnostics.empty());
        // Required keys.
        assert(hasErrorContaining(parse("pom1-preset 1\nram = 8\n"), "missing 'name'"));
        assert(hasErrorContaining(parse("pom1-preset 1\nname = X\n"), "missing 'ram'"));
        // RAM bounds are refused, never clamped.
        assert(!parse(withHeader("ram = 0\n")).ok);
        assert(!parse("pom1-preset 1\nname = X\nram = 65\n").ok);
        assert(!parse("pom1-preset 1\nname = X\nram = -8\n").ok);
        assert(!parse("pom1-preset 1\nname = X\nram = 99999999999\n").ok);
        std::puts("  [PASS] 7. unknown keys and cards are errors; rejection is inert");
    }

    // ── 8. diagnostics, and the size cap ─────────────────────────────────
    {
        const ParsedPreset p = parse("pom1-preset 1\nname = X\nram = 8\nnope = 1\n");
        assert(!p.ok);
        bool located = false;
        for (const Diagnostic& d : p.diagnostics)
            if (d.message.find("unknown key") != std::string::npos && d.line == 4)
                located = true;
        assert(located);              // the LINE, so a user can find it
        // Diagnostics name the file, so a loader reading a directory can report
        // which one is wrong without composing the message itself.
        assert(hasErrorContaining(p, "test.preset"));
        // Past the cap, refused before anything is parsed.
        const std::string huge(kMaxPresetFileBytes + 1, 'x');
        const ParsedPreset big = parse(huge);
        assert(!big.ok);
        assert(hasErrorContaining(big, "larger than"));
        // …and exactly at the cap it is merely a bad preset, not a refused one.
        std::string atCap = "pom1-preset 1\nname = X\nram = 8\n";
        atCap.append(kMaxPresetFileBytes - atCap.size(), '\n');
        assert(parse(atCap).ok);
        std::puts("  [PASS] 8. diagnostics carry file and line; the cap is checked first");
    }

    // ── 9. the vocabulary round-trips ────────────────────────────────────
    {
        // Every name the format accepts resolves, and every card's canonical
        // spelling resolves back to it — so a preset POM1 writes out is a
        // preset POM1 can read.
        for (std::string_view name : knownCardNames()) {
            const CardId id = cardIdFromName(name);
            assert(id != CardId::Invalid);
            assert(!canonicalCardName(id).empty());
            assert(cardIdFromName(canonicalCardName(id)) == id);
        }
        // Krusader is deliberately absent: it is a ROM payload, has no CardId,
        // and is its own `krusader = yes|no` key.
        assert(cardIdFromName("krusader") == CardId::Invalid);
        assert(cardIdFromName("") == CardId::Invalid);
        assert(cardIdFromName("nosuchcard") == CardId::Invalid);
        // That this vocabulary IS `--enable`'s is asserted in
        // cli_dispatcher_smoke, where the real CLI parser is already linked —
        // two spellings of the same card set is the drift this project keeps
        // catching, and a format is the worst place to find it.
        std::puts("  [PASS] 9. the card vocabulary round-trips");
    }

    std::puts("preset_file_smoke: all sections passed");
    return 0;
}
