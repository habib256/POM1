// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// PresetFile — implementation. Format, rationale and strictness rules are in
// PresetFile.h.

#include "PresetFile.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace pom1 {
namespace presetfile {
namespace {

struct CardName { std::string_view name; CardId id; };

// EXACTLY --enable's vocabulary (doc/CLI.md), aliases included, minus
// `krusader`: that is a ROM payload rather than a card, has no CardId, and is
// its own `krusader = yes|no` key here.
constexpr CardName kCardNames[] = {
    {"aci",          CardId::Aci},
    {"sid",          CardId::Sid},
    {"sid-se",       CardId::SidSpecialEdition},
    {"sidse",        CardId::SidSpecialEdition},
    {"microsd",      CardId::MicroSD},
    {"sdcard",       CardId::MicroSD},
    {"tms9918",      CardId::Tms9918},
    {"tms",          CardId::Tms9918},
    {"a1io-rtc",     CardId::A1IoRtc},
    {"a1io",         CardId::A1IoRtc},
    {"rtc",          CardId::A1IoRtc},
    {"hgr",          CardId::Gen2},
    {"gen2",         CardId::Gen2},
    {"cffa1",        CardId::Cffa1},
    {"cffa",         CardId::Cffa1},
    {"wifi",         CardId::WifiModem},
    {"modem",        CardId::WifiModem},
    {"terminal",     CardId::TerminalCard},
    {"jukebox",      CardId::JukeBox},
    {"codetank",     CardId::CodeTank},
    {"pr40",         CardId::Pr40},
    {"printer",      CardId::Pr40},
    {"gt6144",       CardId::Gt6144},
    {"swtpc",        CardId::Gt6144},
    {"iec",          CardId::Iec},
    {"xaci",         CardId::ExtendedAci},
    {"extended-aci", CardId::ExtendedAci},
    {"aci2",         CardId::ExtendedAci},
};

// The spelling a written-out preset uses. First entry per card in the table
// above, listed again so the mapping is explicit rather than positional.
constexpr CardName kCanonical[] = {
    {"aci", CardId::Aci},           {"sid", CardId::Sid},
    {"sid-se", CardId::SidSpecialEdition},
    {"microsd", CardId::MicroSD},   {"tms9918", CardId::Tms9918},
    {"a1io-rtc", CardId::A1IoRtc},  {"hgr", CardId::Gen2},
    {"cffa1", CardId::Cffa1},       {"wifi", CardId::WifiModem},
    {"terminal", CardId::TerminalCard}, {"jukebox", CardId::JukeBox},
    {"codetank", CardId::CodeTank}, {"pr40", CardId::Pr40},
    {"gt6144", CardId::Gt6144},     {"iec", CardId::Iec},
    {"xaci", CardId::ExtendedAci},
};

std::string toLower(std::string_view s)
{
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.remove_suffix(1);
    return s;
}

std::vector<std::string_view> splitList(std::string_view s)
{
    std::vector<std::string_view> out;
    size_t start = 0;
    while (start <= s.size()) {
        const size_t comma = s.find(',', start);
        const size_t end = comma == std::string_view::npos ? s.size() : comma;
        const std::string_view item = trim(s.substr(start, end - start));
        if (!item.empty()) out.push_back(item);
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return out;
}

void addError(ParsedPreset& p, int line, std::string message)
{
    p.diagnostics.push_back({Diagnostic::Severity::Error, line, std::move(message)});
}

bool parseBool(std::string_view v, bool& out)
{
    const std::string s = toLower(v);
    if (s == "yes" || s == "true" || s == "on"  || s == "1") { out = true;  return true; }
    if (s == "no"  || s == "false"|| s == "off" || s == "0") { out = false; return true; }
    return false;
}

/// A checked decimal accumulator, never strtol. `long` is 32-bit on Windows and
/// 64-bit elsewhere, and the memory-image loader has the scar to prove what
/// that costs: the same token truncated differently per platform.
bool parseInt(std::string_view v, int& out)
{
    if (v.empty()) return false;
    int value = 0;
    for (char c : v) {
        if (c < '0' || c > '9') return false;
        if (value > (INT32_MAX - 9) / 10) return false;
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

} // namespace

std::string ParsedPreset::firstError() const
{
    for (const Diagnostic& d : diagnostics)
        if (d.severity == Diagnostic::Severity::Error) return d.message;
    return {};
}

MachineConfig ParsedPreset::toMachineConfig() const
{
    MachineConfig cfg{};
    cfg.name        = name.c_str();
    cfg.description = description.c_str();
    cfg.cards       = cards;
    cfg.krusader    = krusader;
    cfg.ramKB       = ramKB;
    cfg.basicType   = basicType;
    cfg.jukeBox     = {jukeBoxJumper, jukeBoxChipMode};
    cfg.codeTank    = {codeTankJumper,
                       codeTankRomPath.empty() ? nullptr : codeTankRomPath.c_str()};
    cfg.layoutCount = 0;
    return cfg;
}

CardId cardIdFromName(std::string_view name)
{
    const std::string key = toLower(trim(name));
    for (const CardName& e : kCardNames)
        if (e.name == key) return e.id;
    return CardId::Invalid;
}

std::string_view canonicalCardName(CardId id)
{
    for (const CardName& e : kCanonical)
        if (e.id == id) return e.name;
    return {};
}

const std::vector<std::string_view>& knownCardNames()
{
    static const std::vector<std::string_view> names = [] {
        std::vector<std::string_view> v;
        v.reserve(sizeof(kCardNames) / sizeof(kCardNames[0]));
        for (const CardName& e : kCardNames) v.push_back(e.name);
        return v;
    }();
    return names;
}

ParsedPreset parsePreset(std::string_view text, std::string_view displayName)
{
    ParsedPreset preset;
    const std::string who = displayName.empty() ? std::string("preset")
                                                : std::string(displayName);

    if (text.size() > kMaxPresetFileBytes) {
        addError(preset, 0, who + ": preset text is larger than "
                            + std::to_string(kMaxPresetFileBytes) + " bytes");
        return preset;
    }

    bool sawVersion = false;
    bool sawName    = false;
    bool sawRam     = false;
    CardSet requested;

    int lineNo = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t nl = text.find('\n', pos);
        std::string_view raw = text.substr(pos, nl == std::string_view::npos
                                                    ? std::string_view::npos : nl - pos);
        pos = (nl == std::string_view::npos) ? text.size() + 1 : nl + 1;
        ++lineNo;

        std::string_view line = trim(raw);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;

        // The version directive is the ONE line that is not key = value, so it
        // can be recognised before anything else is trusted.
        if (line.rfind("pom1-preset", 0) == 0) {
            int version = 0;
            if (!parseInt(trim(line.substr(11)), version)) {
                addError(preset, lineNo, who + ": malformed 'pom1-preset' directive");
                return preset;
            }
            if (version > kFormatVersion) {
                addError(preset, lineNo, who + ": preset format version "
                                         + std::to_string(version)
                                         + " is newer than this POM1 understands ("
                                         + std::to_string(kFormatVersion) + ")");
                return preset;
            }
            sawVersion = true;
            continue;
        }

        if (!sawVersion) {
            addError(preset, lineNo,
                     who + ": first directive must be 'pom1-preset "
                         + std::to_string(kFormatVersion) + "'");
            return preset;
        }

        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) {
            addError(preset, lineNo, who + ": expected 'key = value'");
            continue;
        }
        const std::string key = toLower(trim(line.substr(0, eq)));
        const std::string_view value = trim(line.substr(eq + 1));

        if (key == "name") {
            preset.name = std::string(value);
            sawName = !preset.name.empty();
        } else if (key == "description") {
            preset.description = std::string(value);
        } else if (key == "cards") {
            for (std::string_view item : splitList(value)) {
                const CardId id = cardIdFromName(item);
                if (id == CardId::Invalid) {
                    addError(preset, lineNo, who + ": unknown card '"
                                             + std::string(item) + "'");
                    continue;
                }
                requested.add(id);
            }
        } else if (key == "ram") {
            int kb = 0;
            if (!parseInt(value, kb)) {
                addError(preset, lineNo, who + ": 'ram' expects a number of kilobytes");
            } else if (kb < 4 || kb > 64) {
                addError(preset, lineNo, who + ": 'ram' must be 4..64 KB, got "
                                         + std::to_string(kb));
            } else {
                preset.ramKB = kb;
                sawRam = true;
            }
        } else if (key == "basic") {
            const std::string v = toLower(value);
            if      (v == "none")           preset.basicType = BasicType::None;
            else if (v == "integer")        preset.basicType = BasicType::Integer;
            else if (v == "integer-cassette") preset.basicType = BasicType::IntegerCassette;
            else if (v == "applesoft-lite") preset.basicType = BasicType::ApplesoftLite;
            else addError(preset, lineNo, who + ": 'basic' must be none, integer, "
                                                "integer-cassette or applesoft-lite");
        } else if (key == "mode") {
            const std::string v = toLower(value);
            if      (v == "strict")  preset.mode = TopologyMode::Strict;
            else if (v == "fantasy") preset.mode = TopologyMode::Fantasy;
            else addError(preset, lineNo, who + ": 'mode' must be strict or fantasy");
        } else if (key == "krusader") {
            if (!parseBool(value, preset.krusader))
                addError(preset, lineNo, who + ": 'krusader' expects yes or no");
        } else if (key == "jukebox-jumper") {
            const std::string v = toLower(value);
            if      (v == "ram16") preset.jukeBoxJumper = JukeBox::Jumper::RAM16_ROM32;
            else if (v == "ram32") preset.jukeBoxJumper = JukeBox::Jumper::RAM32_ROM16;
            else addError(preset, lineNo, who + ": 'jukebox-jumper' must be ram16 or ram32");
        } else if (key == "jukebox-chip") {
            const std::string v = toLower(value);
            if      (v == "flash")  preset.jukeBoxChipMode = JukeBox::ChipMode::Flash;
            else if (v == "eeprom") preset.jukeBoxChipMode = JukeBox::ChipMode::EEPROM28C256;
            else addError(preset, lineNo, who + ": 'jukebox-chip' must be flash or eeprom");
        } else if (key == "codetank-jumper") {
            const std::string v = toLower(value);
            if      (v == "lower") preset.codeTankJumper = CodeTank::Jumper::Lower16;
            else if (v == "upper") preset.codeTankJumper = CodeTank::Jumper::Upper16;
            else addError(preset, lineNo, who + ": 'codetank-jumper' must be lower or upper");
        } else if (key == "codetank-rom") {
            preset.codeTankRomPath = std::string(value);
        } else {
            // An unknown key is an ERROR, not a warning: in a v1 file it is a
            // typo, and a typo that boots a different machine than the one
            // written down is exactly what a config format must not do.
            addError(preset, lineNo, who + ": unknown key '" + key + "'");
        }
    }

    if (!sawVersion) {
        addError(preset, 0, who + ": missing 'pom1-preset "
                            + std::to_string(kFormatVersion) + "' directive");
        return preset;
    }
    if (!sawName)
        addError(preset, 0, who + ": missing 'name'");
    if (!sawRam)
        addError(preset, 0, who + ": missing 'ram'");

    // Close dependencies BEFORE validating, exactly as the GUI and the CLI do:
    // asking for `codetank` gets you `tms9918`, and a preset that named only the
    // daughterboard would otherwise be rejected for a conflict it never wrote.
    CardSet closed = requested;
    for (size_t i = 0; i < kCardCount; ++i) {
        const CardId id = static_cast<CardId>(i);
        if (!requested.contains(id)) continue;
        const CardSet needs = requiredCards(id);
        for (size_t k = 0; k < kCardCount; ++k) {
            const CardId dep = static_cast<CardId>(k);
            if (needs.contains(dep)) closed.add(dep);
        }
    }
    preset.cards = closed;

    // Then the bus. A Strict preset that multiplexes is refused whole; a
    // Fantasy one is allowed to, which is the entire point of writing
    // `mode = fantasy` down.
    const ConflictList conflicts = activeConflicts(closed, preset.mode);
    for (size_t i = 0; i < conflicts.count; ++i) {
        const ActiveConflict& c = conflicts.entries[i];
        std::ostringstream oss;
        oss << who << ": " << canonicalCardName(c.cardA) << " and "
            << canonicalCardName(c.cardB)
            << " cannot share the bus (add 'mode = fantasy' if that is deliberate)";
        addError(preset, 0, oss.str());
    }

    preset.ok = preset.firstError().empty();
    if (!preset.ok) {
        // Nothing partial escapes: a rejected preset must not hand back half a
        // machine, the same all-or-nothing rule the snapshot restore follows.
        ParsedPreset rejected;
        rejected.diagnostics = std::move(preset.diagnostics);
        return rejected;
    }
    return preset;
}

} // namespace presetfile
} // namespace pom1
