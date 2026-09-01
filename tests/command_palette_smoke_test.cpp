// The command palette's matching and ranking — pom1::palette
// (src/CommandPalette.h).
//
// Eighth seam of the family. The palette itself is DERIVED — its entries are
// rebuilt each time it opens from windowRegistry(), pom1::shortcuts::kBindings
// and kMachinePresets[], so it cannot drift from what the app has. What it does
// own is a decision: does this query match this title, and in what order do the
// matches come back. That is what is pinned here, without ImGui.
//
// Covered:
//   §1  an empty query lists everything, in declaration order;
//   §2  subsequence matching, case-insensitive, and what must NOT match;
//   §3  the ranking model — acronyms and prefixes beat scattered letters;
//   §4  ties keep declaration order (a stable sort), so a weak query shows the
//       catalogue as it is written;
//   §5  real POM1 titles: the queries a user actually types find the window
//       they mean, FIRST;
//   §6  degenerate inputs are answered, not crashed.

#include "CommandPalette.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace pom1::palette;

namespace {

Entry win(const char* title) { Entry e; e.kind = Kind::Window; e.title = title; return e; }

// A slice of the real catalogue, spelled as POM1 spells it.
std::vector<Entry> catalogue()
{
    return {
        win("Memory Viewer"),
        win("Memory Map Grid"),
        win("Memory Map Bar"),
        win("CPU Debug Console"),
        win("Apple-1 Cassette Deck"),
        win("Uncle Bernie's GEN2 HGR Graphic Card"),
        win("P-LAB Graphic Card (TMS9918)"),
        win("SWTPC GT-6144 Graphic Terminal"),
        win("P-LAB Wi-Fi Modem"),
        win("P-LAB Juke-Box"),
        win("Tutorial: Integer BASIC"),
        win("Silicon Strict Inspector"),
    };
}

int indexOf(const std::vector<Entry>& es, const char* title)
{
    for (int i = 0; i < static_cast<int>(es.size()); ++i)
        if (std::strcmp(es[static_cast<size_t>(i)].title, title) == 0) return i;
    assert(false && "title not in the fixture");
    return -1;
}

// The entry the palette would run if the user pressed Enter right now.
const char* topHit(const std::vector<Entry>& es, const char* query)
{
    const std::vector<int> r = rank(es, query);
    return r.empty() ? nullptr : es[static_cast<size_t>(r[0])].title;
}

} // namespace

int main()
{
    const std::vector<Entry> es = catalogue();

    // -----------------------------------------------------------------
    // §1 An empty query lists everything, unshuffled.
    //
    // The palette opens with an empty box, and what it shows then is the
    // catalogue — tools, then cards, then tutorials, in the order the tables
    // are written. A ranking that reordered it would make the resting state
    // look arbitrary.
    // -----------------------------------------------------------------
    {
        const std::vector<int> r = rank(es, "");
        assert(r.size() == es.size());
        for (int i = 0; i < static_cast<int>(r.size()); ++i)
            assert(r[static_cast<size_t>(i)] == i);
        assert(score("anything at all", "") == 0);
    }

    // -----------------------------------------------------------------
    // §2 Subsequence matching, and its limits.
    // -----------------------------------------------------------------
    {
        assert(score("Memory Viewer", "mv") >= 0 && "an acronym is a subsequence");
        assert(score("Memory Viewer", "MEMORY") >= 0 && "case-insensitive");
        assert(score("Memory Viewer", "memory viewer") >= 0 && "the whole title");
        assert(score("Memory Viewer", "mem view") >= 0 && "a space just separates");

        assert(score("Memory Viewer", "zz") < 0);
        assert(score("Memory Viewer", "vm") < 0 &&
               "order matters — a subsequence is not a bag of letters");
        assert(score("Mem", "memory") < 0 && "a query longer than the title cannot match");
    }

    // -----------------------------------------------------------------
    // §3 The ranking model.
    //
    // The point of scoring at all: "gt6" must find the GT-6144, not whatever
    // else happens to contain g, t and 6 in that order.
    // -----------------------------------------------------------------
    {
        // A prefix beats a match buried inside.
        assert(score("Memory Viewer", "mem") > score("Silicon Memory", "mem"));
        // Word starts beat mid-word letters. (Note the model's ordering: a match
        // on the title's very FIRST character outranks even a run of word
        // starts — "Warm Fifo Memory" scores above "P-LAB Wi-Fi Modem" on
        // "wfm", which is intended. That is why this case compares against a
        // title whose letters sit inside words, not against another
        // word-start match.)
        assert(score("P-LAB Wi-Fi Modem", "wfm") > score("Answer Software Diagram", "wfm"));
        // Contiguous beats split.
        assert(score("Debug Console", "debug") > score("D e b u g Console", "debug"));
        // A longer match scores higher than a shorter one on the same title.
        assert(score("Memory Viewer", "memory") > score("Memory Viewer", "mem"));
    }

    // -----------------------------------------------------------------
    // §4 Ties keep declaration order.
    // -----------------------------------------------------------------
    {
        // "Memory Map Grid" and "Memory Map Bar" score identically on "memorymap";
        // the one declared first must stay first, so the list does not reshuffle
        // itself as titles are edited elsewhere.
        const std::vector<int> r = rank(es, "memorymap");
        assert(r.size() >= 2);
        assert(r[0] == indexOf(es, "Memory Map Grid"));
        assert(r[1] == indexOf(es, "Memory Map Bar"));
    }

    // -----------------------------------------------------------------
    // §5 What a user actually types.
    //
    // These are the queries the palette exists for. Each must put the intended
    // entry FIRST — being somewhere in the list is not the same as being the
    // row Enter runs.
    // -----------------------------------------------------------------
    {
        struct { const char* query; const char* wanted; } cases[] = {
            { "gt6",       "SWTPC GT-6144 Graphic Terminal" },
            { "cassette",  "Apple-1 Cassette Deck" },
            { "wifi",      "P-LAB Wi-Fi Modem" },
            { "juke",      "P-LAB Juke-Box" },
            { "tms",       "P-LAB Graphic Card (TMS9918)" },
            { "gen2",      "Uncle Bernie's GEN2 HGR Graphic Card" },
            { "debug",     "CPU Debug Console" },
            { "silicon",   "Silicon Strict Inspector" },
            { "tutorial",  "Tutorial: Integer BASIC" },
        };
        for (const auto& c : cases) {
            const char* got = topHit(es, c.query);
            if (!got || std::strcmp(got, c.wanted) != 0) {
                std::printf("FAIL query \"%s\": wanted \"%s\", got \"%s\"\n",
                            c.query, c.wanted, got ? got : "(no match)");
                assert(false && "a query a user would type must rank its target first");
            }
        }
    }

    // -----------------------------------------------------------------
    // §6 Degenerate inputs.
    //
    // The query is a live text field: it is empty, then one character, then
    // something nonsensical, on the way to every real search.
    // -----------------------------------------------------------------
    {
        assert(score("", "") == 0);
        assert(score("", "a") < 0);
        assert(rank({}, "anything").empty());
        assert(rank(es, "qqqqqqqq").empty());
        assert(!rank(es, "m").empty() && "a single character still searches");
        // A query of only spaces matches everything: the separators carry no
        // requirement of their own.
        assert(rank(es, "   ").size() == es.size());
    }

    std::printf("command_palette_smoke: OK\n");
    return 0;
}
