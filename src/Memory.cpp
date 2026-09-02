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

#include <algorithm>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <cmath>

#include "Memory.h"
#include "CardTopology.h"
#include "MemoryImageLoader.h"
#include "FileBytes.h"
#include "Logger.h"
#include "M6502.h"
#include "TMS9918.h"
#include "MicroSD.h"
#include "WiFiModem.h"
#include "TerminalCard.h"
#include "TelemetryPort.h"
#include "A1IO_RTC.h"
#include "PR40Printer.h"
#include "CFFA1.h"
#include "GT6144.h"
#include "JukeBox.h"
#include "CodeTank.h"
// Cards Memory.h now only forward-declares (see the note at the top of that
// header): the TU that constructs and drives them names their headers itself.
#include "AudioDevice.h"
#include "CassetteDevice.h"
#include "SID.h"
#include "IECCard.h"
//#include "configuration.h"
//#include "pia6820.h"

namespace {

// Woz ACI ROM (256 B) — canonical Apple 1 dump, byte-for-byte equivalent
// to the authoritative wozaci.asm / wozaci.txt reference (Steve Wozniak,
// 1976). Used as the compiled-in fallback if `roms/ACI.rom` is missing.
// CRITICAL: chars in the input buffer at $0200+ retain the high bit set
// by $D010 on every keypress, and the ROM's hex parser handles that bit
// natively — `CMP #"R"` assembles to `CMP #$D2`, `EOR #"0"` to
// `EOR #$B0`, etc. Publicly-circulating dumps that strip those high
// bits (`EOR #$30`, `CMP #$52`) only work against bit-7-stripped storage
// and will make the parser restart at $C100 on every char here. See
// tests/aci_tape_loading_test.cpp for the regression pinning this.
constexpr uint8_t kAciRom[0x100] = {
    0xA9,0xAA,0x20,0xEF,0xFF,0xA9,0x8D,0x20,0xEF,0xFF,0xA0,0xFF,0xC8,0xAD,0x11,0xD0,
    0x10,0xFB,0xAD,0x10,0xD0,0x99,0x00,0x02,0x20,0xEF,0xFF,0xC9,0x9B,0xF0,0xE1,0xC9,
    0x8D,0xD0,0xE9,0xA2,0xFF,0xA9,0x00,0x85,0x24,0x85,0x25,0x85,0x26,0x85,0x27,0xE8,
    0xBD,0x00,0x02,0xC9,0xD2,0xF0,0x56,0xC9,0xD7,0xF0,0x35,0xC9,0xAE,0xF0,0x27,0xC9,
    0x8D,0xF0,0x20,0xC9,0xA0,0xF0,0xE8,0x49,0xB0,0xC9,0x0A,0x90,0x06,0x69,0x88,0xC9,
    0xFA,0x90,0xAD,0x0A,0x0A,0x0A,0x0A,0xA0,0x04,0x0A,0x26,0x24,0x26,0x25,0x88,0xD0,
    0xF8,0xF0,0xCC,0x4C,0x1A,0xFF,0xA5,0x24,0x85,0x26,0xA5,0x25,0x85,0x27,0xB0,0xBF,
    0xA9,0x40,0x20,0xCC,0xC1,0x88,0xA2,0x00,0xA1,0x26,0xA2,0x10,0x0A,0x20,0xDB,0xC1,
    0xD0,0xFA,0x20,0xF1,0xC1,0xA0,0x1E,0x90,0xEC,0xA6,0x28,0xB0,0x98,0x20,0xBC,0xC1,
    0xA9,0x16,0x20,0xCC,0xC1,0x20,0xBC,0xC1,0xA0,0x1F,0x20,0xBF,0xC1,0xB0,0xF9,0x20,
    0xBF,0xC1,0xA0,0x3A,0xA2,0x08,0x48,0x20,0xBC,0xC1,0x68,0x2A,0xA0,0x39,0xCA,0xD0,
    0xF5,0x81,0x26,0x20,0xF1,0xC1,0xA0,0x35,0x90,0xEA,0xB0,0xCD,0x20,0xBF,0xC1,0x88,
    0xAD,0x81,0xC0,0xC5,0x29,0xF0,0xF8,0x85,0x29,0xC0,0x80,0x60,0x86,0x28,0xA0,0x42,
    0x20,0xE0,0xC1,0xD0,0xF9,0x69,0xFE,0xB0,0xF5,0xA0,0x1E,0x20,0xE0,0xC1,0xA0,0x2C,
    0x88,0xD0,0xFD,0x90,0x05,0xA0,0x2F,0x88,0xD0,0xFD,0xBC,0x00,0xC0,0xA0,0x29,0xCA,
    0x60,0xA5,0x26,0xC5,0x24,0xA5,0x27,0xE5,0x25,0xE6,0x26,0xD0,0x02,0xE6,0x27,0x60,
};

// Woz Monitor (Steve Wozniak, 1976) — the 256 bytes at $FF00 without which
// the machine does nothing at all: no prompt, no reset vector, no way in.
// Compiled-in fallback for a missing `roms/WozMonitor.rom`, mirroring the ACI
// ROM above. Byte-identical to the shipped file (sha256
// e5af0d1c4057bd8e0ef5cb069c208ff7cc0984a7dff53b12c5cf119de8cb5c25), which
// tests/rom_fallback_smoke_test.cpp re-checks so the two cannot drift.
//
// WHY THIS EXISTS: POM1 used to log "Cannot find ROM file: WozMonitor.rom" and
// carry on booting a machine that could not run — a black screen whose only
// explanation sat in a log file. Launching the binary from anywhere but its own
// directory is the ordinary way to hit that, and it is the single most likely
// support question from someone who just moved POM1.exe.
constexpr uint8_t kWozMonitorRom[0x100] = {
    0xD8,0x58,0xA0,0x7F,0x8C,0x12,0xD0,0xA9,0xA7,0x8D,0x11,0xD0,0x8D,0x13,0xD0,0xC9,
    0xDF,0xF0,0x13,0xC9,0x9B,0xF0,0x03,0xC8,0x10,0x0F,0xA9,0xDC,0x20,0xEF,0xFF,0xA9,
    0x8D,0x20,0xEF,0xFF,0xA0,0x01,0x88,0x30,0xF6,0xAD,0x11,0xD0,0x10,0xFB,0xAD,0x10,
    0xD0,0x99,0x00,0x02,0x20,0xEF,0xFF,0xC9,0x8D,0xD0,0xD4,0xA0,0xFF,0xA9,0x00,0xAA,
    0x0A,0x85,0x2B,0xC8,0xB9,0x00,0x02,0xC9,0x8D,0xF0,0xD4,0xC9,0xAE,0x90,0xF4,0xF0,
    0xF0,0xC9,0xBA,0xF0,0xEB,0xC9,0xD2,0xF0,0x3B,0x86,0x28,0x86,0x29,0x84,0x2A,0xB9,
    0x00,0x02,0x49,0xB0,0xC9,0x0A,0x90,0x06,0x69,0x88,0xC9,0xFA,0x90,0x11,0x0A,0x0A,
    0x0A,0x0A,0xA2,0x04,0x0A,0x26,0x28,0x26,0x29,0xCA,0xD0,0xF8,0xC8,0xD0,0xE0,0xC4,
    0x2A,0xF0,0x97,0x24,0x2B,0x50,0x10,0xA5,0x28,0x81,0x26,0xE6,0x26,0xD0,0xB5,0xE6,
    0x27,0x4C,0x44,0xFF,0x6C,0x24,0x00,0x30,0x2B,0xA2,0x02,0xB5,0x27,0x95,0x25,0x95,
    0x23,0xCA,0xD0,0xF7,0xD0,0x14,0xA9,0x8D,0x20,0xEF,0xFF,0xA5,0x25,0x20,0xDC,0xFF,
    0xA5,0x24,0x20,0xDC,0xFF,0xA9,0xBA,0x20,0xEF,0xFF,0xA9,0xA0,0x20,0xEF,0xFF,0xA1,
    0x24,0x20,0xDC,0xFF,0x86,0x2B,0xA5,0x24,0xC5,0x28,0xA5,0x25,0xE5,0x29,0xB0,0xC1,
    0xE6,0x24,0xD0,0x02,0xE6,0x25,0xA5,0x24,0x29,0x07,0x10,0xC8,0x48,0x4A,0x4A,0x4A,
    0x4A,0x20,0xE5,0xFF,0x68,0x29,0x0F,0x09,0xB0,0xC9,0xBA,0x90,0x02,0x69,0x06,0x2C,
    0x12,0xD0,0x30,0xFB,0x8D,0x12,0xD0,0x60,0x00,0x00,0x00,0x0F,0x00,0xFF,0x00,0x00,
};


// Uncle Bernie's EXTENDED ACI page (256 B) — the second half of the 512x4
// PROM pair on his improved Gen-2 cassette interface, mapped at $C500-$C5FF
// (`xaci.bin`, Applefritter, august 2026). Compiled-in fallback if
// `roms/XACI.rom` is missing.
//
// It carries NO new I/O: the card is still Woz's $C000 flip-flop + $C081
// comparator. What the page does at `C500R` is relocate the stock ACI ROM
// into the stack page and re-vector it:
//
//   $C503  copy $C100-$C1FF -> $0100-$01FF          (RAM copy, patchable)
//   $C50D  $018B := $60 (RTS)                       (shared 'R'/'W' exit)
//   $C512  $0136 := $54, $013A := $50               (retarget the ROM's
//          'R' and 'W' BEQs at that RTS, so the extended page — not Woz's
//          firmware — decides what a read/write means)
//   $C51C  $0184 := $C5, $01B5 := $C5               (high bytes of the two
//          `JSR $C1F1` calls -> `JSR $C5F1`)
//   $C524  JSR $0100                                (reuse Woz's hex parser)
//
// The last patch is the load-bearing one twice over: $C5F1 replaces Woz's
// "compare pointer to end address" helper with a checksum accumulator (the
// Apple-II-style checksum byte that makes a tape readable on both machines),
// AND it retires the RAM copy's own $01F1-$01FF tail — which is exactly the
// handful of bytes the relocated code needs for its stack, since it runs
// out of page 1 with SP starting at $FF.
//
// Format on tape: 8-byte headers carrying from/to addresses (equal
// addresses = autostart), so `RX RX` loads a block without the operator
// typing a load range. Backward compatible — an unmodified ACI or an
// Apple-II reads the same recording by subtracting 8 from each <from>
// address and skipping the autostart block.
constexpr uint8_t kExtendedAciRom[0x100] = {
    0xA2,0xFF,0x9A,0xE8,0xBD,0x00,0xC1,0x9D,0x00,0x01,0xE8,0xD0,0xF7,0xA9,0x60,0x8D,
    0x8B,0x01,0xA9,0x54,0x8D,0x36,0x01,0xA9,0x50,0x8D,0x3A,0x01,0xA9,0xC5,0x8D,0x84,
    0x01,0x8D,0xB5,0x01,0x20,0x00,0x01,0x48,0xA0,0xFF,0x84,0x2A,0x84,0x2C,0x84,0x2B,
    0xC8,0xBD,0x01,0x02,0xC9,0xD8,0xD0,0x03,0xA0,0x80,0xE8,0xC8,0x84,0x2D,0x86,0x28,
    0x68,0xC9,0xD2,0xF0,0x08,0x20,0x70,0x01,0x20,0x25,0x01,0xF0,0xDA,0x24,0x2D,0x10,
    0x04,0xA9,0x06,0x85,0x24,0x20,0x8D,0x01,0xB0,0x05,0xA0,0x32,0x20,0xA4,0x01,0xA5,
    0x2A,0xF0,0x05,0xA9,0xBF,0x4C,0x1C,0xFF,0x26,0x2D,0x90,0xDC,0xA2,0x03,0xB5,0x01,
    0x95,0x24,0x55,0x03,0x95,0x03,0xCA,0x10,0xF5,0x05,0x04,0xD0,0xDD,0x6C,0x24,0x00,
    0xBE,0xA0,0x0E,0xA6,0x2D,0x10,0x0A,0xA0,0x17,0xE0,0x88,0x90,0x09,0xF0,0x3E,0xA0,
    0x0C,0x20,0xF1,0xC1,0x90,0x2E,0xE6,0x2D,0x90,0x17,0xE0,0x0C,0xF0,0x26,0xCA,0xD0,
    0x06,0xA2,0x07,0xA9,0x0A,0xD0,0x08,0xCA,0xE0,0x88,0xD0,0x05,0xA9,0x0B,0x88,0x85,
    0x2D,0x8A,0x29,0x0F,0xAA,0xB5,0x23,0x48,0xA2,0x00,0x41,0x26,0x45,0x2A,0x85,0x2A,
    0x68,0x4C,0x7A,0x01,0xA0,0x13,0x24,0x2D,0x30,0x08,0xC8,0xD0,0x05,0xE6,0x2D,0x18,
    0xA0,0x1A,0x4C,0x87,0x01,0x20,0xF1,0xC1,0xA0,0x32,0x90,0x12,0xA2,0x81,0xA9,0x01,
    0x88,0x24,0x2D,0xF0,0x06,0xA2,0xA0,0xE6,0x2D,0x18,0x88,0x8E,0xB1,0x01,0x4C,0xB8,
    0x01,0xA8,0x28,0xB0,0x02,0xA1,0x26,0x45,0x2A,0x85,0x2A,0x68,0x90,0x83,0xB0,0xD5,
};

} // namespace

