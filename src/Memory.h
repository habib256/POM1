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
#include <utility>
#include <unordered_set>
#include <bitset>
#include <cstddef>
#include <utility>
#include <atomic>

// ── Cards are held by unique_ptr and forward-declared ────────────────────────
//
// Memory owns every peripheral, so Memory.h is the natural place for a card
// header to end up — and eleven of them had. That made this file, the most
// widely-included header in the project, the transitive owner of the whole
// card fleet: `touch src/JukeBox.h` recompiled 105 translation units (5 min 21 s
// of CPU, measured 23 août 2026) although only 15 name JukeBox at all, and
// D64Image.h — reached through IECCard.h -> Drive1541.h — was pulled into ~37
// TUs to serve 3.
//
// None of it was structural. `std::unique_ptr<T>` needs T complete only where
// the deleter is instantiated, i.e. in the destructor — and `~Memory()` has
// been out-of-line in Memory.cpp for a long time. The inline one-line getters
// (`return *cassetteDevice;`) do NOT require it either: binding a reference to
// an lvalue of incomplete type is well-formed. TMS9918 & friends below were
// already proving that. So the includes were pure accretion.
//
// JukeBox and CodeTank were the last two holdouts: they published NESTED types
// (`JukeBox::Jumper`, `JukeBox::ChipMode`, `CodeTank::Jumper`) used in the
// signatures here, and a nested type cannot be forward-declared. Those three
// enums now live at namespace scope in CardTypes.h — a header with no
// dependencies of its own — so the signatures below name pom1::JukeBoxJumper &
// co. and both card headers are gone too. Each card keeps a member alias
// (`using Jumper = pom1::JukeBoxJumper;`), so all 164 existing call sites still
// spell it `JukeBox::Jumper` and none of them had to change.
// Gen2VideoScanner stays: it is a BY-VALUE member.
//
// The rule for anything added later: a card belongs in this list, not in an
// #include. A TU that needs a card's definition includes that card's header.
class AudioDevice;
class CassetteDevice;
class CFFA1;
class CodeTank;
class GT6144;
class JukeBox;
class MicroSD;
class TMS9918;
class WiFiModem;
class TerminalCard;
class TelemetryPort;
class A1IO_RTC;
class PR40Printer;
class M6502;
namespace pom1 { class SID; class IECCard; }

#include "CardTypes.h"         // JukeBoxJumper / JukeBoxChipMode / CodeTankJumper
#include "Peripheral.h"        // pom1::Peripheral* in the CardSlot table below
                               // (it used to arrive by accident via JukeBox.h)
