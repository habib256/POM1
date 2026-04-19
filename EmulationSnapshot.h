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

struct EmulationSnapshot
{
    std::vector<quint8> memory = std::vector<quint8>(0x10000, 0);
    quint16 programCounter = 0;
    quint8 accumulator = 0;
    quint8 xRegister = 0;
    quint8 yRegister = 0;
    quint8 stackPointer = 0;
    quint8 statusRegister = 0;
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
};

#endif // EMULATIONSNAPSHOT_H
