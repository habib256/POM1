// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#ifndef POM1_CARD_TOPOLOGY_H
#define POM1_CARD_TOPOLOGY_H

#include <array>
#include <cstddef>
#include <string_view>

#include "BusConflicts.h"

namespace pom1 {

enum class TopologyMode {
    Strict,
    Fantasy,
};

struct ActiveConflict {
    CardId cardA = CardId::Invalid;
    CardId cardB = CardId::Invalid;
    std::string_view reason;
};

struct ConflictList {
    std::array<ActiveConflict, kBusConflicts.size() + 1> entries{};
    std::size_t count = 0;

    constexpr bool empty() const { return count == 0; }
};

struct TopologyResolution {
    CardSet accepted;
    CardSet evicted;
};

struct CardTransitionPlan {
    CardSet before;
    CardSet after;
    CardSet attach;
    CardSet detach;
};

struct TransitionPlan {
    CardSet current;
    CardSet requested;
    CardSet target;
    std::array<CardId, kCardCount> detachOrder{};
    std::array<CardId, kCardCount> configureOrder{};
    std::array<CardId, kCardCount> attachOrder{};
    std::size_t detachCount = 0;
    std::size_t configureCount = 0;
    std::size_t attachCount = 0;
    ConflictList rejectedConflicts;
    bool accepted = false;
};

/// Returns conflicts that real hardware cannot support. Fantasy mode is
/// intentionally permissive, but callers may still request Strict here to
/// diagnose what a Fantasy configuration is multiplexing.
ConflictList activeConflicts(CardSet active, TopologyMode mode);

bool wouldCreateConflict(CardSet active, CardId candidate, TopologyMode mode);

/// Deterministically reduces a set to a strict-valid topology. The survivor
/// policy preserves the historical Silicon Strict UI behaviour.
TopologyResolution resolveTopology(CardSet requested, TopologyMode mode);

/// Plans one compatibility setter operation. The requested card wins over
/// incompatible cards already present; requirements are attached recursively,
/// and removing a host also removes every dependent daughterboard.
CardTransitionPlan planCardToggle(CardSet current, CardId card, bool enabled);

/// Plans an atomic whole-configuration transition. Dependencies are closed
/// before validation. Strict conflicts reject the complete request; accepted
/// plans are ordered host-safely as detach, configure, attach.
TransitionPlan planConfiguration(CardSet current, CardSet requested,
                                 TopologyMode mode);

/// Typed dependency relation used by both transition planning and descriptor
/// consistency tests.
CardSet requiredCards(CardId card);

} // namespace pom1

#endif // POM1_CARD_TOPOLOGY_H
