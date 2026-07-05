#include "IECCard.h"
#include "SnapshotIO.h"

#include <cstdio>

#ifndef POM1_IEC_DEBUG
#define POM1_IEC_DEBUG 0
#endif

// IEC trace plumbing. Compile-time off by default; flip POM1_IEC_DEBUG to 1
// (or pass -DPOM1_IEC_DEBUG=1) to capture FSM transitions + PORTB reads.
// Hard-cap on log lines so a runaway dump never floods CI.
#if POM1_IEC_DEBUG
namespace pom1 { namespace iecdbg {
    static unsigned long g_cycleCounter = 0;
    static unsigned long g_logLines = 0;
    static bool g_active = false;  // armed by caller (e.g. talker-mode entry)
    constexpr unsigned long kMaxLogLines = 30000;
    inline bool can_log() { return g_active && g_logLines < kMaxLogLines; }
    inline void bump() { ++g_logLines; }
} }
#define IEC_LOG(...) \
    do { if (::pom1::iecdbg::can_log()) { \
        std::fprintf(stderr, "[IEC@%lu] ", ::pom1::iecdbg::g_cycleCounter); \
        std::fprintf(stderr, __VA_ARGS__); \
        std::fputc('\n', stderr); std::fflush(stderr); \
        ::pom1::iecdbg::bump(); \
    } } while (0)
// Force-log: bypasses g_active so we can mark TX boundaries.
#define IEC_FORCE_LOG(...) \
    do { if (::pom1::iecdbg::g_logLines < ::pom1::iecdbg::kMaxLogLines) { \
        std::fprintf(stderr, "[IEC@%lu] ", ::pom1::iecdbg::g_cycleCounter); \
        std::fprintf(stderr, __VA_ARGS__); \
        std::fputc('\n', stderr); std::fflush(stderr); \
        ::pom1::iecdbg::bump(); \
    } } while (0)
#else
#define IEC_LOG(...) do {} while (0)
#define IEC_FORCE_LOG(...) do {} while (0)
#endif

namespace pom1 {

namespace {
constexpr uint8_t kDriveAddress = 8;

#if POM1_IEC_DEBUG
const char* txPhaseName(int p) {
    switch (p) {
        case 0: return "Idle";
        case 1: return "Initial";
        case 2: return "ReadyToSend";
        case 3: return "StartByte";
        case 4: return "SettleEoi";
        case 5: return "BitSetup";
        case 6: return "BitValid";
        case 7: return "BitEnd";
        case 8: return "ByteEnd";
        case 9: return "Hold";
        case 10: return "AllSent";
    }
    return "?";
}
#endif

// Per-bit phase delay (CPU cycles at 1.022 MHz). The kernel's ACP03/ACP03A
// loop runs ~25 cyc/iter; 50 cyc per drive-side phase is comfortably above
// that.
constexpr int kTxBitSettleCycles = 50;
// After byte ack, hold lines released briefly before fetching the next byte.
constexpr int kTxByteAckCycles   = 80;
// Long safety timeouts on event-driven phases. Real CBM drives wait
// indefinitely for the listener — we use a generous 100 ms cap (about
// 102k cycles at 1.022 MHz) so a misbehaving listener doesn't deadlock
// CI, but normal slow paths (BASIC LOAD doing memory work between
// bytes) get plenty of breathing room.
constexpr int kTxEdgeTimeoutCycles = 100000;   // ~100 ms
// EOI hold time on the talker side (CBM convention: >200 µs).
constexpr int kTxEoiHoldCycles   = 250;
// Tiny delay after a kernel-driven DATA edge before the drive proceeds —
// gives the kernel a few cycles to finish its instruction sequence.
constexpr int kTxPostEdgeCycles = 30;
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
#if 0  // disabled debug logging
    static int writeCount = 0;
    if (writeCount < 200) {
        std::fprintf(stderr, "[IEC] W#%d portB=%02X ddrB=%02X\n",
                     writeCount, portB, ddrB);
        std::fflush(stderr);
    }
    writeCount++;
#endif
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
    bool clkHigh = bus_.level(IECBus::Line::CLK);
    bool dataHigh = bus_.level(IECBus::Line::DATA);
    if (clkHigh)  out |= kClkInBit;
    if (dataHigh) out |= kDataInBit;

