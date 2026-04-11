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

#include <vector>
#include <queue>
#include <string>
#include <cstdint>
#include <memory>
#include "AudioDevice.h"
#include "CassetteDevice.h"
#include "SID.h"
class TMS9918;
class WiFiModem;
class TerminalCard;
class A1IO_RTC;
#include "CFFA1.h"
#include "MicroSD.h"

// Remplacer quint8 par uint8_t et quint16 par uint16_t
typedef uint8_t quint8;
typedef uint16_t quint16;

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
    void setTMS9918Enabled(bool b) { tms9918Enabled = b; }
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
    void setWiFiModemEnabled(bool b) { wifiModemEnabled = b; }
    bool isWiFiModemEnabled() const { return wifiModemEnabled; }

    // P-LAB Apple-1 Terminal Card (bidirectional serial bridge)
    TerminalCard& getTerminalCard() { return *terminalCard; }
    const TerminalCard& getTerminalCard() const { return *terminalCard; }
    void setTerminalCardEnabled(bool b) { terminalCardEnabled = b; }
    bool isTerminalCardEnabled() const { return terminalCardEnabled; }

    // P-LAB Apple-1 I/O Board & Real Time Clock (65C22 VIA + ATMEGA32 + DS3231)
    A1IO_RTC& getA1IO_RTC() { return *a1ioRtc; }
    const A1IO_RTC& getA1IO_RTC() const { return *a1ioRtc; }
    void setA1IO_RTCEnabled(bool b) { a1ioRtcEnabled = b; }
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
    int displayCharDelay = 16667;    // Cycles par caractère (1MHz / 60 chars/sec)

private :

    // Memory itself tab
    std::vector<quint8> mem;



    int ramSize; // in kilobytes
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

};

#endif // MEMORY_H