#include "Gen2VideoScanner.h"  // by-value member
#include <array>

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

    // Read-before-write trap (--ram-poison / --ram-trap). Diagnostic harness for
    // the TMS9918 "works on POM1, breaks on silicon" cause #2: seed RAM with a
    // deterministic sentinel byte (poison) instead of $00, and log the first CPU
    // read of any RAM cell in [0, kRamTrapEnd) that the program never wrote this
    // run — i.e. an uninitialised-RAM read that POM1's zero-fill silently makes
    // harmless. setRamPoison arms the sentinel fill; setRamWriteTrap arms the
    // logging. Both consumed at the next reset.
    void setRamPoison(bool enabled, uint8_t value = 0xA5) { systemRamPoison = enabled; systemRamPoisonByte = value; }
    void setRamWriteTrap(bool enabled) { ramWriteTrap = enabled; }
    uint64_t ramWriteTrapHits() const { return ramTrapHitCount; }

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
    // sketchs/doc/Programming_TMS9918.md §27 VRAM power-on init); the soft-switch journal resets on any transition.
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
    // Editor helper: drive the GEN2 soft switches to display a given page/mode
    // (GRAPHICS + full screen + PAGE1/2 + HIRES/lo-res) exactly as a program's
    // $C25x reads would. Used by the HGR Paint editor's HGR/HGR2/GR/GR2 selector
    // so switching the edited page also flips what the live card shows. No-op
    // when the GEN2 card is not attached.
    void setGen2DisplayMode(bool grMode, bool page2);
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

    // Frame-atomic GEN2 framebuffer latch (beam-accuracy Phase A): a copy of the
    // displayed pages ($2000-$5FFF) FROZEN at the V-blank rollover, so the
    // renderer always shows a self-consistent frame instead of an async, possibly
    // mid-XOR-update dirty-page snapshot. Index 0 == $2000. Captured in
    // advanceCycles() at frame rollover and seeded on plug; the renderer sources
    // the framebuffer from here (SnapshotPublisher overlays it on the mirror).
    const uint8_t* gen2FrameLatch(void) const { return gen2FrameLatchBuf.data(); }

    // Re-seed both GEN2 latches (beam + frame) from current RAM $2000-$5FFF.
    // Needed whenever RAM is mutated outside the CPU stream (snapshot/rewind
    // restore, UI hex-editor pokes) — the renderer only ever reads the frame
    // latch, so without this such edits stay invisible until the CPU completes
    // a full frame (never, while paused). No-op when the card is unplugged.
    void gen2ReseedLatchFromRam(void);

    // Load Memory from file
    int loadROM(const char* filename, uint16_t startAddress, size_t maxSize, const char* label);
    int loadBasic(void);
    // Zero the $E000-$EFFF BASIC ROM region — matches a pre-October-1976
    // bare Apple-1 that shipped with no BASIC cassette.
    void unloadBasic(void);
    // Microsoft BASIC 6502 (the OSI-derived 8 KB build with floating point),
    // ported to the Apple-1's PIA. Occupies the SAME $E000 window as Woz's
    // Integer BASIC, so the two are mutually exclusive by construction — see
    // MainWindow's BasicType. Cold start `E000R`, warm start `E003R`.
    //
    // The shipped roms/msbasic.rom is the full 8 KB image built for a replica's
    // EPROM: $E000-$FEFF BASIC, then its own Woz Monitor copy at $FF00 and
    // vectors at $FFFA. It is loaded whole and POM1's own WozMonitor.rom is then
    // put back on top — the bundled copy exists solely because the replica's
    // EPROM had to supply one, and POM1's is the canonical image the rest of the
    // emulator (and configureResetVectors) is built around.
    int loadMsBasic(void);
    // Lee Davison's Enhanced 6502 BASIC 2.22, ported to the Apple-1 PIA. NOT a
    // ROM window: a 12 KB image flashed into plain RAM at $5000-$7FFF, the same
    // model as Applesoft Lite on the microSD card. Cold start `5000R`, warm
    // `5003R`; user programs live in $0300-$4FFF. Sources + provenance in
    // dev/ehbasic/. Mutually exclusive with every card decoding inside
    // $5000-$7FFF (microSD's Applesoft window, CodeTank, Juke-Box).
    int loadEhBasic(void);
    int loadApplesoftLite(void);
    // Explicit variants — let the user pick which flavour to re-flash regardless
    // of which card is plugged. The auto-dispatching loadApplesoftLite()
    // above is what applyMachineConfig() uses for preset loading.
    int loadApplesoftLiteCFFA1(void);
    int loadApplesoftLiteSDCard(void);
    int loadKrusader(void);
    int loadWozMonitor(void);
    int loadAciRom(void);
    int loadExtendedAciRom(void);

    // ── ROM (re)load policy ─────────────────────────────────────────────
    // Both microSD boot paths avoid a redundant disk read by sniffing the
    // first two opcodes already sitting in the ROM window. Those magic bytes
    // used to be inline literals at the two call sites, which read as
    // unexplained numbers and gave no hint that changing a ROM's first
    // instruction would silently turn the guard into "always reload" (a
    // performance bug, invisible in tests). Naming the check keeps the
    // signature next to the ROM it identifies.
    //
    //   $FF00: D8 58  — Woz Monitor entry (CLD / CLI)
    //   $8000: A9 00  — SD CARD OS entry (LDA #$00)
    //
    // Returns true when the expected ROM already occupies `addr`, i.e. when
    // the load can be skipped.
    bool romSignaturePresent(uint16_t addr, uint8_t op0, uint8_t op1) const;
    bool wozMonitorPresent() const  { return romSignaturePresent(0xFF00, 0xD8, 0x58); }
    bool sdCardOsPresent() const    { return romSignaturePresent(0x8000, 0xA9, 0x00); }
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

    /// ROM files POM1 could not find and served from its compiled-in copy
    /// instead (Woz Monitor, ACI, Extended ACI). Empty on a normal launch.
    /// Non-empty means the binary is running somewhere its `roms/` directory
    /// is not — the ordinary consequence of copying the executable out of its
    /// folder — and the UI says so instead of leaving the user with a machine
    /// that boots but is not the one they think they have.
    const std::vector<std::string>& romFallbacksUsed() const { return romFallbacksUsed_; }

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

    // --- Memory watchpoints (debug) --------------------------------------
    // Halt the CPU after an instruction reads and/or writes a watched address.
    // Detection lives on the memRead/memWrite hot path behind the `anyWatch_`
    // gate (a single bool branch when nothing is armed). Session-local: never
    // serialized. The M6502 run loop polls isWatchpointTripped() and stops;
    // EmulationController parks the thread and the UI reads watchHit(). A
    // read-watch on an address that also holds executed code fires on the
    // instruction fetch too — set read-watches on data/I-O addresses.
    struct WatchHit { bool tripped = false; uint16_t address = 0; bool write = false; };
    void setWatchpoint(uint16_t address, bool onRead, bool onWrite);
    void clearWatchpoint(uint16_t address);
    void clearAllWatchpoints();
    bool hasWatchpoints() const { return anyWatch_; }
    int  watchpointCount() const { return watchCount_; }
    /// Per-address armed flags: bit0 = read-watch, bit1 = write-watch, 0 = none.
    uint8_t watchpointFlags(uint16_t address) const {
        return watchFlags_.empty() ? 0 : watchFlags_[address];
    }
    bool isWatchpointTripped() const { return watchHit_.tripped; }
    const WatchHit& watchHit() const { return watchHit_; }
    void clearWatchTrip() { watchHit_.tripped = false; }
    /// Collect up to `maxEntries` armed watchpoints as (address, flags) pairs in
    /// ONE pass — callers (the UI) must use this rather than probing
    /// watchpointFlags() per address, which would lock stateMutex 64 K times/frame.
    std::vector<std::pair<uint16_t, uint8_t>> listWatchpoints(int maxEntries) const;

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
    // True while injected/typed keystrokes are still pending delivery to the CPU
    // (one ready in the PIA latch and/or more queued in keyBuffer).
    bool hasBufferedInput() const { return keyReady || !keyBuffer.empty(); }
    // Drop any pending keystroke (latched + buffered). Called on reset so stale
    // injected keys can't be read by the freshly reset monitor / interpreter.
    void clearKeyboardInput() { keyReady = false; lastKey = 0; std::queue<char> e; std::swap(keyBuffer, e); }
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

    // Uncle Bernie's EXTENDED ACI (improved Gen-2 cassette interface,
    // Applefritter 2026): the second page of the 512x4 PROM pair, mapped at
    // $C500-$C5FF next to Woz's untouched $C100 firmware. It adds Apple-II
    // style checksums and the "extended format" (8-byte from/to headers,
    // equal addresses = autostart) so a tape loads with `C500R` then
    // `RX RX` instead of a hand-typed load range.
    //
    // Daughterboard rule, same shape as CodeTank/TMS9918: the page is on the
    // cassette card, so enabling it cascade-plugs the ACI and unplugging the
    // ACI cascade-unplugs it. No new MMIO — the extended firmware drives the
    // very same $C000 flip-flop and $C081 comparator.
    void setExtendedACIEnabled(bool b);
    bool isExtendedACIEnabled() const { return extendedAciEnabled; }

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
    // Forward to TMS9918::setFrameFlagHostile (worst-case F-flag silicon).
    void setTmsFrameFlagHostile(bool enabled);
    bool isTmsFrameFlagHostile() const;

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
    void setJukeBoxJumper(pom1::JukeBoxJumper j);
    pom1::JukeBoxJumper getJukeBoxJumper() const;   // out-of-line: member access needs JukeBox complete
    // EEPROM RW jumper. No bus change; writable only controls whether writes
    // in the ROM window land in the ROM buffer (and on disk). Ignored in
    // Flash chip mode (flash is always read-only in POM1).
    void setJukeBoxWritable(bool w);
    bool isJukeBoxWritable() const;                 // idem
    // Physical chip selection — Flash (paged, 16 kB..512 kB) or 28c256
    // EEPROM (single-page, writable). Switching modes clears the ROM
    // buffer; a subsequent loadJukeBoxRom() picks a fresh image.
    void setJukeBoxChipMode(pom1::JukeBoxChipMode m);
    pom1::JukeBoxChipMode getJukeBoxChipMode() const;  // idem
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
    void setCodeTankJumper(pom1::CodeTankJumper j);
    pom1::CodeTankJumper getCodeTankJumper() const;    // out-of-line: member access needs CodeTank complete
    // Hot-load a 32 kB CodeTank ROM by path (used by the CodeTank Library
    // window). Empty path falls back to the default probe candidates.
    int loadCodeTankRom(const std::string& path = std::string());
    // Hot-load a 32 kB CodeTank ROM straight from memory (no file). Returns 0 on
    // success, 1 on failure (getLastError() set). Used by the DevBench BASIC inject.
    int loadCodeTankRomBuffer(const std::vector<uint8_t>& data, const std::string& label);

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
    // TMS9918 /INT is wired by default (cf. sketchs/doc/Programming_TMS9918.md §18 Bug N°2);
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

    // ── PIA 6821 register banking ────────────────────────────────────────
    // Each of the PIA's two ports exposes TWO registers behind one address,
    // selected by bit 2 of that port's control register:
    //
    //   CRx bit 2 = 0  ->  the data address reads/writes the DATA DIRECTION
    //                      register (1 = output pin, 0 = input pin)
    //   CRx bit 2 = 1  ->  it reads/writes the peripheral (data) register
    //
    //   $D010 KBD / DDRA      $D011 CRA
    //   $D012 DSP / DDRB      $D013 CRB
    //
    // RESET clears all four, so the DDRs are selected at power-on — which is
    // exactly why the Woz Monitor opens with `LDY #$7F / STY $D012` (set
    // DDRB: bits 0-6 output, bit 7 the DA input) and only then `LDA #$A7 /
    // STA $D011 / STA $D013` to switch both ports to their data registers.
    //
    // POM1 used to model neither: $D013 fell through to plain RAM and $D012
    // always read back the last glyph. Uncle Bernie's Codebreaker probes
    // exactly this — CRB := 0, read $D012, expect $7F, restore CRB := $A7 —
    // and printed "I WANT TO RUN ON A REAL APPLE-1 !" when the DDR came back
    // wrong. Modelling it properly also RETIRES the old raw-$7F write filter:
    // the Woz reset's `STY $D012` now legitimately lands in DDRB instead of
    // reaching the display, so it can no longer paint a spurious '_'.
    // Seeded to the POST-RESET values, matching resetMemory(). The member
    // initialisers matter on their own: a Memory constructed and used without
    // a resetMemory() (several test harnesses, and any embedder) must still
    // present a PIA that software can print through.
    uint8_t piaCrA  = 0xA7;   // $D011 control register A (keyboard side)
    uint8_t piaCrB  = 0xA7;   // $D013 control register B (display side)
    uint8_t piaDdrA = 0x00;   // keyboard port: all inputs on a real Apple-1
    uint8_t piaDdrB = 0x7F;   // display port: what the Woz Monitor programs
    /// True while the port's data (not direction) register is selected.
    bool piaPortASelected() const { return (piaCrA & 0x04) != 0; }
    bool piaPortBSelected() const { return (piaCrB & 0x04) != 0; }

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

    // Memory watchpoints — see setWatchpoint(). watchFlags_ is lazily sized to
    // 64 KB on first arm (bit0=read, bit1=write). anyWatch_ gates the hot path;
    // watchCount_ tracks the number of armed addresses for the UI; watchHit_
    // latches the first watched access of a halted instruction.
    std::vector<uint8_t> watchFlags_;
    bool anyWatch_ = false;
    int  watchCount_ = 0;
    WatchHit watchHit_{};



    int ramSize; // in kilobytes
    int presetRamKB = 64;             // user-visible RAM ceiling for OOR warnings
    int oorAccessCount = 0;
    bool oorStrictMode = false;       // true: enforce bounds (reads→$FF, writes dropped)
    bool systemRamNoiseOnReset = false; // see setSystemRamNoiseOnReset()
    // Read-before-write trap state (see setRamPoison / setRamWriteTrap).
    static constexpr uint16_t kRamTrapEnd = 0x2000;  // watch ZP/stack/BSS/user RAM
    // Also watch the Parmigiani dual-bank HIGH RAM ($E000-$EFFF). On real P-LAB
    // Apple-1s that region is RAM (power-on garbage); POM1 pre-seeds it from
    // basic.rom, so a program that reads its high-bank BSS (e.g. TMS_Rogue's
    // map_buffer / monster pools at $E000+) before writing it gets deterministic
    // BASIC bytes here but garbage on silicon — the "works on POM1, breaks on
    // silicon" mask (TMS9918 cause #2). Neither poison nor the low-window trap
    // covered it. loadBasic() seeds via direct mem[] writes (not memWrite), so
    // the BASIC seed never marks these cells "written" for the trap.
    static constexpr uint16_t kRamTrapHiStart = 0xE000;
    static constexpr uint16_t kRamTrapHiEnd   = 0xF000;
    static constexpr bool ramTrapWatches(uint16_t a)
    { return a < kRamTrapEnd || (a >= kRamTrapHiStart && a < kRamTrapHiEnd); }
    bool     systemRamPoison    = false;
    uint8_t  systemRamPoisonByte = 0xA5;
    bool     ramWriteTrap       = false;
    uint64_t ramTrapHitCount    = 0;
    std::vector<uint8_t> ramWritten;   // 1 = written this run; sized to 64K, gated by ramTrapWatches()
    std::vector<uint8_t> ramTrapLogged;// 1 = already logged (one line per address)
    void resetRamWriteTrap();
    void noteRamWriteForTrap(uint16_t address) { if (ramTrapWatches(address) && !ramWritten.empty()) ramWritten[address] = 1; }
    void checkRamReadTrap(uint16_t address);
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
    std::array<uint8_t, 0x4000> gen2BeamLatchBuf{};   // $2000-$5FFF working latch, updated per scanline at beam time
    std::array<uint8_t, 0x4000> gen2FrameLatchBuf{};  // the beam latch FROZEN at the V-blank rollover (what the renderer reads)
    uint8_t gen2SoftSwitchRead(uint16_t address);
    void gen2LatchScanline(int line);   // beam-accuracy Phase B: latch one scanline (both pages)
    void resetGen2VideoEventJournal();
    std::unordered_set<uint32_t> oorWarned;  // key = (addr<<1)|isWrite; capped at 64
    void checkOutOfRangeAccess(uint16_t address, bool isWrite);
    bool writeInRom = false;   // ROM windows write-protected by default (see initMemory)
    /// True when `address` sits in a window that is physically unwriteable on
    /// the real machine (Woz Monitor PROM, ACI PROM, Uncle Bernie's extended
    /// ACI page) and the ROM write-protect is on.
    ///
    /// Lives here rather than inline in memWrite() because memWrite is NOT the
    /// only path that reaches mem[]: a PeripheralBus handler registered over a
    /// broad window answers FIRST and may fall through to flat RAM for the
    /// addresses it does not decode. GEN2's soft-switch handler spans
    /// $C200-$C7FF for exactly that reason — and its fall-through swallowed the
    /// extended ACI page, so plugging the HGR card silently made that PROM
    /// writable. Any handler with a flat-RAM fall-through must consult this.
    bool isRomWriteProtected(uint16_t address) const
    {
        if (writeInRom) return false;
        if (address >= 0xFF00) return true;                      // Woz Monitor PROM
        if (address >= 0xC100 && address <= 0xC1FF) return true;  // ACI PROM
        // Uncle Bernie's extended ACI page — the other half of the same PROM
        // pair. Gated on its own flag so a machine without the Gen-2 cassette
        // card keeps plain RAM here.
        if (extendedAciEnabled && address >= 0xC500 && address <= 0xC5FF) return true;
        return false;
    }
    std::string lastError;

    /// See romFallbacksUsed(). Appended by noteRomFallback(), deduplicated so a
    /// preset switch (which re-loads every ROM) cannot grow it without bound.
    std::vector<std::string> romFallbacksUsed_;
    void noteRomFallback(const char* filename);
    std::unique_ptr<CassetteDevice> cassetteDevice;
    // All expansion cards start UNPLUGGED. MainWindow::applyMachineConfig
    // re-plugs them 15 frames after the CPU has been running — plugging a
    // card (especially audio-source cards like SID and the cassette deck)
    // before the CPU has issued any cycle produces silent / broken cards
    // that only recover when the user toggles them manually. See the
    // pendingCardEnableFrames rationale in MainWindow_ImGui.h.
    bool aciEnabled = false;
    bool extendedAciEnabled = false;
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
    // True only while readSnapshotSections() is running. The MEM section restores
    // the full 64 KB before the FLAGS section re-plugs cards, so any card-enable
    // side effect that rewrites mem[] (e.g. the Juke-Box flat mirror's
    // $4000-$7FFF clear, which is real user RAM in RAM32/ROM16 mode) would clobber
    // the just-restored RAM. Suppress those cosmetic mirror writes during restore.
    bool snapshotRestoreInProgress = false;
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
    PeripheralBus::Handle cassetteInputBusHandle  = -1; // $C081 read = tape input (priority 5, wins over toggle)
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

