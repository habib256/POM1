// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// EmulationController_Cards.cpp — cassette transport / live audio and the per-card enable-configure-query passthroughs (TMS9918, ACI + extended ACI, SID, microSD + IEC, CFFA1, Juke-Box, CodeTank, Wi-Fi Modem, Terminal Card + telemetry, PR-40, A1-IO & RTC, GT-6144)
//
// One of four translation units implementing the single EmulationController
// class. The class was a 2143-line god file: 207 method definitions covering
// the CPU thread, snapshots, silicon-fidelity knobs and a passthrough per
// expansion card, all in one place. Splitting it along those axes is pure code
// motion — no behaviour, no signature and no call site changed — and follows
// the pattern MainWindow_ImGui already uses (9 TUs behind one class).
//
// The mutex discipline is unchanged and applies to EVERY TU:
//     stateMutex > keyboard.keyMutex > publisher.snapshotMutex
// Anything touching `memory` or `cpu` takes `stateMutex` first; the pacing
// constants and the emulation thread itself stay in EmulationController.cpp.

#include "EmulationController.h"
#include "POM1Build.h"
#include "PR40Printer.h"
#include "RomLoader.h"
#include "TMS9918.h"
#include "TelemetryPort.h"   // complete type for memory->getTelemetryPort()
#include "CassetteDevice.h"  // complete type for memory->getCassetteDevice()
#include "MicroSD.h"         // complete type for memory->getMicroSD()
#include "IECCard.h"         // complete type for memory->getIECCard()
#include "Logger.h"

#include <algorithm>

bool EmulationController::loadTape(const std::string& path, std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    if (!memory->getCassetteDevice().loadTape(path)) {
        error = memory->getCassetteDevice().getLastError();
        publisher.publish(*memory, *cpu, runRequested.load());
        return false;
    }
    memory->getCassetteDevice().rewindTape();
    publisher.publish(*memory, *cpu, runRequested.load());
    return true;
}

bool EmulationController::loadProgramTape(const std::string& path, std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    if (!memory->getCassetteDevice().loadProgramTape(path)) {
        error = memory->getCassetteDevice().getLastError();
        publisher.publish(*memory, *cpu, runRequested.load());
        return false;
    }
    memory->getCassetteDevice().rewindTape();
    publisher.publish(*memory, *cpu, runRequested.load());
    return true;
}

bool EmulationController::saveTape(const std::string& path, std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    if (!memory->getCassetteDevice().saveTape(path)) {
        error = memory->getCassetteDevice().getLastError();
        publisher.publish(*memory, *cpu, runRequested.load());
        return false;
    }
    publisher.publish(*memory, *cpu, runRequested.load());
    return true;
}

