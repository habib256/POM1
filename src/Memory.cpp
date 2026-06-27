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

} // namespace

Memory::Memory()
{
    audioDevice = std::make_unique<AudioDevice>();
    // Pass the audio device's actual sample rate (44.1 kHz requested but
    // miniaudio may negotiate 48 kHz on Apple Silicon, and the browser
    // AudioContext may also force a different rate on WASM) so both
    // cassette and SID produce samples at the rate the OS will consume
    // them — otherwise their tempo drifts by the rate ratio.
    const uint32_t actualRate = audioDevice->getActualSampleRate();
    cassetteDevice = std::make_unique<CassetteDevice>();
    cassetteDevice->setAudioAvailable(audioDevice->isAvailable());
    cassetteDevice->setAudioOutputSampleRate(actualRate);
    cassetteDevice->setAciActive(aciEnabled);
    // NOTE: no addSource here. The cassette is registered on the mixer
    // via activateCassetteAudioSource() which MainWindow calls 15 frames
    // after the CPU starts, matching the SID deferred-plug rule. Adding
    // the source here (before the CPU has run) was producing silent
    // first-tape playback — same symptom as the early SID boot-silence.
    // See pendingCardEnableFrames in MainWindow_ImGui.h.
    tms9918 = std::make_unique<TMS9918>();
    sid = std::make_unique<pom1::SID>(static_cast<int>(actualRate));
    microSD = std::make_unique<MicroSD>();
    // Set SD card path: try common locations relative to executable
    for (const auto& dir : {"sdcard", "../sdcard", "../../sdcard"}) {
        if (std::filesystem::is_directory(dir)) {
            microSD->setSDCardPath(std::filesystem::canonical(dir).string());
            break;
        }
    }
    iecCard = std::make_unique<pom1::IECCard>();
    // Probe for the device-8 disk image. MVP supports a single drive.
    for (const auto& dir : {"disks", "../disks", "../../disks"}) {
        if (std::filesystem::is_directory(dir)) {
            auto imgPath = std::filesystem::canonical(dir).string() + "/iec/dev8.d64";
            if (std::filesystem::exists(imgPath)) {
                iecCard->mountDisk(imgPath);
            }
            break;
        }
    }
    wifiModem = std::make_unique<WiFiModem>();
    terminalCard = std::make_unique<TerminalCard>();
    pr40Printer = std::make_unique<PR40Printer>();
    gt6144 = std::make_unique<GT6144>();
    a1ioRtc = std::make_unique<A1IO_RTC>();
    cffa1 = std::make_unique<CFFA1>();
    // Probe for CF card disk image
    for (const auto& dir : {"cfcard", "../cfcard", "../../cfcard"}) {
        if (std::filesystem::is_directory(dir)) {
            auto imgPath = std::filesystem::canonical(dir).string() + "/cfcard.po";
            if (std::filesystem::exists(imgPath)) {
                cffa1->openDiskImage(imgPath);
            }
            break;
        }
    }
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
        [this](uint16_t a) { return sid->readRegister(a & 0x1F); },
        [this](uint16_t a, uint8_t v) {
            uint8_t reg = a & 0x1F;
            if (reg <= 24) sid->writeRegister(reg, v);
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
        [this](uint16_t a) { return sid->readRegister(a & 0x1F); },
        [this](uint16_t a, uint8_t v) {
            uint8_t reg = a & 0x1F;
            if (reg <= 24) sid->writeRegister(reg, v);
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
            return (a == 0xCC00) ? tms9918->readData() : tms9918->readControl();
        },
        [this](uint16_t a, uint8_t v) {
            if (cpuForIrq) tms9918->setLastAccessPc(cpuForIrq->getProgramCounter());
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
    // Priority 20 to win over the Juke-Box's $4000-$BFFF window when the
    // user tries to plug both — the Memory layer enforces single-card use,
    // but the priority is belt-and-suspenders.
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
            mem[a] = v;      // undecoded → flat-RAM fall-through
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
Memory::~Memory() = default;

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
    if (gen2RecordingEvents.size() >= kGen2MaxEventsPerFrame) {
        gen2RecordingEvents.clear();
        gen2RecordingFrameStart = st;
    } else {
        gen2RecordingEvents.push_back({emuCycle, kind, value});
    }

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

void Memory::resetGen2VideoEventJournal()
{
    gen2RecordingEvents.clear();
    gen2PublishedEvents.clear();
    gen2RecordingFrameStart = gen2Scanner.displayState();
    gen2PublishedFrameStart = gen2RecordingFrameStart;
}

void Memory::setTMS9918Enabled(bool b)
{
    // TMS9918 and A1-AUDIO Special Edition both live at $CC00/$CC01 — plugging
    // the VDP evicts the SE card so the bus dispatch stays unambiguous.
    if (b && sidSpecialEditionEnabled) setSIDSpecialEditionEnabled(false);
    // CodeTank is a daughterboard that physically piggybacks the TMS9918
    // Graphic Card on real P-LAB hardware — yanking the host pulls the
    // daughterboard off with it. Cascade-disable BEFORE flipping the flag
    // so setCodeTankEnabled(false) sees a coherent state.
    if (!b && codeTankEnabled) setCodeTankEnabled(false);
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

void Memory::setACIEnabled(bool b)
{
    if (aciEnabled == b) return;
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

void Memory::activateCassetteAudioSource()
{
    if (cassetteAudioActive) return;
    audioDevice->addSource(cassetteDevice.get());
    cassetteAudioActive = true;
}

void Memory::deactivateCassetteAudioSource()
{
    if (!cassetteAudioActive) return;
    audioDevice->removeSource(cassetteDevice.get());
    cassetteAudioActive = false;
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
    writeInRom = true;
    if (mem.size() < (size_t)(ramSize * 1024)) {
        mem.resize(ramSize * 1024, 0);
    } else {
        std::fill(mem.begin(), mem.end(), 0);
    }
    markAllPagesDirty();
    loadBasic();
    if (aciEnabled) loadAciRom();
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
    sid->resetChip();
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
        resetGen2VideoEventJournal();
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
}

void Memory::resetMemory(void)
{
    // RAM power-on profile. Default = zero-init (legacy, preserves tests
    // and snapshots). When systemRamNoiseOnReset is enabled, seed RAM with
    // mt19937 noise — matches what real Apple-1 6502 RAM actually shows
    // at cold boot (bistable noise). Combined with silicon-strict mode
    // this surfaces programs that assume RAM = 0.
    if (systemRamNoiseOnReset) {
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
    sid->resetChip();
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

    const std::string searchPaths[] = {
        filename,
        std::string("roms/") + filename,
        std::string("../roms/") + filename
    };

    std::ifstream file;
    for (const auto& path : searchPaths) {
        file.open(path, std::ios::binary);
        if (file.is_open())
            break;
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
    std::fill_n(mem.begin() + 0xE000, 0x1000, static_cast<uint8_t>(0));
    markPagesDirty(0xE000, 0x1000);
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
    int ret = loadROM("applesoft-lite-microsd.rom", 0x6000, 0x2000, "Applesoft Lite (P-LAB microSD)");
    if (ret != 0) return ret;
    // The microSD build requires Woz Monitor at $FF00 for the SD OS to link
    // to. Reload it only when it was overwritten (typical case: user just
    // switched away from a CFFA1-flavoured Applesoft that spans $E000-$FFFF
    // and clobbered $FF00). Signature check: Woz Monitor begins with D8 58
    // (CLD / CLI). Skip the disk read when the ROM is still in place — this
    // keeps boot I/O to a minimum.
    if (mem[0xFF00] != 0xD8 || mem[0xFF01] != 0x58) {
        return loadWozMonitor();
    }
    return 0;
}

int Memory::loadKrusader(void)
{
    return loadROM("krusader-1.3.rom", 0xA000, 0x2000, "Krusader");
}

int Memory::loadWozMonitor(void)
{
    return loadROM("WozMonitor.rom", 0xFF00, 0x100, "WOZ Monitor");
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
    pom1::log().info("Mem", "ACI ROM loaded from built-in fallback to 0xC100: " +
                            std::to_string(sizeof(kAciRom)) + " bytes");
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
    // Tracks the current contiguous zone we're filling. Each address-prefix
    // ('AAAA:') flip closes the previous zone and starts a new one. The final
    // zone is closed at end of parse. zoneActive distinguishes "no zone yet"
    // (before the first address) from "zone in progress".
    uint16_t zoneStart = 0;
    unsigned int zoneLastAddr = 0; // last address actually written
    bool zoneActive = false;
    auto closeZone = [&]() {
        if (!zones || !zoneActive) return;
        zones->push_back({zoneStart, static_cast<uint16_t>(zoneLastAddr)});
        zoneActive = false;
    };
    std::ifstream file(filename);
    if (!file.is_open()) {
        pom1::log().error("Mem", std::string("Cannot open file: ") + filename);
        return 1;
    }

    // Lire tout le fichier en une seule chaîne
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    // Supprimer les commentaires (//, #, ;) — en début de ligne ou inline
    std::string cleaned;
    std::istringstream lineStream(content);
    std::string line;
    while (std::getline(lineStream, line)) {
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        char first = line[start];
        if (first == '#' || first == ';') continue;
        if (start + 1 < line.size() && first == '/' && line[start + 1] == '/') continue;
        // Strip inline comments: truncate at first // or ;
        size_t commentPos = line.find("//");
        if (commentPos != std::string::npos) line = line.substr(0, commentPos);
        commentPos = line.find(';');
        if (commentPos != std::string::npos) line = line.substr(0, commentPos);
        cleaned += line;
    }

    unsigned int currentAddr = 0;
    uint16_t runAddr = 0;
    bool firstAddr = true;
    bool hasRunAddr = false;
    int totalBytes = 0;
    int oddDigitsDropped = 0;
    size_t i = 0;

    auto isHex = [](char c) { return std::isxdigit((unsigned char)c); };
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return 0;
    };

    while (i < cleaned.size()) {
        char c = cleaned[i];

        // Sauter espaces
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { i++; continue; }

        // 'T' prefix (turbo) — sauter le T, traiter comme une adresse
        if ((c == 'T' || c == 't') && i + 1 < cleaned.size() && isHex(cleaned[i + 1])) {
            i++; continue;
        }

        // 'X' marker (fin de bloc turbo) — sauter X + adresse hex
        if ((c == 'X' || c == 'x') && i + 1 < cleaned.size() && isHex(cleaned[i + 1])) {
            i++;
            while (i < cleaned.size() && isHex(cleaned[i])) i++;
            continue;
        }

        // ':' continuation — les données hex suivent
        if (c == ':') { i++; continue; }

        // Séquence de chiffres hex
        if (isHex(c)) {
            // Collecter tous les hex digits consécutifs
            size_t hexStart = i;
            while (i < cleaned.size() && isHex(cleaned[i])) i++;
            std::string hexStr = cleaned.substr(hexStart, i - hexStart);

            // Vérifier ce qui suit : 'R' = run, ':' = adresse, sinon = données
            // Sauter les espaces pour voir le prochain caractère significatif
            size_t peek = i;
            while (peek < cleaned.size() && (cleaned[peek] == ' ' || cleaned[peek] == '\t' || cleaned[peek] == '\r' || cleaned[peek] == '\n')) peek++;

            // Inline data-byte writer with zone tracking. Every byte written
            // through this helper extends or starts a zone; address-prefix
            // flips below close the zone before resetting currentAddr.
            auto writeByte = [&](uint8_t v) {
                if (currentAddr >= 0x10000) return;
                if (!zoneActive) {
                    zoneStart = static_cast<uint16_t>(currentAddr);
                    zoneActive = true;
                }
                mem[currentAddr] = v;
                zoneLastAddr = currentAddr;
                currentAddr++;
                totalBytes++;
            };

            if (i < cleaned.size() && (cleaned[i] == 'R' || cleaned[i] == 'r')) {
                // Handle merged data+run: e.g. "FFE2B3R" = data FF, run E2B3
                if (hexStr.size() > 4) {
                    size_t dataLen = hexStr.size() - 4;
                    if (dataLen % 2 != 0) oddDigitsDropped++;
                    for (size_t j = 0; j + 1 < dataLen; j += 2) {
                        uint8_t val = (hexVal(hexStr[j]) << 4) | hexVal(hexStr[j + 1]);
                        writeByte(val);
                    }
                    hexStr = hexStr.substr(dataLen);
                }
                runAddr = (uint16_t)strtol(hexStr.c_str(), nullptr, 16);
                hasRunAddr = true;
                i++; // skip the R
                continue;
            }

            if (peek < cleaned.size() && cleaned[peek] == ':' && hexStr.size() >= 3) {
                // Handle merged data+address: e.g. "ED0300:" = data ED, address 0300
                if (hexStr.size() > 4) {
                    size_t dataLen = hexStr.size() - 4;
                    if (dataLen % 2 != 0) oddDigitsDropped++;
                    for (size_t j = 0; j + 1 < dataLen; j += 2) {
                        uint8_t val = (hexVal(hexStr[j]) << 4) | hexVal(hexStr[j + 1]);
                        writeByte(val);
                    }
                    hexStr = hexStr.substr(dataLen);
                }
                // Address line: only close the current zone when the new
                // address is a real jump. Sequential address lines that
                // pick up exactly where the previous line left off
                // (e.g. "0288:" after 8 bytes from "0280:") stay in the
                // same zone — closing on every address line would shred
                // chess.txt into one zone per 8-byte row.
                uint16_t newAddr = (uint16_t)strtol(hexStr.c_str(), nullptr, 16);
                if (zoneActive && newAddr != currentAddr) closeZone();
                currentAddr = newAddr;
                if (firstAddr) {
                    startAddress = currentAddr;
                    firstAddr = false;
                }
                i = peek + 1; // skip the ':'
                continue;
            }

            // Data bytes — parse in pairs. A lone trailing nibble would
            // otherwise be silently dropped, so track it for the summary
            // warning below — masks real bugs in hand-edited dumps.
            if (hexStr.size() % 2 != 0) oddDigitsDropped++;
            for (size_t j = 0; j + 1 < hexStr.size(); j += 2) {
                uint8_t val = (hexVal(hexStr[j]) << 4) | hexVal(hexStr[j + 1]);
                writeByte(val);
            }
            continue;
        }

        // Caractère inconnu — sauter
        i++;
    }

    // Close the final zone (the one currently being filled when the parse ran
    // off the end of the file).
    closeZone();

    // Utiliser l'adresse de run si disponible, sinon la première adresse
    if (hasRunAddr)
        startAddress = runAddr;

    if (bytesLoaded) *bytesLoaded = totalBytes;
    // Hex dumps scatter writes across arbitrary pages via currentAddr; the
    // precise range isn't tracked here, so fall back to "everything might
    // have changed". Hex-dump loading is a user action (rare), not a hot path.
    if (totalBytes > 0) markAllPagesDirty();
    {
        std::ostringstream oss;
        oss << "Hex dump loaded: " << std::filesystem::path(filename).filename().string()
            << " (" << std::dec << totalBytes << " bytes starting at 0x"
            << std::hex << startAddress << ")";
        pom1::log().info("Mem", oss.str());
    }
    if (oddDigitsDropped > 0) {
        std::ostringstream oss;
        oss << "Hex dump " << std::filesystem::path(filename).filename().string()
            << ": " << std::dec << oddDigitsDropped
            << " odd-length hex run(s) detected — trailing nibble(s) dropped. "
            << "Check the source for a truncated byte.";
        pom1::log().warn("Mem", oss.str());
    }
    return firstAddr && !hasRunAddr ? 1 : 0;
}

uint8_t Memory::memRead(uint16_t address)
{
    // Test mode: flat 64 KB RAM, no side effects (Klaus Dormann functional
    // test expects the whole address space to behave as RAM).
    if (testMode) return mem[address];

    // Watchpoint: latch the first read of a watched address this instruction.
    if (anyWatch_ && !watchHit_.tripped && (watchFlags_[address] & 0x01))
        watchHit_ = { true, address, false };

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
        uint8_t result = keyReady ? 0x80 : 0x00;
        return result;
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

    // Protection ROM (si writeInRom est désactivé)
    if (!writeInRom) {
        // WOZ Monitor: 0xFF00-0xFFFF — real 256-byte bipolar PROM on the
        // motherboard, physically unwriteable.
        if (address >= 0xFF00) return;
        // ACI ROM: 0xC100-0xC1FF — real 256-byte PROM on the ACI card.
        if (address >= 0xC100 && address <= 0xC1FF) return;
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
    // Real hardware only latches a glyph into the 74LS164 shift register when
    // PB7 = 1 (the data-strobe bit). Writes with bit 7 clear are PIA handshake
    // / DDR setup writes — most visibly the WOZ Monitor reset sequence at
    // $FF02 (`LDY #$7F / STY $D012`) which sets the port-B DDR and would
    // otherwise paint a spurious '_' on every soft reset.
    //
    // POM1 however has been historically permissive: emulator-era demos
    // (e.g. software/Apple-1_TMS_CC65/nino-democ.bin, whose startup banner uses the WOZ
    // Monitor ECHO routine with plain ASCII in the accumulator — bit 7
    // clear) print correctly on POM1 even though a real Apple-1 would keep
    // the 74LS164 silent. Breaking that compatibility regresses every
    // legacy program that predates the bit-7-strobe convention, so instead
    // of gating on bit 7 we narrowly filter the single raw-$7F write the
    // WOZ reset sequence emits. Any program that genuinely wants a '_'
    // glyph asks for $DF via ECHO (`value & 0x7F` still yields $5F — the
    // underscore slot in the Apple-1 character ROM) and is unaffected.
    if (address == 0xD012) {
        if (value != 0x7F) {
            displayBusyCycles = displayCharDelay; // Simuler le délai du terminal
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

void Memory::setSIDEnabled(bool b)
{
    if (b == sidEnabled) return;
    if (b) {
        // Prototype and Special Edition share the same `sid` instance —
        // only one can be plugged at a time.
        if (sidSpecialEditionEnabled) setSIDSpecialEditionEnabled(false);
        // Juke-Box bank-select latch lives at $CA00, inside the SID window
        // $C800-$CFFF — the two cards cannot coexist.
        if (jukeBoxEnabled) setJukeBoxEnabled(false);
        // Attach the audio sink BEFORE the emulation starts producing samples
        // (sidEnabled gates advanceCycles). Otherwise the first slice pushes
        // into an undrained ring and the audio callback plays catch-up.
        audioDevice->addSource(sid.get());
        sidEnabled = true;
        bus.setEnabled(sidBusHandle, true);
    } else {
        // Stop production first, then detach the audio sink. This guarantees
        // no advanceCycles() call can land between the removeSource and the
        // sidEnabled flip (which would push samples into a ring no one
        // drains).
        sidEnabled = false;
        bus.setEnabled(sidBusHandle, false);
        audioDevice->removeSource(sid.get());
        sid->reset();
    }
}

void Memory::setSIDSpecialEditionEnabled(bool b)
{
    if (b == sidSpecialEditionEnabled) return;
    if (b) {
        // Special Edition at $CC00-$CC1F collides with TMS9918's $CC00/$CC01
        // window — unplug the VDP first to keep the bus dispatch unambiguous.
        if (tms9918Enabled) setTMS9918Enabled(false);
        // Shares the single `sid` instance with the prototype. If the
        // prototype variant was plugged, unplug it first — real hardware
        // can only have one A1-SID card at a time (same socket, same chip).
        if (sidEnabled) setSIDEnabled(false);
        // SE at $CC00-$CC1F is disjoint from the Juke-Box bank latch
        // ($CA00) so the two can coexist — no eviction needed.
        audioDevice->addSource(sid.get());
        sidSpecialEditionEnabled = true;
        bus.setEnabled(sidSEBusHandle, true);
    } else {
        sidSpecialEditionEnabled = false;
        bus.setEnabled(sidSEBusHandle, false);
        audioDevice->removeSource(sid.get());
        sid->reset();
    }
}

void Memory::setMicroSDEnabled(bool b)
{
    if (b == microSDEnabled) return;
    microSDEnabled = b;
    bus.setEnabled(microSDBusHandle, b);
    if (b) {
        // Mutually exclusive with CFFA1 and Juke-Box (shared $8000-$9FFF window)
        if (cffa1Enabled) setCFFA1Enabled(false);
        if (jukeBoxEnabled) setJukeBoxEnabled(false);
        // Reload only when the ROM window is empty (first plug after it was
        // cleared by a previous disable, or after CFFA1 / Juke-Box overwrote
        // $8000). initMemory() pre-loads the SD CARD OS for the default
        // single-plug boot path, so a redundant disk read is avoided.
        // Signature check: sdcard.rom begins with A9 00 (LDA #$00).
        if (mem[0x8000] != 0xA9 || mem[0x8001] != 0x00) {
            loadSDCardRom();
        }
    } else {
        // IEC daughterboard rides on microSD's VIA — drop it first.
        if (iecCardEnabled) setIECCardEnabled(false);
        // Clear the ROM region (restore to RAM)
        std::fill(mem.begin() + 0x8000, mem.begin() + 0xA000, 0);
        markPagesDirty(0x8000, 0x2000);
    }
}

void Memory::setIECCardEnabled(bool b)
{
    if (b == iecCardEnabled) return;
    iecCardEnabled = b;
    if (b) {
        // The IEC card is a daughterboard that plugs into the microSD card's
        // J1 connector. It physically requires microSD; auto-enable.
        if (!microSDEnabled) setMicroSDEnabled(true);
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
    wifiModemEnabled = b;
    bus.setEnabled(wifiModemBusHandle, b);
    // Juke-Box covers $B000-$B003 when plugged — evict on plug-in.
    if (b && jukeBoxEnabled) setJukeBoxEnabled(false);
}

int Memory::loadSDCardRom()
{
    bool prev = writeInRom;
    writeInRom = true;
    // Clear region first — ROM file (8177 B) may not fill the full 8 KB space
    std::fill(mem.begin() + 0x8000, mem.begin() + 0xA000, 0);
    int ret = loadROM("sdcard.rom", 0x8000, 0x2000, "SD CARD OS");
    markPagesDirty(0x8000, 0x2000);
    writeInRom = prev;
    return ret;
}

void Memory::setCFFA1Enabled(bool b)
{
    if (b == cffa1Enabled) return;
    cffa1Enabled = b;
    bus.setEnabled(cffa1RomBusHandle, b);
    bus.setEnabled(cffa1RegBusHandle, b);
    if (b) {
        // Mutually exclusive with microSD and Juke-Box (shared $9000-$AFDF window)
        if (microSDEnabled) setMicroSDEnabled(false);
        if (jukeBoxEnabled) setJukeBoxEnabled(false);
        loadCFFA1Rom();
    } else {
        // Clear the CFFA1 ROM region
        std::fill(mem.begin() + 0x9000, mem.begin() + 0xB000, 0);
        markPagesDirty(0x9000, 0x2000);
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
    jukeBoxEnabled = b;
    if (b) {
        // Juke-Box monopolises $4000-$BFFF (ROM window) and $CA00 (bank
        // latch). Evict every other card that sits inside $4000-$CFFF so
        // bus dispatch stays unambiguous and the user can't get confused
        // by stale state from a previous preset. A1-SID and A1-AUDIO SE
        // are on the eviction list — they share $CA00 with the Px/Sx
        // bank register. CodeTank's $4000-$7FFF window also collides.
        if (codeTankEnabled) setCodeTankEnabled(false);
        if (cffa1Enabled) setCFFA1Enabled(false);
        if (microSDEnabled) setMicroSDEnabled(false);
        if (wifiModemEnabled) setWiFiModemEnabled(false);
        if (sidEnabled) setSIDEnabled(false);
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
        mem[0x003F] = jukeBox->getBootPage();
        markPagesDirty(0x0000, 0x0100);
        applyJukeBoxFlatMemoryMirror();
    } else {
        bus.setEnabled(jukeBox32BusHandle, false);
        bus.setEnabled(jukeBox16BusHandle, false);
        bus.setEnabled(jukeBoxBankRegBusHandle, false);
    }
}

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
    const char* candidates[] = {
        "jukebox.rom", "roms/jukebox.rom", "../roms/jukebox.rom", "../../roms/jukebox.rom",
    };
    for (const char* p : candidates) {
        std::string error;
        if (jukeBox->loadRomFile(p, error)) {
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
    codeTankEnabled = b;
    if (b) {
        // CodeTank is a daughterboard of the TMS9918 Graphic Card — it has
        // no edge connector and no on-board address decoder, so it cannot
        // exist standalone on the Apple-1 bus. Auto-plug the host first so
        // the daughterboard always rides on real silicon.
        if (!tms9918Enabled) setTMS9918Enabled(true);
        // CodeTank's $4000-$7FFF window collides with the Juke-Box's
        // $4000-$BFFF / $4000-$7FFF half. Keep one ROM card plugged at a
        // time — matches Parmigiani's "one board" rule.
        if (jukeBoxEnabled) setJukeBoxEnabled(false);
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
        // showing stale ROM contents at $4000-$7FFF after unplug.
        std::fill_n(mem.begin() + CodeTank::kBase, CodeTank::kHalfSize, static_cast<uint8_t>(0));
        markPagesDirty(CodeTank::kBase, CodeTank::kHalfSize);
    }
}

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
        // Preset / CLI paths are usually repo-relative (roms/codetank/…).
        // When the process cwd is build/ or another subdir, the bare path
        // misses — try the same ../ and ../../ prefixes as the default
        // probe below so we don't WARN and then succeed on setCodeTankEnabled.
        std::string error;
        namespace fs = std::filesystem;
        const fs::path p(path);
        const char* relPrefixes[] = { "", "../", "../../", "../../../" };
        for (const char* pre : relPrefixes) {
            if (pre[0] != '\0' && p.is_absolute())
                break;
            const std::string tryPath = (pre[0] == '\0') ? path : std::string(pre) + path;
            if (codeTank->loadRomFile(tryPath, error)) {
                if (codeTankEnabled) applyCodeTankFlatMemoryMirror();
                return 0;
            }
        }
        lastError = error;
        pom1::log().warn("Mem", lastError);
        return 1;
    }
    // Default probe order. The shipped CodeTank library image
    // (`Codetank_GAME1.rom`, built by tools/build_codetank_rom.py) wins
    // so plugging the CodeTank from the toolbar/Hardware menu drops the
    // user straight into the bundled software: lower jumper = 3-game
    // menu (Galaga/Sokoban/Snake), upper jumper = TMS_LOGO V2.6 turtle
    // interpreter. The legacy single-file `roms/codetank.rom` (kept
    // around from before the library directory) stays as a fallback.
    // Other carts (GAME2/GAME3/TEST) are picked via File → P-LAB
    // CodeTank Library.
    const char* candidates[] = {
        "roms/codetank/Codetank_GAME1.rom",
        "../roms/codetank/Codetank_GAME1.rom",
        "../../roms/codetank/Codetank_GAME1.rom",
        "codetank.rom",
        "roms/codetank.rom", "../roms/codetank.rom", "../../roms/codetank.rom",
    };
    for (const char* p : candidates) {
        std::string error;
        if (codeTank->loadRomFile(p, error)) {
            if (codeTankEnabled) applyCodeTankFlatMemoryMirror();
            return 0;
        }
    }
    lastError = "CodeTank ROM not found "
                "(expected roms/codetank/Codetank_GAME1.rom)";
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

void Memory::advanceCycles(int cycles)
{
    if (cycles > 0 && displayBusyCycles > 0) {
        displayBusyCycles = std::max(0, displayBusyCycles - cycles);
    }
    cassetteDevice->advanceCycles(cycles);
    // GEN2 release video scanner — drives the cycle-accurate floating bus /
    // beam position. Same gating pattern as the cards above; zero cost when the
    // HGR framebuffer card is unplugged. At every video-frame rollover the
    // soft-switch journal recorded during the frame is published for the
    // beam-raced renderer (POM2 "republish at each video-frame boundary"
    // model — the UI may re-render the same published frame at 60 Hz).
    if (hgrFramebufferAttached) {
        const uint64_t cpf    = gen2Scanner.cyclesPerFrame();
        const uint64_t before = gen2Scanner.cycle() / cpf;
        gen2Scanner.advanceCycles(static_cast<uint64_t>(cycles));
        if (gen2Scanner.cycle() / cpf != before) {
            gen2PublishedEvents = std::move(gen2RecordingEvents);
            gen2RecordingEvents.clear();
            gen2PublishedFrameStart = gen2RecordingFrameStart;
            gen2RecordingFrameStart = gen2Scanner.displayState();
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
    if (sidEnabled || sidSpecialEditionEnabled) sid->advanceCycles(cycles);

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
// Snapshot save / load
// ─────────────────────────────────────────────────────────────────────
//
// Format: see SnapshotIO.h. Sections written in this order:
//   "MEM     " — 64 KB RAM + scalar/flag state
//   "FLAGS   " — packed enable bits (16 bits, 12 used)
//   "<card>  " — per-peripheral payload via Peripheral::serialize()
//
// Reader iterates sections by name and dispatches; unknown sections are
// skipped (forward-compat). Cards that haven't migrated their state yet
// write empty sections (the default Peripheral::serialize is a no-op),
// so the framework already works end-to-end before every card is ready.

#include "SnapshotIO.h"

namespace {

// Pack the 12 card-enabled flags into a single uint16_t. Order is stable
// across versions — appending a new card reserves the next bit; never
// reorder existing bits without bumping the snapshot version.
// uint32_t (not uint16_t): the original 16 bits are full, and v3 needs bit 16
// for GEN2. The FLAGS section is written as a u32 from v3 on; the reader still
// accepts the legacy 2-byte payload (see readSnapshotSections).
constexpr uint32_t kFlagACI            = 1u << 0;
constexpr uint32_t kFlagTMS9918        = 1u << 1;
constexpr uint32_t kFlagSID            = 1u << 2;
constexpr uint32_t kFlagSIDSpecialEdt  = 1u << 3;
constexpr uint32_t kFlagMicroSD        = 1u << 4;
constexpr uint32_t kFlagCFFA1          = 1u << 5;
constexpr uint32_t kFlagJukeBox        = 1u << 6;
constexpr uint32_t kFlagCodeTank       = 1u << 7;
constexpr uint32_t kFlagWiFiModem      = 1u << 8;
constexpr uint32_t kFlagTerminalCard   = 1u << 9;
constexpr uint32_t kFlagA1IO_RTC       = 1u << 10;
constexpr uint32_t kFlagPR40           = 1u << 11;
constexpr uint32_t kFlagGT6144         = 1u << 12;
constexpr uint32_t kFlagCassetteAudio  = 1u << 13;
constexpr uint32_t kFlagSiliconStrict  = 1u << 14;  // TMS9918 silicon-strict timing window
constexpr uint32_t kFlagIECCard        = 1u << 15;  // P-LAB IEC daughterboard (microSD daughterboard)
constexpr uint32_t kFlagGEN2HGR        = 1u << 16;  // GEN2 HGR card attached (v3+; widened to u32)

} // namespace

bool Memory::saveSnapshot(const std::string& path, std::string& error,
                          const M6502* cpu) const
{
    pom1::SnapshotWriter w(path);
    if (!w.good()) {
        error = "cannot open snapshot file for writing: " + path;
        return false;
    }
    writeSnapshotSections(w, cpu);
    if (!w.good()) {
        error = "I/O error while writing snapshot";
        return false;
    }
    return true;
}

std::vector<uint8_t> Memory::saveSnapshotToBuffer(const M6502* cpu) const
{
    pom1::SnapshotWriter w;   // in-memory sink
    if (!w.good()) return {};
    writeSnapshotSections(w, cpu);
    if (!w.good()) return {};
    return w.takeBuffer();
}

bool Memory::loadSnapshotFromBuffer(const std::vector<uint8_t>& buffer,
                                    std::string& error, M6502* cpu)
{
    pom1::SnapshotReader r(buffer);
    if (!r.good()) {
        error = r.error().empty() ? "snapshot read failed" : r.error();
        return false;
    }
    return readSnapshotSections(r, error, cpu);
}

void Memory::writeSnapshotSections(pom1::SnapshotWriter& w, const M6502* cpu) const
{
    // ── CPU section: architecturally-visible 6502 state. Skipped when the
    //    caller didn't supply a CPU (memory-only fixtures); loadSnapshot
    //    treats a missing CPU section the same way for forward-compat.
    if (cpu) {
        auto h = w.beginSection("CPU");
        cpu->serialize(w);
        w.endSection(h);
    }

    // ── MEM section: 64 KB RAM + key/display state + scalar bookkeeping
    {
        auto h = w.beginSection("MEM");
        w.writeBytes(mem.data(), mem.size());
        w.writeU8(static_cast<uint8_t>(lastKey));
        w.writeU8(keyReady ? 1 : 0);
        w.writeU32(static_cast<uint32_t>(displayBusyCycles));
        w.writeU16(static_cast<uint16_t>(ramSize));
        w.writeU16(static_cast<uint16_t>(presetRamKB));
        w.writeU8(oorStrictMode ? 1 : 0);
        w.writeU8(writeInRom ? 1 : 0);
        w.endSection(h);
    }

    // ── FLAGS section: packed card-enabled bitmap (u32 since v3)
    {
        uint32_t flags = 0;
        if (aciEnabled)                flags |= kFlagACI;
        if (tms9918Enabled)            flags |= kFlagTMS9918;
        if (sidEnabled)                flags |= kFlagSID;
        if (sidSpecialEditionEnabled)  flags |= kFlagSIDSpecialEdt;
        if (microSDEnabled)            flags |= kFlagMicroSD;
        if (cffa1Enabled)              flags |= kFlagCFFA1;
        if (jukeBoxEnabled)            flags |= kFlagJukeBox;
        if (codeTankEnabled)           flags |= kFlagCodeTank;
        if (wifiModemEnabled)          flags |= kFlagWiFiModem;
        if (terminalCardEnabled)       flags |= kFlagTerminalCard;
        if (a1ioRtcEnabled)            flags |= kFlagA1IO_RTC;
        if (pr40Enabled)               flags |= kFlagPR40;
        if (gt6144Enabled)             flags |= kFlagGT6144;
        if (cassetteAudioActive)       flags |= kFlagCassetteAudio;
        if (siliconStrictMode)         flags |= kFlagSiliconStrict;
        if (iecCardEnabled)            flags |= kFlagIECCard;
        if (hgrFramebufferAttached)    flags |= kFlagGEN2HGR;

        auto h = w.beginSection("FLAGS");
        w.writeU32(flags);
        w.endSection(h);
    }

    // ── Per-peripheral sections. Iterate the same order so loadSnapshot
    // matches up by sequence; the section name is also written for
    // forward-compat (unknown sections are skipped on load).
    auto writeCard = [&](const pom1::Peripheral& p) {
        auto h = w.beginSection(p.name());
        p.serialize(w);
        w.endSection(h);
    };
    writeCard(*cassetteDevice);
    writeCard(*tms9918);
    writeCard(*sid);
    writeCard(*microSD);
    writeCard(*cffa1);
    writeCard(*jukeBox);
    writeCard(*codeTank);
    writeCard(*wifiModem);
    writeCard(*terminalCard);
    writeCard(*a1ioRtc);
    writeCard(*pr40Printer);
    writeCard(*gt6144);
    writeCard(*iecCard);

    // ── GEN2VID section: GEN2 release soft-switch latch + video phase.
    //    The latch survives Apple-1 RESET on real hardware, so it must
    //    survive snapshots / rewind too (a page-2 game restored mid-frame
    //    would otherwise display the wrong HGR page until its next flip).
    {
        const Gen2VideoScanner::DisplayState& ds = gen2Scanner.displayState();
        auto h = w.beginSection("GEN2VID");
        w.writeU8(ds.textMode  ? 1 : 0);
        w.writeU8(ds.mixedMode ? 1 : 0);
        w.writeU8(ds.page2     ? 1 : 0);
        w.writeU8(ds.hiRes     ? 1 : 0);
        w.writeU8(gen2Scanner.isFiftyHz() ? 1 : 0);
        w.writeU64(gen2Scanner.cycle());
        w.endSection(h);
    }

    // ── SCREEN section: the Apple-1 text grid lives in the display device
    //    (Screen_ImGui), not in RAM. Capture it so rewind / save-state restore
    //    the *visible* screen — otherwise scrubbing the timeline moves CPU+RAM
    //    back but the on-screen text stays at the live frame. Skipped for
    //    memory-only fixtures with no display attached.
    if (displayDevice) {
        auto h = w.beginSection("SCREEN");
        displayDevice->serialize(w);
        w.endSection(h);
    }
}

bool Memory::loadSnapshot(const std::string& path, std::string& error,
                          M6502* cpu)
{
    pom1::SnapshotReader r(path);
    if (!r.good()) {
        error = r.error().empty() ? "snapshot read failed" : r.error();
        return false;
    }
    return readSnapshotSections(r, error, cpu);
}

bool Memory::readSnapshotSections(pom1::SnapshotReader& r, std::string& error, M6502* cpu)
{
  // A corrupt/truncated snapshot can carry a forged length that drives a card's
  // deserialize to allocate gigabytes (std::string/vector ctors, reserve()).
  // Those throw bad_alloc/length_error; catch them here so a bad file (File →
  // Load snapshot, --load-snapshot) or a damaged rewind blob fails gracefully
  // instead of std::terminate. The reader's own per-field length guard handles
  // the common case; this is the backstop for the count-then-reserve paths.
  try {
    // Build a name → peripheral lookup so we can dispatch by section
    // name (insulates us against future card-order changes).
    std::vector<pom1::Peripheral*> cards = {
        cassetteDevice.get(), tms9918.get(), sid.get(),
        microSD.get(), cffa1.get(), jukeBox.get(), codeTank.get(),
        wifiModem.get(), terminalCard.get(), a1ioRtc.get(),
        pr40Printer.get(), gt6144.get(), iecCard.get(),
    };

    std::string sectionName;
    uint32_t    sectionLen = 0;
    while (r.nextSection(sectionName, sectionLen)) {
        if (sectionName == "CPU") {
            if (cpu) {
                cpu->deserialize(r);
            } else {
                r.skipCurrentSection();
            }
            continue;
        }
        if (sectionName == "MEM") {
            // Validate the declared section length before reading: the MEM
            // payload is a fixed 64 KB RAM image plus 12 bytes of trailing
            // scalars. A truncated/forged shorter length would otherwise make
            // readBytes consume bytes belonging to the next section and load
            // garbage into RAM and the machine-state scalars while reporting
            // success. Mirror readString's remainingBytes guard.
            constexpr uint32_t kMemSectionLen =
                0x10000u + 1 + 1 + 4 + 2 + 2 + 1 + 1; // RAM + scalars
            if (sectionLen != kMemSectionLen) {
                error = "corrupt snapshot: MEM section length "
                      + std::to_string(sectionLen) + " (expected "
                      + std::to_string(kMemSectionLen) + ")";
                r.fail();
                return false;
            }
            r.readBytes(mem.data(), mem.size());
            lastKey            = static_cast<char>(r.readU8());
            keyReady           = r.readU8() != 0;
            displayBusyCycles  = static_cast<int>(r.readU32());
            // Clamp restored RAM sizing: resetMemory()/clearMemory() loop over
            // ramSize*1024 into the fixed 64 KB mem[] buffer, so an unvalidated
            // value from a corrupt/forged blob would drive an out-of-bounds
            // heap write (the surrounding try/catch cannot catch UB). presetRamKB
            // would likewise bypass setPresetRamKB()'s clamp.
            ramSize            = std::clamp(static_cast<int>(r.readU16()), 0, 64);
            presetRamKB        = std::clamp(static_cast<int>(r.readU16()), 4, 64);
            oorStrictMode      = r.readU8() != 0;
            writeInRom         = r.readU8() != 0;
            markAllPagesDirty();
            continue;
        }
        if (sectionName == "FLAGS") {
            // v3+ writes a u32; v1/v2 wrote a u16. Pick the width from the
            // section length so old snapshots still load (the GEN2 bit and any
            // future high bits then read as 0).
            const uint32_t flags = (sectionLen >= 4) ? r.readU32()
                                                     : r.readU16();
            // Apply enable flags via the public setters so each card's
            // bus handlers + ROM mirrors reconfigure themselves
            // correctly. setX methods are idempotent.
            setACIEnabled              ((flags & kFlagACI)            != 0);
            setTMS9918Enabled          ((flags & kFlagTMS9918)        != 0);
            setSIDEnabled              ((flags & kFlagSID)            != 0);
            setSIDSpecialEditionEnabled((flags & kFlagSIDSpecialEdt)  != 0);
            setMicroSDEnabled          ((flags & kFlagMicroSD)        != 0);
            setCFFA1Enabled            ((flags & kFlagCFFA1)          != 0);
            setJukeBoxEnabled          ((flags & kFlagJukeBox)        != 0);
            setCodeTankEnabled         ((flags & kFlagCodeTank)       != 0);
            setWiFiModemEnabled        ((flags & kFlagWiFiModem)      != 0);
            terminalCardEnabled       =((flags & kFlagTerminalCard)   != 0);
            setA1IO_RTCEnabled         ((flags & kFlagA1IO_RTC)       != 0);
            pr40Enabled               =((flags & kFlagPR40)           != 0);
            setGT6144Enabled           ((flags & kFlagGT6144)         != 0);
            cassetteAudioActive       =((flags & kFlagCassetteAudio)  != 0);
            // Restoring a v1 .snap (saved before kFlagSiliconStrict existed)
            // will land here with the bit clear — treat as "unknown" rather
            // than "off" by leaving the current value. Versioned snapshots
            // would let us distinguish; for now, be conservative and only
            // overwrite when the flag is set OR when other v2-era bits show
            // the writer was aware of bit 14 (none yet → we just always
            // honour the bit).
            setSiliconStrictMode       ((flags & kFlagSiliconStrict)  != 0);
            // IEC card cascades onto microSD via setIECCardEnabled — make sure
            // microSD has been (re-)enabled by the FLAGS dispatch above first.
            setIECCardEnabled          ((flags & kFlagIECCard)        != 0);
            // GEN2 HGR attach. Restore the member + bus handle DIRECTLY rather
            // than via setHgrFramebufferAttached(): a cold-plug through the
            // setter re-seeds $2000-$3FFF with DRAM noise and resets the video
            // scanner phase, which would clobber the framebuffer just restored
            // by the MEM section and the latch/cycle restored by GEN2VID below.
            // The MEM section already holds the framebuffer bytes, so all that
            // is needed here is the attach state + the soft-switch bus window.
            hgrFramebufferAttached = (flags & kFlagGEN2HGR) != 0;
            bus.setEnabled(gen2SoftSwitchBusHandle, hgrFramebufferAttached);
            continue;
        }

        if (sectionName == "SCREEN") {
            // Restore the visible Apple-1 text grid (rewind / save-state).
            if (displayDevice) displayDevice->deserialize(r);
            else               r.skipCurrentSection();
            continue;
        }

        if (sectionName == "GEN2VID") {
            Gen2VideoScanner::DisplayState ds;
            ds.textMode  = r.readU8() != 0;
            ds.mixedMode = r.readU8() != 0;
            ds.page2     = r.readU8() != 0;
            ds.hiRes     = r.readU8() != 0;
            gen2Scanner.setFiftyHz(r.readU8() != 0);
            gen2Scanner.setCycle(r.readU64());
            gen2Scanner.setDisplayState(ds);
            // Journaled events reference the pre-restore cycle stream —
            // drop them; the next video frame republishes from the restored
            // latch state.
            resetGen2VideoEventJournal();
            continue;
        }

        // Per-peripheral section: dispatch by name. The section name on disk is
        // truncated to kSectionNameLen (8) bytes by writeFixedName, so compare
        // against the card name TRUNCATED to the same width — otherwise a card
        // whose name exceeds 8 chars (e.g. "A1-IO/RTC", "Wi-Fi Modem") never
        // matches its own section and its state is silently dropped.
        bool dispatched = false;
        for (auto* card : cards) {
            if (card && card->name().substr(0, pom1::kSectionNameLen) == sectionName) {
                card->deserialize(r);
                dispatched = true;
                break;
            }
        }
        if (!dispatched) {
            // Unknown section — skip (forward-compat).
            r.skipCurrentSection();
        }
    }

    if (!r.good()) {
        error = "I/O error while reading snapshot";
        return false;
    }
    return true;
  } catch (const std::exception& e) {
    error = std::string("corrupt snapshot: ") + e.what();
    return false;
  }
}


