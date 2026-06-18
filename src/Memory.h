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
#include <utility>
#include <atomic>
#include "AudioDevice.h"
#include "CassetteDevice.h"
#include "SID.h"
class TMS9918;
class WiFiModem;
class TerminalCard;
class TelemetryPort;
class A1IO_RTC;
class PR40Printer;
class M6502;
#include "CFFA1.h"
#include "CodeTank.h"
#include "GT6144.h"
#include "JukeBox.h"
#include "IECCard.h"
#include "MicroSD.h"
#include "Gen2VideoScanner.h"

namespace pom1 { class SnapshotWriter; class SnapshotReader; }

class Memory
{
public:

    Memory();
    // Out-of-line (defined in Memory.cpp) so the forward-declared unique_ptr
    // members (TMS9918 / WiFiModem / TerminalCard / TelemetryPort / A1IO_RTC /
    // PR40Printer) only need their full type at the single dtor definition
    // point, not in every TU that destroys a Memory.
    ~Memory();

    // Memory Options
    void initMemory(void);
    void resetMemory(void);
    void setWriteInRom(bool b);
    bool getWriteInRom(void);
    int getRamSizeKB(void) const { return ramSize; }

    // When true, resetMemory() seeds the RAM range with mt19937 noise
    // instead of zeros — matches real Apple-1 6502 RAM at power-on
    // (bistable noise). Surfaces "assume RAM = 0" bugs in user programs
    // when paired with silicon-strict mode. Default = false (zero-init
    // preserved for tests / snapshots).
    void setSystemRamNoiseOnReset(bool enabled) { systemRamNoiseOnReset = enabled; }
    bool isSystemRamNoiseOnReset() const { return systemRamNoiseOnReset; }

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

    // Uncle Bernie's GEN2 release card carries its own DRAM behind both HGR
    // pages ($2000-$3FFF and $4000-$5FFF). When the card is plugged those
    // regions behave like RAM regardless of presetRamKB / strict mode (the
    // `isOorAddress` carve-out), the $C250-$C257 soft-switch bus handler is
    // enabled, and the video scanner advances from advanceCycles(). On every
    // off→on transition the page-1 framebuffer is seeded with mt19937 noise
    // to mimic real DRAM bistable power-on state (see resetMemory() and
    // dev/Programming_TMS9918.md §27 VRAM power-on init); the soft-switch journal resets on any transition.
    void setHgrFramebufferAttached(bool e);
    bool isHgrFramebufferAttached(void) const { return hgrFramebufferAttached; }

    // GEN2 *release* video scanner (cycle-accurate floating bus + beam timing).
    // The counter is advanced from advanceCycles() while the card is plugged.
    // The $C250-$C257 soft switches (read-only: read toggles + returns HST0
    // in D7; writes are no-ops — Bernie's PDF, doc/GEN2_RELEASE_questions.md)
    // are served by the "GEN2_softswitch" PeripheralBus entry registered in
    // the ctor and enabled with the card.
    uint64_t peekGen2VideoCycle(void) const { return gen2Scanner.peekVideoCycle(); }
    uint8_t  gen2FloatingBus(void) const { return gen2Scanner.floatingBus(mem.data()); }
    void     setGen2DisplayState(const Gen2VideoScanner::DisplayState& s) {
        gen2Scanner.setDisplayState(s);
    }
    const Gen2VideoScanner::DisplayState& gen2DisplayState(void) const {
        return gen2Scanner.displayState();
    }
    // 50/60 Hz vertical-rate jumper of the release card (NTSC color either way).
    void setGen2FiftyHz(bool on) { gen2Scanner.setFiftyHz(on); }
    bool isGen2FiftyHz(void) const { return gen2Scanner.isFiftyHz(); }

