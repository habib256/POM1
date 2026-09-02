// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// PresetFile — the external machine-preset format, and its PURE parser.
//
// WHY
//     `kMachinePresets[]` is thirteen machines POM1 ships. Everything the
//     emulator can be told about a machine — which cards, how much RAM, which
//     BASIC, which jumpers — is expressible as data, but only Arnaud can add a
//     row, because adding one means editing C++ and rebuilding. This file is
//     the other half of TODO.md's "d'application à plateforme": with the
//     scripting control channel (src/CommandPort.h) POM1 can be DRIVEN from
//     outside; with this it can be CONFIGURED from outside.
//
// WHAT IT IS NOT
//     It does not replace the built-in table and does not renumber it. The
//     thirteen shipped presets keep their indices, their `PresetId`s and every
//     pin on them (preset_ram_profiles_smoke, preset_decisions_smoke);
//     `kMachinePresets[]` is read by 57 sites and a dynamic table would be the
//     wide refactor TODO.md declines. External presets are an ADDITIVE layer
//     above `kMachinePresetCount`.
//
// PURE
//     Text in, a result and diagnostics out. No filesystem (the loader reads
//     the bytes), no `Memory`, no UI, no log sink — same seam rule as
//     MemoryImageLoader.h and PcmFile.h, and the reason `preset_file_smoke`
//     can assert on the RESULTING MACHINE rather than on the source text,
//     which is what TODO.md asked for.
//
// THE FORMAT — line-based `key = value`, deliberately not JSON
//
//     # a comment
//     pom1-preset 1               <- REQUIRED first directive; version gate
//     name = My Apple-1
//     description = TMS9918 with a full 32K
//     cards = tms9918, codetank
//     ram = 32
//     basic = applesoft-lite
//     mode = strict               <- strict (default) | fantasy
//     krusader = no
//     jukebox-jumper = ram16      <- ram16 | ram32
//     jukebox-chip = flash        <- flash | eeprom
//     codetank-jumper = lower     <- lower | upper
//     codetank-rom = roms/codetank/Codetank_ARCADE.rom
//
//     Card names are EXACTLY `--enable`'s vocabulary (doc/CLI.md), aliases
//     included, so a user who can write a command line can write a preset.
//     `preset_file_smoke` pins that agreement against the real CLI parser —
//     two spellings of the same card set is the drift this project keeps
//     catching, and a file format is the worst place to discover it.
//
// STRICTNESS
//     An unknown key or an unknown card is an ERROR, not a warning, and the
//     whole preset is rejected. `cards = tms9819` must never quietly boot a
//     different machine than the one written down; the same argument the
//     snapshot gate makes for refusing a version it does not understand
//     (SnapshotIO.h). A rejected preset carries no partial machine — `ok` is
//     false and every field stays at its default.

#ifndef POM1_PRESET_FILE_H
#define POM1_PRESET_FILE_H

#include "CardTopology.h"
#include "CardTypes.h"
#include "MachinePresets.h"

#include <string>
#include <string_view>
#include <vector>

namespace pom1 {
namespace presetfile {

/// Bumped only when a change would make an older POM1 misread a newer file.
/// A file declaring a higher version is REFUSED, never half-understood.
inline constexpr int kFormatVersion = 1;

/// Upper bound on one preset file. A machine description is a few hundred
/// bytes; anything past this is not one, and the loader checks it before
/// reading — the same order as kMaxMemoryImageBytes and for the same reason.
inline constexpr size_t kMaxPresetFileBytes = 64 * 1024;

struct Diagnostic {
    enum class Severity { Warning, Error };
    Severity    severity = Severity::Error;
    int         line     = 0;      ///< 1-based; 0 when the file as a whole is at fault
    std::string message;
};

/// One parsed machine. Owns its strings, unlike `MachineConfig`, whose members
/// are `const char*` into a static table.
struct ParsedPreset {
    bool          ok = false;
    std::string   name;
    std::string   description;
    CardSet       cards;
    bool          krusader = false;
    int           ramKB    = 8;
    BasicType     basicType = BasicType::None;
    /// The file carries this because an external preset has no `PresetId`, and
    /// `isFantasyPreset()` answers by identity. A machine that multiplexes must
    /// say so itself.
    TopologyMode  mode = TopologyMode::Strict;
    JukeBox::Jumper   jukeBoxJumper   = JukeBox::Jumper::RAM16_ROM32;
    JukeBox::ChipMode jukeBoxChipMode = JukeBox::ChipMode::Flash;
    CodeTank::Jumper  codeTankJumper  = CodeTank::Jumper::Lower16;
    std::string       codeTankRomPath;

    std::vector<Diagnostic> diagnostics;

    /// First error, or an empty string. For a caller that wants one line.
    std::string firstError() const;

    /// Project into the struct the rest of POM1 already speaks, so an external
    /// preset travels the SAME code path as a built-in one — `romProfileFor`,
    /// `CardConfigurationRequest`, `applyHeadlessConfig`. Anything else would
    /// be a second way to apply a machine, and this codebase has the scars from
    /// rules stated twice (see PresetDecisions.h).
    ///
    /// The result BORROWS `name`, `description` and `codeTankRomPath` from this
    /// object — `MachineConfig` holds `const char*` into a static table — so it
    /// must not outlive it. `layout` is empty: window placement is a shipped
    /// profile's business, and an external preset gets the default arrangement.
    MachineConfig toMachineConfig() const;
};

/// Parse `text`. `displayName` is only ever echoed in diagnostics — it is
/// never opened.
///
/// Dependencies are closed before validation (asking for `codetank` gets you
/// `tms9918`, exactly as the GUI and the CLI do), then the resulting set is
/// checked against `CardTopology` in the file's own mode. A Strict preset that
/// multiplexes is rejected whole; a Fantasy one is allowed to, which is the
/// point of saying `mode = fantasy`.
ParsedPreset parsePreset(std::string_view text, std::string_view displayName);

/// Card name → id, using `--enable`'s vocabulary including its aliases.
/// `CardId::Invalid` when the name is not one POM1 knows.
CardId cardIdFromName(std::string_view name);

/// The canonical spelling for `id` — what a written-out preset uses.
std::string_view canonicalCardName(CardId id);

/// Every name the format accepts, canonical spellings and aliases alike.
/// Exposed so a test can hold it against the CLI's own table.
const std::vector<std::string_view>& knownCardNames();

} // namespace presetfile
} // namespace pom1

#endif // POM1_PRESET_FILE_H
