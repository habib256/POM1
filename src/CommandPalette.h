// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// CommandPalette.h — matching and ranking for the command palette (F9).
//
// Eighth seam of the family (Apple1KeyMap / FullscreenExpand / WindowGeometry /
// StagedCardConfiguration / LayoutDecisions / PresetDecisions / ShortcutTable /
// SoftwareDirRules): no ImGui, no GLFW, no MainWindow.
//
// WHY A PALETTE. POM1's surfaces outnumber what a menu bar can present: 68
// registry windows, 13 machine profiles, 8 key commands, spread over eight
// menus and two submenu levels. The palette is DERIVED from those three tables
// — it holds no list of its own — so it cannot drift from what the app actually
// has, which is the same property the Windows menu already gets from
// windowRegistry(). Adding a window is still one registry row; it appears here
// for free.
//
// WHAT LIVES HERE is only the part that is a decision rather than a draw call:
// does this query match this entry, and in what order do the matches come back.
// The caller builds the Entry list from the live tables and carries out the
// choice.

#ifndef POM1_COMMAND_PALETTE_H
#define POM1_COMMAND_PALETTE_H

#include <cstddef>
#include <string_view>
#include <vector>

namespace pom1::palette {

/// Where an entry came from — the caller uses it to dispatch, and the palette
/// shows it as the row's group label.
enum class Kind {
    Window,   ///< A windowRegistry() row: toggle its show flag.
    Action,   ///< A ShortcutTable binding: run its command.
    Preset,   ///< A kMachinePresets[] row: apply that profile.
};

struct Entry {
    Kind kind = Kind::Window;
    /// What the user reads and what the query matches against.
    const char* title = "";
    /// Accelerator to show on the right, or null.
    const char* accel = nullptr;
    /// Index into whichever table `kind` names. The palette never interprets it.
    int index = 0;
    /// False when the entry is listed but cannot act yet — a card panel whose
    /// card is unplugged. Shown greyed rather than hidden, for the same reason
    /// the Windows menu shows it: hiding a window the user knows exists reads
    /// as a bug, and the flag is still their remembered intent.
    bool enabled = true;
};

/// Highest score a query can reach on one character. Kept small and integral so
/// ranking is exact — no float comparison decides row order.
inline constexpr int kBonusStart      = 15;  ///< match begins at the title's first char
inline constexpr int kBonusWordStart  = 8;   ///< match begins a word
/// Deliberately ABOVE kBonusWordStart. With it below, every character of a
/// spaced-out title counts as its own word start, so "D e b u g Console" beat
/// "Debug Console" on the query "debug" — a user typing a word means the word.
inline constexpr int kBonusContiguous = 10;  ///< match continues the previous one
inline constexpr int kPenaltyPerSkip  = 1;   ///< each unmatched char before the first hit
inline constexpr int kMaxLeadPenalty  = 12;  ///< …capped, so a long title is not doomed

inline constexpr char lowerAscii(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

/// True at the start of a word: the first character, or one that follows a
/// separator. "P-LAB Wi-Fi Modem" makes W, F and M word starts, which is what
/// lets "wfm" find it.
inline constexpr bool isWordStart(std::string_view s, std::size_t i)
{
    if (i == 0) return true;
    const char p = s[i - 1];
    return p == ' ' || p == '-' || p == '_' || p == '(' || p == '/' || p == ':'
        || p == '.' || p == ',';
}

/// Score `title` against `query`, or -1 when the query is not a subsequence of
/// it (case-insensitive). An EMPTY query scores 0 — it matches everything, so
/// an untouched palette lists the whole catalogue in declaration order.
///
/// The model is the usual fuzzy-finder one, deliberately: contiguous runs and
/// word starts are what make an acronym ("gt6" for "SWTPC GT-6144 Graphic
/// Terminal") outrank an accidental scatter of the same letters.
inline constexpr int score(std::string_view title, std::string_view query)
{
    if (query.empty()) return 0;
    if (query.size() > title.size()) return -1;

    int total = 0;
    std::size_t t = 0;
    std::size_t firstHit = title.size();
    bool prevMatched = false;

    for (std::size_t q = 0; q < query.size(); ++q) {
        const char want = lowerAscii(query[q]);
        if (want == ' ') { prevMatched = false; continue; }   // spaces only separate
        bool hit = false;
        while (t < title.size()) {
            if (lowerAscii(title[t]) == want) { hit = true; break; }
            ++t;
            prevMatched = false;
        }
        if (!hit) return -1;
        if (firstHit == title.size()) firstHit = t;
        total += 1;
        if (t == 0) total += kBonusStart;
        else if (isWordStart(title, t)) total += kBonusWordStart;
        if (prevMatched) total += kBonusContiguous;
        prevMatched = true;
        ++t;
    }

    if (firstHit < title.size()) {
        const int lead = static_cast<int>(firstHit) * kPenaltyPerSkip;
        total -= (lead < kMaxLeadPenalty) ? lead : kMaxLeadPenalty;
    }
    return total;
}

/// Indices of the entries matching `query`, best first.
///
/// Ties keep DECLARATION ORDER — the tables are ordered meaningfully (tools,
/// then cards, then tutorials; presets in their historical order), so a stable
/// sort means an empty or weak query shows the catalogue as it is written
/// rather than in an order that shifts as titles are edited.
inline std::vector<int> rank(const std::vector<Entry>& entries, std::string_view query)
{
    std::vector<int> order;
    std::vector<int> scores;
    order.reserve(entries.size());
    scores.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const int s = score(entries[i].title, query);
        if (s < 0) continue;
        order.push_back(static_cast<int>(i));
        scores.push_back(s);
    }
    // Insertion sort by descending score, stable by construction. The list is
    // ~90 entries and this runs once per keystroke; a stable_sort would do too,
    // but this keeps the header free of <algorithm> for its consumers.
    for (std::size_t i = 1; i < order.size(); ++i) {
        const int oi = order[i], si = scores[i];
        std::size_t j = i;
        while (j > 0 && scores[j - 1] < si) {
            order[j] = order[j - 1];
            scores[j] = scores[j - 1];
            --j;
        }
        order[j] = oi;
        scores[j] = si;
    }
    return order;
}

} // namespace pom1::palette

#endif // POM1_COMMAND_PALETTE_H