Memory::Memory(bool initializeAudioHardware, pom1::ResourceLocator locator,
               pom1::IAudioService* injectedAudio)
    : resources_(std::move(locator))
{
    const pom1::ResourceLocator& resources = resources_;
    if (injectedAudio) {
        audio = injectedAudio;
    } else {
        ownedAudio = std::make_unique<AudioDevice>(initializeAudioHardware);
        audio = ownedAudio.get();
    }
    // Pass the audio device's actual sample rate (44.1 kHz requested but
    // miniaudio may negotiate 48 kHz on Apple Silicon, and the browser
    // AudioContext may also force a different rate on WASM) so both
    // cassette and SID produce samples at the rate the OS will consume
    // them — otherwise their tempo drifts by the rate ratio.
    const uint32_t actualRate = audio->getActualSampleRate();
    cassetteDevice = std::make_unique<CassetteDevice>();
    cassetteDevice->setAudioAvailable(audio->isAvailable());
    cassetteDevice->setAudioOutputSampleRate(actualRate);
    cassetteDevice->setAciActive(aciEnabled);
    // NOTE: no addSource here. The cassette is registered on the mixer
    // via activateCassetteAudioSource() after the synchronous card
    // transaction. Adding the source here would expose a half-configured deck.
    tms9918 = std::make_unique<TMS9918>();
    // NOTE: no SID here — sidChip() builds it on first use. See there.
    microSD = std::make_unique<MicroSD>();
    // Where the data lives is the locator's business, not three hand-rolled
    // ../ walks that climbed different distances (see ResourceLocator.h).
    if (const auto sd = resources.findDirectory("sdcard"); !sd.empty())
        microSD->setSDCardPath(sd.string());
    iecCard = std::make_unique<pom1::IECCard>();
    // Device-8 disk image. MVP supports a single drive.
    if (const auto dev8 = resources.find("disks/iec/dev8.d64"); !dev8.empty())
        iecCard->mountDisk(dev8.string());
    wifiModem = std::make_unique<WiFiModem>();
    terminalCard = std::make_unique<TerminalCard>();
    pr40Printer = std::make_unique<PR40Printer>();
    gt6144 = std::make_unique<GT6144>();
    a1ioRtc = std::make_unique<A1IO_RTC>();
    cffa1 = std::make_unique<CFFA1>();
    if (const auto cf = resources.find("cfcard/cfcard.po"); !cf.empty())
        cffa1->openDiskImage(cf.string());
    jukeBox = std::make_unique<JukeBox>();
    codeTank = std::make_unique<CodeTank>();
    terminalCard->setKeyInjector([this](char key, bool raw) {
        if (raw) setKeyPressedRaw(key);
        else setKeyPressed(key);
    });

    telemetryPort = std::make_unique<TelemetryPort>();
    telemetryPort->setKeyInjector([this](char key, bool raw) {
        if (raw) setKeyPressedRaw(key);
        else setKeyPressed(key);
    });

    // Register peripherals on the bus. Each entry starts disabled; enable()
    // is flipped by setXxxEnabled. Priority 0 for non-overlapping peripherals;
    // TMS9918 later uses a higher priority to win at $CC00/$CC01 vs SID.
    a1ioRtcBusHandle = bus.registerHandle(
        "A1IO_RTC", {0x2000, 0x200F}, /*priority*/ 0,
        [this](uint16_t a) { return a1ioRtc->readRegister(a); },
        [this](uint16_t a, uint8_t v) { a1ioRtc->writeRegister(a, v); });
    bus.setEnabled(a1ioRtcBusHandle, a1ioRtcEnabled);

    // CFFA1: registered before MicroSD so that at overlapping addresses
    // ($A000-$A00F) CFFA1 wins (matches the original inline dispatch order).
    // The presets make the two cards mutually exclusive anyway.
    //  - $9000-$AFDF: firmware ROM — read returns ROM byte; writes are silently
    //    swallowed by the bus (no write handler → bus consumes the access).
    //  - $AFE0-$AFFF: ATA/IDE register window — full read/write.
    cffa1RomBusHandle = bus.registerHandle(
        "CFFA1_ROM", {0x9000, 0xAFDF}, /*priority*/ 0,
        [this](uint16_t a) { return cffa1->readByte(a); },
        // Explicit no-op write handler: ROM swallows writes (returns true
        // from tryWrite, blocking the fall-through to raw mem[]).
        [](uint16_t, uint8_t) { /* CFFA1 firmware ROM is read-only */ });
    bus.setEnabled(cffa1RomBusHandle, cffa1Enabled);

    cffa1RegBusHandle = bus.registerHandle(
        "CFFA1_REG", {0xAFE0, 0xAFFF}, /*priority*/ 0,
        [this](uint16_t a) { return cffa1->readByte(a); },
        [this](uint16_t a, uint8_t v) { cffa1->writeByte(a, v); });
    bus.setEnabled(cffa1RegBusHandle, cffa1Enabled);

    microSDBusHandle = bus.registerHandle(
        "microSD", {0xA000, 0xA00F}, /*priority*/ 0,
        [this](uint16_t a) { return microSD->readRegister(a); },
        [this](uint16_t a, uint8_t v) { microSD->writeRegister(a, v); });
    bus.setEnabled(microSDBusHandle, microSDEnabled);

    wifiModemBusHandle = bus.registerHandle(
        "WiFiModem", {0xB000, 0xB003}, /*priority*/ 0,
        [this](uint16_t a) { return wifiModem->readRegister(a); },
        [this](uint16_t a, uint8_t v) { wifiModem->writeRegister(a, v); });
    bus.setEnabled(wifiModemBusHandle, wifiModemEnabled);

    // SID gets the whole $C800-$CFFF range at priority 0; TMS9918 overrides
    // $CC00/$CC01 at priority 10 so when both cards are enabled the VDP wins
    // those two addresses (matches the original inline dispatch).
    // SID's register window is 32 regs (addr & 0x1F); only regs 0-24 are writable.
    sidBusHandle = bus.registerHandle(
        "SID", {0xC800, 0xCFFF}, /*priority*/ 0,
        [this](uint16_t a) { return sidChip().readRegister(a & 0x1F); },
        [this](uint16_t a, uint8_t v) {
            uint8_t reg = a & 0x1F;
            if (reg <= 24) sidChip().writeRegister(reg, v);
        });
    bus.setEnabled(sidBusHandle, sidEnabled);

    // A1-AUDIO Special Edition — same MOS 6581/8580 chip, register window
    // relocated to $CC00-$CC1F (32 regs, internal decode is `addr & 0x1F`).
    // Collides with TMS9918 at $CC00/$CC01 — mutually exclusive at the
    // preset/UI layer. Routes to the same `sid` instance so the chip
    // model, ring buffer, and SID UI window stay shared between the two
    // variants (only one can be plugged at a time).
    sidSEBusHandle = bus.registerHandle(
        "SID_SE", {0xCC00, 0xCC1F}, /*priority*/ 0,
        [this](uint16_t a) { return sidChip().readRegister(a & 0x1F); },
        [this](uint16_t a, uint8_t v) {
            uint8_t reg = a & 0x1F;
            if (reg <= 24) sidChip().writeRegister(reg, v);
        });
    bus.setEnabled(sidSEBusHandle, sidSpecialEditionEnabled);

    tms9918BusHandle = bus.registerHandle(
        "TMS9918", {0xCC00, 0xCC01}, /*priority*/ 10,
        [this](uint16_t a) {
            // Snapshot the CPU PC for the silicon-strict drop trace —
            // when a future read drops, the log can name the offending
            // instruction directly. PC is sampled mid-opcode (after
            // operand fetch) so a 3-byte `LDA $CC0X` shows PC = (LDA
            // address + 3); subtract 3 in the disassembly to find it.
            if (cpuForIrq) tms9918->setLastAccessPc(cpuForIrq->getProgramCounter());
            if (a == 0xCC00) return tms9918->readData();
            // Status read ($CC01): sync the sprite scan to the exact beam
            // scanline first, so 5S / collision reflect the beam at the read
            // cycle — makes the 5S raster-split poll loop cycle-precise
            // (Étape 1). Same in-flight-cycle idiom as the write catch-up.
            const int inFlight = cpuForIrq
                ? static_cast<int>(cpuForIrq->getCurrentInstructionCycles()) : 0;
            tms9918->syncSpriteScanToBeam(inFlight);
            return tms9918->readControl();
        },
        [this](uint16_t a, uint8_t v) {
            if (cpuForIrq) tms9918->setLastAccessPc(cpuForIrq->getProgramCounter());
            // Beam/CPU sync (Étape 0): commit the framebuffer up to the exact
            // beam pixel BEFORE the register/VRAM mutation, so a mid-scanline
            // R7/R5/R6/R4/VRAM change splits the line at the right column. The
            // write lands mid-instruction; advanceCycles() books those cycles
            // only afterwards, so add the in-flight count for sub-instruction
            // accuracy (same idiom as the GEN2 video-event journal above).
            const int inFlight = cpuForIrq
                ? static_cast<int>(cpuForIrq->getCurrentInstructionCycles()) : 0;
            tms9918->renderBeamCatchUp(inFlight);
            if (a == 0xCC00) tms9918->writeData(v);
            else tms9918->writeControl(v);
        });
    bus.setEnabled(tms9918BusHandle, tms9918Enabled);

    // Apple-1 Cassette Interface — plugged on most boards, but the bare 4K
    // preset unplugs it via setACIEnabled(false). READ-only on the bus: the
    // write toggle stays inline in memWrite() because it's a sniffer (the
    // byte must still land in mem[] after the side effect, which the bus
    // model doesn't express).
    cassetteToggleBusHandle = bus.registerHandle(
        "ACI_toggle", {0xC000, 0xC0FF}, /*priority*/ 0,
        [this](uint16_t /*a*/) { return cassetteDevice->toggleOutput(); },
        /*onWrite=*/ {});
    bus.setEnabled(cassetteToggleBusHandle, aciEnabled);

    // $C081 specifically returns the tape input. Higher priority than the
    // generic toggle range so it wins for that one address.
    cassetteInputBusHandle = bus.registerHandle(
        "ACI_input", {0xC081, 0xC081}, /*priority*/ 5,
        [this](uint16_t /*a*/) { return cassetteDevice->readTapeInput(); },
        /*onWrite=*/ {});
    bus.setEnabled(cassetteInputBusHandle, aciEnabled);

    // P-LAB Juke-Box: only one of the two ROM windows is ever enabled at a
    // time (chosen by the RAM/ROM jumper). Priority 20 so the card wins the
    // full window against any other peripheral that happened to register
    // lower-priority handlers inside it (CFFA1 at $9000-$AFDF, microSD at
    // $A000-$A00F, Wi-Fi Modem at $B000-$B003). Real-hardware-wise those
    // cards are mutually exclusive with the Juke-Box, and setJukeBoxEnabled()
    // unplugs them defensively; the priority guard is belt-and-suspenders.
    jukeBox32BusHandle = bus.registerHandle(
        "JukeBox_ROM32", {0x4000, 0xBFFF}, /*priority*/ 20,
        [this](uint16_t a) { return jukeBox->readByte(a); },
        [this](uint16_t a, uint8_t v) { jukeBox->writeByte(a, v); });
    bus.setEnabled(jukeBox32BusHandle, false);

    jukeBox16BusHandle = bus.registerHandle(
        "JukeBox_ROM16", {0x8000, 0xBFFF}, /*priority*/ 20,
        [this](uint16_t a) { return jukeBox->readByte(a); },
        [this](uint16_t a, uint8_t v) { jukeBox->writeByte(a, v); });
    bus.setEnabled(jukeBox16BusHandle, false);

    // Juke-Box Px/Sx bank-select latch at $CA00. Write-only; reads fall
    // through to RAM/SID ($CA00 sits inside the SID window $C800-$CFFF,
    // which is why setJukeBoxEnabled() evicts SID + SID SE). Priority 15
    // so it wins against SID (priority 0) as belt-and-suspenders — normal
    // operation keeps SID unplugged while Juke-Box is on.
    jukeBoxBankRegBusHandle = bus.registerHandle(
        "JukeBox_BankReg", {0xCA00, 0xCA00}, /*priority*/ 15,
        /*onRead=*/ {},
        [this](uint16_t /*a*/, uint8_t v) {
            jukeBox->writeBankRegister(v);
            applyJukeBoxFlatMemoryMirror();
        });
    bus.setEnabled(jukeBoxBankRegBusHandle, false);

    // P-LAB CodeTank: fixed 16 kB ROM window at $4000-$7FFF, jumper selects
    // which 16 kB half of the 32 kB 28c256 is visible. No $CA00 latch.
    // Priority 20 — the SAME as the Juke-Box's $4000-$BFFF window, so if both
    // were somehow enabled the tie would be broken by registration order (the
    // Juke-Box registers first, so it would actually win). Moot in practice:
    // the Memory layer enforces single-card use (setCodeTankEnabled /
    // setJukeBoxEnabled mutually evict), so the two are never live together.
    codeTankBusHandle = bus.registerHandle(
        "CodeTank", {CodeTank::kBase, CodeTank::kEnd}, /*priority*/ 20,
        [this](uint16_t a) { return codeTank->readByte(a); },
        [this](uint16_t a, uint8_t v) { codeTank->writeByte(a, v); });
    bus.setEnabled(codeTankBusHandle, false);

    // SWTPC GT-6144 graphic terminal — write-only at $D00A. memWrite runs
    // bus.tryWrite BEFORE the $D0xx PIA-alias normalisation, so priority 0
    // is enough for the bus to intercept the byte before the keyboard-port
    // mirror rewrites the address. Reads are left unhandled (empty onRead)
    // so they fall through to the PIA-alias path — matches real hardware,
    // which has no read-back on this port.
    gt6144BusHandle = bus.registerHandle(
        "GT6144", {0xD00A, 0xD00A}, /*priority*/ 0,
        /*onRead=*/ {},
        [this](uint16_t /*a*/, uint8_t v) { gt6144->writeCommand(v); });
    bus.setEnabled(gt6144BusHandle, gt6144Enabled);

    // Uncle Bernie's GEN2 release soft switches. Decode (Bernie's PDF, Q6):
    //   SEL = $Cxxx & !A11 & A9 & A4
    // i.e. the eight switches mirror every 8 locations across $C2xx, $C3xx,
    // $C6xx, $C7xx wherever A4 = 1 ($C250-$C257 is the canonical block). One
    // bus window covers $C200-$C7FF; the lambdas re-check the decode and
    // mimic the flat-RAM fall-through for undecoded addresses ($C4xx/$C5xx
    // have A9 = 0; A4 = 0 offsets are skipped by the card's decoder).
    //   Read  (decoded): toggles the addressed switch AND returns HST0 in D7
    //                    with floating-bus noise in D6-D0 (read-only design).
    //   Write (decoded): ignored — a write would clash the card's D7 bus
    //                    driver, so the hardware doesn't react. Blocked here
    //                    (NOT pass-through) so the byte never lands in RAM.
    gen2SoftSwitchBusHandle = bus.registerHandle(
        "GEN2_softswitch", {0xC200, 0xC7FF}, /*priority*/ 0,
        [this](uint16_t a) -> uint8_t {
            if ((a & 0x0200) && (a & 0x0010)) return gen2SoftSwitchRead(a);
            return mem[a];   // undecoded → flat-RAM fall-through
        },
        [this](uint16_t a, uint8_t v) {
            if ((a & 0x0200) && (a & 0x0010)) return;  // switches ignore writes
            // Undecoded → flat-RAM fall-through, but this handler is reached
            // from bus.tryWrite() at the TOP of memWrite, i.e. BEFORE its ROM
            // write-protect. Uncle Bernie's extended ACI PROM at $C500-$C5FF
            // sits inside this window (A9 = 0 there, so the GEN2 decoder is
            // structurally blind to it), and plugging the HGR card therefore
            // used to make that PROM writable — silent corruption of the page
            // whose whole design note is that it cannot be moved or trimmed.
            if (isRomWriteProtected(a)) return;
            mem[a] = v;
            dirtyPages.set(static_cast<std::size_t>(a >> 8));
        });
    bus.setEnabled(gen2SoftSwitchBusHandle, hgrFramebufferAttached);

    // Telemetry side channel (dev-only virtual device, $C440-$C443). Sits in the
    // $C4xx A9=0 dead zone where GEN2's decoder (SEL = $Cxxx & !A11 & A9 & A4,
    // needs A9=1) is structurally blind; no other card claims $C4xx/$C5xx.
    // Priority 30 so it owns its four bytes over GEN2's broad $C200-$C7FF
    // pass-through handler. Enabled only via setTelemetryEnabled (--telemetry-port).
    telemetryBusHandle = bus.registerHandle(
        "Telemetry", {TelemetryPort::kBaseAddr, TelemetryPort::kEndAddr}, /*priority*/ 30,
        [this](uint16_t a) { return telemetryPort->readReg(a); },
        [this](uint16_t a, uint8_t v) {
            telemetryPort->writeReg(a, v);
            // Lock-step: an end-frame write may have armed the ACK gate. Halt the
            // CPU now so run() exits right after this STA (cycle-exact); the slice
            // loop parks until the harness ACKs. Game-transparent, no deadlock.
            if (cpuForIrq && telemetryPort->isAwaitingAck()) cpuForIrq->stop();
        });
    bus.setEnabled(telemetryBusHandle, telemetryEnabled.load());

    initMemory();
}

