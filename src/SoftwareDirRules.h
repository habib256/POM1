// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// SoftwareDirRules.h — which card a shipped software directory implies.
//
// Seventh seam of the family (Apple1KeyMap / FullscreenExpand / WindowGeometry /
// StagedCardConfiguration / LayoutDecisions / PresetDecisions / ShortcutTable):
// no ImGui, no GLFW, no MainWindow.
//
// WHY THIS EXISTS. Loading a program from `software/Graphic HGR/` plugs the GEN2
// card and opens its framebuffer window; loading from `software/NET/` plugs the
// Wi-Fi modem — or resets it, if a BBS session is already up. That mapping was
// a seven-branch `else if` chain of `path.find("/X/") || path.find("\\X\\")`
// inside `performMemoryLoad`, and the file picker held the SAME mapping AGAIN,
// backwards, under a comment saying so out loud: "This is the reverse of the
// auto-enable-by-source-dir mapping in performMemoryLoad()". Two tables, one
// relation.
//
// Both directions are now one table. The rules that are easy to get wrong and
// invisible when you do are the ones this buys a test for:
//
//   - the match is on a path COMPONENT, both separators. Searching for a bare
//     substring would fire on `software/NETWORKING/`, and forgetting the
//     backslash form makes every rule silently dead on Windows.
//   - the chain was `else if`, so the FIRST rule wins on a path that carries
//     two markers. That was implicit in the source order; it is a documented
//     property here.
//   - not every rule is a picker default. microSD's content lives on the SD
//     filesystem rather than under `software/`, so it plugs the card on load but
//     must never be the folder the picker opens into — an omission from the
//     reverse table that was a comment, and is now a field.

#ifndef POM1_SOFTWARE_DIR_RULES_H
#define POM1_SOFTWARE_DIR_RULES_H

#include <cstddef>
#include <string_view>

#include "CardTypes.h"

namespace pom1::softwaredir {

struct Rule {
    /// Canonical directory name, matched as a whole path component.
    const char* directory;
    /// The card this directory's programs need.
    CardId card;
    /// Raise the window the registry gates on `card`. Not every rule does: a
    /// SID program makes noise, it does not open a panel.
    bool raiseCardWindow;
    /// Clear ROM/IO storage cards off the bus BEFORE loading, so the program
    /// lands in clean RAM. The multiplexing-Fantasy preset plugs microSD/CFFA1,
    /// whose $6000-$AFFF windows otherwise shadow a graphics program — which
    /// shows up as a black card, not as an error.
    bool evictStorageCards;
    /// Already plugged → reset the card rather than do nothing. Reloading from
    /// `software/NET/` must drop a live BBS connection and clear ACIA state, or
    /// the incoming auto-dial program starts against someone else's session.
    bool resetWhenAlreadyPlugged;
    /// This folder is what the file picker defaults into when `card` is the one
    /// content card plugged — the REVERSE direction. False for a directory
    /// whose content does not live under `software/`, and for an alias.
    bool isPickerDefault;
    /// Status line when the card is plugged (null = say nothing; GEN2 reports
    /// the storage cards it evicted instead).
    const char* pluggedMessage;
    /// Status line on the reset path. Only meaningful with resetWhenAlreadyPlugged.
    const char* resetMessage;
    float messageSeconds;
};

inline constexpr Rule kRules[] = {
    { "Graphic HGR", CardId::Gen2, true, /*evict*/ true, false, /*picker*/ true,
      nullptr, nullptr, 0.0f },
    { "SOUND SID", CardId::Sid, /*raise*/ false, false, false, true,
      "P-LAB A1-SID plugged", nullptr, 2.0f },
    { "Graphic TMS9918", CardId::Tms9918, true, false, false, true,
      "P-LAB TMS9918 plugged", nullptr, 2.0f },
    // cc65 CodeTank drop-ins. This directory stopped shipping on 2026-06-22
    // (commit 72b39a7, whose message does not mention it) — the rule is kept
    // because it still serves a user-made folder of that name. An ALIAS of the
    // row above, so it is not a picker default: two rows offering the same
    // folder would make the reverse direction ambiguous.
    { "Apple-1_TMS_CC65", CardId::Tms9918, true, false, false, /*picker*/ false,
      "P-LAB TMS9918 plugged", nullptr, 2.0f },
    // The SD card's files live on the SD filesystem, not under software/ —
    // hence no picker default.
    { "sdcard", CardId::MicroSD, false, false, false, /*picker*/ false,
      "P-LAB microSD Card plugged", nullptr, 2.0f },
    { "NET", CardId::WifiModem, true, false, /*reset*/ true, true,
      "P-LAB Wi-Fi Modem plugged", "P-LAB Wi-Fi Modem reset", 2.0f },
    { "a1io_rtc", CardId::A1IoRtc, true, false, false, true,
      "P-LAB I/O Board & RTC plugged", nullptr, 2.0f },
    { "Graphic gt-6144", CardId::Gt6144, true, /*evict*/ true, false, true,
      "SWTPC GT-6144 plugged (64x96 framebuffer at $D00A)", nullptr, 3.0f },
};

inline constexpr int kRuleCount =
    static_cast<int>(sizeof(kRules) / sizeof(kRules[0]));

/// True if `name` appears in `path` as a whole component, under either
/// separator convention — and under a MIX of the two, which is what a
/// forward-slash relative path joined onto a Windows base produces.
/// `software/Graphic HGRX/f` does not match "Graphic HGR"; that is the point of
/// requiring a separator on both sides rather than a bare substring search,
/// which would also fire on `software/NETWORKING/`.
inline constexpr bool isSeparator(char c) { return c == '/' || c == '\\'; }

inline constexpr bool pathHasComponent(std::string_view path, std::string_view name)
{
    if (name.empty()) return false;
    // i indexes the separator BEFORE the component; the one after it must exist,
    // so stop before the last position that could hold it.
    for (std::size_t i = 0; i + name.size() + 1 < path.size(); ++i) {
        if (!isSeparator(path[i])) continue;
        if (path.compare(i + 1, name.size(), name) != 0) continue;
        if (isSeparator(path[i + 1 + name.size()])) return true;
    }
    return false;
}

/// The rule a loaded file's path implies, or nullptr. FIRST match wins, in
/// table order — a path carrying two markers takes the earlier rule, which is
/// what the `else if` chain this replaces did.
inline const Rule* matchPath(std::string_view path)
{
    for (const Rule& r : kRules)
        if (pathHasComponent(path, r.directory)) return &r;
    return nullptr;
}

/// The folder the file picker should open into: the directory of the SINGLE
/// content card plugged, or nullptr when none or several are — an ambiguous
/// machine leaves the picker at the `software/` root rather than guessing.
///
/// CodeTank needs no row: it cannot exist without its TMS9918 host, so a
/// CodeTank machine already matches the TMS row.
inline const char* pickerDefaultDirectory(const CardSet& cards)
{
    const char* found = nullptr;
    for (const Rule& r : kRules) {
        if (!r.isPickerDefault || !cards.contains(r.card)) continue;
        if (found) return nullptr;          // ≥2 content cards → ambiguous
        found = r.directory;
    }
    return found;
}

} // namespace pom1::softwaredir

#endif // POM1_SOFTWARE_DIR_RULES_H
