// Pom1 Apple 1 Emulator
// Copyright (C) 2012 John D. Corrado
// Copyright (C) 2000-2026 Verhille Arnaud
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#ifndef MEMORY_H
#define MEMORY_H

#include "CpuClock.h"
#include "PeripheralBus.h"

#include <vector>
#include <queue>
#include <string>
#include <cstdint>
#include <memory>
#include <unordered_set>
#include "AudioDevice.h"
#include "CassetteDevice.h"
#include "SID.h"
class TMS9918;
class WiFiModem;
class TerminalCard;
class A1IO_RTC;
#include "CFFA1.h"
#include "MicroSD.h"

using quint8  = uint8_t;
using quint16 = uint16_t;

class Memory
{
public:

    Memory();

    // Memory Options
    void initMemory(void);
    void resetMemory(void);
    void setWriteInRom(bool b);
    bool getWriteInRom(void);
    int getRamSizeKB(void) const { return ramSize; }

    // Preset RAM size (KB). Address space stays 64 KB; this drives the
    // out-of-range warning for user programs that reach beyond the preset's
    // physical RAM. Default 64 (no warnings).
    void setPresetRamKB(int kb);
    int getPresetRamKB(void) const { return presetRamKB; }
    int getOutOfRangeAccessCount(void) const { return oorAccessCount; }
    void resetOutOfRangeAccessCount(void);

    // Strict out-of-range enforcement. When enabled AND presetRamKB < 64,
    // reads from [ramKB*1024, 0x8000) return $FF and writes are dropped —
    // matching a real Apple-1 with no RAM board in that region. The OOR
    // counter still increments so you can see activity in the status bar.
    // No effect when presetRamKB >= 64.
    void setOutOfRangeStrictMode(bool enable) { oorStrictMode = enable; }
    bool isOutOfRangeStrictMode(void) const { return oorStrictMode; }

    // Load Memory from file
    int loadROM(const char* filename, quint16 startAddress, size_t maxSize, const char* label);
    int loadBasic(void);
    int loadApplesoftLite(void);
    int loadKrusader(void);
    int loadWozMonitor(void);
    int loadAciRom(void);
    void configureResetVectors(quint16 vectorAddress = 0xFF00);
    int loadBinary(const char* filename, quint16 startAddress, int* bytesLoaded = nullptr);
    int loadHexDump(const char* filename, quint16 &startAddress, int* bytesLoaded = nullptr);

    // Last ROM loading error (empty if no error)
    const std::string& getLastError() const { return lastError; }

    quint8 memRead(quint16 address);
    //quint8 memReadAbsolute(quint16 adr);
    void memWrite(quint16 address, quint8 value);
    const quint8* getMemoryPointer() const { return mem.data(); }

    /// Monotonic counter incremented every time the 64 KB backing array
    /// changes (CPU writes that land in mem[], ROM loads, hard resets, …).
    /// SnapshotPublisher uses this to skip the 64 KB memcpy when no write
    /// has happened since the last publish — i.e. when the CPU is idle in
    /// a Wozmon polling loop, the snapshot bandwidth drops to zero. The
    /// counter wraps around naturally; publish only cares about equality
    /// with its last-seen value.
    uint64_t getMemoryDirtyCounter() const { return memDirtyCounter; }

    // Callback pour l'affichage Apple 1
    void setDisplayCallback(void (*callback)(char));
    
    // Gestion du clavier Apple 1
    void setKeyPressed(char key);
    void setKeyPressedRaw(char key);
    bool isKeyReady() const { return keyReady; }
    char getLastKey() const { return lastKey; }

    // Vitesse du terminal (caractères par seconde)
    void setTerminalSpeed(int charsPerSec);
    int getTerminalSpeed() const;

    // Horloge CPU partagée avec les périphériques synchronisés
    void advanceCycles(int cycles);

    // Apple Cassette Interface (ACI)
    CassetteDevice& getCassetteDevice() { return *cassetteDevice; }
    const CassetteDevice& getCassetteDevice() const { return *cassetteDevice; }

    // P-LAB Graphic Card (TMS9918 VDP)
    TMS9918& getTMS9918() { return *tms9918; }
    const TMS9918& getTMS9918() const { return *tms9918; }
    void setTMS9918Enabled(bool b);
    bool isTMS9918Enabled() const { return tms9918Enabled; }

    // P-LAB A1-SID Sound Card (MOS 6581/8580)
    pom1::SID& getSID() { return *sid; }
    const pom1::SID& getSID() const { return *sid; }
    void setSIDEnabled(bool b);
    bool isSIDEnabled() const { return sidEnabled; }

    // P-LAB microSD Storage Card (65C22 VIA + MCU)
    MicroSD& getMicroSD() { return *microSD; }
    const MicroSD& getMicroSD() const { return *microSD; }
    void setMicroSDEnabled(bool b);
    bool isMicroSDEnabled() const { return microSDEnabled; }
    int loadSDCardRom(void);

    // CFFA1 CompactFlash Interface (Rich Dreher)
    CFFA1& getCFFA1() { return *cffa1; }
    const CFFA1& getCFFA1() const { return *cffa1; }
    void setCFFA1Enabled(bool b);
    bool isCFFA1Enabled() const { return cffa1Enabled; }
    int loadCFFA1Rom(void);