// Defined here (not defaulted in the header) so the forward-declared unique_ptr
// peripheral members get their complete type from this TU's includes. See
// Memory.h ~Memory().
Memory::~Memory()
{
    // An injected service outlives this machine and keeps mixing, so hand back
    // every source before the members that own them are destroyed;
    // removeSource returns only once no callback is still reading it.
    // Unconditional, not mirroring the enable flags: unregistering a source
    // that was never added is a no-op, and a card can be detached while its
    // audio source stays registered (the cassette's independent rail).
    if (audio) {
        if (cassetteDevice) audio->removeSource(cassetteDevice.get());
        if (sid) audio->removeSource(sid.get());
    }
}

uint8_t Memory::gen2SoftSwitchRead(uint16_t address)
{
    // $C250-$C257 mapping (1:1 port of Apple II $C050-$C057, Bernie Table 1):
    // A2-A1 pick the switch pair (TEXT / MIXED / PAGE / RES), A0 is the value.
    //   $C250 TEXT_OFF  $C251 TEXT_ON   $C252 MIX_OFF  $C253 MIX_ON
    //   $C254 PAGE_ONE  $C255 PAGE_TWO  $C256 LORES_ON $C257 HIRES_ON
    const int  sw    = address & 0x07;
    const bool value = (sw & 1) != 0;
    Gen2VideoScanner::DisplayState st = gen2Scanner.displayState();
    Gen2VideoScanner::EventKind kind = Gen2VideoScanner::EventKind::TextMode;
    switch (sw >> 1) {
        case 0: st.textMode  = value; kind = Gen2VideoScanner::EventKind::TextMode;  break;
        case 1: st.mixedMode = value; kind = Gen2VideoScanner::EventKind::MixedMode; break;
        case 2: st.page2     = value; kind = Gen2VideoScanner::EventKind::Page2;     break;
        case 3: st.hiRes     = value; kind = Gen2VideoScanner::EventKind::HiRes;     break;
    }
    gen2Scanner.setDisplayState(st);

    // Journal the flip at its in-instruction cycle: advanceCycles() runs
    // after the instruction completes, so the scanner counter still points
    // at the instruction's start — add the cycles the CPU has accumulated
    // for the in-flight instruction (POM2 pushVideoEventLocked idiom).
    const uint64_t emuCycle = gen2Scanner.cycle()
        + (cpuForIrq ? static_cast<uint64_t>(cpuForIrq->getCurrentInstructionCycles()) : 0u);
    gen2Scanner.journalEvent(emuCycle, kind, value);

    // HST0 in D7 (sampled at the access cycle). The low 7 bits are the
    // floating data bus, which Bernie's spec says software must NEVER rely on.
    // With "Floating-bus noise" ON (Silicon Strict default) we hand back
    // xorshift32 garbage so that unreliability is impossible to miss —
    // Bernie's explicit recommendation, to "show rookie programmers they do
    // something they shouldn't." OFF, we expose the deterministic byte the
    // video scanner is presenting (reproducible — headless tests / debugging).
    const uint8_t low7 = gen2RandomFloatingBus
        ? static_cast<uint8_t>(gen2Scanner.nextNoise() & 0x7F)
        : static_cast<uint8_t>(gen2Scanner.floatingBusAt(mem.data(), emuCycle) & 0x7F);
    return static_cast<uint8_t>((gen2Scanner.hst0At(emuCycle) << 7) | low7);
}

void Memory::setGen2DisplayMode(bool grMode, bool page2)
{
    // Replay the four soft-switch reads a program does to show a page — GRAPHICS
    // (TEXT off), full screen (MIXED off), PAGE1/2, HIRES/LORES. gen2SoftSwitchRead
    // sets the scanner state and journals each flip at the current cycle, so the
    // beam-race renderer picks up the new mode on the next frame. The returned
    // floating-bus byte is irrelevant here.
    if (!hgrFramebufferAttached) return;
    gen2SoftSwitchRead(0xC250);                     // TEXT off  -> graphics
    gen2SoftSwitchRead(0xC252);                     // MIXED off -> full screen
    gen2SoftSwitchRead(page2 ? 0xC255 : 0xC254);    // PAGE2 / PAGE1
    gen2SoftSwitchRead(grMode ? 0xC256 : 0xC257);   // LORES / HIRES
}

pom1::CardSet Memory::activeCards() const
{
    pom1::CardSet cards;
    if (aciEnabled) cards.add(pom1::CardId::Aci);
    if (tms9918Enabled) cards.add(pom1::CardId::Tms9918);
    if (sidEnabled) cards.add(pom1::CardId::Sid);
    if (sidSpecialEditionEnabled) cards.add(pom1::CardId::SidSpecialEdition);
    if (microSDEnabled) cards.add(pom1::CardId::MicroSD);
    if (cffa1Enabled) cards.add(pom1::CardId::Cffa1);
    if (jukeBoxEnabled) cards.add(pom1::CardId::JukeBox);
    if (codeTankEnabled) cards.add(pom1::CardId::CodeTank);
    if (wifiModemEnabled) cards.add(pom1::CardId::WifiModem);
    if (terminalCardEnabled.load()) cards.add(pom1::CardId::TerminalCard);
    if (a1ioRtcEnabled) cards.add(pom1::CardId::A1IoRtc);
    if (pr40Enabled) cards.add(pom1::CardId::Pr40);
    if (gt6144Enabled) cards.add(pom1::CardId::Gt6144);
    if (iecCardEnabled) cards.add(pom1::CardId::Iec);
    if (hgrFramebufferAttached) cards.add(pom1::CardId::Gen2);
    if (extendedAciEnabled) cards.add(pom1::CardId::ExtendedAci);
    return cards;
}

void Memory::setCardEnabledFromTopology(pom1::CardId card, bool enabled)
{
    switch (card) {
    case pom1::CardId::Aci: setACIEnabled(enabled); break;
    case pom1::CardId::Tms9918: setTMS9918Enabled(enabled); break;
    case pom1::CardId::Sid: setSIDEnabled(enabled); break;
    case pom1::CardId::SidSpecialEdition: setSIDSpecialEditionEnabled(enabled); break;
    case pom1::CardId::MicroSD: setMicroSDEnabled(enabled); break;
    case pom1::CardId::Cffa1: setCFFA1Enabled(enabled); break;
    case pom1::CardId::JukeBox: setJukeBoxEnabled(enabled); break;
    case pom1::CardId::CodeTank: setCodeTankEnabled(enabled); break;
    case pom1::CardId::WifiModem: setWiFiModemEnabled(enabled); break;
    case pom1::CardId::TerminalCard: setTerminalCardEnabled(enabled); break;
    case pom1::CardId::A1IoRtc: setA1IO_RTCEnabled(enabled); break;
    case pom1::CardId::Pr40: setPR40Enabled(enabled); break;
    case pom1::CardId::Gt6144: setGT6144Enabled(enabled); break;
    case pom1::CardId::Iec: setIECCardEnabled(enabled); break;
    case pom1::CardId::Gen2: setHgrFramebufferAttached(enabled); break;
    case pom1::CardId::ExtendedAci: setExtendedACIEnabled(enabled); break;
    case pom1::CardId::Count:
    case pom1::CardId::Invalid:
        break;
    }
}

void Memory::applyTopologyRelations(pom1::CardId requested, bool enabled)
{
    const pom1::CardTransitionPlan plan =
        pom1::planCardToggle(activeCards(), requested, enabled);

    // Reverse CardId order removes daughterboards before their hosts; forward
    // order attaches hosts before daughterboards. CardId's append-only order
    // deliberately preserves those three dependency pairs.
    for (std::size_t i = pom1::kCardCount; i-- > 0;) {
        const auto card = static_cast<pom1::CardId>(i);
        if (card != requested && plan.detach.contains(card))
            setCardEnabledFromTopology(card, false);
    }
    for (std::size_t i = 0; i < pom1::kCardCount; ++i) {
        const auto card = static_cast<pom1::CardId>(i);
        if (card != requested && plan.attach.contains(card))
            setCardEnabledFromTopology(card, true);
    }
}

void Memory::setTMS9918Enabled(bool b)
{
    if (tms9918Enabled == b) return;
    applyTopologyRelations(pom1::CardId::Tms9918, b);
    tms9918Enabled = b;
    bus.setEnabled(tms9918BusHandle, b);
}

void Memory::setSiliconStrictMode(bool enabled)
{
    siliconStrictMode = enabled;
    tms9918->setSiliconStrictMode(enabled);
    jukeBox->setSiliconStrictMode(enabled);
}

void Memory::setVramNoiseOnReset(bool enabled)
{
    tms9918->setVramNoiseOnReset(enabled);
}

bool Memory::isVramNoiseOnReset() const
{
    return tms9918->isVramNoiseOnReset();
}

void Memory::setTmsFrameFlagHostile(bool enabled)
{
    tms9918->setFrameFlagHostile(enabled);
}

bool Memory::isTmsFrameFlagHostile() const
{
    return tms9918->isFrameFlagHostile();
}

void Memory::setACIEnabled(bool b)
{
    if (aciEnabled == b) return;
    applyTopologyRelations(pom1::CardId::Aci, b);
    aciEnabled = b;
    bus.setEnabled(cassetteToggleBusHandle, b);
    bus.setEnabled(cassetteInputBusHandle, b);
    // Let the cassette device know so future loadTape() calls pick the
    // right mode: pulses while ACI is plugged, raw-audio streaming once
    // the card is out.
    if (cassetteDevice) cassetteDevice->setAciActive(b);
    if (b) {
        loadAciRom();
    } else {
        std::fill_n(mem.begin() + 0xC100, 0x100, static_cast<uint8_t>(0));
        markPagesDirty(0xC100, 0x100);
    }
}

void Memory::setExtendedACIEnabled(bool b)
{
    if (extendedAciEnabled == b) return;
    applyTopologyRelations(pom1::CardId::ExtendedAci, b);
    extendedAciEnabled = b;
    if (b) {
        loadExtendedAciRom();
    } else {
        std::fill_n(mem.begin() + 0xC500, 0x100, static_cast<uint8_t>(0));
        markPagesDirty(0xC500, 0x100);
    }
}

void Memory::activateCassetteAudioSource()
{
    if (cassetteAudioActive) return;
    audio->addSource(cassetteDevice.get());
    cassetteAudioActive = true;
}

void Memory::deactivateCassetteAudioSource()
{
    if (!cassetteAudioActive) return;
    audio->removeSource(cassetteDevice.get());
    cassetteAudioActive = false;
}

void Memory::setTerminalCardEnabled(bool b)
{
    terminalCardEnabled = b;
    terminalCard->setEnabled(b);   // the TCP listener follows the plug
}

void Memory::setA1IO_RTCEnabled(bool b)
{
    a1ioRtcEnabled = b;
    bus.setEnabled(a1ioRtcBusHandle, b);
}

void Memory::setGT6144Enabled(bool b)
{
    // Replugging the card reseeds the framebuffer so the user sees the
    // Intel 2102 bistable power-on noise each time, matching the real card.
    if (b && !gt6144Enabled) gt6144->reset();
    gt6144Enabled = b;
    bus.setEnabled(gt6144BusHandle, b);
}

void Memory::setTelemetryEnabled(bool b)
{
    if (b == telemetryEnabled.load()) return;
    telemetryEnabled.store(b);
    bus.setEnabled(telemetryBusHandle, b);
    // The TCP server is opened only while the port is active. reset() (re)starts
    // it + clears the FIFOs; shutdown() stops it + drops the client.
    if (b) telemetryPort->reset();
    else   telemetryPort->shutdown();
}

void Memory::setPresetRamKB(int kb)
{
    if (kb <= 0) kb = 64;
    if (kb > 64) kb = 64;
    presetRamKB = kb;
    resetOutOfRangeAccessCount();
}

void Memory::resetOutOfRangeAccessCount(void)
{
    oorAccessCount = 0;
    oorWarned.clear();
}