    // Per-video-frame soft-switch journal (beam racing, Phase 3). Soft-switch
    // flips are recorded with their scanner cycle; at every video-frame
    // rollover inside advanceCycles() the recording journal is published as
    // "the events of the frame that just completed" together with the display
    // state that was live when that frame started. SnapshotPublisher copies
    // the published set each publish; re-rendering the same frame is safe
    // (POM2 model — Memory republishes at each video-frame boundary).
    const std::vector<Gen2VideoScanner::Event>& gen2PublishedVideoEvents(void) const {
        return gen2PublishedEvents;
    }
    Gen2VideoScanner::DisplayState gen2PublishedFrameStartState(void) const {
        return gen2PublishedFrameStart;
    }

    // Load Memory from file
    int loadROM(const char* filename, uint16_t startAddress, size_t maxSize, const char* label);
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
    void configureResetVectors(uint16_t vectorAddress = 0xFF00);
    int loadBinary(const char* filename, uint16_t startAddress, int* bytesLoaded = nullptr);
    // loadHexDump: parse a Wozmon-hex dump (e.g. games_chess Chess.txt) and
    // write each `AAAA: BB BB ...` block into mem[]. Multi-zone dumps that
    // jump between disjoint address ranges (chess: $0280 lo block + $E000 hi
    // block) populate the optional `zones` vector with one (start, end) pair
    // per contiguous zone — used by the file-dialog post-load metadata so the
    // Memory Map can display each zone as its own loadedPrograms entry.
    int loadHexDump(const char* filename, uint16_t &startAddress,
                    int* bytesLoaded = nullptr,
                    std::vector<std::pair<uint16_t,uint16_t>>* zones = nullptr);

    // Last ROM loading error (empty if no error)
    const std::string& getLastError() const { return lastError; }

    // ─────────────────────────────────────────────────────────────────
    // Snapshot save/load — see SnapshotIO.h for the file format.
    //
    // Captured today (v1):
    //   * 64 KB flat RAM (mem[])
    //   * Card "enabled" flags (12 bools packed)
    //   * Per-card payloads — every card now overrides Peripheral::serialize.
    //     See each card's header for what its section captures.
    //
    // CPU state (PC/A/X/Y/SP/status/IRQ/NMI/cycles) is round-tripped via the
    // optional `cpu` parameter — pass nullptr to skip the "CPU" section
    // entirely (useful for memory-only fixtures and the lower-level test
    // path that doesn't construct an M6502).
    //
    // NOT captured (intentional limitations):
    //   * Cassette deck mid-stream playback position (the recording buffer
    //     and $C000 flip-flop round-trip; the in-flight playback cursor
    //     into a loaded tape is reset and the user re-presses PLAY).
    //   * WiFiModem / TerminalCard TCP connections (kernel-side state —
    //     reset on load; the user re-dials).
    //   * libresidfp internal filter integrators / oscillator phase (not
    //     exposed by the engine; shadow regs are re-poked at load).
    //
    // Returns false on error and fills `error`.
    // Caller is responsible for stopping the CPU before invoking either of
    // these (saveSnapshot is read-only but loadSnapshot rewrites RAM).
    bool saveSnapshot(const std::string& path, std::string& error,
                      const class M6502* cpu = nullptr) const;
    bool loadSnapshot(const std::string& path, std::string& error,
                      class M6502* cpu = nullptr);

    // In-memory snapshot variants — same byte layout as the file path, but
    // the bytes never touch the disk. Used by the state-rewind ring buffer
    // (EmulationController) to capture/restore frames cheaply. Returns an
    // empty vector on failure for the save; the load fills `error` and
    // returns false on a malformed buffer.
    std::vector<uint8_t> saveSnapshotToBuffer(const class M6502* cpu = nullptr) const;
    bool loadSnapshotFromBuffer(const std::vector<uint8_t>& buffer,
                                std::string& error, class M6502* cpu = nullptr);

    uint8_t memRead(uint16_t address);
    //uint8_t memReadAbsolute(uint16_t adr);
    void memWrite(uint16_t address, uint8_t value);
    const uint8_t* getMemoryPointer() const { return mem.data(); }
    uint8_t* getMemoryPointerMutable() { return mem.data(); }
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
    void setSiliconStrictMode(bool enabled);
    bool isSiliconStrictMode() const { return siliconStrictMode; }

