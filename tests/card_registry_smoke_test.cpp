#include <array>
#include <cassert>
#include <cstddef>
#include <string_view>

#include "CardTypes.h"
#include "BusConflicts.h"
#include "CardTopology.h"
#include "MachineCoordinator.h"
#include "Memory.h"

namespace {

class LifecycleProbe final : public pom1::Peripheral {
public:
    std::string_view name() const override { return "lifecycle-probe"; }
};

} // namespace

int main()
{
    using namespace pom1;

    LifecycleProbe probe;
    assert(probe.lifecycleState() == PeripheralLifecycleState::Constructed);
    assert(!probe.markReset());
    assert(!probe.markActive());
    assert(probe.markAttached() && probe.markAttached());
    assert(probe.lifecycleState() == PeripheralLifecycleState::Attached);
    assert(!probe.markActive());
    assert(probe.markReset() && probe.markReset());
    assert(probe.markActive() && probe.markActive());
    assert(probe.lifecycleState() == PeripheralLifecycleState::Active);
    assert(probe.markInactive() && probe.markInactive());
    assert(probe.lifecycleState() == PeripheralLifecycleState::Reset);
    assert(probe.markActive());
    assert(probe.markReset()); // reset of an active device is a valid cycle
    probe.markDetached();
    assert(probe.lifecycleState() == PeripheralLifecycleState::Constructed);

    CardSet set;
    assert(set.empty());
    set.add(CardId::Aci).add(CardId::Tms9918);
    assert(set.contains(CardId::Aci));
    assert(set.contains(CardId::Tms9918));
    assert(!set.contains(CardId::Invalid));
    assert(set.intersects(CardSet{CardId::Tms9918}));
    set.remove(CardId::Aci);
    assert(!set.contains(CardId::Aci));

    std::array<bool, kCardCount> seen{};
    std::array<std::string_view, kCardCount> keys{};
    std::array<const CardDescriptor*, kCardCount> descriptors{};
    std::size_t descriptorCount = 0;
    std::size_t modeRowCount = 0;
    for (const Memory::CardSlot& slot : Memory::cardSlots()) {
        const CardDescriptor& descriptor = slot.descriptor;
        if (descriptor.id == CardId::Invalid) {
            ++modeRowCount;
            continue;
        }
        const auto index = static_cast<std::size_t>(descriptor.id);
        assert(index < kCardCount);
        assert(!seen[index]);
        assert(!descriptor.stableKey.empty());
        assert(!descriptor.uiLabel.empty());
        assert(descriptor.rangeCount > 0);
        assert(descriptor.rangeCount <= descriptor.ranges.size());
        assert(descriptor.capabilities != CardCapability::None);
        assert(!descriptor.dependencies.contains(descriptor.id));
        assert(!descriptor.incompatible.contains(descriptor.id));
        for (std::size_t i = 0; i < descriptor.rangeCount; ++i)
            assert(descriptor.ranges[i].first <= descriptor.ranges[i].last);
        if (slot.name) assert(descriptor.snapshotTag == slot.name);
        for (std::size_t i = 0; i < descriptorCount; ++i)
            assert(keys[i] != descriptor.stableKey);
        seen[index] = true;
        descriptors[index] = &descriptor;
        keys[descriptorCount++] = descriptor.stableKey;
    }
    assert(descriptorCount == kCardCount);
    assert(modeRowCount == 2); // cassette-audio and silicon-strict FLAGS rows
    for (bool present : seen) assert(present);

    const auto descriptor = [&](CardId id) -> const CardDescriptor& {
        const auto index = static_cast<std::size_t>(id);
        assert(index < descriptors.size());
        assert(descriptors[index]);
        return *descriptors[index];
    };

    // Incompatibility is a symmetric physical relation. Every typed conflict
    // entry must be represented by both descriptors, and descriptors may not
    // silently invent a conflict absent from the authoritative table.
    CardSet conflictsFromTable[kCardCount]{};
    for (const BusConflict& conflict : kBusConflicts) {
        assert(conflict.cardA != conflict.cardB);
        assert(!conflict.reason.empty());
        assert(descriptor(conflict.cardA).incompatible.contains(conflict.cardB));
        assert(descriptor(conflict.cardB).incompatible.contains(conflict.cardA));
        conflictsFromTable[static_cast<std::size_t>(conflict.cardA)].add(conflict.cardB);
        conflictsFromTable[static_cast<std::size_t>(conflict.cardB)].add(conflict.cardA);
    }
    for (std::size_t a = 0; a < kCardCount; ++a) {
        assert(descriptors[a]->incompatible == conflictsFromTable[a]);
        for (std::size_t b = 0; b < kCardCount; ++b) {
            const auto aId = static_cast<CardId>(a);
            const auto bId = static_cast<CardId>(b);
            assert(descriptor(aId).incompatible.contains(bId) ==
                   descriptor(bId).incompatible.contains(aId));
        }
    }

    assert(descriptor(CardId::ExtendedAci).dependencies == CardSet{CardId::Aci});
    assert(descriptor(CardId::Iec).dependencies == CardSet{CardId::MicroSD});
    assert(descriptor(CardId::CodeTank).dependencies == CardSet{CardId::Tms9918});
    for (std::size_t i = 0; i < kCardCount; ++i) {
        const auto id = static_cast<CardId>(i);
        assert(descriptor(id).dependencies == requiredCards(id));
    }
    assert(descriptor(CardId::Sid).variantGroup == "sid");
    assert(descriptor(CardId::SidSpecialEdition).variantGroup == "sid");
    assert(hasCapability(descriptor(CardId::MicroSD).capabilities,
                         CardCapability::Storage));
    assert(hasCapability(descriptor(CardId::Tms9918).capabilities,
                         CardCapability::Video));

    // Exercise the typed executor, not only the pure plan: attaching a
    // daughterboard materialises its host, and removing the host removes the
    // daughterboard in the same command.
    Memory memory;
    MachineCoordinator::setCardEnabled(memory, CardId::ExtendedAci, true);
    assert(memory.isACIEnabled() && memory.isExtendedACIEnabled());
    const auto lifecycleOf = [&](CardId id) {
        for (const Memory::CardSlot& slot : Memory::cardSlots())
            if (slot.descriptor.id == id && slot.card) return slot.card(memory);
        return static_cast<Peripheral*>(nullptr);
    };
    assert(lifecycleOf(CardId::Aci)->lifecycleState() ==
           PeripheralLifecycleState::Reset);
    MachineCoordinator::setCardEnabled(memory, CardId::Aci, false);
    assert(!memory.isACIEnabled() && !memory.isExtendedACIEnabled());
    MachineCoordinator::setCardEnabled(memory, CardId::Iec, true);
    assert(memory.isMicroSDEnabled() && memory.isIECCardEnabled());
    MachineCoordinator::setCardEnabled(memory, CardId::MicroSD, false);
    assert(!memory.isMicroSDEnabled() && !memory.isIECCardEnabled());
    assert(lifecycleOf(CardId::MicroSD)->lifecycleState() ==
           PeripheralLifecycleState::Constructed);
    MachineCoordinator::setCardEnabled(memory, CardId::CodeTank, true);
    assert(memory.isTMS9918Enabled() && memory.isCodeTankEnabled());
    MachineCoordinator::setCardEnabled(memory, CardId::Tms9918, false);
    assert(!memory.isTMS9918Enabled() && !memory.isCodeTankEnabled());
    MachineCoordinator::setCardEnabled(memory, CardId::SidSpecialEdition, true);
    MachineCoordinator::setCardEnabled(memory, CardId::Sid, true);
    assert(memory.isSIDEnabled() && !memory.isSIDSpecialEditionEnabled());
    MachineCoordinator::setCardEnabled(memory, CardId::JukeBox, true);
    assert(memory.isJukeBoxEnabled() && !memory.isSIDEnabled());
    // Cold-plug is now synchronous: reset happens before bus exposure and a
    // non-gated device is Active as soon as the transaction returns.
    assert(lifecycleOf(CardId::JukeBox)->lifecycleState() ==
           PeripheralLifecycleState::Active);
    memory.resetMemory();
    MachineCoordinator::markAttachedCardsReset(memory);
    assert(lifecycleOf(CardId::JukeBox)->lifecycleState() ==
           PeripheralLifecycleState::Reset);
    MachineCoordinator::activateResetCards(memory);
    assert(lifecycleOf(CardId::JukeBox)->lifecycleState() ==
           PeripheralLifecycleState::Active);

    // A board-configuration failure is typed and rolls back the detach phase:
    // the previously valid Juke-Box topology must remain visible, and neither
    // CodeTank nor its TMS host may leak from the rejected transaction.
    const CardSet beforeFailedTransaction = memory.enabledCards();
    CardConfigurationRequest badCodeTank;
    badCodeTank.cards.add(CardId::CodeTank);
    badCodeTank.codeTankRomPath =
        "/path/that/cannot/exist/pom1-codetank-transaction.rom";
    const CardConfigurationResult failed =
        MachineCoordinator::applyCardConfiguration(memory, badCodeTank);
    assert(!failed);
    assert(failed.code == CardConfigurationError::DeviceConfigurationFailed);
    assert(!failed.message.empty());
    assert(memory.enabledCards() == beforeFailedTransaction);
    assert(memory.isJukeBoxEnabled());
    assert(!memory.isCodeTankEnabled() && !memory.isTMS9918Enabled());

    MachineCoordinator::setCardEnabled(memory, CardId::JukeBox, false);
    MachineCoordinator::setCardEnabled(memory, CardId::Aci, true);
    memory.resetMemory();
    MachineCoordinator::markAttachedCardsReset(memory);
    MachineCoordinator::activateResetCards(memory);
    assert(lifecycleOf(CardId::Aci)->lifecycleState() ==
           PeripheralLifecycleState::Reset);
    memory.activateCassetteAudioSource();
    assert(MachineCoordinator::markCardActive(memory, CardId::Aci));
    assert(lifecycleOf(CardId::Aci)->lifecycleState() ==
           PeripheralLifecycleState::Active);
    memory.deactivateCassetteAudioSource();
    assert(MachineCoordinator::markCardInactive(memory, CardId::Aci));
}