    // Read-back of OUT bits (whatever the host wrote).
    out |= (lastPortB_ & (kAtnOutBit | kClkOutBit | kDataOutBit));

#if POM1_IEC_DEBUG
    // Log every PORTB read while we're in an active TX role. This is the
    // strongest "what does the kernel see" signal: ACPTR samples DATA via
    // PORTB.bit6 on every iteration of its CLK-poll + bit-shift loop. By
    // correlating reads with the current TX phase, we can tell whether the
    // kernel sampled the right bit at the right time.
    ++dbgPortBReads_;
    if (role_ == Role::Talker && txPhase_ != TxPhase::Idle &&
        txPhase_ != TxPhase::AllSent) {
        IEC_LOG("R#%lu portB phase=%-11s bit=%d byte=%02X clk=%d data=%d → %02X",
                dbgPortBReads_, txPhaseName(static_cast<int>(txPhase_)),
                txBitIndex_, txByte_, clkHigh ? 1 : 0, dataHigh ? 1 : 0, out);
    }
#endif
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

#if 0  // disabled debug logging
static void iecLog(const char* tag) {
    std::fprintf(stderr, "[IEC] %s\n", tag);
    std::fflush(stderr);
}
#endif

void IECCard::onAtnAsserted() {
    IEC_LOG("ATN asserted (role=%d)", static_cast<int>(role_));
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
    IEC_LOG("ATN released → role=%d", static_cast<int>(role_));
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
    // Bus CLK went HIGH — talker released CLK. Under ATN every device
    // listens for command bytes regardless of prior role; after ATN
    // release the established role determines whether we keep listening.
    const bool listenerActive = prevAtnLow_ || role_ == Role::Listener;
    if (!listenerActive) return;
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
    const bool listenerActive = prevAtnLow_ || role_ == Role::Listener;
    if (!listenerActive) return;
    switch (rxPhase_) {
        case RxPhase::ReadyAckPulled:
        case RxPhase::EoiAckRelease:
            // Talker pulled CLK low → start of byte. (For non-EOI we go
            // ReadyAckPulled → ReceivingBits; for EOI we go through the
            // EoiAckPulse / EoiAckRelease detour and end up here.)
            rxPhase_ = RxPhase::ReceivingBits;
            rxBitCount_ = 0;
            rxByte_ = 0;
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
    // Bus DATA went HIGH — listener (kernel) released DATA. This is the
    // signal that the kernel reached DATAHI in ACPTR's EOIACP, meaning it's
    // ready for the bit transfer. Synchronise the TX FSM to this event.
    IEC_LOG("DATA rising  (role=%d phase=%s)", static_cast<int>(role_),
            txPhaseName(static_cast<int>(txPhase_)));
    if (role_ == Role::Talker && txPhase_ == TxPhase::ReadyToSend) {
        // After a brief settling time, drive pulls CLK low to start bit 0.
        txPhase_ = TxPhase::StartByte;
        txDelayCycles_ = txEoi_ ? kTxEoiHoldCycles : kTxPostEdgeCycles;
        IEC_LOG("  → StartByte (delay=%d)", txDelayCycles_);
    }
}

void IECCard::onDataFallingEdge() {
    // Bus DATA went LOW — listener (kernel) pulled DATA. After a byte's
    // 8 bits the kernel does JSR DATALO to ack the byte received; the
    // edge tells the drive it's safe to fetch and prepare the next byte.
    IEC_LOG("DATA falling (role=%d phase=%s)", static_cast<int>(role_),
            txPhaseName(static_cast<int>(txPhase_)));
    if (role_ == Role::Talker && txPhase_ == TxPhase::ByteEnd) {
        txPhase_ = TxPhase::Hold;
        txDelayCycles_ = kTxByteAckCycles;
        IEC_LOG("  → Hold (byte-ack edge, delay=%d)", txDelayCycles_);
    }
}

// ---------- Listener byte handling -----------------------------------------

void IECCard::deliverByteToDrive(uint8_t b, bool eoi) {
    const bool inFilenameWindow = wasOpenSecondary_;
    const bool isUnHandshake    = (b == 0x3F || b == 0x5F);
#if 0  // disabled debug logging
    std::fflush(stderr); std::fprintf(stderr, "[IEC] rx byte=%02X eoi=%d atn=%d filenameWin=%d role=%d\n",
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
#if POM1_IEC_DEBUG
    iecdbg::g_active = true;  // arm tracing for the upcoming TX session
#endif
    if (!drive_.talkByte(txByte_, txEoi_)) {
        IEC_FORCE_LOG("startTransmitting: NO byte (drive empty) → AllSent");
        releaseAllDriveLines();
        txPhase_ = TxPhase::AllSent;
        txDelayCycles_ = 0;
#if POM1_IEC_DEBUG
        iecdbg::g_active = false;
#endif
        return;
    }
    IEC_FORCE_LOG("startTransmitting: byte#1=%02X eoi=%d", txByte_, txEoi_ ? 1 : 0);
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
#if POM1_IEC_DEBUG
    iecdbg::g_cycleCounter += static_cast<unsigned long>(cpuCycles);
#endif

    // EOI detection: if we're waiting in ReadyAckPulled (CLK released by host)
    // for >200 µs (~205 cycles), the firmware is signalling EOI on the next
    // byte (CBM convention: talker holds CLK > 200µs).
    // EOI listener handshake: if the talker holds CLK released for >200 µs
    // without pulling it low, that's an EOI signal. The listener acks by
    // pulling DATA briefly (>60 µs) then releasing — see firmware ISR02/
    // ISR03 in kernal_serial.lm.
    // EOI is a DATA-phase concept only. Gate on role_==Listener (true only after
    // ATN is released), NOT prevAtnLow_: arming during ATN command reception could
    // spuriously fire an EOI ack pulse mid-command if the controller stalled with
    // CLK released across a slice boundary.
    if (role_ == Role::Listener && rxPhase_ == RxPhase::ReadyAckPulled) {
        rxEoiTimerCycles_ += cpuCycles;
        if (rxEoiTimerCycles_ > 205) {
            rxEoi_ = true;
            bus_.setDrivePulled(IECBus::Line::DATA, true);  // EOI ack pulse
            rxPhase_ = RxPhase::EoiAckPulse;
            rxEoiTimerCycles_ = 0;
        }
    } else if (role_ == Role::Listener && rxPhase_ == RxPhase::EoiAckPulse) {
        rxEoiTimerCycles_ += cpuCycles;
        if (rxEoiTimerCycles_ > 80) {
            bus_.setDrivePulled(IECBus::Line::DATA, false); // release after pulse
            rxPhase_ = RxPhase::EoiAckRelease;
            rxEoiTimerCycles_ = 0;
        }
    }

    if (role_ != Role::Talker) return;
    txDelayCycles_ -= cpuCycles;
    if (txDelayCycles_ > 0) return;

#if POM1_IEC_DEBUG
    TxPhase dbgPrevPhase = txPhase_;
    int dbgPrevBit = txBitIndex_;
    uint8_t dbgPrevByte = txByte_;
#endif

    switch (txPhase_) {
        case TxPhase::Idle: break;

        case TxPhase::Initial:
            // CLK has been low long enough for the listener to notice
            // "talker ready". Release CLK now = "byte coming, EOI window
            // starts". The next phase (ReadyToSend) is event-driven: it
            // waits for the kernel's DATAHI write (= bus DATA rising
            // edge) before pulling CLK low to begin the bit transfer.
            bus_.setDrivePulled(IECBus::Line::CLK,  false);
            bus_.setDrivePulled(IECBus::Line::DATA, false);
            if (bus_.level(IECBus::Line::DATA)) {
                // Kernel already released DATA before we got here (rare,
                // but possible if the host CPU was running fast). Skip
                // the edge wait and proceed to bit transfer.
                txPhase_ = TxPhase::StartByte;
                txDelayCycles_ = txEoi_ ? kTxEoiHoldCycles : kTxPostEdgeCycles;
            } else {
                txPhase_ = TxPhase::ReadyToSend;
                txDelayCycles_ = kTxEdgeTimeoutCycles;  // edge will break early
            }
            break;

        case TxPhase::ReadyToSend:
            // Edge-driven phase — onDataRisingEdge advances us to StartByte
            // when the kernel does DATAHI in EOIACP. The cycle-based
            // timeout here is a safety net for misbehaving firmware.
            txPhase_ = TxPhase::StartByte;
            txDelayCycles_ = txEoi_ ? kTxEoiHoldCycles : kTxPostEdgeCycles;
            break;

        case TxPhase::StartByte:
        case TxPhase::SettleEoi:
            // Pull CLK low = "starting bit transfer". Begin bit 0.
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
            // Pull CLK low (= falling edge) — this is the kernel's
            // ACP03A "wait for CLK LOW" signal, marking the end of the
            // current bit. Required for both inter-bit transitions AND
            // the last bit of the byte (otherwise ACP03A loops forever
            // and the byte never completes).
            bus_.setDrivePulled(IECBus::Line::CLK, true);
            txBitIndex_++;
            if (txBitIndex_ >= 8) {
                // End of byte: keep CLK pulled (so kernel sees "end of
                // bit 7"), but release DATA so kernel can pull DATA in
                // its post-byte JSR DATALO ack.
                bus_.setDrivePulled(IECBus::Line::DATA, false);
                txPhase_ = TxPhase::ByteEnd;
                txDelayCycles_ = kTxEdgeTimeoutCycles;
            } else {
                txPhase_ = TxPhase::BitSetup;
                txDelayCycles_ = kTxBitSettleCycles;
            }
            break;

        case TxPhase::ByteEnd:
            // Reached here from a fired timer in ByteEnd = kernel never sent
            // the DATALO ack edge (or we entered ByteEnd after the edge had
            // already fired in a different phase). Force the transition to
            // Hold so the FSM doesn't deadlock — and log so we know it
            // happened.
            IEC_LOG("ByteEnd TIMEOUT (no DATALO ack edge) → Hold "
                    "[clk=%d data=%d byte=%02X]",
                    bus_.level(IECBus::Line::CLK) ? 1 : 0,
                    bus_.level(IECBus::Line::DATA) ? 1 : 0, txByte_);
            txPhase_ = TxPhase::Hold;
            txDelayCycles_ = kTxByteAckCycles;
            break;

        case TxPhase::Hold:
            // Post-ack hold. Reached either by edge (kernel's DATALO byte
            // ack pulled DATA → onDataFallingEdge) or by safety timeout.
            // CLK is currently pulled (from BitEnd of bit 7) — same state
            // as just after startTransmitting for the first byte. Hold just
            // transitions back into the standard byte-send dance, where
            // Initial will release CLK (kernel's ACPTR ACP00A exits),
            // ReadyToSend waits for kernel DATAHI, and StartByte pulls
            // CLK back to begin bit transfer.
            txByte_ = 0;
            txEoi_ = false;
            if (drive_.talkByte(txByte_, txEoi_)) {
                IEC_FORCE_LOG("Hold → Initial: next byte=%02X eoi=%d",
                              txByte_, txEoi_ ? 1 : 0);
                txPhase_ = TxPhase::Initial;
                txDelayCycles_ = kTxBitSettleCycles;
            } else {
                IEC_FORCE_LOG("Hold → AllSent: drive empty");
                releaseAllDriveLines();
                txPhase_ = TxPhase::AllSent;
                txDelayCycles_ = 0;
#if POM1_IEC_DEBUG
                iecdbg::g_active = false;
#endif
            }
            break;

        case TxPhase::AllSent:
            txDelayCycles_ = 0;
            break;
    }
#if POM1_IEC_DEBUG
    if (dbgPrevPhase != txPhase_ || dbgPrevBit != txBitIndex_ ||
        dbgPrevByte != txByte_) {
        IEC_LOG("FSM %s(b%d,%02X) → %s(b%d,%02X) delay=%d clk=%d data=%d",
                txPhaseName(static_cast<int>(dbgPrevPhase)), dbgPrevBit, dbgPrevByte,
                txPhaseName(static_cast<int>(txPhase_)), txBitIndex_, txByte_,
                txDelayCycles_,
                bus_.level(IECBus::Line::CLK) ? 1 : 0,
                bus_.level(IECBus::Line::DATA) ? 1 : 0);
    }
#endif
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
    // Size these from the bus API (kLineCount) rather than a hardcoded 4, so they
    // stay in lockstep with serialize()'s bus_.hostBits()/driveBits() (and the
    // restoreHostBits/restoreDriveBits parameter types) if the line count changes.
    auto h = bus_.hostBits();
    auto d = bus_.driveBits();
    for (auto& b : h) b = r.readU8() != 0;
    for (auto& b : d) b = r.readU8() != 0;
    bus_.restoreHostBits(h);
    bus_.restoreDriveBits(d);
    lastPortB_     = r.readU8();
    lastDdrB_      = r.readU8();
    prevAtnLow_    = r.readU8() != 0;
    prevClkLow_    = r.readU8() != 0;
    prevDataLow_   = r.readU8() != 0;
    // Clamp FSM enums restored from (possibly corrupt) snapshot bytes back to
    // a known-good Idle, mirroring the validation in Drive1541/CFFA1::deserialize.
    // Role: Idle..Talker (0..2); RxPhase: Idle..ByteAck (0..6);
    // TxPhase: Idle..AllSent (0..10).
    { uint8_t v = r.readU8(); role_ = (v <= 2) ? static_cast<Role>(v) : Role::Idle; }
    lastSecondary_ = r.readU8();
    wasOpenSecondary_ = r.readU8() != 0;
    { uint8_t v = r.readU8(); rxPhase_ = (v <= 6) ? static_cast<RxPhase>(v) : RxPhase::Idle; }
    rxByte_        = r.readU8();
    rxBitCount_    = static_cast<int>(r.readU32());
    rxEoi_         = r.readU8() != 0;
    rxEoiTimerCycles_ = static_cast<int>(r.readU32());
    { uint8_t v = r.readU8(); txPhase_ = (v <= 10) ? static_cast<TxPhase>(v) : TxPhase::Idle; }
    txByte_        = r.readU8();
    txBitIndex_    = static_cast<int>(r.readU32());
    txEoi_         = r.readU8() != 0;
    txDelayCycles_ = static_cast<int>(r.readU32());
    drive_.deserialize(r);
}

} // namespace pom1