    // Silicon fidelity toggles — forward to TMS9918::setVramNoiseOnReset.
    // Kept here so the EmulationController facade only ever talks to Memory.
    void setVramNoiseOnReset(bool enabled);
    bool isVramNoiseOnReset() const;

    // GEN2 HGR Graphic Card — power-on fidelity. The release card has four
    // independent cold-boot uncertainties, each individually toggleable so a
    // user can mix-and-match Silicon-Strict aspects (e.g. random latch but
    // zeroed DRAM for headless tests). The grouped setter sets all four; the
    // grouped getter returns true iff all four are on.
    //
    //   gen2RandomLatch        : soft-switch latch ($C250-$C257) randomized
    //                            at cold plug vs documented GRAPHICS + HIRES
    //                            + PAGE1 + MIX off pick.
    //   gen2RandomFloatingBus  : $C250-$C257 D6..D0 reads return xorshift32
    //                            noise vs the byte the video scanner is
    //                            presenting at that cycle.
    //   gen2RandomScannerPhase : vertical scanner phase (cycleCounter)
    //                            randomized at cold plug vs reset to 0.
    //   gen2RandomDramNoise    : 8 KB framebuffer DRAM at $2000-$3FFF seeded
    //                            with mt19937 noise at cold plug + hard
    //                            reset vs zeroed.
    //
    // All four default ON (Silicon Strict baseline) for every preset except
    // the Fantasy ones; setGen2RandomPowerOn flips all four together so
    // preset code and the master button still work as before.
    void setGen2RandomLatch(bool enabled)        { gen2RandomLatch        = enabled; }
    void setGen2RandomFloatingBus(bool enabled)  { gen2RandomFloatingBus  = enabled; }
    void setGen2RandomScannerPhase(bool enabled) { gen2RandomScannerPhase = enabled; }
    void setGen2RandomDramNoise(bool enabled)    { gen2RandomDramNoise    = enabled; }
    bool isGen2RandomLatch()        const { return gen2RandomLatch; }
    bool isGen2RandomFloatingBus()  const { return gen2RandomFloatingBus; }
    bool isGen2RandomScannerPhase() const { return gen2RandomScannerPhase; }
    bool isGen2RandomDramNoise()    const { return gen2RandomDramNoise; }
    void setGen2RandomPowerOn(bool enabled) {
        gen2RandomLatch        = enabled;
        gen2RandomFloatingBus  = enabled;
        gen2RandomScannerPhase = enabled;
        gen2RandomDramNoise    = enabled;
    }
    bool isGen2RandomPowerOn() const {
        return gen2RandomLatch && gen2RandomFloatingBus
            && gen2RandomScannerPhase && gen2RandomDramNoise;
    }

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

    // P-LAB IEC daughterboard for the microSD card. Drives the Commodore
    // IEC serial bus on unused VIA pins; backed by a virtual 1541 mounted
    // from disks/iec/dev8.d64. Cascade rule mirrors CodeTank/TMS9918:
    // enabling auto-enables microSD; disabling microSD also drops IEC.
    pom1::IECCard& getIECCard() { return *iecCard; }
    const pom1::IECCard& getIECCard() const { return *iecCard; }
    void setIECCardEnabled(bool b);
    bool isIECCardEnabled() const { return iecCardEnabled; }

    // CFFA1 CompactFlash Interface (Rich Dreher)
    CFFA1& getCFFA1() { return *cffa1; }
    const CFFA1& getCFFA1() const { return *cffa1; }
    void setCFFA1Enabled(bool b);
    bool isCFFA1Enabled() const { return cffa1Enabled; }
    int loadCFFA1Rom(void);

