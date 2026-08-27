// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "CardTopology.h"

namespace pom1 {
namespace {

constexpr BusConflict kStrictPriorityConflict{
    CardId::Sid, CardId::Tms9918,
    "$CC00/$CC01 overlap (bus priority allowed only in Fantasy)"};

constexpr CardId loser(const BusConflict& conflict)
{
    // These choices preserve the established inspector's auto-eviction policy.
    if ((conflict.cardA == CardId::Gen2 && conflict.cardB == CardId::A1IoRtc) ||
        (conflict.cardB == CardId::Gen2 && conflict.cardA == CardId::A1IoRtc))
        return CardId::A1IoRtc;
    if (conflict.cardA == CardId::JukeBox || conflict.cardB == CardId::JukeBox)
        return CardId::JukeBox;
    if (conflict.cardA == CardId::SidSpecialEdition ||
        conflict.cardB == CardId::SidSpecialEdition)
        return CardId::SidSpecialEdition;
    if ((conflict.cardA == CardId::Sid && conflict.cardB == CardId::Tms9918) ||
        (conflict.cardB == CardId::Sid && conflict.cardA == CardId::Tms9918))
        return CardId::Sid;
    return conflict.cardB;
}

constexpr bool active(CardSet cards, const BusConflict& conflict)
{
    return cards.contains(conflict.cardA) && cards.contains(conflict.cardB);
}

void detachWithDependents(CardSet& cards, CardId card)
{
    if (!cards.contains(card)) return;
    cards.remove(card);
    for (std::size_t i = 0; i < kCardCount; ++i) {
        const auto candidate = static_cast<CardId>(i);
        if (cards.contains(candidate) && requiredCards(candidate).contains(card))
            detachWithDependents(cards, candidate);
    }
}

void attachWithPolicy(CardSet& cards, CardId card)
{
    const CardSet requirements = requiredCards(card);
    for (std::size_t i = 0; i < kCardCount; ++i) {
        const auto requirement = static_cast<CardId>(i);
        if (requirements.contains(requirement)) attachWithPolicy(cards, requirement);
    }
    for (const BusConflict& conflict : kBusConflicts) {
        if (conflict.cardA == card) detachWithDependents(cards, conflict.cardB);
        if (conflict.cardB == card) detachWithDependents(cards, conflict.cardA);
    }
    cards.add(card);
}

void closeDependencies(CardSet& cards)
{
    bool changed;
    do {
        changed = false;
        for (std::size_t i = 0; i < kCardCount; ++i) {
            const auto card = static_cast<CardId>(i);
            if (!cards.contains(card)) continue;
            const CardSet requirements = requiredCards(card);
            for (std::size_t r = 0; r < kCardCount; ++r) {
                const auto requirement = static_cast<CardId>(r);
                if (requirements.contains(requirement) && !cards.contains(requirement)) {
                    cards.add(requirement);
                    changed = true;
                }
            }
        }
    } while (changed);
}

} // namespace

CardSet requiredCards(CardId card)
{
    switch (card) {
    case CardId::ExtendedAci: return CardSet{CardId::Aci};
    case CardId::CodeTank: return CardSet{CardId::Tms9918};
    case CardId::Iec: return CardSet{CardId::MicroSD};
    default: return {};
    }
}

ConflictList activeConflicts(CardSet cards, TopologyMode mode)
{
    ConflictList out;
    if (mode == TopologyMode::Fantasy) return out;
    for (const BusConflict& conflict : kBusConflicts) {
        if (active(cards, conflict))
            out.entries[out.count++] = {conflict.cardA, conflict.cardB, conflict.reason};
    }
    if (active(cards, kStrictPriorityConflict)) {
        out.entries[out.count++] = {kStrictPriorityConflict.cardA,
                                    kStrictPriorityConflict.cardB,
                                    kStrictPriorityConflict.reason};
    }
    return out;
}

bool wouldCreateConflict(CardSet cards, CardId candidate, TopologyMode mode)
{
    if (mode == TopologyMode::Fantasy || cards.contains(candidate)) return false;
    cards.add(candidate);
    closeDependencies(cards);
    return !activeConflicts(cards, mode).empty();
}

TopologyResolution resolveTopology(CardSet requested, TopologyMode mode)
{
    TopologyResolution out{requested, {}};
    if (mode == TopologyMode::Fantasy) return out;

    // Iterate because evicting one endpoint can retire several conflicts.
    for (;;) {
        const ConflictList conflicts = activeConflicts(out.accepted, mode);
        if (conflicts.empty()) break;
        const ActiveConflict& first = conflicts.entries[0];
        const BusConflict conflict{first.cardA, first.cardB, first.reason};
        const CardId evicted = loser(conflict);
        out.accepted.remove(evicted);
        out.evicted.add(evicted);
    }
    return out;
}

CardTransitionPlan planCardToggle(CardSet current, CardId card, bool enabled)
{
    CardTransitionPlan plan{current, current, {}, {}};
    if (enabled)
        attachWithPolicy(plan.after, card);
    else
        detachWithDependents(plan.after, card);

    for (std::size_t i = 0; i < kCardCount; ++i) {
        const auto id = static_cast<CardId>(i);
        if (plan.before.contains(id) && !plan.after.contains(id)) plan.detach.add(id);
        if (!plan.before.contains(id) && plan.after.contains(id)) plan.attach.add(id);
    }
    return plan;
}

TransitionPlan planConfiguration(CardSet current, CardSet requested,
                                 TopologyMode mode)
{
    TransitionPlan plan;
    plan.current = current;
    plan.requested = requested;
    plan.target = requested;
    closeDependencies(plan.target);
    plan.rejectedConflicts = activeConflicts(plan.target, mode);
    if (!plan.rejectedConflicts.empty()) return plan;

    // Reverse identity order removes daughterboards before hosts.
    for (std::size_t i = kCardCount; i-- > 0;) {
        const auto card = static_cast<CardId>(i);
        if (current.contains(card) && !plan.target.contains(card))
            plan.detachOrder[plan.detachCount++] = card;
    }
    // Configure every target deterministically, even when already attached:
    // options such as jumpers/chip mode may have changed independently.
    for (std::size_t i = 0; i < kCardCount; ++i) {
        const auto card = static_cast<CardId>(i);
        if (plan.target.contains(card))
            plan.configureOrder[plan.configureCount++] = card;
    }
    // Forward identity order attaches hosts before their daughterboards.
    for (std::size_t i = 0; i < kCardCount; ++i) {
        const auto card = static_cast<CardId>(i);
        if (!current.contains(card) && plan.target.contains(card))
            plan.attachOrder[plan.attachCount++] = card;
    }
    plan.accepted = true;
    return plan;
}

} // namespace pom1
