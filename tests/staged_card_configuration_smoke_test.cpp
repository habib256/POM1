// The UI's card-configuration transaction — pom1::StagedCardConfiguration.
//
// This is the decision half of the GUI's hardware rail, extracted so it can be
// tested without ImGui or GLFW (same seam rule as Apple1KeyMap / WindowGeometry).
// It exists because CardConfigurationRequest::cards is an ABSOLUTE target set:
// MachineCoordinator detaches every card the request does not name, so the DTO
// alone cannot tell "nothing staged" from "stage the empty machine".
//
// Getting that wrong is not theoretical — it is the BBS auto-dial regression.
// performMemoryLoad() and the DevBench commit up front to flush a preset's
// plugs before the CPU runs, and applyMachineConfig() has usually committed
// already; the unguarded repeat applied the empty topology and swept every card
// off the bus, including the Wi-Fi modem the program at $0280 was about to
// drive at $B000.
//
// Covered:
//   - a commit with nothing staged has nothing to apply (§1);
//   - the first stage seeds cards AND board options from the live machine, so
//     an amend adds to the bus instead of replacing it (§2, §3);
//   - a wholesale assignment over the seed still wins — applyMachineConfig()
//     replaces the topology rather than amending it (§4);
//   - clear() closes the transaction but preserves `mode`, the only carrier of
//     Strict-vs-Fantasy (§5);
//   - stage() is idempotent within one transaction: re-seeding would discard
//     amendments already made (§6).

#include "StagedCardConfiguration.h"

#include <cassert>
#include <cstdio>

using pom1::CardId;
using pom1::CardSet;
using pom1::CodeTankJumper;
using pom1::JukeBoxChipMode;
using pom1::JukeBoxJumper;
using pom1::MachineTopologySeed;
using pom1::StagedCardConfiguration;
using pom1::TopologyMode;

namespace {

// The live machine the BBS regression happens on: POM1 Fantasy (preset 12)
// carries the Wi-Fi modem, and it is the shipped default boot preset.
MachineTopologySeed fantasySeed()
{
    MachineTopologySeed live;
    live.cards.add(CardId::Aci);
    live.cards.add(CardId::Sid);
    live.cards.add(CardId::MicroSD);
    live.cards.add(CardId::WifiModem);
    live.cards.add(CardId::TerminalCard);
    live.cards.add(CardId::ExtendedAci);
    live.jukeBoxJumper   = JukeBoxJumper::RAM32_ROM16;
    live.jukeBoxChipMode = JukeBoxChipMode::EEPROM28C256;
    live.codeTankJumper  = CodeTankJumper::Upper16;
    return live;
}

} // namespace