    // P-LAB Apple-1 Juke-Box (Claudio Parmigiani & Jacopo Rosselli):
    // memory-mapped flash or 28c256 EEPROM with a runtime jumper for the
    // RAM/ROM split and a Px/Sx bank-select latch at $CA00. Mutually
    // exclusive with CFFA1, microSD, Krusader, Wi-Fi Modem, A1-SID and
    // CodeTank in its RAM16/ROM32 jumper position (all share the
    // $4000-$CFFF address window).
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
    // in the ROM window land in the ROM buffer (and on disk). Ignored in
    // Flash chip mode (flash is always read-only in POM1).
    void setJukeBoxWritable(bool w);
    bool isJukeBoxWritable() const { return jukeBox->isWritable(); }
    // Physical chip selection — Flash (paged, 16 kB..512 kB) or 28c256
    // EEPROM (single-page, writable). Switching modes clears the ROM
    // buffer; a subsequent loadJukeBoxRom() picks a fresh image.
    void setJukeBoxChipMode(JukeBox::ChipMode m);
    JukeBox::ChipMode getJukeBoxChipMode() const { return jukeBox->getChipMode(); }
    // Load a Juke-Box ROM file (up to 512 kB in flash mode, exactly 32 kB
    // in EEPROM mode). Populates `lastError` on failure.
    int loadJukeBoxRom(void);  // default path: roms/jukebox.rom
    // UI-driven page navigation: write the bank-select latch ($CA00) and
    // refresh the flat ROM mirror so the CPU sees the new page immediately.
    void setJukeBoxBankRegister(uint8_t value);
    // Duplicate one 32 kB page over another in the in-memory ROM buffer
    // and refresh the mirror. Authoring helper — RAM-only until the user
    // calls saveJukeBoxRom().
    bool copyJukeBoxPage(uint8_t fromPage, uint8_t toPage, std::string& error);
    // Persist the current in-memory ROM buffer back to disk (defaults to
    // the path the buffer was loaded from).
    bool saveJukeBoxRom(const std::string& path, std::string& error) const;

    // P-LAB CodeTank (formerly bundled inside the Juke-Box). Standalone ROM
    // card carrying a 32 kB 28c256 with a board jumper that picks lower or
    // upper 16 kB; the selected half is mapped at $4000-$7FFF. Designed to
    // pair with the TMS9918 Graphic Card so games shipped on a CodeTank ROM
    // can run on a real Apple-1 + P-LAB stack without depending on the
    // cassette deck or microSD. Mutually exclusive with the Juke-Box and
    // any other card claiming $4000-$7FFF.
    CodeTank& getCodeTank() { return *codeTank; }
    const CodeTank& getCodeTank() const { return *codeTank; }
    void setCodeTankEnabled(bool b);
    bool isCodeTankEnabled() const { return codeTankEnabled; }
    void setCodeTankJumper(CodeTank::Jumper j);
    CodeTank::Jumper getCodeTankJumper() const { return codeTank->getJumper(); }
    // Hot-load a 32 kB CodeTank ROM by path (used by the CodeTank Library
    // window). Empty path falls back to the default probe candidates.
    int loadCodeTankRom(const std::string& path = std::string());

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

    // Telemetry side channel ($C440-$C443) — dev-only virtual test-harness port.
    // Server opens only when enabled (--telemetry-port). doc/TELEMETRY_SIDE_CHANNEL.md.
    TelemetryPort& getTelemetryPort() { return *telemetryPort; }
    const TelemetryPort& getTelemetryPort() const { return *telemetryPort; }
    void setTelemetryEnabled(bool b);
    bool isTelemetryEnabled() const { return telemetryEnabled.load(); }

    // P-LAB Apple-1 I/O Board & Real Time Clock (65C22 VIA + ATMEGA32 + DS3231)
    A1IO_RTC& getA1IO_RTC() { return *a1ioRtc; }
    const A1IO_RTC& getA1IO_RTC() const { return *a1ioRtc; }
    void setA1IO_RTCEnabled(bool b);
    bool isA1IO_RTCEnabled() const { return a1ioRtcEnabled; }

    // SWTPC PR-40 Printer (Steve Jobs' Oct. 1976 Interface Age hack)
    // Passive $D012 sniffer, no MMIO. See PR40Printer.h and
    // Memory::memRead(0xD012) for the busy-OR merge that implements the
    // DPDT switch wiring.
    PR40Printer& getPR40() { return *pr40Printer; }
    const PR40Printer& getPR40() const { return *pr40Printer; }
    void setPR40Enabled(bool b) { pr40Enabled = b; }
    bool isPR40Enabled() const { return pr40Enabled; }