    // P-LAB Apple-1 Wi-Fi Modem (65C51 ACIA + ESP8266)
    WiFiModem& getWiFiModem() { return *wifiModem; }
    const WiFiModem& getWiFiModem() const { return *wifiModem; }
    void setWiFiModemEnabled(bool b);
    bool isWiFiModemEnabled() const { return wifiModemEnabled; }

    // P-LAB Apple-1 Terminal Card (bidirectional serial bridge)
    TerminalCard& getTerminalCard() { return *terminalCard; }
    const TerminalCard& getTerminalCard() const { return *terminalCard; }
    void setTerminalCardEnabled(bool b) { terminalCardEnabled = b; }
    bool isTerminalCardEnabled() const { return terminalCardEnabled; }

    // P-LAB Apple-1 I/O Board & Real Time Clock (65C22 VIA + ATMEGA32 + DS3231)
    A1IO_RTC& getA1IO_RTC() { return *a1ioRtc; }
    const A1IO_RTC& getA1IO_RTC() const { return *a1ioRtc; }
    void setA1IO_RTCEnabled(bool b);
    bool isA1IO_RTCEnabled() const { return a1ioRtcEnabled; }

    // Central audio device (mixes CassetteDevice + SID)
    AudioDevice& getAudioDevice() { return *audioDevice; }

private:
    void (*displayCallback)(char) = nullptr;
    
    // Clavier Apple 1 (0xD010 = KBD, 0xD011 = KBDCR)
    char lastKey = 0;
    bool keyReady = false;
    std::queue<char> keyBuffer;

    // Display Apple 1 (0xD012) - délai d'affichage
    int displayBusyCycles = 0;       // Cycles restants avant display ready
    int displayCharDelay = POM1_CPU_CLOCK_HZ / 60;    // 60 chars/sec à l'horloge CPU nominale

private :

    // Memory itself tab
    std::vector<quint8> mem;

    // Bumped every time mem[] is mutated. Hot-path increment is one ADD
    // inside memWrite (after the early-return / strict-OOR drops). Bulk
    // operations (loadROM, init/reset, region clears) bump it once at the
    // end. Read by SnapshotPublisher to skip the per-frame 64 KB memcpy
    // when nothing changed.
    uint64_t memDirtyCounter = 1;



    int ramSize; // in kilobytes
    int presetRamKB = 64;             // user-visible RAM ceiling for OOR warnings
    int oorAccessCount = 0;
    bool oorStrictMode = false;       // true: enforce bounds (reads→$FF, writes dropped)
    std::unordered_set<uint32_t> oorWarned;  // key = (addr<<1)|isWrite; capped at 64
    void checkOutOfRangeAccess(quint16 address, bool isWrite);
    bool writeInRom;
    std::string lastError;
    std::unique_ptr<CassetteDevice> cassetteDevice;
    std::unique_ptr<TMS9918> tms9918;
    bool tms9918Enabled = false;
    std::unique_ptr<AudioDevice> audioDevice;
    std::unique_ptr<pom1::SID> sid;
    bool sidEnabled = false;
    std::unique_ptr<MicroSD> microSD;
    bool microSDEnabled = true;
    std::unique_ptr<CFFA1> cffa1;
    bool cffa1Enabled = false;
    std::unique_ptr<WiFiModem> wifiModem;
    bool wifiModemEnabled = false;
    std::unique_ptr<TerminalCard> terminalCard;
    bool terminalCardEnabled = !POM1_IS_WASM;
    std::unique_ptr<A1IO_RTC> a1ioRtc;
    bool a1ioRtcEnabled = false;

    // PeripheralBus — central dispatch for memory-mapped I/O. Each peripheral
    // registers a range + read/write handler; memRead/memWrite delegate to
    // `bus.tryRead`/`bus.tryWrite` instead of inline per-device branches.
    // `*Handle` fields identify the bus entries so enable/disable can flip them.
    PeripheralBus bus;
    PeripheralBus::Handle a1ioRtcBusHandle = -1;
    PeripheralBus::Handle cffa1RomBusHandle = -1;    // $9000-$AFDF read, writes swallowed
    PeripheralBus::Handle cffa1RegBusHandle = -1;    // $AFE0-$AFFF read+write
    PeripheralBus::Handle microSDBusHandle = -1;     // $A000-$A00F (overridden by CFFA1 when both enabled; but the presets are mutually exclusive)
    PeripheralBus::Handle wifiModemBusHandle = -1;   // $B000-$B003
    PeripheralBus::Handle sidBusHandle = -1;         // $C800-$CFFF, priority 0
    PeripheralBus::Handle tms9918BusHandle = -1;     // $CC00/$CC01, priority 10 (wins over SID)
    PeripheralBus::Handle cassetteToggleBusHandle = -1; // $C000-$C0FF read = toggle output
    PeripheralBus::Handle cassetteInputBusHandle  = -1; // $C081 read = tape input (priority 1, wins over toggle)

};

#endif // MEMORY_H

