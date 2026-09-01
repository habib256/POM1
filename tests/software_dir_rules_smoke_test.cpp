// Which card a shipped software directory implies — pom1::softwaredir
// (src/SoftwareDirRules.h).
//
// Seventh seam of the family. Loading from `software/Graphic HGR/` plugs the
// GEN2 card and opens its framebuffer; loading from `software/NET/` plugs the
// Wi-Fi modem, or resets it when a BBS session is already up. That mapping was a
// seven-branch `else if` chain of `path.find("/X/") || path.find("\\X\\")`, and
// the file picker held the SAME mapping again, backwards, under a comment
// saying so: "This is the reverse of the auto-enable-by-source-dir mapping in
// performMemoryLoad()".
//
// Covered:
//   §1  every rule is usable, and each card appears once as a picker default;
//   §2  the match is on a path COMPONENT, both separators — the part that is
//       invisible when wrong (a bare substring fires on NETWORKING/; a missing
//       backslash form makes every rule dead on Windows);
//   §3  every shipped directory resolves to its card;
//   §4  first match wins on a path carrying two markers;
//   §5  the reverse direction: one content card names its folder, none or
//       several name nothing;
//   §6  the rules with consequences — evict-before-load, reset-on-reload — are
//       the ones that carry them, and no others.
//
// Links nothing at all: the table is a header of constexpr data.

#include "SoftwareDirRules.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace pom1::softwaredir;
using pom1::CardId;
using pom1::CardSet;