// Apple-1 OOR range for the current preset.
//   - presetRamKB == 8: Parmigiani dual-bank — RAM lives at $0000-$0FFF and
//     $E000-$EFFF, so the OOR gap is [$1000, $8000). The high bank is above
//     $8000 and naturally falls outside the gap (the existing dispatch order
//     hits ROM / peripheral pages there before this check).
//   - otherwise: contiguous low — gap is [presetRamKB * 1024, $8000).
//   - GEN2 HGR carve-out: when the card is plugged it brings its own DRAM
//     behind the two HGR pages ($2000-$3FFF and $4000-$5FFF) — those ranges
//     become RAM-backed regardless of presetRamKB, matching real hardware
//     (the release card's onboard DRAM mirrors CPU writes via the VMA
//     write-through latch). Without this exception, strict mode would
//     silently drop every pixel write on small-RAM presets.
static inline bool isOorAddress(uint16_t address, int presetRamKB,
                                bool hgrFramebufferAttached)
{
    if (presetRamKB >= 64) return false;
    if (address >= 0x8000) return false;
    // GEN2 brings its own DRAM behind both HGR pages: page 1 $2000-$3FFF and
    // page 2 $4000-$5FFF (Bernie's PDF Q5/Q9 — the release card is a RAM
    // expansion whose graphics pages mirror CPU writes via write-through).
    if (hgrFramebufferAttached && address >= 0x2000 && address < 0x6000) {
        return false;
    }
    const uint16_t oorLow = (presetRamKB == 8)
        ? 0x1000
        : static_cast<uint16_t>(presetRamKB * 1024);
    return address >= oorLow;
}

void Memory::checkOutOfRangeAccess(uint16_t address, bool isWrite)
{
    // User-RAM ceiling: warn when a program touches RAM past the preset budget.
    // Skip ROM/IO ($8000+) and the dual-bank high RAM ($E000-$EFFF) — those
    // are handled earlier in the dispatch.
    if (!isOorAddress(address, presetRamKB, hgrFramebufferAttached)) return;
    ++oorAccessCount;
    if (oorWarned.size() >= 64) return;
    uint32_t key = (static_cast<uint32_t>(address) << 1) | (isWrite ? 1u : 0u);
    if (oorWarned.insert(key).second) {
        std::ostringstream oss;
        oss << "Out-of-range " << (isWrite ? "write to" : "read from")
            << " $" << std::hex << std::uppercase << address << std::dec
            << " (preset RAM: " << presetRamKB << " KB)";
        pom1::log().warn("Mem", oss.str());
    }
}

void Memory::initMemory(){
    ramSize = 64;  // Ouaahh 64Kbytes !
    // Protect the ROM windows by default — the Woz Monitor ($FF00-$FFFF, incl.
    // the reset vector) and ACI PROM ($C100-$C1FF) are physically unwriteable on
    // real hardware, so a stray CPU `STA` there must be a no-op. The ROM loaders
    // and configureResetVectors below write mem[] directly (not via memWrite), and
    // every injection path (loadHexDump direct-writes; runFromSync / DevBench set
    // writeInRom=true around their writes), so protection-by-default doesn't block
    // any legitimate load. Settings → "Write-protect ROMs" toggles it live.
    writeInRom = false;
    if (mem.size() < (size_t)(ramSize * 1024))
        mem.resize(ramSize * 1024, 0);
    // Power-on RAM fill. Mirror resetMemory() so the cold-boot profile is
    // consistent across BOTH halves of a hard reset. CRUCIAL: hardReset() runs
    // resetMemory() *then* initMemory(); a plain zero-fill here wipes the noise
    // resetMemory() just laid down, so silicon presets used to boot with
    // ZP/BSS = $00 and POM1 masked programs that read uninitialised RAM
    // (TMS9918 cause #2 — Galaga/Rogue/Mandel). The ROM loads below overwrite
    // their own windows regardless of the fill. Poison takes precedence (a
    // deterministic sentinel for the --ram-poison read-before-write trap).
    if (systemRamPoison) {
        std::fill(mem.begin(), mem.begin() + ramSize * 1024, systemRamPoisonByte);
    } else if (systemRamNoiseOnReset) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(0, 255);
        for (int i = 0; i < ramSize * 1024; ++i)
            mem[i] = static_cast<uint8_t>(dist(gen));
    } else {
        std::fill(mem.begin(), mem.end(), static_cast<uint8_t>(0));
    }
    resetRamWriteTrap();
    markAllPagesDirty();
    loadBasic();
    if (aciEnabled) loadAciRom();
    if (extendedAciEnabled) loadExtendedAciRom();
    loadWozMonitor();
    loadSDCardRom();
    // microSDEnabled stays false here — MainWindow's applyMachineConfig
    // is the single source of truth for which cards are plugged, and it
    // defers every plug by 15 frames after CPU startup.
    cassetteDevice->reset();
    tms9918->reset();
    // resetChip() (not full reset()) — when the SID is registered as an
    // audio source, touching ringTail here would race with the audio
    // callback's SPSC drain. Residual samples drain naturally via
    // fillAudioBuffer in a few ms of tail audio.
    if (sid) sid->resetChip();
    microSD->reset();
    // The IEC daughterboard rides on microSD's VIA PORTB — reset its serial-bus
    // FSM too, or it desyncs from the freshly-cleared VIA after a mid-transfer
    // reset (busReset is only otherwise called on plug/unplug).
    if (iecCard) iecCard->busReset();
    wifiModem->reset();
    terminalCard->reset();
    a1ioRtc->reset();
    cffa1->reset();
    jukeBox->reset();
    codeTank->reset();
    // Re-seat zero-page $3F to the Juke-Box boot page so the PM's first
    // instruction stays self-consistent after a hard reset (see
    // setJukeBoxEnabled for the reasoning — real multi-page P-LAB ROMs
    // don't need this, but POM1 tolerates partial ROMs).
    if (jukeBoxEnabled) mem[0x003F] = jukeBox->getBootPage();
    gt6144->reset();
    configureResetVectors(0xFF00);

    // GEN2 HGR carries its own 8 KB DRAM at $2000-$3FFF — re-seed bistable
    // noise on every init. EmulationController::hardReset() invokes
    // resetMemory() *then* initMemory(); without this seed the zero-fill
    // above wipes the noise that resetMemory just laid down, leaving the
    // GEN2 window solid black after every hard reset.
    if (hgrFramebufferAttached) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(0, 255);
        for (int i = 0x2000; i < 0x4000; ++i) {
            mem[i] = static_cast<uint8_t>(dist(gen));
        }
        markPagesDirty(0x2000, 0x2000);
    }

    setWriteInRom(0);
}

// --- Read-before-write trap (--ram-poison / --ram-trap) -------------------
void Memory::resetRamWriteTrap()
{
    if (ramWriteTrap) {
        // Sized to the full address space so the high-bank window ($E000-$EFFF)
        // is directly indexable; ramTrapWatches() gates which cells are live.
        ramWritten.assign(0x10000, 0);
        ramTrapLogged.assign(0x10000, 0);
    } else {
        ramWritten.clear();
        ramTrapLogged.clear();
    }
    ramTrapHitCount = 0;
}

void Memory::checkRamReadTrap(uint16_t address)
{
    if (!ramTrapWatches(address) || ramWritten.empty()) return;
    if (ramWritten[address] || ramTrapLogged[address]) return;
    ramTrapLogged[address] = 1;
    ++ramTrapHitCount;
    const uint16_t pc = cpuForIrq ? cpuForIrq->getProgramCounter() : 0;
    char msg[128];
    std::snprintf(msg, sizeof(msg),
                  "[RAM TRAP #%llu] read uninitialised $%04X (value $%02X) from PC $%04X",
                  (unsigned long long)ramTrapHitCount, address, mem[address], pc);
    pom1::log().warn("RAMTRAP", msg);
}

void Memory::setHgrFramebufferAttached(bool e)
{
    // Off→on transition seeds the 8 KB framebuffer with mt19937 noise.
    // Real Uncle Bernie GEN2 hardware holds its own DRAM at $2000-$3FFF;
    // on a cold plug or first power-up the chips show bistable random
    // values (same model GT-6144 and TMS9918 already use). resetMemory()
    // re-seeds on F5 hard reset; this setter covers the cases resetMemory
    // can't: first boot (applyMachineConfig skips hardReset on its very
    // first invocation) and runtime menu/toolbar plug. The Silicon Strict
    // Inspector can opt out via gen2DramNoiseOnPlug=false for tests that
    // need a deterministic blank framebuffer.
    const bool wasAttached = hgrFramebufferAttached;
    hgrFramebufferAttached = e;
    bus.setEnabled(gen2SoftSwitchBusHandle, e);
    if (e != wasAttached) {
        // Any plug/unplug invalidates the beam journal — events carry scanner
        // cycles that only make sense within one continuous power-on session.
        // The soft-switch latch itself is left alone (Bernie: RESET never
        // touches it; POM1 keeps whatever state the latch held).
        gen2Scanner.resetJournal();
    }
    if (e && !wasAttached) {
        // Cold plug: re-seed the soft-switch latch + xorshift noise + scanner
        // phase + 8 KB framebuffer DRAM. Each of the four aspects is gated by
        // its own knob (Silicon Strict default = all four ON, so the card
        // behaves like Bernie's release silicon: PLD POR indeterminate, DRAM
        // bistable bytes, scanner starting somewhere in the middle of a
        // frame). Any knob OFF restores the documented cold state for that
        // aspect — useful for headless tests and pre-Phase-2 demos that need
        // reproducible behaviour piece by piece.
        std::random_device rd;
        std::mt19937 gen(rd());
        gen2Scanner.applyPowerOnState(gen2RandomLatch,
                                      gen2RandomScannerPhase,
                                      gen());
        if (gen2RandomDramNoise) {
            std::uniform_int_distribution<int> dist(0, 255);
            for (int i = 0x2000; i < 0x4000; ++i) {
                mem[i] = static_cast<uint8_t>(dist(gen));
            }
        } else {
            for (int i = 0x2000; i < 0x4000; ++i) mem[i] = 0;
        }
        markPagesDirty(0x2000, 0x2000);
    }
    // Seed both beam-accuracy latches (working + published) so the renderer has a
    // complete frame to show before the first V-blank rollover captures one.
    if (e) {
        std::memcpy(gen2BeamLatchBuf.data(), mem.data() + 0x2000, gen2BeamLatchBuf.size());
        gen2FrameLatchBuf = gen2BeamLatchBuf;
    }
}

void Memory::resetMemory(void)
{
    // PIA state as it stands the instant the Woz Monitor's reset code has
    // run — NOT the bare silicon power-on state (all zeros). See Memory.h for
    // why: POM1 jumps straight into programs (--run, DevBench Run, jumpTo)
    // without executing $FF00, and a zeroed CRB would leave the DDRs selected,
    // so the Monitor's ECHO would write its characters into DDRB and then hang
    // forever on its own `BIT $D012 / BMI` once a character with bit 7 set
    // landed there. Seeding the post-reset values makes every entry path see
    // the same PIA a real machine presents to software.
    piaCrA  = 0xA7;   // what `LDA #$A7 / STA $D011` leaves behind
    piaCrB  = 0xA7;   // ... and `STA $D013`
    piaDdrA = 0x00;   // keyboard: all inputs
    piaDdrB = 0x7F;   // display: `LDY #$7F / STY $D012`, bits 0-6 out, PB7 in

    // RAM power-on profile. Default = zero-init (legacy, preserves tests
    // and snapshots). When systemRamNoiseOnReset is enabled, seed RAM with
    // mt19937 noise — matches what real Apple-1 6502 RAM actually shows
    // at cold boot (bistable noise). Combined with silicon-strict mode
    // this surfaces programs that assume RAM = 0.
    if (systemRamPoison) {
        // Deterministic sentinel fill — the --ram-poison read-before-write trap.
        for (int i = 0; i < ramSize * 1024; ++i) mem[i] = systemRamPoisonByte;
    } else if (systemRamNoiseOnReset) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(0, 255);
        for (int i = 0; i < ramSize * 1024; ++i) {
            mem[i] = static_cast<uint8_t>(dist(gen));
        }
    } else {
        for (int i = 0; i < ramSize * 1024; ++i) {
            mem[i] = 0;
        }
    }
    resetRamWriteTrap();
    // GEN2 HGR carries its own 8 KB DRAM at $2000-$3FFF. Real Uncle Bernie
    // hardware shows bistable noise on cold boot (matches GT-6144 and the
    // TMS9918 VRAM model). When the card is plugged AND gen2RandomDramNoise
    // is on (Silicon Strict default), force noise on this region regardless
    // of systemRamNoiseOnReset — HGR DRAM is independent of the Apple-1
    // main-RAM bank and never starts cleared on real silicon. Knob OFF zeros
    // the bank instead — useful for headless tests / pre-Phase-2 demos.
    if (hgrFramebufferAttached) {
        if (gen2RandomDramNoise) {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<int> dist(0, 255);
            for (int i = 0x2000; i < 0x4000; ++i) {
                mem[i] = static_cast<uint8_t>(dist(gen));
            }
        } else {
            for (int i = 0x2000; i < 0x4000; ++i) mem[i] = 0;
        }
    }
    markAllPagesDirty();
    // Apple-1 hard reset — zero only the bits of the ACI that are
    // electrically tied to the reset line (output flip-flop, CPU cycle
    // counter). The tape / transport / recording state is mechanical
    // and must survive a host reset.
    cassetteDevice->resetApple1Side();
    tms9918->reset();
    // See initMemory() — resetChip() avoids the ringTail race when the
    // SID stays registered as an audio source across hardReset.
    if (sid) sid->resetChip();
    microSD->reset();
    // Keep the IEC daughterboard FSM in sync with the microSD VIA it rides on
    // (see resetMemory) — otherwise an F5 mid-transfer leaves it desynced.
    if (iecCard) iecCard->busReset();
    wifiModem->reset();
    terminalCard->reset();
    a1ioRtc->reset();
    cffa1->reset();
    jukeBox->reset();
    codeTank->reset();
    if (jukeBoxEnabled) mem[0x003F] = jukeBox->getBootPage();
    gt6144->reset();
}