void EmulationController::rewindTape()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getCassetteDevice().rewindTape();
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::playTape()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getCassetteDevice().playTape();
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::stopTape()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getCassetteDevice().stopTape();
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::pauseTape(bool paused)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getCassetteDevice().setPlaybackPaused(paused);
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::seekTapeRelative(double deltaSeconds)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getCassetteDevice().seekRelativeSeconds(deltaSeconds);
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::ejectTape()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getCassetteDevice().ejectTape();
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::clearTapeCapture()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getCassetteDevice().clearRecordedTape();
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::setHardwareAccurateLiveAudio(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getCassetteDevice().setHardwareAccurateLiveAudio(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::setCassetteVolume(float volume)
{
    // No stateMutex: CassetteDevice::setVolume() only writes to a
    // std::atomic<float> that the audio thread reads with relaxed memory
    // order. Skipping the mutex keeps the +/- buttons instant even when
    // the emulation thread is burning cycles at MAX speed.
    memory->getCassetteDevice().setVolume(volume);
}

void EmulationController::armCassetteRecord()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getCassetteDevice().armRecording();
    publisher.publish(*memory, *cpu, runRequested.load());
}

pom1::CardConfigurationResult EmulationController::applyCardConfiguration(
    const pom1::CardConfigurationRequest& request)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);

    // Reject an invalid target before applying any of the optional machine
    // settings carried by the same DTO. MachineCoordinator validates again at
    // execution time, but this facade-level gate preserves the stronger
    // transaction promise: TopologyRejected performs no mutation at all.
    const pom1::TransitionPlan validation = pom1::planConfiguration(
        memory->enabledCards(), request.cards, request.mode);
    if (!validation.accepted) {
        pom1::CardConfigurationResult rejected;
        rejected.code = pom1::CardConfigurationError::TopologyRejected;
        rejected.message = "card configuration rejected: " +
            std::to_string(validation.rejectedConflicts.count) +
            " conflict(s)";
        return rejected;
    }

    // stateMutex excludes the emulation thread, but make the CPU state itself
    // match the transaction as well.  Peripheral detach/configure/attach can
    // touch bus handlers and audio producers; none of it should happen while
    // M6502 still advertises a running core.  Keep runRequested unchanged so
    // no transient stopped state is published and restore the caller's exact
    // run/pause state before the single final publication.
    const bool resumeCpu = request.coldReset || runRequested.load();
    cpu->stop();

    // Preset-wide machine policy must be armed before resetMemory()/initMemory:
    // RAM/VRAM power-on noise and GEN2 initial state are consumed by reset.
    if (request.presetRamKB)
        memory->setPresetRamKB(*request.presetRamKB);
    if (request.siliconStrict)
        memory->setSiliconStrictMode(*request.siliconStrict);
    if (request.outOfRangeStrict)
        memory->setOutOfRangeStrictMode(*request.outOfRangeStrict);
    if (request.vramNoiseOnReset)
        memory->setVramNoiseOnReset(*request.vramNoiseOnReset);
    if (request.systemRamNoiseOnReset)
        memory->setSystemRamNoiseOnReset(*request.systemRamNoiseOnReset);
    if (request.cpuDecimalBugNMOS)
        cpu->setDecimalBugNMOS(*request.cpuDecimalBugNMOS);
    if (request.dramRefresh)
        cpu->setDramRefreshEnabled(*request.dramRefresh);
    if (request.gen2RandomPowerOn)
        memory->setGen2RandomPowerOn(*request.gen2RandomPowerOn);

    if (request.coldReset) {
        // Detach every producer/bus endpoint before resetting RAM and devices.
        // This intermediate empty topology is never published.
        memory->deactivateCassetteAudioSource();
        pom1::CardConfigurationRequest detachAll;
        const pom1::CardConfigurationResult detached =
            pom1::MachineCoordinator::applyCardConfiguration(*memory, detachAll);
        if (!detached) {
            if (resumeCpu) cpu->start();
            publisher.publish(*memory, *cpu, runRequested.load());
            if (resumeCpu) wakeCv.notify_all();
            return detached; // Empty topology is expected to be infallible.
        }

        programGeneration_.fetch_add(1, std::memory_order_relaxed);
        memory->resetMemory();
        memory->initMemory();
        keyboard.clear();
        memory->clearKeyboardInput();
        preferredSoftResetVector = kDefaultResetVector;
        memory->configureResetVectors(kDefaultResetVector);
        cpu->hardReset();
    }
    pom1::CardConfigurationResult result =
        pom1::MachineCoordinator::applyCardConfiguration(*memory, request);

    if (result && request.activateCassetteAudio) {
        memory->activateCassetteAudioSource();
        if (request.cards.contains(pom1::CardId::Aci))
            pom1::MachineCoordinator::markCardActive(*memory, pom1::CardId::Aci);
        if (request.cards.contains(pom1::CardId::ExtendedAci))
            pom1::MachineCoordinator::markCardActive(
                *memory, pom1::CardId::ExtendedAci);
    }
    if (resumeCpu) cpu->start();
    if (request.coldReset && screen) {
        if (request.animateBoot)
            screen->resetDisplay();
        else
            screen->clear();
    }
    publisher.publish(*memory, *cpu, runRequested.load());
    if (resumeCpu) wakeCv.notify_all();
    return result;
}

pom1::CardSet EmulationController::getEnabledCards() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->enabledCards();
}

void EmulationController::setCardEnabled(pom1::CardId card, bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    pom1::MachineCoordinator::setCardEnabled(*memory, card, enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isTMS9918Enabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isTMS9918Enabled();
}

void EmulationController::setHgrFramebufferAttached(bool attached)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setHgrFramebufferAttached(attached);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isHgrFramebufferAttached() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isHgrFramebufferAttached();
}

void EmulationController::setGen2FiftyHz(bool fiftyHz)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setGen2FiftyHz(fiftyHz);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isGen2FiftyHz() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isGen2FiftyHz();
}

