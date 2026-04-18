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
#include "DisplayDevice.h"
#include "PeripheralBus.h"

#include <vector>
#include <queue>
#include <string>
#include <cstdint>
#include <memory>
#include <unordered_set>
#include <bitset>
#include <cstddef>
#include "AudioDevice.h"
#include "CassetteDevice.h"
#include "SID.h"
class TMS9918;
class WiFiModem;
class TerminalCard;
class A1IO_RTC;
#include "CFFA1.h"
#include "JukeBox.h"
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
    // Zero the $E000-$EFFF BASIC ROM region — matches a pre-October-1976
    // bare Apple-1 that shipped with no BASIC cassette.
    void unloadBasic(void);
    int loadApplesoftLite(void);
    // Explicit variants — let the user pick which flavour to re-flash regardless
    // of which card is plugged. The auto-dispatching loadApplesoftLite()
    // above is what applyMachineConfig() uses for preset loading.
    int loadApplesoftLiteCFFA1(void);
    int loadApplesoftLiteSDCard(void);
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
    quint8* getMemoryPointerMutable() { return mem.data(); }
    // Debug: diagnostic string summarising which bus handlers are enabled.
    std::string busStateSummary() const;

    /// Flip the full 64 KB address space into a "flat RAM" mode: memRead /
    /// memWrite skip the PeripheralBus, PIA 6821 aliasing, strict-OOR, ROM
    /// write protection, and cassette sniffer — every access is a plain
    /// mem[addr] load or store. Used exclusively by the Klaus Dormann 6502
    /// functional test, which expects the whole 64 KB to behave as RAM.
    /// Must NOT be enabled in normal emulation; no safety checks remain.
    void setTestMode(bool enabled) { testMode = enabled; }
    bool isTestMode() const { return testMode; }

    /// Page-level dirty bitmap: bit p is set if the 256-byte page starting
    /// at $pp00 has been written since the last clearDirtyPages(). memWrite
    /// sets exactly one bit; bulk loaders (ROM reloads, hard resets) mark
    /// ranges via markPagesDirty() / markAllPagesDirty(). SnapshotPublisher
    /// walks the bitmap and memcpy's only the dirty pages — a typical
    /// running program touches ~4-8 pages per frame, so the snapshot cost
    /// goes from 64 KB/frame to ~1-2 KB/frame.
    const std::bitset<256>& getDirtyPages() const { return dirtyPages; }
    bool anyDirtyPage() const { return dirtyPages.any(); }
    void clearDirtyPages() { dirtyPages.reset(); }
    void markAllPagesDirty() { dirtyPages.set(); }
    void markPagesDirty(uint16_t addr, std::size_t length) {
        if (length == 0) return;
        const int first = addr >> 8;
        const std::size_t lastByte = static_cast<std::size_t>(addr) + length - 1;
        const int last = static_cast<int>(std::min<std::size_t>(lastByte >> 8, 255));
        for (int p = first; p <= last; ++p) dirtyPages.set(static_cast<std::size_t>(p));
    }

    // Apple 1 display sink (PIA 6821 $D012 output). Non-owning — the caller
    // keeps the DisplayDevice alive for Memory's lifetime (typically the
    // Screen_ImGui attached via EmulationController, or a test fake).
    // A null device means writes are silently dropped.
    void setDisplayDevice(DisplayDevice* device) { displayDevice = device; }
    DisplayDevice* getDisplayDevice() const { return displayDevice; }
    
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
    // Plug/unplug the ACI. When disabled: $C000-$C0FF bus handlers are off,
    // the $C000-$C0FF write sniffer (toggleOutput) is suppressed, and the
    // $C100-$C1FF ROM is zeroed so reads return 0 — matches a bare Apple-1
    // with no cassette interface wired to the expansion connector.
    void setACIEnabled(bool b);
    bool isACIEnabled() const { return aciEnabled; }

    // Cassette audio source registration on the audio mixer. Separate from
    // setACIEnabled() because the audio output (speaker you hear) belongs
    // to the tape deck itself, not the $C000/$C081 cassette interface
    // hooks. Deferred at boot the same way the SID is: a card added to
    // the mixer before the CPU has run any cycle stays silent on the
    // first playback. Idempotent.
    void activateCassetteAudioSource();
    void deactivateCassetteAudioSource();
    bool isCassetteAudioActive() const { return cassetteAudioActive; }

    // P-LAB Graphic Card (TMS9918 VDP)
    TMS9918& getTMS9918() { return *tms9918; }
    const TMS9918& getTMS9918() const { return *tms9918; }
    void setTMS9918Enabled(bool b);
    bool isTMS9918Enabled() const { return tms9918Enabled; }

    // P-LAB A1-SID Sound Card (MOS 6581/8580)
    pom1::SID& getSID() { return *sid; }
    const pom1::SID& getSID() const { return *sid; }
    void setSIDEnabled(bool b);
    // Claudio Parmigiani's A1-AUDIO Special Edition — same MOS chip but
    // register window mapped at $CC00-$CC1F instead of the prototype's
    // $C800-$CFFF. Mutually exclusive with the TMS9918 Graphic Card (they
    // share the same $CC00/$CC01 addresses). Internally the two variants
    // share the single `sid` instance; enabling one auto-disables the
    // other to keep the hardware invariants clean.
    void setSIDSpecialEditionEnabled(bool b);
    bool isSIDSpecialEditionEnabled() const { return sidSpecialEditionEnabled; }
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

    // P-LAB Apple-1 Juke-Box (Claudio Parmigiani) — memory-mapped 32 kB EEPROM
    // with a runtime jumper for the RAM/ROM split. Mutually exclusive with
    // CFFA1, microSD, Krusader, and the Wi-Fi Modem (shared address windows).
    JukeBox& getJukeBox() { return *jukeBox; }
    const JukeBox& getJukeBox() const { return *jukeBox; }
    void setJukeBoxEnabled(bool b);
    bool isJukeBoxEnabled() const { return jukeBoxEnabled; }
    // Jumper position (runtime-toggleable). Changing it disables the active
    // PeripheralBus range and enables the other one — addresses between
    // $4000-$7FFF and $8000-$BFFF swap between RAM and ROM.
    void setJukeBoxJumper(JukeBox::Jumper j);
    JukeBox::Jumper getJukeBoxJumper() const { return jukeBox->getJumper(); }
    // EEPROM RW jumper. No bus change; writable only controls whether writes
    // in the ROM window land in the ROM buffer (and on disk).
    void setJukeBoxWritable(bool w);
    bool isJukeBoxWritable() const { return jukeBox->isWritable(); }
    // Load a Juke-Box ROM file (up to 32 kB) from disk. `error` is populated
    // on failure.
    int loadJukeBoxRom(void);  // default path: roms/jukebox.rom

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
    DisplayDevice* displayDevice = nullptr;     // non-owning; injected by EmulationController
    
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

    // Page-level dirty bitmap (256 pages × 256 bytes = 64 KB). memWrite
    // sets one bit; bulk loaders mark ranges via markPagesDirty(). Consumed
    // by SnapshotPublisher, which copies only the set pages and resets the
    // bitmap. Initial state is all-zero — the Memory ctor's ROM loads will
    // mark the affected pages dirty so the very first snapshot is complete.
    std::bitset<256> dirtyPages{};
    bool testMode = false;            // see setTestMode() — flat-RAM mode for unit tests



    int ramSize; // in kilobytes
    int presetRamKB = 64;             // user-visible RAM ceiling for OOR warnings
    int oorAccessCount = 0;
    bool oorStrictMode = false;       // true: enforce bounds (reads→$FF, writes dropped)
    std::unordered_set<uint32_t> oorWarned;  // key = (addr<<1)|isWrite; capped at 64
    void checkOutOfRangeAccess(quint16 address, bool isWrite);
    bool writeInRom;
    std::string lastError;
    std::unique_ptr<CassetteDevice> cassetteDevice;
    // All expansion cards start UNPLUGGED. MainWindow::applyMachineConfig
    // re-plugs them 15 frames after the CPU has been running — plugging a
    // card (especially audio-source cards like SID and the cassette deck)
    // before the CPU has issued any cycle produces silent / broken cards
    // that only recover when the user toggles them manually. See the
    // pendingCardEnableFrames rationale in MainWindow_ImGui.h.
    bool aciEnabled = false;
    bool cassetteAudioActive = false;
    std::unique_ptr<TMS9918> tms9918;
    bool tms9918Enabled = false;
    // NOTE on destruction order: AudioDevice must outlive every AudioSource
    // it may be draining (CassetteDevice, SID). C++ destroys members in
    // reverse declaration order, so sources must be declared BEFORE
    // audioDevice to be destroyed AFTER it. CassetteDevice (declared above)
    // already satisfies this; `sid` must be declared here — NOT after
    // `audioDevice` — or a UAF window opens between ~sid and
    // ~audioDevice (which is what stops the miniaudio callback).
    std::unique_ptr<pom1::SID> sid;
    bool sidEnabled = false;
    bool sidSpecialEditionEnabled = false;
    std::unique_ptr<AudioDevice> audioDevice;
    std::unique_ptr<MicroSD> microSD;
    bool microSDEnabled = false;
    std::unique_ptr<CFFA1> cffa1;
    bool cffa1Enabled = false;
    std::unique_ptr<JukeBox> jukeBox;
    bool jukeBoxEnabled = false;
    std::unique_ptr<WiFiModem> wifiModem;
    bool wifiModemEnabled = false;
    std::unique_ptr<TerminalCard> terminalCard;
    bool terminalCardEnabled = false;
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
    PeripheralBus::Handle sidSEBusHandle = -1;       // A1-AUDIO SE: $CC00-$CC1F, priority 0 (shares sid instance)
    PeripheralBus::Handle tms9918BusHandle = -1;     // $CC00/$CC01, priority 10 (wins over SID)
    PeripheralBus::Handle cassetteToggleBusHandle = -1; // $C000-$C0FF read = toggle output
    PeripheralBus::Handle cassetteInputBusHandle  = -1; // $C081 read = tape input (priority 1, wins over toggle)
    // Juke-Box: two disjoint windows — one per jumper position. At most one
    // is enabled at a time. Priority 20 so the card wins over any other
    // peripheral that might have been registered in the same range (the
    // Juke-Box physically replaces any card at $4000-$BFFF on the real bus).
    PeripheralBus::Handle jukeBox32BusHandle = -1;   // RAM-16/ROM-32: $4000-$BFFF
    PeripheralBus::Handle jukeBox16BusHandle = -1;   // RAM-32/ROM-16: $8000-$BFFF

};

#endif // MEMORY_H

