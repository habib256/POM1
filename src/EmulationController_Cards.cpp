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

void EmulationController::setTMS9918Enabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setTMS9918Enabled(enabled);
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

void EmulationController::setACIEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setACIEnabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isACIEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isACIEnabled();
}

void EmulationController::setExtendedACIEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setExtendedACIEnabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isExtendedACIEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isExtendedACIEnabled();
}

void EmulationController::setSIDEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setSIDEnabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::activateCassetteAudioSource()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->activateCassetteAudioSource();
}

void EmulationController::deactivateCassetteAudioSource()
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->deactivateCassetteAudioSource();
}

void EmulationController::setSIDSpecialEditionEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setSIDSpecialEditionEnabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
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

void EmulationController::setMicroSDEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setMicroSDEnabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

void EmulationController::setIECCardEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setIECCardEnabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
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

void EmulationController::setCFFA1Enabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setCFFA1Enabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
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

void EmulationController::setJukeBoxEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setJukeBoxEnabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
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

void EmulationController::setCodeTankEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setCodeTankEnabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
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

void EmulationController::setWiFiModemEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setWiFiModemEnabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
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

void EmulationController::setTerminalCardEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setTerminalCardEnabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
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

void EmulationController::setPR40Enabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setPR40Enabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
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

void EmulationController::setA1IO_RTCEnabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setA1IO_RTCEnabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
}

bool EmulationController::isA1IO_RTCEnabled() const
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    return memory->isA1IO_RTCEnabled();
}

void EmulationController::setGT6144Enabled(bool enabled)
{
    std::lock_guard<PriorityMutex> lock(stateMutex);
    memory->setGT6144Enabled(enabled);
    publisher.publish(*memory, *cpu, runRequested.load());
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