bool EmulationController::isACIEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isACIEnabled();
}

bool EmulationController::isExtendedACIEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isExtendedACIEnabled();
}

void EmulationController::activateCassetteAudioSource()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->activateCassetteAudioSource();
    pom1::MachineCoordinator::markCardActive(*memory, pom1::CardId::Aci);
}

void EmulationController::deactivateCassetteAudioSource()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->deactivateCassetteAudioSource();
    pom1::MachineCoordinator::markCardInactive(*memory, pom1::CardId::Aci);
}

bool EmulationController::isSIDSpecialEditionEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isSIDSpecialEditionEnabled();
}

void EmulationController::setSIDChipModel(pom1::SID::ChipModel m)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getSID().setChipModel(m);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isSIDEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isSIDEnabled();
}

bool EmulationController::isIECCardEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isIECCardEnabled();
}

bool EmulationController::mountIECDisk(const std::string& path)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->getIECCard().mountDisk(path);
}

void EmulationController::unmountIECDisk()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getIECCard().unmount();
}

EmulationController::IECCardUIState EmulationController::getIECCardUIState() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    IECCardUIState s;
    if (!memory->isIECCardEnabled()) return s;
    const auto& iec = memory->getIECCard();
    const auto& disk = iec.drive().image();
    s.hasDisk = iec.hasDisk();
    if (s.hasDisk) {
        s.diskPath = iec.diskPath();
        s.label = disk.labelAscii();
        s.id = disk.idAscii();
        s.blocksFree = disk.blocksFree();
        s.totalBlocks = disk.totalBlocks();
        for (const auto& e : disk.directory("*")) {
            if (e.type == 0) continue;
            IECCardUIState::Entry ue;
            for (uint8_t b : e.name) {
                if (b >= 0x20 && b <= 0x7E) ue.name += static_cast<char>(b);
                else if (b >= 0xC1 && b <= 0xDA) ue.name += static_cast<char>(b - 0x80);
                else ue.name += '?';
            }
            ue.blocks = e.blocks;
            ue.type = e.type;
            s.directory.push_back(std::move(ue));
        }
    }
    return s;
}

bool EmulationController::isMicroSDEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isMicroSDEnabled();
}

std::string EmulationController::getMicroSDRootPath() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->getMicroSD().getSDCardPath();
}

bool EmulationController::isCFFA1Enabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isCFFA1Enabled();
}

bool EmulationController::reloadCFFA1Rom(std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    bool ok = RomLoader::reloadCFFA1Rom(*memory, error);
    publisher.publish(*memory, *cpu, runRequested.load());
    return ok;
}

bool EmulationController::reloadSDCardRom(std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    bool ok = RomLoader::reloadSDCardRom(*memory, error);
    publisher.publish(*memory, *cpu, runRequested.load());
    return ok;
}

bool EmulationController::isJukeBoxEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isJukeBoxEnabled();
}

void EmulationController::setJukeBoxJumper(JukeBox::Jumper jumper)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setJukeBoxJumper(jumper);
    publisher.publish(*memory, *cpu, runRequested.load());
}

JukeBox::Jumper EmulationController::getJukeBoxJumper() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->getJukeBoxJumper();
}

void EmulationController::setJukeBoxChipMode(JukeBox::ChipMode mode)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setJukeBoxChipMode(mode);
    publisher.publish(*memory, *cpu, runRequested.load());
}

JukeBox::ChipMode EmulationController::getJukeBoxChipMode() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->getJukeBoxChipMode();
}

void EmulationController::setJukeBoxWritable(bool writable)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setJukeBoxWritable(writable);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isJukeBoxWritable() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isJukeBoxWritable();
}

bool EmulationController::reloadJukeBoxRom(std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    bool ok = RomLoader::reloadJukeBoxRom(*memory, error);
    publisher.publish(*memory, *cpu, runRequested.load());
    return ok;
}

void EmulationController::setJukeBoxBankRegister(uint8_t value)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setJukeBoxBankRegister(value);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::copyJukeBoxPage(uint8_t fromPage, uint8_t toPage, std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    bool ok = memory->copyJukeBoxPage(fromPage, toPage, error);
    if (ok) publisher.publish(*memory, *cpu, runRequested.load());
    return ok;
}

bool EmulationController::saveJukeBoxRom(const std::string& path, std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->saveJukeBoxRom(path, error);
}