void Memory::configureResetVectors(uint16_t vectorAddress)
{
    // Only set the RESET vector ($FFFC/$FFFD). The 6502 has three vectors —
    // NMI ($FFFA/$FFFB), RESET ($FFFC/$FFFD), IRQ/BRK ($FFFE/$FFFF) — and
    // mass-overwriting all three to the same target was clobbering authentic
    // Apple-1 values from WozMonitor.rom (NMI=$0F00, IRQ=$0000) on every
    // load/hard-reset. That broke any P-LAB program that installs its own
    // IRQ handler via the canonical Apple-1 trampoline at $0000 — IRQs
    // ended up jumping to the loaded program's entry instead of routing
    // through the user's RAM trampoline.
    mem[0xFFFC] = static_cast<uint8_t>(vectorAddress & 0xFF);
    mem[0xFFFD] = static_cast<uint8_t>((vectorAddress >> 8) & 0xFF);
    markPagesDirty(0xFFFC, 2);
}

void Memory::setWriteInRom(bool b)
{
    writeInRom = b;
}

bool Memory::getWriteInRom(void)
{
    return writeInRom;
}

void Memory::setWatchpoint(uint16_t address, bool onRead, bool onWrite)
{
    const uint8_t flags = static_cast<uint8_t>((onRead ? 0x01 : 0) | (onWrite ? 0x02 : 0));
    if (flags == 0) { clearWatchpoint(address); return; }
    if (watchFlags_.empty()) watchFlags_.assign(0x10000, 0);
    if (watchFlags_[address] == 0) ++watchCount_;
    watchFlags_[address] = flags;
    anyWatch_ = true;
}

void Memory::clearWatchpoint(uint16_t address)
{
    if (watchFlags_.empty() || watchFlags_[address] == 0) return;
    watchFlags_[address] = 0;
    if (--watchCount_ <= 0) { watchCount_ = 0; anyWatch_ = false; }
}

void Memory::clearAllWatchpoints()
{
    if (!watchFlags_.empty())
        std::fill(watchFlags_.begin(), watchFlags_.end(), static_cast<uint8_t>(0));
    watchCount_ = 0;
    anyWatch_ = false;
    watchHit_.tripped = false;
}

std::vector<std::pair<uint16_t, uint8_t>> Memory::listWatchpoints(int maxEntries) const
{
    std::vector<std::pair<uint16_t, uint8_t>> out;
    if (watchFlags_.empty() || maxEntries <= 0) return out;
    for (int a = 0; a <= 0xFFFF && static_cast<int>(out.size()) < maxEntries; ++a)
        if (watchFlags_[a])
            out.emplace_back(static_cast<uint16_t>(a), watchFlags_[a]);
    return out;
}

std::string Memory::busStateSummary() const
{
    std::ostringstream oss;
    auto tag = [&](const char* n, PeripheralBus::Handle h) {
        oss << " " << n << "=" << (bus.isEnabled(h) ? "ON" : "off");
    };
    tag("a1ioRtc",   a1ioRtcBusHandle);
    tag("cffa1ROM",  cffa1RomBusHandle);
    tag("cffa1REG",  cffa1RegBusHandle);
    tag("microSD",   microSDBusHandle);
    tag("wifi",      wifiModemBusHandle);
    tag("SID",       sidBusHandle);
    tag("SID_SE",    sidSEBusHandle);
    tag("TMS9918",   tms9918BusHandle);
    tag("ACIToggle", cassetteToggleBusHandle);
    tag("ACIInput",  cassetteInputBusHandle);
    tag("JukeBox32", jukeBox32BusHandle);
    tag("JukeBox16", jukeBox16BusHandle);
    tag("JukeBoxBankReg", jukeBoxBankRegBusHandle);
    tag("CodeTank", codeTankBusHandle);
    oss << " | presetRamKB=" << presetRamKB
        << " oorStrict=" << (oorStrictMode ? "ON" : "off")
        << " writeInRom=" << (writeInRom ? "1" : "0");
    return oss.str();
}

