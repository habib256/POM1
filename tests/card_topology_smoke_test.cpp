#include <cassert>
#include <cstddef>
#include <string_view>

#include "CardTopology.h"
#include "MachinePresets.h"

namespace {

std::size_t cardCount(pom1::CardSet cards)
{
    std::size_t count = 0;
    for (std::size_t i = 0; i < pom1::kCardCount; ++i)
        if (cards.contains(static_cast<pom1::CardId>(i))) ++count;
    return count;
}

pom1::CardSet dependencyClosure(pom1::CardSet cards)
{
    bool changed;
    do {
        changed = false;
        for (std::size_t i = 0; i < pom1::kCardCount; ++i) {
            const auto card = static_cast<pom1::CardId>(i);
            if (!cards.contains(card)) continue;
            const pom1::CardSet requirements = pom1::requiredCards(card);
            for (std::size_t r = 0; r < pom1::kCardCount; ++r) {
                const auto required = static_cast<pom1::CardId>(r);
                if (requirements.contains(required) && !cards.contains(required)) {
                    cards.add(required);
                    changed = true;
                }
            }
        }
    } while (changed);
    return cards;
}

void assertPlanShape(const pom1::TransitionPlan& plan)
{
    using namespace pom1;
    assert(plan.accepted);
    CardSet simulated = plan.current;
    CardSet seenDetach;
    for (std::size_t i = 0; i < plan.detachCount; ++i) {
        const CardId card = plan.detachOrder[i];
        assert(simulated.contains(card));
        assert(!plan.target.contains(card));
        assert(!seenDetach.contains(card));
        if (i > 0)
            assert(static_cast<unsigned>(plan.detachOrder[i - 1]) >
                   static_cast<unsigned>(card));
        seenDetach.add(card);
        simulated.remove(card);
    }
    CardSet seenConfigure;
    for (std::size_t i = 0; i < plan.configureCount; ++i) {
        const CardId card = plan.configureOrder[i];
        assert(plan.target.contains(card));
        assert(!seenConfigure.contains(card));
        if (i > 0)
            assert(static_cast<unsigned>(plan.configureOrder[i - 1]) <
                   static_cast<unsigned>(card));
        seenConfigure.add(card);
    }
    CardSet seenAttach;
    for (std::size_t i = 0; i < plan.attachCount; ++i) {
        const CardId card = plan.attachOrder[i];
        assert(!simulated.contains(card));
        assert(plan.target.contains(card));
        assert(!seenAttach.contains(card));
        if (i > 0)
            assert(static_cast<unsigned>(plan.attachOrder[i - 1]) <
                   static_cast<unsigned>(card));
        seenAttach.add(card);
        simulated.add(card);
    }
    assert(simulated == plan.target);
    assert(seenConfigure == plan.target);
    assert(plan.configureCount == cardCount(plan.target));
}

} // namespace