bool EmulationController::isCodeTankEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isCodeTankEnabled();
}

void EmulationController::setCodeTankJumper(CodeTank::Jumper jumper)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setCodeTankJumper(jumper);
    publisher.publish(*memory, *cpu, runRequested.load());
}

CodeTank::Jumper EmulationController::getCodeTankJumper() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->getCodeTankJumper();
}

bool EmulationController::loadCodeTankRom(const std::string& path, std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    int rc = memory->loadCodeTankRom(path);
    if (rc != 0) {
        error = memory->getLastError();
        return false;
    }
    publisher.publish(*memory, *cpu, runRequested.load());
    return true;
}

bool EmulationController::loadCodeTankRomBuffer(const std::vector<uint8_t>& data,
                                                const std::string& label, std::string& error)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    int rc = memory->loadCodeTankRomBuffer(data, label);
    if (rc != 0) {
        error = memory->getLastError();
        return false;
    }
    publisher.publish(*memory, *cpu, runRequested.load());
    return true;
}

bool EmulationController::isWiFiModemEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isWiFiModemEnabled();
}

void EmulationController::wifiModemDisconnect()
{
    // No stateMutex needed: WiFiModem::requestDisconnect() takes its own modemMutex.
    if (memory->isWiFiModemEnabled()) {
        memory->getWiFiModem().requestDisconnect();
    }
}

void EmulationController::wifiModemReset()
{
    // No stateMutex needed: WiFiModem::reset() takes its own modemMutex.
    memory->getWiFiModem().reset();
}

void EmulationController::setTelemetryEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setTelemetryEnabled(enabled);
}

void EmulationController::setTelemetryListenPort(uint16_t port)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getTelemetryPort().setListenPort(port);
}

void EmulationController::setTelemetryLogFile(const std::string& path)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getTelemetryPort().setLogFile(path);
}

void EmulationController::telemetryInject(const uint8_t* data, std::size_t len)
{
    if (!data || len == 0) return;
    std::lock_guard<PriorityMutex> lock(stateMutex);
    if (!memory->isTelemetryEnabled()) return;
    memory->getTelemetryPort().injectInbound(data, len);
}

void EmulationController::telemetryReleaseFrame()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    if (!memory->isTelemetryEnabled()) return;
    // Same effect as the harness ACK: drop the park gate so the slice loop
    // resumes the CPU until the next end-frame re-arms it (if lock-step is on).
    memory->getTelemetryPort().clearAwaitingAck();
}

void EmulationController::setTelemetryLockstep(bool on)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    if (!memory->isTelemetryEnabled()) return;
    auto& tp = memory->getTelemetryPort();
    tp.setLockstep(on);
    if (!on) tp.clearAwaitingAck();   // disarm → release any current park (resume)
}

bool EmulationController::isTerminalCardEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isTerminalCardEnabled();
}

TerminalCard* EmulationController::getTerminalCardIfEnabled()
{
    // No stateMutex: the card itself owns its own atomics + mutex, and the
    // render thread must not contend with the long-held emulation lock.
    if (!memory) return nullptr;
    if (!memory->isTerminalCardEnabled()) return nullptr;
    return &memory->getTerminalCard();
}

bool EmulationController::isPR40Enabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isPR40Enabled();
}

void EmulationController::setPR40SwitchMode(int mode)
{
    PR40Printer::SwitchMode m = PR40Printer::SwitchMode::Mixed;
    switch (mode) {
        case 0: m = PR40Printer::SwitchMode::Off;       break;
        case 1: m = PR40Printer::SwitchMode::Mixed;     break;
        case 2: m = PR40Printer::SwitchMode::PrintOnly; break;
        default: return;
    }
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getPR40().setMode(m);
    publisher.publish(*memory, *cpu, runRequested.load());
}

int EmulationController::getPR40SwitchMode() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return static_cast<int>(memory->getPR40().getMode());
}

bool EmulationController::savePR40PaperRoll(const std::string& path, std::string& error) const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->getPR40().savePaperRoll(path, error);
}

void EmulationController::clearPR40Paper()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getPR40().tearOffPage();
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isA1IO_RTCEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isA1IO_RTCEnabled();
}

bool EmulationController::isGT6144Enabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isGT6144Enabled();
}

void EmulationController::setRtcOverrideTime(std::time_t target)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->getA1IO_RTC().setOverrideTime(target);
}
