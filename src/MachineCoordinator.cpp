// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "MachineCoordinator.h"

#include "Memory.h"
#include "RomLoader.h"

namespace pom1 {
namespace {

Peripheral* peripheralForCard(Memory& memory, CardId card)
{
    for (const Memory::CardSlot& slot : Memory::cardSlots()) {
        if (slot.descriptor.id == card)
            if (slot.card) return slot.card(memory);
    }
    // Descriptor-only identities share a concrete lifecycle participant.
    if (card == CardId::ExtendedAci)
        return peripheralForCard(memory, CardId::Aci);
    if (card == CardId::SidSpecialEdition)
        return peripheralForCard(memory, CardId::Sid);
    return nullptr;
}

void setCard(Memory& memory, CardId card, bool enabled)
{
    Peripheral* peripheral = peripheralForCard(memory, card);
    // Initialise while the device is still unreachable by the CPU and audio
    // callback. This replaces the GUI's timing-dependent warm-up period with
    // an explicit cold-plug transaction.
    if (enabled && peripheral) peripheral->reset();

    switch (card) {
    case CardId::Aci: memory.setACIEnabled(enabled); break;
    case CardId::Tms9918: memory.setTMS9918Enabled(enabled); break;
    case CardId::Sid: memory.setSIDEnabled(enabled); break;
    case CardId::SidSpecialEdition: memory.setSIDSpecialEditionEnabled(enabled); break;
    case CardId::MicroSD: memory.setMicroSDEnabled(enabled); break;
    case CardId::Cffa1: memory.setCFFA1Enabled(enabled); break;
    case CardId::JukeBox: memory.setJukeBoxEnabled(enabled); break;
    case CardId::CodeTank: memory.setCodeTankEnabled(enabled); break;
    case CardId::WifiModem: memory.setWiFiModemEnabled(enabled); break;
    case CardId::TerminalCard: memory.setTerminalCardEnabled(enabled); break;
    case CardId::A1IoRtc: memory.setA1IO_RTCEnabled(enabled); break;
    case CardId::Pr40: memory.setPR40Enabled(enabled); break;
    case CardId::Gt6144: memory.setGT6144Enabled(enabled); break;
    case CardId::Iec: memory.setIECCardEnabled(enabled); break;
    case CardId::Gen2: memory.setHgrFramebufferAttached(enabled); break;
    case CardId::ExtendedAci: memory.setExtendedACIEnabled(enabled); break;
    case CardId::Count:
    case CardId::Invalid: break;
    }
    if (peripheral) {
        if (enabled)
        {
            peripheral->markAttached();
            peripheral->markReset();
            // Cassette audio is a separately registered producer. All other
            // reset devices are ready as soon as their bus attachment lands.
            if ((card != CardId::Aci && card != CardId::ExtendedAci) ||
                memory.isCassetteAudioActive())
                peripheral->markActive();
        } else {
            peripheral->markDetached();
        }
    }
}

} // namespace

void MachineCoordinator::setCardEnabled(Memory& memory, CardId card, bool enabled)
{
    const CardTransitionPlan plan = planCardToggle(
        memory.enabledCards(), card, enabled);
    // Reverse identity order removes daughterboards before hosts; forward
    // order attaches hosts before daughterboards.
    for (std::size_t i = kCardCount; i-- > 0;) {
        const auto id = static_cast<CardId>(i);
        if (plan.detach.contains(id)) setCard(memory, id, false);
    }
    for (std::size_t i = 0; i < kCardCount; ++i) {
        const auto id = static_cast<CardId>(i);
        if (plan.attach.contains(id)) setCard(memory, id, true);
    }
}

void MachineCoordinator::markAttachedCardsReset(Memory& memory)
{
    const CardSet attached = memory.enabledCards();
    for (std::size_t i = 0; i < kCardCount; ++i) {
        const auto card = static_cast<CardId>(i);
        if (!attached.contains(card)) continue;
        if (Peripheral* peripheral = peripheralForCard(memory, card)) {
            // Covers configurations restored/constructed before lifecycle
            // tracking began, while remaining idempotent for normal attaches.
            if (peripheral->lifecycleState() == PeripheralLifecycleState::Constructed)
                peripheral->markAttached();
            peripheral->markReset();
        }
    }
}

bool MachineCoordinator::markCardActive(Memory& memory, CardId card)
{
    Peripheral* peripheral = peripheralForCard(memory, card);
    return peripheral && peripheral->markActive();
}

bool MachineCoordinator::markCardInactive(Memory& memory, CardId card)
{
    Peripheral* peripheral = peripheralForCard(memory, card);
    return peripheral && peripheral->markInactive();
}

void MachineCoordinator::activateResetCards(Memory& memory)
{
    const CardSet attached = memory.enabledCards();
    for (std::size_t i = 0; i < kCardCount; ++i) {
        const auto card = static_cast<CardId>(i);
        if (!attached.contains(card)) continue;
        // ACI audio is an independent mixer source. Its lifecycle cannot be
        // Active merely because the bus interface is reset.
        if ((card == CardId::Aci || card == CardId::ExtendedAci) &&
            !memory.isCassetteAudioActive()) continue;
        if (Peripheral* peripheral = peripheralForCard(memory, card))
            peripheral->markActive();
    }
}

CardConfigurationResult MachineCoordinator::applyCardConfiguration(
    Memory& memory, const CardConfigurationRequest& request)
{
    CardConfigurationResult result;
    const CardSet originalCards = memory.enabledCards();
    const JukeBoxJumper originalJukeBoxJumper = memory.getJukeBoxJumper();
    const JukeBoxChipMode originalJukeBoxChipMode = memory.getJukeBoxChipMode();
    const CodeTankJumper originalCodeTankJumper = memory.getCodeTankJumper();
    const TransitionPlan plan = planConfiguration(
        originalCards, request.cards, request.mode);
    if (!plan.accepted) {
        result.code = CardConfigurationError::TopologyRejected;
        result.message = "card configuration rejected: " +
                         std::to_string(plan.rejectedConflicts.count) +
                         " conflict(s)";
        return result;
    }

    for (std::size_t i = 0; i < plan.detachCount; ++i)
        setCard(memory, plan.detachOrder[i], false);
    for (std::size_t i = 0; i < plan.configureCount; ++i) {
        switch (plan.configureOrder[i]) {
        case CardId::JukeBox:
            memory.setJukeBoxJumper(request.jukeBoxJumper);
            memory.setJukeBoxChipMode(request.jukeBoxChipMode);
            break;
        case CardId::CodeTank:
            memory.setCodeTankJumper(request.codeTankJumper);
            if (!request.codeTankRomPath.empty() &&
                memory.loadCodeTankRom(request.codeTankRomPath) != 0) {
                result.code = CardConfigurationError::DeviceConfigurationFailed;
                result.message = memory.getLastError();
            }
            break;
        default:
            break;
        }
    }
    if (!result) {
        // No target card has been attached yet. Restore board options and the
        // former topology before returning, so callers never inherit the
        // partially detached state produced by the failed transaction.
        memory.setJukeBoxJumper(originalJukeBoxJumper);
        memory.setJukeBoxChipMode(originalJukeBoxChipMode);
        memory.setCodeTankJumper(originalCodeTankJumper);
        const TransitionPlan rollback = planConfiguration(
            memory.enabledCards(), originalCards, TopologyMode::Fantasy);
        for (std::size_t i = 0; i < rollback.detachCount; ++i)
            setCard(memory, rollback.detachOrder[i], false);
        for (std::size_t i = 0; i < rollback.attachCount; ++i)
            setCard(memory, rollback.attachOrder[i], true);
        return result;
    }

    // System/card ROMs are part of the same critical transaction and land
    // while the target peripherals are still unreachable from the CPU.  This
    // avoids publishing the former sequence of partially loaded ROM images.
    std::string romError;
    bool romOk = true;
    using RomProfile = CardConfigurationRequest::SystemRomProfile;
    switch (request.systemRomProfile) {
    case RomProfile::Preserve:
        break;
    case RomProfile::MonitorOnly:
        memory.unloadBasic();
        romOk = RomLoader::reloadWozMonitor(memory, romError);
        break;
    case RomProfile::IntegerBasic:
        romOk = RomLoader::reloadBasic(memory, romError) &&
                RomLoader::reloadWozMonitor(memory, romError);
        break;
    case RomProfile::ApplesoftSd:
        romOk = RomLoader::reloadApplesoftLiteSDCard(memory, romError);
        if (romOk) memory.unloadBasic();
        if (romOk) romOk = RomLoader::reloadWozMonitor(memory, romError);
        break;
    case RomProfile::ApplesoftSdFantasy:
        romOk = RomLoader::reloadApplesoftLiteSDCard(memory, romError) &&
                RomLoader::reloadBasic(memory, romError) &&
                RomLoader::reloadWozMonitor(memory, romError);
        break;
    case RomProfile::ApplesoftCffa1:
        romOk = RomLoader::reloadApplesoftLiteCFFA1(memory, romError);
        break;
    }
    if (romOk && request.loadKrusader)
        romOk = RomLoader::reloadKrusader(memory, romError);
    if (romOk && request.loadCffa1Firmware)
        romOk = RomLoader::reloadCFFA1Rom(memory, romError);
    if (!romOk) {
        result.code = CardConfigurationError::SystemRomLoadingFailed;
        result.message = romError;
        // The topology remains detached on failure. Restore the former valid
        // card set; no target peripheral has observed the incomplete ROM map.
        memory.setJukeBoxJumper(originalJukeBoxJumper);
        memory.setJukeBoxChipMode(originalJukeBoxChipMode);
        memory.setCodeTankJumper(originalCodeTankJumper);
        const TransitionPlan rollback = planConfiguration(
            memory.enabledCards(), originalCards, TopologyMode::Fantasy);
        for (std::size_t i = 0; i < rollback.detachCount; ++i)
            setCard(memory, rollback.detachOrder[i], false);
        for (std::size_t i = 0; i < rollback.attachCount; ++i)
            setCard(memory, rollback.attachOrder[i], true);
        return result;
    }
    for (std::size_t i = 0; i < plan.attachCount; ++i)
        setCard(memory, plan.attachOrder[i], true);
    return result;
}

} // namespace pom1
