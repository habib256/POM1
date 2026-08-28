// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#ifndef POM1_STAGED_CARD_CONFIGURATION_H
#define POM1_STAGED_CARD_CONFIGURATION_H

#include "MachineCoordinator.h"

namespace pom1 {

/// Live machine state a fresh transaction is seeded from. Only the fields
/// `planConfiguration()` re-applies to every target card: the card set plus the
/// board options carried by the same DTO.
struct MachineTopologySeed {
    CardSet cards;
    JukeBoxJumper jukeBoxJumper = JukeBoxJumper::RAM16_ROM32;
    JukeBoxChipMode jukeBoxChipMode = JukeBoxChipMode::Flash;
    CodeTankJumper codeTankJumper = CodeTankJumper::Lower16;
};

/// The UI's half of a card-configuration transaction: what has been staged, and
/// whether anything has been staged at all.
///
/// `CardConfigurationRequest::cards` is an ABSOLUTE target topology —
/// `MachineCoordinator` detaches every card the request does not name — so
/// "nothing staged" and "stage the empty machine" are different intents that
/// the DTO alone cannot tell apart. This type carries that distinction, and
/// with it the two rules the UI must not get wrong:
///
///  1. Committing an untouched transaction must apply NOTHING. The load paths
///     and the DevBench drain up front, and `applyMachineConfig()` has usually
///     committed already, so an unguarded repeat applied the empty topology and
///     unplugged every card. That is what silently broke BBS auto-dial: loading
///     `software/NET/bbs.*.txt` swept the Wi-Fi modem off the bus microseconds
///     before the program at $0280 started writing to the ACIA at $B000.
///
///  2. The FIRST `stage()` of a transaction seeds the target from the LIVE
///     machine, so an amend ("also plug the TMS9918", "swap in this CodeTank
///     cart") adds to what is on the bus instead of replacing it with a
///     single-card machine. A caller that means to replace the topology
///     wholesale — `applyMachineConfig()` — assigns `cards` over the seed.
///
/// Pure value logic, no UI and no `Memory`: this is the decision half of the
/// GUI rail, kept where a test can reach it (same seam rule as `Apple1KeyMap`
/// and `WindowGeometry`).
class StagedCardConfiguration {
public:
    /// Open or amend the transaction and return the request to write into.
    /// EVERY write to the staged request must come through here.
    CardConfigurationRequest& stage(const MachineTopologySeed& live)
    {
        if (!pending_) {
            request_.cards           = live.cards;
            request_.jukeBoxJumper   = live.jukeBoxJumper;
            request_.jukeBoxChipMode = live.jukeBoxChipMode;
            request_.codeTankJumper  = live.codeTankJumper;
            pending_ = true;
        }
        return request_;
    }

    /// True when a transaction is open, i.e. when there is something to commit.
    bool pending() const { return pending_; }

    /// Read-only view for the commit path (and for callers that only inspect,
    /// such as the Juke-Box memory-map eviction).
    const CardConfigurationRequest& request() const { return request_; }

    /// Close the transaction: neutral defaults, nothing staged.
    ///
    /// `mode` deliberately SURVIVES. It is not part of the seed — the machine
    /// stores no topology mode — so this field is the only carrier of "is this
    /// a Strict machine or a Fantasy one", and resetting it would silently
    /// downgrade a Fantasy machine to Strict on the next amend.
    void clear()
    {
        const TopologyMode keepMode = request_.mode;
        request_ = CardConfigurationRequest{};
        request_.mode = keepMode;
        pending_ = false;
    }

private:
    CardConfigurationRequest request_;
    bool pending_ = false;
};

} // namespace pom1

#endif // POM1_STAGED_CARD_CONFIGURATION_H