int main()
{
    using namespace pom1;

    // Golden hardware policy. This intentionally duplicates the ten physical
    // pairs and their diagnostics: a table-driven test that merely iterates
    // production data cannot detect an accidentally deleted or rewritten rule.
    constexpr std::array<BusConflict, 10> expectedBusConflicts{{
        {CardId::Gen2, CardId::A1IoRtc, "$2000-$200F overlap"},
        {CardId::SidSpecialEdition, CardId::Tms9918,
         "$CC00-$CC1F vs VDP $CC00/$CC01"},
        {CardId::Sid, CardId::SidSpecialEdition,
         "shared SID instance, two windows"},
        {CardId::MicroSD, CardId::Cffa1, "$9000-$9FFF overlap"},
        {CardId::JukeBox, CardId::Cffa1,
         "$9000-$AFDF inside $8000-$BFFF window"},
        {CardId::JukeBox, CardId::MicroSD,
         "$8000-$9FFF + $A000-$A00F inside ROM window"},
        {CardId::JukeBox, CardId::WifiModem,
         "$B000-$B003 inside ROM window"},
        {CardId::JukeBox, CardId::Sid,
         "$C800-$CFFF inside ROM window (RAM-16 jumper)"},
        {CardId::CodeTank, CardId::JukeBox, "$4000-$7FFF overlap"},
        {CardId::CodeTank, CardId::MicroSD,
         "$6000-$7FFF Applesoft Lite SD ROM overlap"},
    }};
    static_assert(expectedBusConflicts.size() == kBusConflicts.size());
    for (std::size_t i = 0; i < expectedBusConflicts.size(); ++i) {
        assert(kBusConflicts[i].cardA == expectedBusConflicts[i].cardA);
        assert(kBusConflicts[i].cardB == expectedBusConflicts[i].cardB);
        assert(kBusConflicts[i].reason == expectedBusConflicts[i].reason);
        for (std::size_t j = i + 1; j < kBusConflicts.size(); ++j) {
            const bool duplicate =
                (kBusConflicts[i].cardA == kBusConflicts[j].cardA &&
                 kBusConflicts[i].cardB == kBusConflicts[j].cardB) ||
                (kBusConflicts[i].cardA == kBusConflicts[j].cardB &&
                 kBusConflicts[i].cardB == kBusConflicts[j].cardA);
            assert(!duplicate);
        }
    }

    const CardSet sidAndTms = CardSet{CardId::Sid} | CardSet{CardId::Tms9918};
    assert(activeConflicts(sidAndTms, TopologyMode::Fantasy).empty());
    assert(activeConflicts(sidAndTms, TopologyMode::Strict).count == 1);
    assert(activeConflicts(sidAndTms, TopologyMode::Strict).entries[0].reason ==
           "$CC00/$CC01 overlap (bus priority allowed only in Fantasy)");
    assert(resolveTopology(sidAndTms, TopologyMode::Strict).accepted ==
           CardSet{CardId::Tms9918});

    CardSet crowded = CardSet{CardId::JukeBox} | CardSet{CardId::CodeTank} |
                      CardSet{CardId::MicroSD} | CardSet{CardId::Sid};
    const TopologyResolution strict = resolveTopology(crowded, TopologyMode::Strict);
    assert(!strict.accepted.contains(CardId::JukeBox));
    assert(!strict.accepted.contains(CardId::MicroSD));
    assert(strict.accepted.contains(CardId::CodeTank));
    assert(strict.accepted.contains(CardId::Sid));
    assert(activeConflicts(strict.accepted, TopologyMode::Strict).empty());
    assert(resolveTopology(strict.accepted, TopologyMode::Strict).evicted.empty());

    assert(wouldCreateConflict(CardSet{CardId::Gen2}, CardId::A1IoRtc,
                               TopologyMode::Strict));
    assert(!wouldCreateConflict(CardSet{CardId::Gen2}, CardId::A1IoRtc,
                                TopologyMode::Fantasy));
    // Candidate dependencies participate in the diagnosis: CodeTank itself
    // does not overlap SID, but its mandatory TMS9918 host does in Strict mode.
    assert(wouldCreateConflict(CardSet{CardId::Sid}, CardId::CodeTank,
                               TopologyMode::Strict));

    for (const BusConflict& conflict : kBusConflicts) {
        const CardSet pair = CardSet{conflict.cardA} | CardSet{conflict.cardB};
        assert(activeConflicts(pair, TopologyMode::Strict).count >= 1);
        assert(activeConflicts(resolveTopology(pair, TopologyMode::Strict).accepted,
                               TopologyMode::Strict).empty());
    }

    const CardTransitionPlan codeTank =
        planCardToggle(CardSet{CardId::MicroSD} | CardSet{CardId::Iec},
                       CardId::CodeTank, true);
    assert(codeTank.attach ==
           (CardSet{CardId::Tms9918} | CardSet{CardId::CodeTank}));
    assert(codeTank.detach ==
           (CardSet{CardId::MicroSD} | CardSet{CardId::Iec}));

    const CardTransitionPlan unplugHost =
        planCardToggle(CardSet{CardId::Tms9918} | CardSet{CardId::CodeTank},
                       CardId::Tms9918, false);
    assert(unplugHost.after.empty());

    const CardTransitionPlan xaci =
        planCardToggle({}, CardId::ExtendedAci, true);
    assert(xaci.attach ==
           (CardSet{CardId::Aci} | CardSet{CardId::ExtendedAci}));

    const TransitionPlan closed =
        planConfiguration({}, CardSet{CardId::CodeTank} | CardSet{CardId::ExtendedAci},
                          TopologyMode::Strict);
    assert(closed.accepted);
    assert(closed.target ==
           (CardSet{CardId::Aci} | CardSet{CardId::Tms9918} |
            CardSet{CardId::CodeTank} | CardSet{CardId::ExtendedAci}));
    assert(closed.attachCount == 4);
    assert(closed.attachOrder[0] == CardId::Aci);
    assert(closed.attachOrder[1] == CardId::Tms9918);
    assert(closed.attachOrder[2] == CardId::CodeTank);
    assert(closed.attachOrder[3] == CardId::ExtendedAci);

    const TransitionPlan rejected =
        planConfiguration({}, sidAndTms, TopologyMode::Strict);
    assert(!rejected.accepted);
    assert(rejected.rejectedConflicts.count == 1);
    assert(rejected.detachCount == 0 && rejected.attachCount == 0);
    assert(planConfiguration({}, sidAndTms, TopologyMode::Fantasy).accepted);
    assert(!planConfiguration({},
                              CardSet{CardId::CodeTank} | CardSet{CardId::Iec},
                              TopologyMode::Strict).accepted);

    const TransitionPlan removal =
        planConfiguration(closed.target, {}, TopologyMode::Strict);
    assert(removal.accepted && removal.detachCount == 4);
    assert(removal.detachOrder[0] == CardId::ExtendedAci);
    assert(removal.detachOrder[1] == CardId::CodeTank);
    assert(removal.detachOrder[2] == CardId::Tms9918);
    assert(removal.detachOrder[3] == CardId::Aci);

    // Exhaust every ordered card pair in both modes. Strict acceptance is
    // exactly the conflict status after dependency closure; Fantasy accepts
    // every pair. Re-planning an accepted target is idempotent.
    for (std::size_t a = 0; a < kCardCount; ++a) {
        for (std::size_t b = 0; b < kCardCount; ++b) {
            const CardSet requested = CardSet{static_cast<CardId>(a)} |
                                      CardSet{static_cast<CardId>(b)};
            const CardSet target = dependencyClosure(requested);
            if (a != b) {
                assert(wouldCreateConflict(CardSet{static_cast<CardId>(a)},
                                           static_cast<CardId>(b),
                                           TopologyMode::Strict) ==
                       !activeConflicts(target, TopologyMode::Strict).empty());
                assert(!wouldCreateConflict(CardSet{static_cast<CardId>(a)},
                                            static_cast<CardId>(b),
                                            TopologyMode::Fantasy));
            }
            for (TopologyMode mode : {TopologyMode::Strict, TopologyMode::Fantasy}) {
                const TransitionPlan pair = planConfiguration({}, requested, mode);
                const bool expected = mode == TopologyMode::Fantasy ||
                                      activeConflicts(target, mode).empty();
                assert(pair.accepted == expected);
                assert(pair.target == target);
                if (!expected) {
                    assert(pair.detachCount == 0 && pair.configureCount == 0 &&
                           pair.attachCount == 0);
                    continue;
                }
                assertPlanShape(pair);
                const TransitionPlan repeat = planConfiguration(target, requested, mode);
                assertPlanShape(repeat);
                assert(repeat.detachCount == 0 && repeat.attachCount == 0);
            }
        }
    }

    // 13 x 13 preset transitions, using the target preset's hardware mode.
    // This includes transitions out of Fantasy combinations into strict real
    // hardware profiles, the most demanding detach case.
    assert(kMachinePresetCount == presetIndex(PresetId::Count));
    std::string presetError;
    assert(validateMachinePresets(presetError));
    for (int from = 0; from < kMachinePresetCount; ++from) {
        for (int to = 0; to < kMachinePresetCount; ++to) {
            const TopologyMode mode = isFantasyPreset(presetIdFromIndex(to))
                ? TopologyMode::Fantasy : TopologyMode::Strict;
            const TransitionPlan preset = planConfiguration(
                kMachinePresets[from].enabledCards(),
                kMachinePresets[to].enabledCards(), mode);
            assertPlanShape(preset);
            assert(preset.target ==
                   dependencyClosure(kMachinePresets[to].enabledCards()));

            const TransitionPlan again = planConfiguration(
                kMachinePresets[from].enabledCards(),
                kMachinePresets[to].enabledCards(), mode);
            assert(again.target == preset.target);
            assert(again.detachOrder == preset.detachOrder);
            assert(again.configureOrder == preset.configureOrder);
            assert(again.attachOrder == preset.attachOrder);
            assert(again.detachCount == preset.detachCount);
            assert(again.configureCount == preset.configureCount);
            assert(again.attachCount == preset.attachCount);
        }
    }
}