public:
    // ── Card registry (snapshot serialization) ──────────────────────────
    // ONE ordered table replacing what used to be four hand-synced lists that
    // had to stay in lockstep, all in this file: the FLAGS bitmap pack, the
    // FLAGS unpack, the per-card section write order, and the read-dispatch
    // vector. Adding a card meant editing all four and silently corrupting
    // save-states if you missed one.
    //
    // Iterating this table in order reproduces every one of those sequences
    // byte for byte — that the four orders already agreed (once the flag-only
    // rows are interleaved at their historical positions) is what makes the
    // collapse safe rather than a format change. Row order is therefore
    // LOAD-BEARING twice over: it fixes the on-disk section order, and the
    // unpack has real ordering constraints (IEC cascades onto microSD, so it
    // must follow it; GEN2 attaches last). Append new cards at the end, and
    // never reorder existing rows without bumping the snapshot version.
    struct CardSlot {
        // Section name. Also the uniqueness key — see kCardNamesUnique below.
        // nullptr for a flag-only row (a bit in FLAGS with no card section:
        // the A1-SID Special Edition variant, the cassette-audio and
        // silicon-strict mode bits, and the GEN2 HGR attach).
        const char* name;
        // FLAGS bit, or 0 for a row that owns a section but no enable bit.
        uint32_t    flag;
        // Read/write the enable state. Function pointers rather than
        // pointer-to-member because the members are not uniform: most are
        // plain bools, terminalCardEnabled is a std::atomic<bool>, and GEN2
        // needs a bus handle flipped alongside the member. Captureless
        // lambdas convert to these, so each row stays a one-liner.
        bool (*isEnabled)(const Memory&);
        void (*setEnabled)(Memory&, bool);
        // The peripheral owning this section, or nullptr for a flag-only row.
        pom1::Peripheral* (*card)(const Memory&);
    };
    // The table. Defined in MemorySnapshot.cpp next to its only users.
    static const std::array<CardSlot, 17>& cardSlots();

private:
};

#endif // MEMORY_H

