// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// EmulationSnapshot — immutable picture of the emulator state, sized for
// lock-free reads by the UI thread. Populated by SnapshotPublisher under
// EmulationController::stateMutex and copied out under its internal
// snapshotMutex. Kept in its own header so SnapshotPublisher and
// EmulationController can share the type without creating an include cycle.

#ifndef EMULATIONSNAPSHOT_H
#define EMULATIONSNAPSHOT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "M6502.h"
#include "SID.h"
#include "TMS9918.h"
#include "WiFiModem.h"
#include "TerminalCard.h"
#include "A1IO_RTC.h"
#include "JukeBox.h"
#include "CodeTank.h"
#include "PR40Printer.h"
#include "GT6144.h"

struct EmulationSnapshot
{
    std::vector<uint8_t> memory = std::vector<uint8_t>(0x10000, 0);
    uint16_t programCounter = 0;
    uint8_t accumulator = 0;
    uint8_t xRegister = 0;
    uint8_t yRegister = 0;
    uint8_t stackPointer = 0;
    uint8_t statusRegister = 0;
    bool cpuRunning = false;
    bool keyReady = false;
    char lastKey = 0;
    bool writeInRom = false;
    int ramSizeKB = 0;
    bool oorStrictMode = false;
    bool cassetteLoadedTape = false;
    bool cassetteRecordedTape = false;
    bool cassettePlaybackActive = false;
    bool cassettePlaybackArmed = false;
    bool cassetteRewinding = false;
    bool cassetteAudioAvailable = false;
    bool cassetteHardwareAccurateLiveAudio = true;
    bool cassettePlaybackPaused = false;
    bool cassetteAudioStreamMode = false;
    double cassetteQueuedAudioSeconds = 0.0;
    double cassettePlaybackPositionSeconds = 0.0;
    double cassettePlaybackTotalSeconds = 0.0;
    size_t cassetteLoadedTransitionCount = 0;
    size_t cassetteRecordedTransitionCount = 0;
    float  cassetteVolume = 1.0f;
    std::string cassetteLoadedTapePath;
    std::string cassetteLoadInfo;
    TMS9918::Snapshot tms9918;
    bool sidEnabled = false;
    bool sidSpecialEditionEnabled = false;
    pom1::SID::ChipModel sidChipModel = pom1::SID::ChipModel::MOS6581;
    bool microSDEnabled = false;
    bool wifiModemEnabled = false;
    WiFiModem::Snapshot wifiModem;
    bool terminalCardEnabled = false;
    TerminalCard::Snapshot terminalCard;
    bool a1ioRtcEnabled = false;
    A1IO_RTC::Snapshot a1ioRtc;
    bool jukeBoxEnabled = false;
    JukeBox::Snapshot jukeBox;
    bool codeTankEnabled = false;
    CodeTank::Snapshot codeTank;
    bool pr40Enabled = false;
    PR40Printer::Snapshot pr40;
    bool gt6144Enabled = false;
    GT6144::Snapshot gt6144;
};

#endif // EMULATIONSNAPSHOT_H
