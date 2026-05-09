#include "IECCard.h"
#include "SnapshotIO.h"

#include <cstdio>

#ifndef POM1_IEC_DEBUG
#define POM1_IEC_DEBUG 0
#endif

namespace pom1 {

namespace {
constexpr uint8_t kDriveAddress = 8;

// Drive1541 talker per-bit phase delay (in CPU cycles at 1.022 MHz).
// SD OS 1.3 bit-time is ~60 µs; we use 50-cycle granularity as a
// generous floor that beats the firmware's NOP-spacing without being
// too lax against the 1ms T2 watchdog.
constexpr int kTxBitSettleCycles = 30;
constexpr int kTxByteAckCycles   = 60;
constexpr int kTxEoiHoldCycles   = 250;  // ~250 µs for EOI signal
}

IECCard::IECCard() : drive_(kDriveAddress) {
    // Start with everything released.
    bus_.release();
}

bool IECCard::mountDisk(const std::string& d64Path) {
    return drive_.mount(d64Path);
}

void IECCard::unmount() {
    drive_.unmount();
}

void IECCard::busReset() {
    bus_.release();
    drive_.busReset();
    role_ = Role::Idle;
    lastSecondary_ = 0xFF;
    wasOpenSecondary_ = false;
    rxPhase_ = RxPhase::Idle;
    rxBitCount_ = 0;
    rxByte_ = 0;
    rxEoi_ = false;
    rxEoiTimerCycles_ = 0;
    txPhase_ = TxPhase::Idle;
    txBitIndex_ = 0;
    txByte_ = 0;
    txEoi_ = false;
    txDelayCycles_ = 0;
    prevAtnLow_ = prevClkLow_ = prevDataLow_ = false;
}

// ---------- VIA hooks -------------------------------------------------------

void IECCard::onViaPortBWrite(uint8_t portB, uint8_t ddrB) {
    lastPortB_ = portB;
    lastDdrB_  = ddrB;
    evaluateEdges(portB, ddrB);
}

void IECCard::onViaDdrBWrite(uint8_t ddrB) {
    lastDdrB_ = ddrB;
    evaluateEdges(lastPortB_, ddrB);
}

uint8_t IECCard::mergeViaPortBRead(uint8_t microsdValue) const {
    // The microSD pre-built value already has bits 0+7 (CPU/MCU strobes).
    // We replace IN/OUT IEC bits with a faithful read of the bus level.
    uint8_t out = microsdValue & ~kAllBits;

    // Per nippur72 schematic / kernal_serial.lm: VIA IN bits track bus level
    // directly (set = HIGH = released).
    if (bus_.level(IECBus::Line::CLK))  out |= kClkInBit;
    if (bus_.level(IECBus::Line::DATA)) out |= kDataInBit;

    // Read-back of OUT bits (whatever the host wrote).
    out |= (lastPortB_ & (kAtnOutBit | kClkOutBit | kDataOutBit));
    return out;
}

void IECCard::evaluateEdges(uint8_t portB, uint8_t ddrB) {
    // Recompute host-pulled IEC lines: SN7406 inverts, so DDR=1 + bit set
    // pulls the line LOW. DDR=0 (input mode) means no host drive.
    auto pulled = [&](uint8_t bit) -> bool {
        return (ddrB & bit) && (portB & bit);
    };
    bool atnNow  = pulled(kAtnOutBit);
    bool clkNow  = pulled(kClkOutBit);
    bool dataNow = pulled(kDataOutBit);

    bus_.setHostPulled(IECBus::Line::ATN,  atnNow);
    bus_.setHostPulled(IECBus::Line::CLK,  clkNow);
    bus_.setHostPulled(IECBus::Line::DATA, dataNow);

    // ATN edges.
    if (atnNow != prevAtnLow_) {
        if (atnNow) onAtnAsserted();
        else        onAtnReleased();
        prevAtnLow_ = atnNow;
    }
    // CLK edges.
    bool clkLineLow = !bus_.level(IECBus::Line::CLK);
    if (clkLineLow != prevClkLow_) {
        if (clkLineLow) onClkFallingEdge();
        else            onClkRisingEdge();
        prevClkLow_ = clkLineLow;
    }
    // DATA edges (only relevant for talker mode).
    bool dataLineLow = !bus_.level(IECBus::Line::DATA);
    if (dataLineLow != prevDataLow_) {
        if (dataLineLow) onDataFallingEdge();
        else             onDataRisingEdge();
        prevDataLow_ = dataLineLow;
    }
}

// ---------- ATN handling ----------------------------------------------------

#if POM1_IEC_DEBUG
static void iecLog(const char* tag) {
    std::fprintf(stderr, "[IEC] %s\n", tag);
}
#endif

void IECCard::onAtnAsserted() {
#if POM1_IEC_DEBUG
    iecLog("ATN asserted");
#endif
    // Under ATN, every device on the bus listens for command bytes. The
    // SD OS 1.3 kernel keeps ATN asserted across the *whole* OPEN+filename
    // +UNLSN+TALK+SECND command burst, so we don't reset role/secondary
    // state here — that's done by the command bytes themselves as they
    // arrive (interpretCommandByte).
    txPhase_ = TxPhase::Idle;
    rxPhase_ = RxPhase::WaitClkReleased;
    rxBitCount_ = 0;
    rxByte_ = 0;
    rxEoi_ = false;
    bus_.setDrivePulled(IECBus::Line::DATA, true);
    bus_.setDrivePulled(IECBus::Line::CLK,  false);
    prevDataLow_ = !bus_.level(IECBus::Line::DATA);
}

void IECCard::onAtnReleased() {
#if POM1_IEC_DEBUG
    std::fprintf(stderr, "[IEC] ATN released, role=%d\n", static_cast<int>(role_));
#endif
    // Role was already set by the last LISTEN/TALK/UNLSN/UNTALK byte that
    // arrived under ATN. Just act on the final role.
    if (role_ == Role::Talker) {
        rxPhase_ = RxPhase::Idle;
        startTransmitting();
    } else if (role_ == Role::Listener) {
        rxPhase_ = RxPhase::WaitClkReleased;
        bus_.setDrivePulled(IECBus::Line::DATA, true);
        bus_.setDrivePulled(IECBus::Line::CLK,  false);
    } else {
        releaseAllDriveLines();
    }
}

// ---------- CLK / DATA edge dispatch ---------------------------------------

void IECCard::onClkRisingEdge() {
    // Bus CLK went HIGH — talker released CLK.
    if (role_ != Role::Listener) return;
    switch (rxPhase_) {
        case RxPhase::WaitClkReleased:
            // Talker is ready ("I have a byte"). Release DATA to say "ready".
            bus_.setDrivePulled(IECBus::Line::DATA, false);
            rxPhase_ = RxPhase::ReadyAckPulled;
            // EOI window starts here. If the talker holds CLK released for
            // >200µs without pulling it low, that's an EOI signal.
            rxEoiTimerCycles_ = 0;
            break;
        case RxPhase::ReceivingBits: {
            // Sample DATA bit on CLK rising edge.
            int bit = bus_.level(IECBus::Line::DATA) ? 1 : 0;
            // LSB first (CBM convention).
            rxByte_ |= static_cast<uint8_t>(bit << rxBitCount_);
            rxBitCount_++;
            if (rxBitCount_ >= 8) {
                // Byte complete — fall through and wait for talker's CLK fall
                // before we ack with DATA. Listener acks DATA after talker
                // pulls CLK low to end the byte. Per-bit dance: talker pulls
                // CLK low again before next bit (BitSetup-equivalent).
            }
            break;
        }
        case RxPhase::ReadyAckPulled:
            // Talker keeps CLK released — track for EOI detection in advanceCycles.
            break;
        default: break;
    }
}

void IECCard::onClkFallingEdge() {
    if (role_ != Role::Listener) return;
    switch (rxPhase_) {
        case RxPhase::ReadyAckPulled:
            // Talker pulled CLK low → start of byte.
            rxPhase_ = RxPhase::ReceivingBits;
            rxBitCount_ = 0;
            rxByte_ = 0;
            // EOI handshake: if we observed >200µs CLK-released window and
            // pulsed DATA back, mark the byte as EOI. (Simplified: we set
            // rxEoi_ = true if the window exceeded threshold.)
            break;
        case RxPhase::ReceivingBits:
            if (rxBitCount_ >= 8) {
                // End of byte: pull DATA low to ack.
                bus_.setDrivePulled(IECBus::Line::DATA, true);
                rxPhase_ = RxPhase::ByteAck;
                deliverByteToDrive(rxByte_, rxEoi_);
                rxByte_ = 0;
                rxBitCount_ = 0;
                rxEoi_ = false;
            }
            break;
        default: break;
    }
}

void IECCard::onDataRisingEdge() {
    // Bus DATA went HIGH (released by everyone).
    if (role_ == Role::Talker && txPhase_ == TxPhase::ByteEnd) {
        // Listener has not yet pulled DATA — keep waiting.
    }
}

void IECCard::onDataFallingEdge() {
    // Bus DATA went LOW (someone pulled). Only relevant in Talker mode for
    // ack detection. The TX FSM is otherwise time-driven (see startTransmitting
    // for why we don't hinge on DATA edges in the ready-to-send window).
    if (role_ == Role::Talker && txPhase_ == TxPhase::ByteEnd) {
        // Listener pulled DATA = byte ack.
        txPhase_ = TxPhase::Hold;
        txDelayCycles_ = kTxByteAckCycles;
    }
}

// ---------- Listener byte handling -----------------------------------------

void IECCard::deliverByteToDrive(uint8_t b, bool eoi) {
    const bool inFilenameWindow = wasOpenSecondary_;
    const bool isUnHandshake    = (b == 0x3F || b == 0x5F);
#if POM1_IEC_DEBUG
    std::fprintf(stderr, "[IEC] rx byte=%02X eoi=%d atn=%d filenameWin=%d role=%d\n",
                 b, eoi ? 1 : 0, prevAtnLow_ ? 1 : 0, inFilenameWindow ? 1 : 0,
                 static_cast<int>(role_));
#endif
    if (prevAtnLow_ && !inFilenameWindow) {
        interpretCommandByte(b);
    } else if (prevAtnLow_ && inFilenameWindow && isUnHandshake) {
        interpretCommandByte(b);
    } else {
        drive_.listenByte(b, eoi);
    }
    rxPhase_ = RxPhase::WaitClkReleased;
}

void IECCard::interpretCommandByte(uint8_t b) {
    // ATN command bytes:
    //   $20-$3E: LISTEN device 0..30 (LISTEN = $20 | dev)
    //   $3F:     UNLISTEN
    //   $40-$5E: TALK device 0..30   (TALK = $40 | dev)
    //   $5F:     UNTALK
    //   $60-$6F: re-OPEN data channel for talk/listen
    //   $E0-$EF: CLOSE channel
    //   $F0-$FF: OPEN channel (filename to follow)
    if (b >= 0x20 && b <= 0x3E) {
        uint8_t dev = b & 0x1F;
        role_ = (dev == kDriveAddress) ? Role::Listener : Role::Idle;
        return;
    }
    if (b == 0x3F) {
        // UNLISTEN ($3F): bus-wide drop of listener role. Also closes any
        // OPEN-with-filename window — finalise it now so the drive can
        // pre-fill the read buffer before TALK arrives.
        if (wasOpenSecondary_) {
            drive_.unlistenAfterOpen(lastSecondary_);
            wasOpenSecondary_ = false;
        }
        if (role_ == Role::Listener) role_ = Role::Idle;
        return;
    }
    if (b >= 0x40 && b <= 0x5E) {
        uint8_t dev = b & 0x1F;
        role_ = (dev == kDriveAddress) ? Role::Talker : Role::Idle;
        return;
    }
    if (b == 0x5F) {
        // UNTALK ($5F): bus-wide drop of talker role.
        if (role_ == Role::Talker) role_ = Role::Idle;
        return;
    }
    // Secondary addresses follow LISTEN/TALK addressing.
    if (role_ == Role::Listener || role_ == Role::Talker) {
        uint8_t hi = b & 0xF0;
        uint8_t sec = b & 0x0F;
        if (hi == 0x60) {
            // Data channel re-open.
            lastSecondary_ = sec;
            wasOpenSecondary_ = false;
            drive_.openChannel(sec, /*isOpenCommand=*/false);
        } else if (hi == 0xE0) {
            // CLOSE channel.
            drive_.closeChannel(sec);
            lastSecondary_ = 0xFF;
            wasOpenSecondary_ = false;
        } else if (hi == 0xF0) {
            // OPEN channel — filename follows under ATN-released phase.
            lastSecondary_ = sec;
            wasOpenSecondary_ = true;
            drive_.openChannel(sec, /*isOpenCommand=*/true);
        }
    }
}

// ---------- Talker FSM ------------------------------------------------------

void IECCard::startTransmitting() {
    txBitIndex_ = 0;
    txByte_ = 0;
    txEoi_ = false;
    if (!drive_.talkByte(txByte_, txEoi_)) {
        // Nothing to send — release the bus and stay quiet. Listener will
        // eventually time out; the kernel handles that as STATUS bit-1
        // (timeout, BCS LD40 retry / EOI exit).
        releaseAllDriveLines();
        txPhase_ = TxPhase::AllSent;
        txDelayCycles_ = 0;
        return;
    }
    // Pull CLK low (= "I have a byte"); release DATA. Kernel's TKATN has
    // already pulled DATA on its side, so the listener-ready handshake is
    // satisfied as soon as we enter the dance. No DATA falling-edge wait.
    bus_.setDrivePulled(IECBus::Line::CLK,  true);
    bus_.setDrivePulled(IECBus::Line::DATA, false);
    txPhase_ = TxPhase::Initial;
    txDelayCycles_ = kTxBitSettleCycles;
}

void IECCard::advanceCycles(int cpuCycles) {
    if (cpuCycles <= 0) return;

    // EOI detection: if we're waiting in ReadyAckPulled (CLK released by host)
    // for >200 µs (~205 cycles), the firmware is signalling EOI on the next
    // byte (CBM convention: talker holds CLK > 200µs).
    if (role_ == Role::Listener && rxPhase_ == RxPhase::ReadyAckPulled) {
        rxEoiTimerCycles_ += cpuCycles;
        if (rxEoiTimerCycles_ > 205) {
            rxEoi_ = true;
            // Pulse DATA low briefly (>60µs) to ack EOI — but the next CLK
            // falling edge will pull DATA low anyway to ack the byte, so
            // we just remember the flag.
        }
    }

    if (role_ != Role::Talker) return;
    txDelayCycles_ -= cpuCycles;
    if (txDelayCycles_ > 0) return;

    switch (txPhase_) {
        case TxPhase::Idle: break;

        case TxPhase::Initial:
            // CLK has been low long enough for the listener to see "ready";
            // release CLK = "byte coming, EOI window starts now".
            bus_.setDrivePulled(IECBus::Line::CLK,  false);
            bus_.setDrivePulled(IECBus::Line::DATA, false);
            txPhase_ = TxPhase::ReadyToSend;
            txDelayCycles_ = txEoi_ ? kTxEoiHoldCycles : kTxBitSettleCycles;
            break;

        case TxPhase::ReadyToSend:
            // Optional EOI hold expired (or no EOI). Pull CLK low =
            // "starting bit transfer". Begin bit 0.
            bus_.setDrivePulled(IECBus::Line::CLK,  true);
            txBitIndex_ = 0;
            txPhase_ = TxPhase::BitSetup;
            txDelayCycles_ = kTxBitSettleCycles;
            break;

        case TxPhase::StartByte:
        case TxPhase::SettleEoi:
            // Legacy edge-driven phases — fold into ReadyToSend behaviour.
            bus_.setDrivePulled(IECBus::Line::CLK, true);
            txBitIndex_ = 0;
            txPhase_ = TxPhase::BitSetup;
            txDelayCycles_ = kTxBitSettleCycles;
            break;

        case TxPhase::BitSetup: {
            // Set DATA according to current bit (LSB first), keep CLK low.
            int bit = (txByte_ >> txBitIndex_) & 1;
            // SN7406 inverts OUT bits: drive_pulled=true → bus LOW.
            // Bit value 1 = bus HIGH = drive does NOT pull.
            // Bit value 0 = bus LOW  = drive pulls.
            bus_.setDrivePulled(IECBus::Line::DATA, bit == 0);
            bus_.setDrivePulled(IECBus::Line::CLK,  true);
            txPhase_ = TxPhase::BitValid;
            txDelayCycles_ = kTxBitSettleCycles;
            break;
        }
        case TxPhase::BitValid:
            // Release CLK with bit on DATA → kernel samples on rising edge.
            bus_.setDrivePulled(IECBus::Line::CLK, false);
            txPhase_ = TxPhase::BitEnd;
            txDelayCycles_ = kTxBitSettleCycles;
            break;

        case TxPhase::BitEnd:
            txBitIndex_++;
            if (txBitIndex_ >= 8) {
                // End of byte: release DATA + CLK so listener can ack
                // (it pulls DATA briefly after seeing CLK low, then we wait
                // for that DATA-falling edge before sending the next byte).
                bus_.setDrivePulled(IECBus::Line::DATA, false);
                bus_.setDrivePulled(IECBus::Line::CLK,  false);
                txPhase_ = TxPhase::ByteEnd;
                txDelayCycles_ = 0;
            } else {
                // Pull CLK low again (= falling edge) for next bit setup.
                bus_.setDrivePulled(IECBus::Line::CLK, true);
                txPhase_ = TxPhase::BitSetup;
                txDelayCycles_ = kTxBitSettleCycles;
            }
            break;

        case TxPhase::ByteEnd:
            // Edge-driven: kernel pulls DATA → onDataFallingEdge → Hold.
            // Safety timeout: after 1500 cycles (~1.5 ms), assume ack
            // happened and try to send next byte regardless.
            txDelayCycles_ = 1500;
            txPhase_ = TxPhase::Hold;
            break;

        case TxPhase::Hold:
            // Post-ack hold; fetch next byte. Reached either by the safety
            // timeout above or by onDataFallingEdge from the listener.
            txByte_ = 0;
            txEoi_ = false;
            if (drive_.talkByte(txByte_, txEoi_)) {
                // Pull CLK low to signal "next byte coming" (between bytes
                // the kernel's ACP00A loop is waiting for CLK to go HIGH;
                // that requires us to be pulling now and to release shortly).
                bus_.setDrivePulled(IECBus::Line::CLK,  true);
                bus_.setDrivePulled(IECBus::Line::DATA, false);
                txPhase_ = TxPhase::Initial;
                txDelayCycles_ = kTxBitSettleCycles;
            } else {
                releaseAllDriveLines();
                txPhase_ = TxPhase::AllSent;
                txDelayCycles_ = 0;
            }
            break;

        case TxPhase::AllSent:
            txDelayCycles_ = 0;
            break;
    }
}

void IECCard::enterIdle() {
    role_ = Role::Idle;
    rxPhase_ = RxPhase::Idle;
    txPhase_ = TxPhase::Idle;
    releaseAllDriveLines();
}

void IECCard::releaseAllDriveLines() {
    bus_.setDrivePulled(IECBus::Line::ATN,  false);
    bus_.setDrivePulled(IECBus::Line::CLK,  false);
    bus_.setDrivePulled(IECBus::Line::DATA, false);
    bus_.setDrivePulled(IECBus::Line::SRQ,  false);
}

// ---------- Snapshot --------------------------------------------------------

void IECCard::serialize(SnapshotWriter& w) const {
    auto h = bus_.hostBits();
    auto d = bus_.driveBits();
    for (bool b : h) w.writeU8(b ? 1 : 0);
    for (bool b : d) w.writeU8(b ? 1 : 0);
    w.writeU8(lastPortB_);
    w.writeU8(lastDdrB_);
    w.writeU8(prevAtnLow_  ? 1 : 0);
    w.writeU8(prevClkLow_  ? 1 : 0);
    w.writeU8(prevDataLow_ ? 1 : 0);
    w.writeU8(static_cast<uint8_t>(role_));
    w.writeU8(lastSecondary_);
    w.writeU8(wasOpenSecondary_ ? 1 : 0);
    w.writeU8(static_cast<uint8_t>(rxPhase_));
    w.writeU8(rxByte_);
    w.writeU32(static_cast<uint32_t>(rxBitCount_));
    w.writeU8(rxEoi_ ? 1 : 0);
    w.writeU32(static_cast<uint32_t>(rxEoiTimerCycles_));
    w.writeU8(static_cast<uint8_t>(txPhase_));
    w.writeU8(txByte_);
    w.writeU32(static_cast<uint32_t>(txBitIndex_));
    w.writeU8(txEoi_ ? 1 : 0);
    w.writeU32(static_cast<uint32_t>(txDelayCycles_));
    drive_.serialize(w);
}

void IECCard::deserialize(SnapshotReader& r) {
    std::array<bool, 4> h{}, d{};
    for (auto& b : h) b = r.readU8() != 0;
    for (auto& b : d) b = r.readU8() != 0;
    bus_.restoreHostBits(h);
    bus_.restoreDriveBits(d);
    lastPortB_     = r.readU8();
    lastDdrB_      = r.readU8();
    prevAtnLow_    = r.readU8() != 0;
    prevClkLow_    = r.readU8() != 0;
    prevDataLow_   = r.readU8() != 0;
    role_          = static_cast<Role>(r.readU8());
    lastSecondary_ = r.readU8();
    wasOpenSecondary_ = r.readU8() != 0;
    rxPhase_       = static_cast<RxPhase>(r.readU8());
    rxByte_        = r.readU8();
    rxBitCount_    = static_cast<int>(r.readU32());
    rxEoi_         = r.readU8() != 0;
    rxEoiTimerCycles_ = static_cast<int>(r.readU32());
    txPhase_       = static_cast<TxPhase>(r.readU8());
    txByte_        = r.readU8();
    txBitIndex_    = static_cast<int>(r.readU32());
    txEoi_         = r.readU8() != 0;
    txDelayCycles_ = static_cast<int>(r.readU32());
    drive_.deserialize(r);
}

} // namespace pom1