    // SWTPC GT-6144 Graphic Terminal (1976) — write-only 64x96 monochrome
    // framebuffer at $D00A. See GT6144.h for the FSM and hardware notes.
    GT6144& getGT6144() { return *gt6144; }
    const GT6144& getGT6144() const { return *gt6144; }
    void setGT6144Enabled(bool b);
    bool isGT6144Enabled() const { return gt6144Enabled; }

    // Central audio device (mixes CassetteDevice + SID)
    AudioDevice& getAudioDevice() { return *audioDevice; }

    // /IRQ aggregator — see Memory::advanceCycles() for the wire-OR logic.
    // EmulationController calls this once at startup so peripherals can
    // pull /IRQ on the 6502 (TMS9918 vblank, 65C22 timers, 65C51 Rx, …).
    // TMS9918 /INT is wired by default (cf. dev/Programming_TMS9918.md §18 Bug N°2);
    // polling-only programs keep working because they leave interrupts
    // masked (never CLI) or never set R1 bit 5.
    void setCpuForIrq(M6502* c) { cpuForIrq = c; }

private:
    // Shared snapshot orchestration core — the file and in-memory save/load
    // entry points both funnel through these so the section layout stays in
    // one place.
    void writeSnapshotSections(pom1::SnapshotWriter& w, const class M6502* cpu) const;
    bool readSnapshotSections(pom1::SnapshotReader& r, std::string& error, class M6502* cpu);

    DisplayDevice* displayDevice = nullptr;     // non-owning; injected by EmulationController
    M6502* cpuForIrq = nullptr;                 // non-owning; aggregator target for setIRQ()
    
    // Clavier Apple 1 (0xD010 = KBD, 0xD011 = KBDCR)
    // REQUIRES: stateMutex held by caller. UI never touches these directly —
    // it queues via KeyboardController; drainTo() crosses the bridge inside
    // the emulation slice. CPU $D010/$D011 reads, terminal injection, and
    // snapshot publish/save/load all run on the emul thread under stateMutex.
    char lastKey = 0;
    bool keyReady = false;
    std::queue<char> keyBuffer;

    // Display Apple 1 (0xD012) - délai d'affichage
    int displayBusyCycles = 0;       // Cycles restants avant display ready
    int displayCharDelay = POM1_CPU_CLOCK_HZ / 60;    // 60 chars/sec à l'horloge CPU nominale

private :

    // Memory itself tab
    std::vector<uint8_t> mem;