int main()
{
    // -----------------------------------------------------------------
    // §1 A untouched transaction has nothing to commit.
    //
    // THE regression pin. Every load path calls the commit up front; if this
    // ever reports "pending" again, that commit applies an empty request and
    // unplugs the machine.
    // -----------------------------------------------------------------
    {
        StagedCardConfiguration staged;
        assert(!staged.pending());
        // And it stays that way across a commit that found nothing to do.
        staged.clear();
        assert(!staged.pending());
    }

    // -----------------------------------------------------------------
    // §2 The first stage seeds the target from the live card set, so an amend
    //    adds to the bus rather than replacing it.
    // -----------------------------------------------------------------
    {
        StagedCardConfiguration staged;
        staged.stage(fantasySeed()).cards.add(CardId::Tms9918);

        assert(staged.pending());
        const CardSet& target = staged.request().cards;
        assert(target.contains(CardId::Tms9918));   // the amendment
        assert(target.contains(CardId::WifiModem)); // ...and the live machine
        assert(target.contains(CardId::Aci));
        assert(target.contains(CardId::Sid));
        assert(target.contains(CardId::MicroSD));
        assert(target.contains(CardId::TerminalCard));
        assert(target.contains(CardId::ExtendedAci));
    }

    // -----------------------------------------------------------------
    // §3 Board options ride the seed too. planConfiguration() re-configures
    //    every target card, so a stale jumper in the request would revert a
    //    jumper the user set through the Hardware menu since the last commit.
    // -----------------------------------------------------------------
    {
        StagedCardConfiguration staged;
        staged.stage(fantasySeed()).cards.add(CardId::JukeBox);

        assert(staged.request().jukeBoxJumper   == JukeBoxJumper::RAM32_ROM16);
        assert(staged.request().jukeBoxChipMode == JukeBoxChipMode::EEPROM28C256);
        assert(staged.request().codeTankJumper  == CodeTankJumper::Upper16);
    }

    // -----------------------------------------------------------------
    // §4 A wholesale assignment still wins: applyMachineConfig() replaces the
    //    topology with the preset's card set, it does not amend the live one.
    // -----------------------------------------------------------------
    {
        StagedCardConfiguration staged;
        CardSet preset;                     // preset 9: TMS9918 + CodeTank
        preset.add(CardId::Tms9918);
        preset.add(CardId::CodeTank);
        staged.stage(fantasySeed()).cards = preset;

        const CardSet& target = staged.request().cards;
        assert(target.contains(CardId::Tms9918));
        assert(target.contains(CardId::CodeTank));
        assert(!target.contains(CardId::WifiModem));
        assert(!target.contains(CardId::Sid));
    }

    // -----------------------------------------------------------------
    // §5 clear() closes the transaction and neutralises the request — except
    //    `mode`, which is not part of the seed. The machine stores no topology
    //    mode, so this field is the only carrier of Strict-vs-Fantasy and
    //    resetting it would silently downgrade a Fantasy machine on the next
    //    amend (and then reject the multiplexed card set as a bus conflict).
    // -----------------------------------------------------------------
    {
        StagedCardConfiguration staged;
        auto& req = staged.stage(fantasySeed());
        req.mode = TopologyMode::Fantasy;
        req.coldReset = true;
        req.loadKrusader = true;
        req.codeTankRomPath = "roms/codetank/Codetank_ARCADE.rom";
        req.presetRamKB = 48;

        staged.clear();

        assert(!staged.pending());
        assert(staged.request().mode == TopologyMode::Fantasy);   // preserved
        assert(staged.request().cards.empty());
        assert(!staged.request().coldReset);
        assert(!staged.request().loadKrusader);
        assert(staged.request().codeTankRomPath.empty());
        assert(!staged.request().presetRamKB.has_value());

        // The next transaction re-seeds from whatever is live at that point.
        MachineTopologySeed bare;
        bare.cards.add(CardId::Aci);
        staged.stage(bare);
        assert(staged.request().cards.contains(CardId::Aci));
        assert(!staged.request().cards.contains(CardId::WifiModem));
        assert(staged.request().mode == TopologyMode::Fantasy);
    }

    // -----------------------------------------------------------------
    // §6 stage() seeds ONCE per transaction. Re-seeding on every call would
    //    throw away amendments already staged — the DevBench stages a CodeTank
    //    cart and then commits, several statements apart.
    // -----------------------------------------------------------------
    {
        StagedCardConfiguration staged;
        staged.stage(fantasySeed()).cards.remove(CardId::Sid);
        staged.stage(fantasySeed()).cards.add(CardId::Gt6144);

        const CardSet& target = staged.request().cards;
        assert(!target.contains(CardId::Sid));      // the first amendment held
        assert(target.contains(CardId::Gt6144));    // ...and the second landed
        assert(target.contains(CardId::WifiModem)); // seed still there
    }

    // -----------------------------------------------------------------
    // §7 effectiveCards() — what the UI displays. The decision that lets the
    //    sixteen mirror booleans go: while a transaction is open the staged
    //    target is what the user asked for; outside one, the machine's
    //    published set is the only truth. A second copy of the topology is
    //    what could disagree, and disagreeing copies are the family the BBS
    //    auto-dial regression came from.
    // -----------------------------------------------------------------
    {
        CardSet published;
        published.add(CardId::Aci);
        published.add(CardId::MicroSD);

        StagedCardConfiguration staged;
        // Nothing staged: the machine answers, verbatim.
        assert(staged.effectiveCards(published).contains(CardId::Aci));
        assert(staged.effectiveCards(published).contains(CardId::MicroSD));
        assert(!staged.effectiveCards(published).contains(CardId::Sid));

        // Staged plug: visible immediately, though the machine has not moved.
        MachineTopologySeed live;
        live.cards = published;
        staged.stage(live).cards.add(CardId::Sid);
        assert(staged.effectiveCards(published).contains(CardId::Sid) &&
               "a card the user just asked for must show as plugged before the "
               "transaction commits — that is what the mirrors were for");
        assert(staged.effectiveCards(published).contains(CardId::Aci) &&
               "the seed means an amend adds to the machine, it does not replace it");

        // Staged UNPLUG: equally visible, and the published set must not win.
        staged.stage(live).cards.remove(CardId::MicroSD);
        assert(!staged.effectiveCards(published).contains(CardId::MicroSD));

        // Committed (cleared): the machine is the truth again, whatever it says.
        staged.clear();
        assert(staged.effectiveCards(published).contains(CardId::MicroSD) &&
               "after the commit the UI must follow the machine, not a memory "
               "of what was asked — a refused or evicted card shows as it IS");
        assert(!staged.effectiveCards(published).contains(CardId::Sid));

        // The empty machine is a legal published value, not "no information".
        assert(staged.effectiveCards(CardSet{}).empty());
    }

    std::printf("staged_card_configuration_smoke: OK\n");
    return 0;
}