int Memory::loadROM(const char* filename, uint16_t startAddress, size_t maxSize, const char* label)
{
    lastError.clear();

    // This site used to climb ONE level while the disk-image probes climbed
    // two and the CodeTank probe three, so running from build/tests/ found the
    // disks but not the ROMs — and POM1 then substituted its built-in Woz
    // Monitor with a WARN nobody reads. One search order now, via the locator.
    std::ifstream file;
    for (const std::string& rel : {std::string(filename),
                                   std::string("roms/") + filename}) {
        const std::filesystem::path resolved = resources_.find(rel);
        if (resolved.empty()) continue;
        file.open(resolved, std::ios::binary);
        if (file.is_open()) break;
    }

    if (!file.is_open()) {
        lastError = std::string("Cannot find ROM file: ") + filename;
        pom1::log().error("Mem", lastError);
        return 1;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff rawSize = file.tellg();
    file.seekg(0, std::ios::beg);

    if (rawSize < 0) {
        lastError = std::string(label) + " ROM: cannot determine file size";
        pom1::log().error("Mem", lastError);
        file.close();
        return 1;
    }

    const size_t fileSize = static_cast<size_t>(rawSize);
    if (fileSize > maxSize) {
        lastError = std::string(label) + " ROM too large (" + std::to_string(fileSize)
                  + " bytes, max " + std::to_string(maxSize) + ")";
        pom1::log().error("Mem", lastError);
        file.close();
        return 1;
    }

    std::vector<char> fileContent(fileSize);
    file.read(fileContent.data(), fileSize);
    const std::streamsize got = file.gcount();
    file.close();
    if (static_cast<size_t>(got) != fileSize) {
        lastError = std::string(label) + " ROM: short read ("
                  + std::to_string(got) + "/" + std::to_string(fileSize) + " bytes)";
        pom1::log().error("Mem", lastError);
        return 1;
    }

    for (size_t i = 0; i < fileContent.size(); ++i) {
        mem[startAddress + i] = (uint8_t)fileContent[i];
    }
    markPagesDirty(startAddress, fileContent.size());
    {
        std::ostringstream oss;
        oss << label << " loaded to 0x" << std::hex << std::uppercase << startAddress
            << ": " << std::dec << fileContent.size() << " bytes";
        pom1::log().info("Mem", oss.str());
    }
    return 0;
}

int Memory::loadBasic(void)
{
    return loadROM("basic.rom", 0xE000, 0x1000, "BASIC");
}

void Memory::unloadBasic(void)
{
    // $E000-$FEFF, not $E000-$EFFF: Microsoft BASIC is nearly twice the size of
    // Woz's Integer BASIC and shares the window, so clearing only the low 4 KB
    // would leave 3 KB of the other interpreter behind when switching flavours.
    // The region above $EFFF is bare RAM on an Apple-1 anyway (nothing else maps
    // there — see the memory map), so zeroing it costs nothing when the previous
    // occupant was Integer BASIC.
    std::fill_n(mem.begin() + 0xE000, 0x1F00, static_cast<uint8_t>(0));
    markPagesDirty(0xE000, 0x1F00);
}

int Memory::loadMsBasic(void)
{
    // The file is the replica's full 8 KB EPROM image ($E000-$FFFF): BASIC up to
    // $FEFF, then its own Woz Monitor copy at $FF00 and vectors at $FFFA. It is
    // kept whole so its sha256 still matches the published ROM (see
    // dev/msbasic/README.md) — loadROM cannot map a slice, and truncating the
    // file would break that provenance check.
    //
    // So: load it entire, then put POM1's canonical Woz Monitor back on top.
    // Doing that HERE rather than relying on a later reload in the preset path
    // keeps the function order-independent — a caller that flashes MS BASIC on
    // its own (the Hardware menu, --basic msbasic) must not be left running the
    // replica's tweaked monitor copy. The vectors are identical either way
    // (NMI $0F00, IRQ $0000), so nothing else has to be re-asserted.
    const int rc = loadROM("msbasic.rom", 0xE000, 0x2000, "Microsoft BASIC");
    if (rc != 0) return rc;
    return loadWozMonitor();
}

int Memory::loadEhBasic(void)
{
    return loadROM("ehbasic.rom", 0x5000, 0x3000, "EhBASIC");
}

int Memory::loadApplesoftLite(void)
{
    // CFFA1: txgx42/applesoft-lite + cffa1.s — 8 KB at $E000-$FFFF (includes Woz Monitor).
    if (cffa1Enabled) {
        return loadApplesoftLiteCFFA1();
    }
    // P-LAB microSD: APPLESOFT-FT.zip (Fast Terminal + SD OS 1.2) — 8 KB at $6000-$7FFF;
    // Integer BASIC stays at $E000; Woz Monitor at $FF00. Cold/warm: 6000R / 6003R.
    if (microSDEnabled) {
        return loadApplesoftLiteSDCard();
    }
    return loadApplesoftLiteCFFA1();
}

int Memory::loadApplesoftLiteCFFA1(void)
{
    return loadROM("applesoft-lite-cffa1.rom", 0xE000, 0x2000, "Applesoft Lite (CFFA1)");
}

int Memory::loadApplesoftLiteSDCard(void)
{
    // NOT a ROM window despite the loadROM() helper: on the real P-LAB card the
    // only EEPROM is the 8 KB SD CARD OS at $8000-$9FFF (manual §6.3). Applesoft
    // Lite is a file the SD CARD OS loads into the card's own RAM expansion at
    // $6000-$7FFF — hence `6000R` = "cold start (needed at least once)" and
    // `6003R` = warm start "it does not destroy the BASIC program in RAM".
    // POM1 pre-seeds those bytes so the user doesn't have to load it on every
    // boot, exactly like the $E000 Integer BASIC bank; writes are NOT blocked
    // (memWrite only protects $FF00+ and the ACI PROM), so the emulated
    // behaviour already matches the card. Only the label had to be honest.
    int ret = loadROM("applesoft-lite-microsd.rom", 0x6000, 0x2000, "Applesoft Lite (loaded in card RAM)");
    if (ret != 0) return ret;
    // The microSD build requires Woz Monitor at $FF00 for the SD OS to link
    // to. Reload it only when it was overwritten (typical case: user just
    // switched away from a CFFA1-flavoured Applesoft that spans $E000-$FFFF
    // and clobbered $FF00). Skipping the disk read when the ROM is still in
    // place keeps boot I/O to a minimum.
    if (!wozMonitorPresent()) {
        return loadWozMonitor();
    }
    return 0;
}

bool Memory::romSignaturePresent(uint16_t addr, uint8_t op0, uint8_t op1) const
{
    // addr is the ROM's entry point, so addr+1 is always in range for the two
    // call sites ($FF00, $8000); guard anyway so a future caller can't wrap.
    if (addr == 0xFFFF) return false;
    return mem[addr] == op0 && mem[addr + 1] == op1;
}

int Memory::loadKrusader(void)
{
    // The image is linked at $E000 (its first opcode is JMP $E2B0) and its
    // documented cold entry is $F000. Loading it at $A000 relocates neither
    // absolute jump and immediately escapes into unrelated RAM.
    return loadROM("krusader-1.3.rom", 0xE000, 0x2000, "Krusader");
}

void Memory::noteRomFallback(const char* filename)
{
    // Deduplicated: applyMachineConfig re-loads ROMs on every preset switch, and
    // the banner must not grow a new line each time the user changes machine.
    const std::string name(filename);
    for (const auto& f : romFallbacksUsed_) {
        if (f == name) return;
    }
    romFallbacksUsed_.push_back(name);
}

int Memory::loadWozMonitor(void)
{
    if (loadROM("WozMonitor.rom", 0xFF00, 0x100, "WOZ Monitor") == 0) {
        return 0;
    }

    // No file — fall back to the compiled-in copy rather than booting a machine
    // with $FF00 empty. Without the Monitor there is no reset vector and no
    // prompt: POM1 came up black and said so only in the log.
    for (size_t i = 0; i < sizeof(kWozMonitorRom); ++i) {
        mem[0xFF00 + i] = kWozMonitorRom[i];
    }
    markPagesDirty(0xFF00, sizeof(kWozMonitorRom));
    lastError.clear();
    noteRomFallback("WozMonitor.rom");
    pom1::log().warn("Mem", "WOZ Monitor loaded from built-in fallback to 0xFF00: " +
                            std::to_string(sizeof(kWozMonitorRom)) +
                            " bytes (roms/WozMonitor.rom not found)");
    return 0;
}

int Memory::loadAciRom(void)
{
    if (loadROM("ACI.rom", 0xC100, 0x100, "ACI ROM") == 0) {
        return 0;
    }

    for (size_t i = 0; i < sizeof(kAciRom); ++i) {
        mem[0xC100 + i] = kAciRom[i];
    }
    markPagesDirty(0xC100, sizeof(kAciRom));
    lastError.clear();
    noteRomFallback("ACI.rom");
    pom1::log().info("Mem", "ACI ROM loaded from built-in fallback to 0xC100: " +
                            std::to_string(sizeof(kAciRom)) + " bytes");
    return 0;
}

int Memory::loadExtendedAciRom(void)
{
    if (loadROM("XACI.rom", 0xC500, 0x100, "Extended ACI ROM") == 0) {
        return 0;
    }

    for (size_t i = 0; i < sizeof(kExtendedAciRom); ++i) {
        mem[0xC500 + i] = kExtendedAciRom[i];
    }
    markPagesDirty(0xC500, sizeof(kExtendedAciRom));
    lastError.clear();
    noteRomFallback("ExtendedACI.rom");
    pom1::log().info("Mem", "Extended ACI ROM loaded from built-in fallback to 0xC500: " +
                            std::to_string(sizeof(kExtendedAciRom)) + " bytes");
    return 0;
}

int Memory::loadBinary(const char* filename, uint16_t startAddress, int* bytesLoaded)
{
    if (bytesLoaded) *bytesLoaded = 0;
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        pom1::log().error("Mem", std::string("Cannot open file: ") + filename);
        return 1;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff rawSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // tellg() returns -1 on an openable-but-unseekable path (e.g. a procfs
    // node or FIFO). Assigning that straight into size_t yields SIZE_MAX, which
    // both wraps the size guard below and triggers a huge std::vector allocation
    // (uncaught std::length_error). Reject it cleanly, mirroring loadROM.
    if (rawSize < 0) {
        pom1::log().error("Mem", std::string("Cannot determine file size: ") + filename);
        file.close();
        return 1;
    }
    const size_t fileSize = static_cast<size_t>(rawSize);

    if (startAddress + fileSize > 0x10000) {
        std::ostringstream oss;
        oss << "File too large for address 0x" << std::hex << startAddress;
        pom1::log().error("Mem", oss.str());
        file.close();
        return 1;
    }

    std::vector<char> fileContent(fileSize);
    file.read(fileContent.data(), fileSize);
    file.close();

    for (size_t i = 0; i < fileContent.size(); ++i) {
        mem[startAddress + i] = (uint8_t)fileContent[i];
    }
    markPagesDirty(startAddress, fileContent.size());
    if (bytesLoaded) *bytesLoaded = static_cast<int>(fileContent.size());
    {
        std::ostringstream oss;
        oss << "Binary loaded: " << std::filesystem::path(filename).filename().string()
            << " (" << std::dec << fileContent.size() << " bytes at 0x"
            << std::hex << startAddress << ")";
        pom1::log().info("Mem", oss.str());
    }
    return 0;
}

int Memory::loadHexDump(const char* filename, uint16_t &startAddress, int* bytesLoaded,
                        std::vector<std::pair<uint16_t,uint16_t>>* zones)
{
    if (bytesLoaded) *bytesLoaded = 0;
    if (zones) zones->clear();

    // Size FIRST, then read. A hex dump is text describing at most 64 KB of
    // 6502 memory; anything past kMaxMemoryImageBytes is not one, and slurping
    // it to find that out is exactly the allocation the limit exists to avoid.
    // (parseMemoryImage repeats the check for its other callers.)
    const std::string displayName =
        std::filesystem::path(filename).filename().string();
    std::vector<uint8_t> raw;
    std::string readError;
    if (!pom1::readFileBounded(filename, pom1::kMaxMemoryImageBytes,
                               "memory image", raw, readError)) {
        pom1::log().error("Mem", readError);
        return 1;
    }
    const std::string content(raw.begin(), raw.end());

    // Parse to completion FIRST (MemoryImageLoader.h — pure, no mem[] in
    // sight), then decide, then apply. The order matters: this function used to
    // write each byte as it recognised it, so an Intel HEX whose fifth record
    // lay outside the 6502's 64 KB left the first four already in RAM before
    // returning the error. A rejected image now carries no writes at all.
    const pom1::MemoryImage image = pom1::parseMemoryImage(content, displayName);

    if (!image.ok) {
        // Nothing usable in the file. Report whatever the parser objected to;
        // a dump that simply established no address says nothing at all, which
        // is the historical silent "return 1".
        for (const auto& d : image.diagnostics)
            if (d.severity == pom1::MemoryImageDiagnostic::Severity::Error)
                pom1::log().error("Mem", d.message);
        return 1;
    }

    // Apply. Hex dumps deliberately bypass the ROM write-protect: a WOZMON dump
    // is the user typing at the Monitor, and several bundled programs load a
    // high block into the BASIC/Monitor window on purpose.
    for (const pom1::MemoryImageSpan& span : image.writes) {
        unsigned int addr = span.start;
        for (uint8_t byte : span.bytes) {
            if (addr >= 0x10000) break;
            mem[addr++] = byte;
        }
    }
    startAddress = image.startAddress;
    if (bytesLoaded) *bytesLoaded = image.byteCount;
    if (zones) *zones = image.zones();
    // Hex dumps scatter writes across arbitrary pages; the precise set isn't
    // worth tracking here, so fall back to "everything might have changed".
    // Loading a dump is a user action (rare), not a hot path.
    if (image.byteCount > 0) markAllPagesDirty();

    for (const auto& d : image.diagnostics) {
        switch (d.severity) {
        case pom1::MemoryImageDiagnostic::Severity::Info:    pom1::log().info("Mem", d.message);  break;
        case pom1::MemoryImageDiagnostic::Severity::Warning: pom1::log().warn("Mem", d.message);  break;
        case pom1::MemoryImageDiagnostic::Severity::Error:   pom1::log().error("Mem", d.message); break;
        }
    }
    return 0;
}

uint8_t Memory::memRead(uint16_t address)
{
    // Test mode: flat 64 KB RAM, no side effects (Klaus Dormann functional
    // test expects the whole address space to behave as RAM).
    if (testMode) return mem[address];

    // Watchpoint: latch the first read of a watched address this instruction.
    if (anyWatch_ && !watchHit_.tripped && (watchFlags_[address] & 0x01))
        watchHit_ = { true, address, false };

    // Read-before-write trap: flag uninitialised RAM reads. Watches [0,$2000)
    // (ZP/stack/BSS/user) plus the Parmigiani high RAM bank [$E000,$F000) — both
    // pure RAM, no peripherals. --ram-trap only. See ramTrapWatches().
    if (ramWriteTrap) checkRamReadTrap(address);

    // Memory-mapped peripherals (A1IO_RTC, CFFA1, microSD, WiFiModem, SID,
    // TMS9918, Cassette read) live on the PeripheralBus. The remaining
    // logic below handles the Apple-1 core that's not really a peripheral:
    // PIA 6821 ($D010-$D012 with $D0xx aliasing), strict-OOR enforcement,
    // and the raw 64 KB backing array.
    uint8_t busValue;
    if (bus.tryRead(address, busValue)) return busValue;

    // PIA 6821 alias: the Apple 1's 74154 decoder selects the PIA for the full
    // $D000-$DFFF range (4 KB page). Only address lines A0-A1 reach the PIA:
    //   bits 1:0 = 00 → $D010 (KBD), 01 → $D011 (KBDCR), 10 → $D012 (DSP)
    // Pagetable BASIC uses $D0F2, P-LAB Fast Terminal uses $DF12, etc.
    if ((address & 0xF000) == 0xD000
        && address != 0xD010 && address != 0xD011 && address != 0xD012) {
        address = 0xD010 | (address & 0x03);
    }

    // Apple 1 Clavier : lecture de 0xD010 (KBD) et 0xD011 (KBDCR)
    // Protocole Apple 1 :
    // - 0xD011 (KBDCR) : bit 7 = strobe (1 si touche prête). La lecture réinitialise le strobe.
    // - 0xD010 (KBD) : caractère avec bit 7 = 1 si prêt. Le caractère reste disponible jusqu'à nouvelle touche.
    // PIA register banking: with the port's CR bit 2 clear, the data address
    // is the DATA DIRECTION register, not the port. See Memory.h.
    if (address == 0xD010 && !piaPortASelected()) return piaDdrA;
    if (address == 0xD012 && !piaPortBSelected()) return piaDdrB;
    if (address == 0xD013) {
        // CRB read-back. Bit 7 (IRQB1, driven by the display's RDA on CB1) is
        // not modelled; nothing in the corpus polls it, and the Apple-1's
        // display handshake goes through PB7 on the data port instead.
        return piaCrB;
    }

    if (address == 0xD010) {
        // KBD : retourne le caractère avec bit 7 à 1
        // Lire 0xD010 efface le strobe (PIA 6821 behavior)
        uint8_t result = keyReady ? (lastKey | 0x80) : 0x00;
        keyReady = false;
        // Charger la touche suivante du buffer si disponible
        if (!keyBuffer.empty()) {
            lastKey = keyBuffer.front();
            keyBuffer.pop();
            keyReady = true;
        }
        return result;
    } else if (address == 0xD012) {
        // Display port: bit 7 = busy flag. Le compteur displayBusyCycles décrémente dans
        // advanceCycles() (cycles 6502 réels) pour que le mode Step avance comme RUN
        // (boucle BIT $D012 / BMI du Woz ~0xFFEF).
        //
        // SWTPC PR-40 co-opts the same PB7 via Steve Jobs' DPDT switch
        // (Interface Age, Oct. 1976):
        //   Off        → PB7 reflects the video busy alone.
        //   Mixed      → OR of video + printer busy (2-position mod).
        //   PrintOnly  → printer busy alone (3-position community mod,
        //                isolates PB7 from the video's 60 Hz /RDA so the
        //                CPU can flood the FIFO at 1 MHz).
        bool busy;
        if (pr40Enabled && pr40Printer->getMode() == PR40Printer::SwitchMode::PrintOnly) {
            busy = pr40Printer->isMechBusy();
        } else {
            busy = (displayBusyCycles > 0) ||
                   (pr40Enabled && pr40Printer->isMechBusy());
        }
        if (busy) return mem[address] | 0x80;
        return mem[address] & 0x7F;
    } else if (address == 0xD011) {
        // CRA read-back is deliberately NOT modelled: bit 7 is IRQA1, the
        // keyboard strobe, and that is the only bit any Apple-1 program tests
        // (always with BIT/BPL). Returning the control bits too would change
        // every $D011 read in the corpus from $00/$80 to $27/$A7 for no
        // demonstrated caller — a wide blast radius bought for nothing.
        return keyReady ? 0x80 : 0x00;
    }

    checkOutOfRangeAccess(address, false);
    // Strict enforcement: unmapped RAM on a real 1976 4 K Apple-1 floats on
    // the bus; the ROMs that follow at $C1xx/$E0xx/$FFxx are handled above.
    // Return $FF as a safe stand-in for "nothing driving the bus". For
    // 8 KB Parmigiani dual-bank presets the $E000-$EFFF high bank is also
    // valid RAM (handled inside isOorAddress). Same carve-out for the GEN2
    // HGR framebuffer at $2000-$3FFF when the card is plugged.
    if (oorStrictMode && isOorAddress(address, presetRamKB,
                                      hgrFramebufferAttached)) {
        return 0xFF;
    }
    return mem[address];
}

void Memory::memWrite(uint16_t address, uint8_t value)
{
    // Test mode: flat 64 KB RAM, no ROM protection, no peripheral side
    // effects. Keep the dirty-page bit accurate in case a test ever checks
    // the snapshot publisher.
    if (testMode) {
        mem[address] = value;
        dirtyPages.set(static_cast<std::size_t>(address >> 8));
        return;
    }

    // Read-before-write trap: mark this RAM cell as written this run (--ram-trap).
    if (ramWriteTrap) noteRamWriteForTrap(address);

// Watchpoint: latch the first write to a watched address this instruction.
    if (anyWatch_ && !watchHit_.tripped && (watchFlags_[address] & 0x02))
        watchHit_ = { true, address, true };

    // Peripheral bus first — same rationale as memRead().
    if (bus.tryWrite(address, value)) return;

    // PIA 6821 alias (same normalization as memRead — full $D000-$DFFF page)
    if ((address & 0xF000) == 0xD000
        && address != 0xD010 && address != 0xD011 && address != 0xD012) {
        address = 0xD010 | (address & 0x03);
    }

    // Protection ROM (si writeInRom est désactivé). The windows themselves live
    // in Memory::isRomWriteProtected — PeripheralBus handlers with a flat-RAM
    // fall-through answer before this point and have to apply the same rule.
    if (isRomWriteProtected(address)) return;
    if (!writeInRom) {
        // $E000-$EFFF is RAM on a real Apple 1: Apple BASIC is distributed
        // on cassette and loaded into RAM there by the Woz Monitor
        // (`E000.EFFR`). POM1 pre-seeds the RAM from basic.rom at boot so
        // the user doesn't have to re-load on every start, but writes must
        // land like real hardware — a BASIC program writing zero-page
        // pointers into its own code segment or a user patching the
        // interpreter from the Monitor both worked on the original board.
        // SD CARD OS ROM: 0x8000-0x9FFF is intentionally NOT write-protected
        // either — user programs (e.g. SID tunes) load over this range and
        // must be able to write their own variables there at runtime.
    }

    if (aciEnabled && address >= 0xC000 && address <= 0xC0FF && address != 0xC081) {
        cassetteDevice->toggleOutput();
    }

    // Apple 1 Display : écriture vers 0xD012 (PIA 6821).
    //
    // Real hardware only latches a glyph into the 74LS164 shift register when
    // PB7 = 1 (the data-strobe bit), but POM1 stays deliberately permissive
    // here: emulator-era demos whose banner calls the WOZ Monitor's ECHO with
    // plain ASCII (bit 7 clear) print correctly on POM1 even though a real
    // Apple-1 would keep the shift register silent. Gating on bit 7 would
    // regress every legacy program that predates the strobe convention.
    //
    // The reset sequence's `LDY #$7F / STY $D012` DOES still reach here: CRB
    // is seeded to its post-reset $A7 (see resetMemory), so that write lands
    // on the data register rather than on DDRB — which is why the narrow
    // raw-$7F filter below survives. It would otherwise paint a spurious '_'
    // on every soft reset. A program that genuinely wants that glyph asks for
    // $DF through ECHO (`value & 0x7F` = $5F, the underscore slot) and is
    // unaffected. DDRB is pre-seeded to $7F, so a program that banks it in
    // still reads back exactly what the Monitor would have programmed.
    // PIA control registers, and the direction registers they bank in.
    // The mem[] mirror is cosmetic (every READ answers from the shadow member),
    // but the Memory Viewer draws the PUBLISHED snapshot, which copies only
    // pages flagged dirty — so the store has to flag page $D0 or the viewer
    // keeps showing the previous control byte until some other write to that
    // page happens to mark it.
    if (address == 0xD011) { piaCrA = value; mem[address] = value; dirtyPages.set(0xD0); return; }
    if (address == 0xD013) { piaCrB = value; mem[address] = value; dirtyPages.set(0xD0); return; }
    if (address == 0xD010 && !piaPortASelected()) { piaDdrA = value; return; }
    if (address == 0xD012 && !piaPortBSelected()) {
        // The Woz Monitor's `LDY #$7F / STY $D012` lands HERE, not on the
        // display — which is why the old raw-$7F filter that used to guard
        // this branch is gone. A program that genuinely wants to print $7F
        // now can, and the reset can no longer paint a spurious '_'.
        piaDdrB = value;
        return;
    }

    if (address == 0xD012) {
        if (value != 0x7F) {
            // How long PB7 stays busy is pom1::terminal's call — a fixed
            // countdown by default, or phase-locked to the scan under
            // FieldSync. The store lands MID-INSTRUCTION and advanceCycles()
            // books those cycles only afterwards, so add the in-flight count
            // for sub-instruction accuracy (same idiom as the TMS9918 beam
            // catch-up above).
            const int inFlight = cpuForIrq
                ? static_cast<int>(cpuForIrq->getCurrentInstructionCycles()) : 0;
            displayBusyCycles = pom1::terminal::busyCyclesAfterWrite(
                displayBusyModel_,
                pom1::terminal::advanceFieldPhase(terminalFieldPhase_, inFlight),
                displayCharDelay);
            if (displayDevice) {
                displayDevice->onChar(static_cast<char>(value & 0x7F));
            }
        }
        // Terminal Card: send the RAW value (before & 0x7F) for 8-bit mode support
        if (terminalCardEnabled) {
            terminalCard->onDisplayWrite(value);
        }
        // SWTPC PR-40 printer (Steve Jobs 1976 hack): third passive sniff on
        // the same PIA port B. The printer's DPDT switch mode gates whether
        // it also drives the DSP busy flag back to the CPU (see memRead).
        if (pr40Enabled) {
            pr40Printer->onDisplayWrite(value);
        }
    }

    checkOutOfRangeAccess(address, true);
    // Strict enforcement: drop writes to unmapped RAM so programs that stray
    // past the preset's physical RAM ceiling can't silently corrupt the
    // backing array and then read back their own garbage.
    if (oorStrictMode && isOorAddress(address, presetRamKB,
                                      hgrFramebufferAttached)) {
        return;
    }
    mem[address] = value;
    dirtyPages.set(static_cast<std::size_t>(address >> 8));
}

void Memory::setKeyPressed(char key)
{
    if (key >= 'a' && key <= 'z') {
        key = key - 'a' + 'A';
    }
    char k = key & 0x7F;
    if (!keyReady) {
        lastKey = k;
        keyReady = true;
    } else {
        keyBuffer.push(k);
    }
}

void Memory::setKeyPressedRaw(char key)
{
    // Like setKeyPressed but WITHOUT forced uppercase conversion
    // Used by Terminal Card for lowercase / 8-bit mode support
    char k = key & 0x7F;
    if (!keyReady) {
        lastKey = k;
        keyReady = true;
    } else {
        keyBuffer.push(k);
    }
}

void Memory::setTerminalSpeed(int charsPerSec)
{
    if (charsPerSec <= 0)
        displayCharDelay = 0; // Pas de délai (vitesse max)
    else
        displayCharDelay = POM1_CPU_CLOCK_HZ / charsPerSec;
}

int Memory::getTerminalSpeed() const
{
    if (displayCharDelay <= 0) return 0;
    return POM1_CPU_CLOCK_HZ / displayCharDelay;
}

// The one place a pom1::SID is built, and it is NOT the constructor:
// libresidfp computes its filter tables there, which costs ~120 ms for the
// FIRST chip in a process (0,46 ms for every one after it, per process). That
// was ~96 % of the runtime of every test binary that constructs a bare Memory
// — 60 of the 68 — and only two of them ever touch the card. `mutable` so the
// const getSID() can still materialise it; std::call_once because the UI and
// emulation threads can both be the first to ask. Note the cost has only
// MOVED: EmulationController's constructor warms the chip on purpose, off any
// lock, because every site a frontend reaches it from holds stateMutex.
pom1::SID& Memory::sidChip() const
{
    std::call_once(sidOnce, [this] {
        sid = std::make_unique<pom1::SID>(
            static_cast<int>(audio->getActualSampleRate()));
    });
    return *sid;
}

void Memory::setSIDEnabled(bool b)
{
    if (b == sidEnabled) return;
    applyTopologyRelations(pom1::CardId::Sid, b);
    if (b) {
        // Attach the audio sink BEFORE the emulation starts producing samples
        // (sidEnabled gates advanceCycles). Otherwise the first slice pushes
        // into an undrained ring and the audio callback plays catch-up.
        audio->addSource(&sidChip());
        sidEnabled = true;
        bus.setEnabled(sidBusHandle, true);
    } else {
        // Stop production first, then detach the audio sink. This guarantees
        // no advanceCycles() call can land between the removeSource and the
        // sidEnabled flip (which would push samples into a ring no one
        // drains).
        sidEnabled = false;
        bus.setEnabled(sidBusHandle, false);
        audio->removeSource(sid.get());
        sid->reset();
    }
}

void Memory::setSIDSpecialEditionEnabled(bool b)
{
    if (b == sidSpecialEditionEnabled) return;
    applyTopologyRelations(pom1::CardId::SidSpecialEdition, b);
    if (b) {
        // SE at $CC00-$CC1F is disjoint from the Juke-Box bank latch
        // ($CA00) so the two can coexist — no eviction needed.
        audio->addSource(&sidChip());
        sidSpecialEditionEnabled = true;
        bus.setEnabled(sidSEBusHandle, true);
    } else {
        sidSpecialEditionEnabled = false;
        bus.setEnabled(sidSEBusHandle, false);
        audio->removeSource(sid.get());
        sid->reset();
    }
}

void Memory::setMicroSDEnabled(bool b)
{
    if (b == microSDEnabled) return;
    applyTopologyRelations(pom1::CardId::MicroSD, b);
    microSDEnabled = b;
    bus.setEnabled(microSDBusHandle, b);
    if (b) {
        // Reload only when the ROM window is empty (first plug after it was
        // cleared by a previous disable, or after CFFA1 / Juke-Box overwrote
        // $8000). initMemory() pre-loads the SD CARD OS for the default
        // single-plug boot path, so a redundant disk read is avoided.
        if (!sdCardOsPresent()) {
            loadSDCardRom();
        }
    } else {
        // Clear the ROM region (restore to RAM). Skip during snapshot
        // restore: the MEM section already holds the correct bytes and
        // FLAGS runs after MEM — zeroing here would wipe just-restored RAM
        // (e.g. Applesoft Lite at $6000-$7FFF when switching presets).
        if (!snapshotRestoreInProgress) {
            std::fill(mem.begin() + 0x8000, mem.begin() + 0xA000, static_cast<uint8_t>(0));
            markPagesDirty(0x8000, 0x2000);
        }
    }
}

void Memory::setIECCardEnabled(bool b)
{
    if (b == iecCardEnabled) return;
    applyTopologyRelations(pom1::CardId::Iec, b);
    iecCardEnabled = b;
    if (b) {
        microSD->attachIECCard(iecCard.get());
        iecCard->busReset();
    } else {
        if (microSD) microSD->attachIECCard(nullptr);
        iecCard->busReset();
    }
}

void Memory::setWiFiModemEnabled(bool b)
{
    if (b == wifiModemEnabled) return;
    applyTopologyRelations(pom1::CardId::WifiModem, b);
    wifiModemEnabled = b;
    bus.setEnabled(wifiModemBusHandle, b);
}

int Memory::loadSDCardRom()
{
    bool prev = writeInRom;
    writeInRom = true;
    // Clear region first — ROM file (8177 B) may not fill the full 8 KB space
    std::fill(mem.begin() + 0x8000, mem.begin() + 0xA000, static_cast<uint8_t>(0));
    int ret = loadROM("sdcard.rom", 0x8000, 0x2000, "SD CARD OS");
    markPagesDirty(0x8000, 0x2000);
    writeInRom = prev;
    return ret;
}

void Memory::setCFFA1Enabled(bool b)
{
    if (b == cffa1Enabled) return;
    applyTopologyRelations(pom1::CardId::Cffa1, b);
    cffa1Enabled = b;
    bus.setEnabled(cffa1RomBusHandle, b);
    bus.setEnabled(cffa1RegBusHandle, b);
    if (b) {
        loadCFFA1Rom();
    } else {
        // Clear the CFFA1 ROM region. Skip during snapshot restore — same
        // MEM-then-FLAGS ordering hazard as setMicroSDEnabled(false).
        if (!snapshotRestoreInProgress) {
            std::fill(mem.begin() + 0x9000, mem.begin() + 0xB000, static_cast<uint8_t>(0));
            markPagesDirty(0x9000, 0x2000);
        }
    }
}

int Memory::loadCFFA1Rom()
{
    bool prev = writeInRom;
    writeInRom = true;

    // Load ROM file into the flat memory array (for code that reads mem[] directly)
    int ret = loadROM("cffa1.rom", 0x9000, 0x2000, "CFFA1");
    if (ret == 0) {
        // Also load into the CFFA1 object's internal ROM buffer
        cffa1->loadRom(mem.data() + 0x9000, CFFA1::kRomSize);
    }

    writeInRom = prev;
    return ret;
}

void Memory::applyJukeBoxFlatMemoryMirror()
{
    if (!jukeBoxEnabled) return;
    // During snapshot restore the MEM section already holds the correct mirror
    // bytes AND the real $4000-$7FFF user RAM (RAM32/ROM16 mode); re-running the
    // mirror here would memset that restored RAM to zero. Skip it — the bus
    // handlers serve CPU reads from jukeBox->readByte() regardless of the mirror.
    if (snapshotRestoreInProgress) return;
    // Mirror the currently-banked page into the flat RAM shadow so the
    // memory viewer / snapshot pipeline sees ROM content at the right
    // address. The bus handler always serves CPU reads via
    // jukeBox->readByte() directly, so the mirror is purely cosmetic
    // and must be refreshed whenever the bank register at $CA00 changes.
    const uint8_t* romBuf  = jukeBox->getRomPointer();
    const size_t   romSize = jukeBox->getRomBufferSize();
    const size_t   pageOff = static_cast<size_t>(jukeBox->getCurrentPage())
                             * JukeBox::kPageSize;
    if (jukeBox->getJumper() == JukeBox::Jumper::RAM16_ROM32) {
        // Full 32 kB page visible at $4000-$BFFF.
        if (pageOff + JukeBox::kPageSize <= romSize) {
            std::memcpy(mem.data() + 0x4000, romBuf + pageOff, JukeBox::kPageSize);
        } else {
            std::memset(mem.data() + 0x4000, 0xFF, JukeBox::kPageSize);
        }
        markPagesDirty(0x4000, JukeBox::kPageSize);
    } else {
        // RAM32/ROM16: only 16 kB visible at $8000-$BFFF; Sx picks upper
        // or lower half of the current 32 kB page. Clear $4000-$7FFF so
        // stale expansion-ROM images (e.g. Applesoft at $6000) don't
        // linger in the RAM half of the address space.
        std::memset(mem.data() + 0x4000, 0, 0x4000);
        const size_t subOff = static_cast<size_t>(jukeBox->getCurrentSubPage())
                              * JukeBox::kSubPageSize;
        const size_t srcOff = pageOff + subOff;
        if (srcOff + JukeBox::kSubPageSize <= romSize) {
            std::memcpy(mem.data() + 0x8000, romBuf + srcOff, JukeBox::kSubPageSize);
        } else {
            std::memset(mem.data() + 0x8000, 0xFF, JukeBox::kSubPageSize);
        }
        markPagesDirty(0x4000, 0x8000);
    }
}

void Memory::setJukeBoxEnabled(bool b)
{
    if (b == jukeBoxEnabled) return;
    applyTopologyRelations(pom1::CardId::JukeBox, b);
    jukeBoxEnabled = b;
    if (b) {
        // A1-AUDIO SE at $CC00-$CC1F is disjoint from the Juke-Box bank
        // latch ($CA00) — do NOT evict; the two can coexist.
        loadJukeBoxRom();
        const bool use32 = (jukeBox->getJumper() == JukeBox::Jumper::RAM16_ROM32);
        const bool use16 = (jukeBox->getJumper() == JukeBox::Jumper::RAM32_ROM16);
        bus.setEnabled(jukeBox32BusHandle, use32);
        bus.setEnabled(jukeBox16BusHandle, use16);
        bus.setEnabled(jukeBoxBankRegBusHandle, true);
        // Seed zero-page $3F to match the boot page so the PM's first
        // instruction ($BD00: LDA $3F / STA $CA00) is a no-op instead of
        // bank-switching to page 0 (where the shipped ROM has game data
        // rather than firmware). Real-world P-LAB ROMs put a PM copy in
        // every page so this wouldn't matter; POM1 tolerates partial ROMs.
        // Skip the zero-page boot-page seed during snapshot restore: the MEM
        // section already restored $3F, and overwriting it would desync a
        // running PM from the bank it was actually executing from.
        if (!snapshotRestoreInProgress) {
            mem[0x003F] = jukeBox->getBootPage();
            markPagesDirty(0x0000, 0x0100);
        }
        applyJukeBoxFlatMemoryMirror();
    } else {
        bus.setEnabled(jukeBox32BusHandle, false);
        bus.setEnabled(jukeBox16BusHandle, false);
        bus.setEnabled(jukeBoxBankRegBusHandle, false);
        // Clear the flat ROM shadow applyJukeBoxFlatMemoryMirror() wrote into
        // $4000-$BFFF. Otherwise the executable ROM image lingers as if it were
        // RAM after the card is unplugged — and, worse, a later
        // setMicroSDEnabled(true) reads $8000 to decide whether to reload the SD
        // CARD OS ($A9 00 signature guard); stale Juke-Box bytes there can defeat
        // that guard. Skip during snapshot restore (MEM-then-FLAGS ordering, same
        // as setCFFA1Enabled/setMicroSDEnabled).
        if (!snapshotRestoreInProgress) {
            std::fill(mem.begin() + 0x4000, mem.begin() + 0xC000, static_cast<uint8_t>(0));
            markPagesDirty(0x4000, 0x8000);
        }
    }
}

// The three JukeBox / CodeTank getters below are out-of-line for one reason:
// they do MEMBER ACCESS on the card (`jukeBox->getJumper()`), which needs the
// complete type, unlike the getters that merely return a reference to an
// incomplete one (`return *cassetteDevice;`). Memory.h forward-declares both
// cards now — see the note at the top of that header.
pom1::JukeBoxJumper Memory::getJukeBoxJumper() const { return jukeBox->getJumper(); }
bool Memory::isJukeBoxWritable() const { return jukeBox->isWritable(); }
pom1::JukeBoxChipMode Memory::getJukeBoxChipMode() const { return jukeBox->getChipMode(); }

void Memory::setJukeBoxJumper(JukeBox::Jumper j)
{
    if (jukeBox->getJumper() == j) return;
    jukeBox->setJumper(j);
    if (!jukeBoxEnabled) return; // bus handles stay off; jumper just noted
    const bool use32 = (j == JukeBox::Jumper::RAM16_ROM32);
    const bool use16 = (j == JukeBox::Jumper::RAM32_ROM16);
    bus.setEnabled(jukeBox32BusHandle, use32);
    bus.setEnabled(jukeBox16BusHandle, use16);
    applyJukeBoxFlatMemoryMirror();
}

void Memory::setJukeBoxWritable(bool w)
{
    jukeBox->setWritable(w);
}

void Memory::setJukeBoxChipMode(JukeBox::ChipMode m)
{
    if (jukeBox->getChipMode() == m) return;
    jukeBox->setChipMode(m);
    if (jukeBoxEnabled) {
        loadJukeBoxRom();
        const bool use32 = (jukeBox->getJumper() == JukeBox::Jumper::RAM16_ROM32);
        const bool use16 = (jukeBox->getJumper() == JukeBox::Jumper::RAM32_ROM16);
        bus.setEnabled(jukeBox32BusHandle, use32);
        bus.setEnabled(jukeBox16BusHandle, use16);
        applyJukeBoxFlatMemoryMirror();
    }
}

int Memory::loadJukeBoxRom(void)
{
    lastError.clear();
    for (const char* rel : {"roms/jukebox.rom", "jukebox.rom"}) {
        const std::filesystem::path found = resources().find(rel);
        if (found.empty()) continue;
        std::string error;
        if (jukeBox->loadRomFile(found.string(), error)) {
            if (jukeBoxEnabled)
                applyJukeBoxFlatMemoryMirror();
            return 0;
        }
    }
    // No ROM on disk — leave the card "installed but blank" (all $FF). The
    // Hardware window will show "firmware missing"; the user can still
    // drop in a ROM through the Memory Options dialog later.
    lastError = "Juke-Box ROM not found (expected roms/jukebox.rom)";
    pom1::log().warn("Mem", lastError);
    return 1;
}

void Memory::applyCodeTankFlatMemoryMirror()
{
    if (!codeTankEnabled) return;
    // During snapshot restore the MEM section already holds the correct
    // mirror bytes; re-running the mirror here would clobber them (and
    // FLAGS runs before the CodeTank card section is deserialized).
    if (snapshotRestoreInProgress) return;
    const uint8_t* romBuf = codeTank->getRomPointer();
    const size_t   romSize = codeTank->getRomSize();
    const size_t halfOff = (codeTank->getJumper() == CodeTank::Jumper::Upper16)
                           ? CodeTank::kHalfSize : 0u;
    if (halfOff + CodeTank::kHalfSize <= romSize) {
        std::memcpy(mem.data() + CodeTank::kBase,
                    romBuf + halfOff,
                    CodeTank::kHalfSize);
    } else {
        std::memset(mem.data() + CodeTank::kBase, 0xFF, CodeTank::kHalfSize);
    }
    markPagesDirty(CodeTank::kBase, CodeTank::kHalfSize);
}

void Memory::setCodeTankEnabled(bool b)
{
    if (b == codeTankEnabled) return;
    applyTopologyRelations(pom1::CardId::CodeTank, b);
    codeTankEnabled = b;
    if (b) {
        // Probe for a default ROM image when the user hasn't loaded one
        // explicitly through the CodeTank Library. The same probe paths
        // the previous Juke-Box CodeTank chip mode used.
        if (!codeTank->hasRom()) {
            loadCodeTankRom();
        }
        bus.setEnabled(codeTankBusHandle, true);
        applyCodeTankFlatMemoryMirror();
    } else {
        bus.setEnabled(codeTankBusHandle, false);
        // Clear the mirrored ROM bytes so the Memory Viewer doesn't keep
        // showing stale ROM contents at $4000-$7FFF after unplug. Skip
        // during snapshot restore — MEM already restored the correct bytes.
        if (!snapshotRestoreInProgress) {
            std::fill_n(mem.begin() + CodeTank::kBase, CodeTank::kHalfSize, static_cast<uint8_t>(0));
            markPagesDirty(CodeTank::kBase, CodeTank::kHalfSize);
        }
    }
}

pom1::CodeTankJumper Memory::getCodeTankJumper() const { return codeTank->getJumper(); }

void Memory::setCodeTankJumper(CodeTank::Jumper j)
{
    if (codeTank->getJumper() == j) return;
    codeTank->setJumper(j);
    if (codeTankEnabled) applyCodeTankFlatMemoryMirror();
}

int Memory::loadCodeTankRom(const std::string& path)
{
    lastError.clear();
    if (!path.empty()) {
        // Preset / CLI paths are usually repo-relative (roms/codetank/…) and
        // the process cwd is often build/. The locator owns that walk — and
        // returns an absolute path untouched, which is what --codetank-rom
        // and the file picker hand us.
        std::string error;
        const std::filesystem::path found = resources().find(path);
        if (!found.empty() && codeTank->loadRomFile(found.string(), error)) {
            if (codeTankEnabled) applyCodeTankFlatMemoryMirror();
            return 0;
        }
        lastError = error.empty() ? ("CodeTank ROM not found: " + path) : error;
        pom1::log().warn("Mem", lastError);
        return 1;
    }
    // Default probe order. The shipped CodeTank library image
    // (`Codetank_ARCADE.rom`, built by tools/build_codetank_rom.py) wins
    // so plugging the CodeTank from the toolbar/Hardware menu drops the
    // user straight into the bundled software: lower jumper = 3-game
    // menu (Galaga/Sokoban/Snake), upper jumper = TMS_Rogue. The legacy
    // single-file `roms/codetank.rom` (kept around from before the
    // library directory) stays as a fallback. Other carts (CLASSICS /
    // BASIC_LOGO / DEMOS) are picked via File → P-LAB CodeTank Library.
    for (const char* rel : {"roms/codetank/Codetank_ARCADE.rom",
                            "codetank.rom", "roms/codetank.rom"}) {
        const std::filesystem::path found = resources().find(rel);
        if (found.empty()) continue;
        std::string error;
        if (codeTank->loadRomFile(found.string(), error)) {
            if (codeTankEnabled) applyCodeTankFlatMemoryMirror();
            return 0;
        }
    }
    lastError = "CodeTank ROM not found "
                "(expected roms/codetank/Codetank_ARCADE.rom)";
    pom1::log().warn("Mem", lastError);
    return 1;
}

int Memory::loadCodeTankRomBuffer(const std::vector<uint8_t>& data, const std::string& label)
{
    lastError.clear();
    std::string error;
    if (!codeTank->loadRomBuffer(data, label, error)) {
        lastError = error;
        pom1::log().warn("Mem", lastError);
        return 1;
    }
    if (codeTankEnabled) applyCodeTankFlatMemoryMirror();
    return 0;
}

void Memory::setJukeBoxBankRegister(uint8_t value)
{
    jukeBox->writeBankRegister(value);
    if (jukeBoxEnabled)
        applyJukeBoxFlatMemoryMirror();
}

bool Memory::copyJukeBoxPage(uint8_t fromPage, uint8_t toPage, std::string& error)
{
    if (!jukeBox->copyPage(fromPage, toPage, error))
        return false;
    if (jukeBoxEnabled)
        applyJukeBoxFlatMemoryMirror();
    return true;
}

bool Memory::saveJukeBoxRom(const std::string& path, std::string& error) const
{
    return jukeBox->saveRomFile(path, error);
}

// Apple II HGR interleave offset (within an 8 KB page) of scanline `y`.
static inline int gen2HgrRowOffset(int y)
{
    return ((y & 7) << 10) | (((y >> 3) & 7) << 7) | ((y >> 6) * 40);
}

// Beam-accuracy Phase B: latch one scanline of BOTH HGR pages into the frame
// latch at the cycle the beam crossed it, so line Y reflects RAM at its own beam
// time. The latch is $2000-$5FFF-shaped: page 1 line at `off`, page 2 at
// 0x2000+off. The renderer reads either page per band, unchanged.
void Memory::gen2ReseedLatchFromRam(void)
{
    if (!hgrFramebufferAttached) return;
    std::memcpy(gen2BeamLatchBuf.data(), mem.data() + 0x2000,
                gen2BeamLatchBuf.size());
    gen2FrameLatchBuf = gen2BeamLatchBuf;
}

void Memory::gen2LatchScanline(int line)
{
    const int off = gen2HgrRowOffset(line);
    std::memcpy(gen2BeamLatchBuf.data() + off,
                mem.data() + 0x2000 + off, 40);            // page 1 ($2000)
    std::memcpy(gen2BeamLatchBuf.data() + 0x2000 + off,
                mem.data() + 0x4000 + off, 40);            // page 2 ($4000)
}

void Memory::advanceCycles(int cycles)
{
    if (cycles > 0 && displayBusyCycles > 0) {
        displayBusyCycles = std::max(0, displayBusyCycles - cycles);
    }
    // Where the video scan is inside the 60 Hz field. Free-running and cheap
    // (one add, one compare); only consulted when the display busy model is
    // FieldSync. M6502 calls advanceCycles() per instruction, so the phase is
    // accurate to a few cycles without any extra plumbing.
    terminalFieldPhase_ = pom1::terminal::advanceFieldPhase(terminalFieldPhase_, cycles);
    cassetteDevice->advanceCycles(cycles);
    // GEN2 release video scanner — drives the cycle-accurate floating bus /
    // beam position. Same gating pattern as the cards above; zero cost when the
    // HGR framebuffer card is unplugged. At every video-frame rollover the
    // soft-switch journal recorded during the frame is published for the
    // beam-raced renderer (POM2 "republish at each video-frame boundary"
    // model — the UI may re-render the same published frame at 60 Hz).
    if (hgrFramebufferAttached) {
        const uint64_t cpf    = gen2Scanner.cyclesPerFrame();
        const uint64_t cyc0   = gen2Scanner.cycle();
        const uint64_t before = cyc0 / cpf;
        gen2Scanner.advanceCycles(static_cast<uint64_t>(cycles));
        const uint64_t cyc1   = gen2Scanner.cycle();

        // Beam-accuracy Phase B: latch each visible scanline the beam FINISHED in
        // [cyc0, cyc1) at its own beam time, so a single-buffer program that
        // updates the framebuffer mid-frame renders per-line-correct (like real
        // hardware racing the beam) instead of from one async snapshot. A line is
        // 65 cycles, instructions <= 7, so this is usually 0-1 lines per call; the
        // cap keeps a huge delta (save-state / headless replay) at O(one frame).
        constexpr uint64_t lpc = Gen2VideoScanner::kCyclesPerLine;   // 65
        const uint64_t lpf = cpf / lpc;                              // lines/frame
        uint64_t a0 = cyc0 / lpc;
        const uint64_t a1 = cyc1 / lpc;
        if (a1 - a0 > lpf) a0 = a1 - lpf;
        for (uint64_t a = a0; a < a1; ++a) {
            const int line = static_cast<int>(a % lpf);
            if (line < Gen2VideoScanner::kVisibleLines) gen2LatchScanline(line);
        }

        if (cyc1 / cpf != before) {
            gen2Scanner.publishFrame();
            // Freeze the just-completed frame's per-scanline latch as the PUBLISHED
            // frame. The working latch (gen2BeamLatchBuf) is a moving target: read
            // mid-sweep (the SnapshotPublisher fires at slice boundaries, not frame
            // boundaries) it is a top=this-frame / bottom=last-frame SPLIT, which
            // tears a single-buffer sprite. Snapshotting it at the V-blank rollover
            // hands the renderer a COMPLETE, self-consistent frame -- all 192 lines
            // from one beam sweep -- while keeping per-scanline beam-time content.
            gen2FrameLatchBuf = gen2BeamLatchBuf;
        }
    }
    if (tms9918Enabled) tms9918->advanceCycles(cycles);
    if (microSDEnabled) microSD->advanceCycles(cycles);
    if (wifiModemEnabled) wifiModem->advanceCycles(cycles);
    if (terminalCardEnabled) terminalCard->advanceCycles(cycles);
    if (telemetryEnabled) telemetryPort->advanceCycles(cycles);
    if (a1ioRtcEnabled) a1ioRtc->advanceCycles(cycles);
    if (pr40Enabled) pr40Printer->advanceCycles(cycles);
    if (jukeBoxEnabled) jukeBox->advanceCycles(cycles);
    // SID is driven by the *emulated* CPU clock, not by the audio device.
    // Without this call, libresidfp would produce samples at wallclock
    // 44.1 kHz independent of executionSpeed, decoupling music tempo from
    // CPU speed (Max mode → way too fast, WASM frame drop → too slow).
    if (sidEnabled || sidSpecialEditionEnabled) sidChip().advanceCycles(cycles);

    // Aggregate /IRQ line — wire-OR of every plugged peripheral's interrupt
    // request. The CPU's setIRQ() takes a level (1 = asserted, 0 = clear),
    // matching the real 6502's level-triggered /IRQ pin: the line is
    // re-evaluated after every opcode, so a peripheral that lowers its
    // request between two CPU ticks naturally de-asserts /IRQ.
    //
    // Sources currently wired (per sketchs/doc/Programming_TMS9918.md §18 Bug N°2):
    //   - TMS9918  : default = WIRED. The P-LAB card connects /INT → /IRQ
    //                (trace verified on real hardware by Parmigiani), so
    //                irqAsserted() = R1.5 (IRQ enable) AND status.7 (F flag);
    //                read of $CC01 clears F → IRQ self-clears next tick.
    //                Stays harmless until the program does CLI (polling-only
    //                Nippur72 code is unaffected). Toggle off with
    //                setIrqStrapped(false) to model an un-wired card.
    //   - A1-IO RTC: 65C22 IFR bit 7 (any IRQ-enabled flag set).
    //   - microSD  : 65C22 IFR bit 7 (Timer 1/2 + SR + handshake flags).
    //   - WiFiModem: 65C51 ACIA status bit 7 (IRQ pending) AND control
    //                command-reg IRQ-enable inverted-polarity bit.
    //
    // ACI cassette is software-polled on real Apple-1 hardware — no /IRQ
    // line on the cassette interface — so it stays out of the OR.
    // GraphicsCard (GEN2 HGR), CFFA1, JukeBox, CodeTank, GT-6144, PR-40
    // and TerminalCard either have no /INT pin or never wire it on the
    // P-LAB / community Apple-1 implementations — they stay polled.
    if (cpuForIrq) {
        bool irq = false;
        if (tms9918Enabled && tms9918->irqAsserted()) irq = true;
        if (a1ioRtcEnabled && a1ioRtc->irqAsserted()) irq = true;
        if (microSDEnabled && microSD->irqAsserted()) irq = true;
        if (wifiModemEnabled && wifiModem->irqAsserted()) irq = true;
        cpuForIrq->setIRQ(irq ? 1 : 0);
    }
}

// ─────────────────────────────────────────────────────────────────────
// Snapshot save / load  →  MemorySnapshot.cpp
// ─────────────────────────────────────────────────────────────────────
// The serialization concern (save/load/rewind sections + the card registry
// that drives them) lives in its own translation unit. They are still Memory
// member functions — this is a file split, not an API change.