int main()
{
    assert(kRuleCount == 8);

    // -----------------------------------------------------------------
    // §1 Every rule is usable.
    // -----------------------------------------------------------------
    {
        for (const Rule& r : kRules) {
            assert(r.directory && r.directory[0]);
            assert(r.card != CardId::Invalid);
            // A message with no duration is a status line that never clears.
            if (r.pluggedMessage || r.resetMessage) assert(r.messageSeconds > 0.0f);
            // A reset message only means something on the reset path.
            if (r.resetMessage) assert(r.resetWhenAlreadyPlugged);
            // No directory is listed twice — the second row would be dead.
            int same = 0;
            for (const Rule& o : kRules)
                if (std::strcmp(o.directory, r.directory) == 0) ++same;
            assert(same == 1);
        }
        // A card may have several directories (the TMS9918 has its own plus the
        // cc65 CodeTank drop-in folder), but only ONE of them can be the picker
        // default — otherwise the reverse direction is ambiguous by construction.
        for (const Rule& r : kRules) {
            int defaults = 0;
            for (const Rule& o : kRules)
                if (o.card == r.card && o.isPickerDefault) ++defaults;
            assert(defaults <= 1);
        }
    }

    // -----------------------------------------------------------------
    // §2 Component matching, both separators, and mixed.
    // -----------------------------------------------------------------
    {
        assert(pathHasComponent("software/NET/bbs.txt", "NET"));
        assert(pathHasComponent("software\\NET\\bbs.txt", "NET"));
        // A forward-slash relative path joined onto a Windows base.
        assert(pathHasComponent("C:\\pom1\\software/NET\\bbs.txt", "NET"));

        // The two failures a bare substring search would produce.
        assert(!pathHasComponent("software/NETWORKING/x.txt", "NET") &&
               "a prefix is not a component");
        assert(!pathHasComponent("software/SUBNET/x.txt", "NET") &&
               "a suffix is not one either");

        // Separators are required on BOTH sides: a path that ENDS at the
        // directory names no file, so nothing is being loaded from it.
        assert(!pathHasComponent("software/NET", "NET"));
        assert(!pathHasComponent("NET/bbs.txt", "NET") &&
               "no leading separator: this is a relative path from inside it");

        assert(!pathHasComponent("", "NET"));
        assert(!pathHasComponent("software/NET/x", ""));

        // Spaces are ordinary characters — most of the shipped folders have one.
        assert(pathHasComponent("software/Graphic HGR/demo.txt", "Graphic HGR"));
        assert(!pathHasComponent("software/Graphic HGRX/demo.txt", "Graphic HGR"));
    }

    // -----------------------------------------------------------------
    // §3 Every shipped directory resolves to its card.
    // -----------------------------------------------------------------
    {
        struct { const char* path; CardId card; } cases[] = {
            { "software/Graphic HGR/A1-CrazyCycle.txt",    CardId::Gen2 },
            { "software/SOUND SID/tune.txt",               CardId::Sid },
            { "software/Graphic TMS9918/sprites.txt",      CardId::Tms9918 },
            { "software/Apple-1_TMS_CC65/demo.txt",        CardId::Tms9918 },
            // Absolute, like every path the picker hands over: the marker
            // needs a separator on both sides, so a path that STARTS at the
            // directory is a relative path from inside it, not a load from it.
            { "/Users/dev/pom1/sdcard/NONO/HGR/x",         CardId::MicroSD },
            { "software/NET/bbs.wifi.txt",                 CardId::WifiModem },
            { "software/a1io_rtc/clock.txt",               CardId::A1IoRtc },
            { "software/Graphic gt-6144/plot.txt",         CardId::Gt6144 },
        };
        for (const auto& c : cases) {
            const Rule* r = matchPath(c.path);
            assert(r && r->card == c.card);
        }
        // Everything else plugs nothing. Loading a plain Apple-1 program must
        // not rearrange the machine.
        assert(matchPath("software/games_chess/Chess.txt") == nullptr);
        assert(matchPath("cassettes/BASIC.aci") == nullptr);
        assert(matchPath("") == nullptr);
    }

    // -----------------------------------------------------------------
    // §4 First match wins.
    //
    // The chain this replaces was `else if`, so table order decided a path
    // carrying two markers. That was implicit in the source; here it is a
    // property with a test.
    // -----------------------------------------------------------------
    {
        const Rule* r = matchPath("software/Graphic HGR/NET/odd.txt");
        assert(r && r->card == CardId::Gen2 &&
               "Graphic HGR precedes NET in the table");
        // Same path, markers swapped: still the earlier RULE, not the earlier
        // path component.
        const Rule* r2 = matchPath("software/NET/Graphic HGR/odd.txt");
        assert(r2 && r2->card == CardId::Gen2);
    }

    // -----------------------------------------------------------------
    // §5 The reverse direction.
    // -----------------------------------------------------------------
    {
        assert(pickerDefaultDirectory(CardSet{}) == nullptr);

        CardSet one;
        one.add(CardId::Gen2);
        const char* d = pickerDefaultDirectory(one);
        assert(d && std::strcmp(d, "Graphic HGR") == 0);

        // A machine with two content cards is ambiguous: the picker stays at the
        // software/ root rather than guessing which one the user meant.
        CardSet two = one;
        two.add(CardId::Sid);
        assert(pickerDefaultDirectory(two) == nullptr);

        // A card with no directory of its own contributes nothing either way.
        CardSet withAci = one;
        withAci.add(CardId::Aci);
        const char* d2 = pickerDefaultDirectory(withAci);
        assert(d2 && std::strcmp(d2, "Graphic HGR") == 0);

        // microSD plugs on load but is NOT a picker default — its files live on
        // the SD filesystem, not under software/.
        CardSet sd;
        sd.add(CardId::MicroSD);
        assert(pickerDefaultDirectory(sd) == nullptr);

        // Round trip: every picker default resolves back to its own card.
        for (const Rule& r : kRules) {
            if (!r.isPickerDefault) continue;
            CardSet only;
            only.add(r.card);
            const char* back = pickerDefaultDirectory(only);
            assert(back && std::strcmp(back, r.directory) == 0);
            const Rule* fwd = matchPath(std::string("software/") + r.directory + "/x.txt");
            assert(fwd && fwd->card == r.card);
        }
    }

    // -----------------------------------------------------------------
    // §6 The consequences sit on the rules that want them.
    // -----------------------------------------------------------------
    {
        // Evicting storage before a load is for the two GRAPHICS cards: the
        // Fantasy preset plugs microSD/CFFA1, whose $6000-$AFFF windows shadow
        // the program and show up as a black card rather than an error.
        for (const Rule& r : kRules) {
            const bool graphics = (r.card == CardId::Gen2 || r.card == CardId::Gt6144);
            assert(r.evictStorageCards == graphics);
        }
        // Only a reload from software/NET/ resets its card — dropping a live BBS
        // connection is not something to do to a machine that did not ask.
        for (const Rule& r : kRules)
            assert(r.resetWhenAlreadyPlugged == (r.card == CardId::WifiModem));

        // A SID program makes noise; it opens no panel.
        const Rule* sid = matchPath("software/SOUND SID/tune.txt");
        assert(sid && !sid->raiseCardWindow);
        // The graphics and device panels do open — that is the whole point of
        // "open a file from this folder" on the Fantasy preset, which leaves
        // those cards unplugged.
        for (const char* p : {"software/Graphic HGR/x", "software/Graphic TMS9918/x",
                              "software/NET/x", "software/a1io_rtc/x",
                              "software/Graphic gt-6144/x"}) {
            const Rule* r = matchPath(p);
            assert(r && r->raiseCardWindow);
        }
    }

    std::printf("software_dir_rules_smoke: OK\n");
    return 0;
}