    // Copy Juke-Box EEPROM into mem[] for the active ROM window (and clear
    // $4000-$7FFF in RAM32/ROM16 mode) so the flat array matches the bus.
    void applyJukeBoxFlatMemoryMirror();
    // Copy the CodeTank's selected 16 kB half into mem[$4000-$7FFF] so the
    // flat-memory shadow / Memory Viewer / snapshot reflect the bank wired
    // by the board jumper. PeripheralBus serves CPU reads via codeTank->
    // readByte() directly, so the mirror is purely cosmetic.
    void applyCodeTankFlatMemoryMirror();

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
    bool systemRamNoiseOnReset = false; // see setSystemRamNoiseOnReset()
    bool hgrFramebufferAttached = false;  // GEN2 HGR card supplies RAM at $2000-$3FFF
    // GEN2 HGR cold-boot fidelity — four independent knobs (Silicon Strict
    // bundles all four ON; the SILICON STRICT inspector exposes each one).
    bool gen2RandomLatch        = true;   // soft-switch latch random at cold plug
    bool gen2RandomFloatingBus  = true;   // $C25x D6..D0 reads = xorshift32 noise
    bool gen2RandomScannerPhase = true;   // vertical scanner cycle counter random
    bool gen2RandomDramNoise    = true;   // 8 KB framebuffer DRAM mt19937 vs zeroed
    Gen2VideoScanner gen2Scanner;         // GEN2 release video address generator (floating bus)
    // GEN2 soft-switch journal — recording half (current video frame) and
    // published half (last completed frame). See gen2PublishedVideoEvents().
    // A runaway program could flip a switch on every cycle; past the cap the
    // journal collapses to "no events at the current state" (the renderer's
    // fast path), which is the right degradation for a saturated frame.
    static constexpr size_t kGen2MaxEventsPerFrame = 4096;
    std::vector<Gen2VideoScanner::Event> gen2RecordingEvents;
    std::vector<Gen2VideoScanner::Event> gen2PublishedEvents;
    Gen2VideoScanner::DisplayState gen2RecordingFrameStart{};
    Gen2VideoScanner::DisplayState gen2PublishedFrameStart{};
    uint8_t gen2SoftSwitchRead(uint16_t address);
    void resetGen2VideoEventJournal();
    std::unordered_set<uint32_t> oorWarned;  // key = (addr<<1)|isWrite; capped at 64
    void checkOutOfRangeAccess(uint16_t address, bool isWrite);
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
    std::unique_ptr<pom1::IECCard> iecCard;
    bool iecCardEnabled = false;
    std::unique_ptr<CFFA1> cffa1;
    bool cffa1Enabled = false;
    std::unique_ptr<JukeBox> jukeBox;
    bool jukeBoxEnabled = false;
    std::unique_ptr<CodeTank> codeTank;
    bool codeTankEnabled = false;
    std::unique_ptr<WiFiModem> wifiModem;
    bool wifiModemEnabled = false;
    std::unique_ptr<TerminalCard> terminalCard;
    // Atomic: written from UI thread under stateMutex, but read without the
    // lock from the render thread (getTerminalCardIfEnabled) and from the
    // emulation thread after it releases stateMutex (post-slice reset/clear
    // drain in EmulationController::runEmulationSlice).
    std::atomic<bool> terminalCardEnabled{false};
    std::unique_ptr<TelemetryPort> telemetryPort;
    // Atomic for the same reason as terminalCardEnabled: queried off-thread.
    std::atomic<bool> telemetryEnabled{false};
    std::unique_ptr<A1IO_RTC> a1ioRtc;
    bool a1ioRtcEnabled = false;
    std::unique_ptr<PR40Printer> pr40Printer;
    bool pr40Enabled = false;
    std::unique_ptr<GT6144> gt6144;
    bool gt6144Enabled = false;
    bool siliconStrictMode = false;

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
    // Juke-Box ROM windows. Two disjoint windows (one per RAM/ROM jumper);
    // at most one is enabled at a time. Priority 20 so the card wins over
    // overlapping peripherals.
    PeripheralBus::Handle jukeBox32BusHandle = -1;   // RAM-16/ROM-32: $4000-$BFFF
    PeripheralBus::Handle jukeBox16BusHandle = -1;   // RAM-32/ROM-16: $8000-$BFFF
    PeripheralBus::Handle jukeBoxBankRegBusHandle = -1; // Px/Sx latch at $CA00
    // CodeTank ROM window — fixed $4000-$7FFF, jumper selects which 16 kB
    // half of the 28c256 is wired into the bus. Priority 20 to win against
    // overlapping peripherals (no $CA00 latch — CodeTank has no paging).
    PeripheralBus::Handle codeTankBusHandle = -1;
    PeripheralBus::Handle gt6144BusHandle    = -1;   // SWTPC GT-6144: $D00A, write-only
    // GEN2 release soft switches. One window spanning $C200-$C7FF; the
    // handler applies Bernie's decode SEL = $Cxxx & !A11 & A9 & A4 internally
    // (pages $C2/$C3/$C6/$C7 with A4=1) and mimics flat-RAM fall-through for
    // the addresses the card leaves undecoded.
    PeripheralBus::Handle gen2SoftSwitchBusHandle = -1;
    PeripheralBus::Handle telemetryBusHandle = -1;   // $C440-$C443, priority 30 (GEN2 A9=0 blind zone)

};

#endif // MEMORY_H

