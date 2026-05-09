#pragma once

// IECCard — P-LAB IEC daughterboard glue (microSD daughterboard).
//
// Hardware: SN7406 hex inverter + Schottky diodes between unused VIA 65C22
// pins on the P-LAB microSD Storage Card and the Commodore IEC 6-pin DIN.
// VIA pin map (PORT B at $A000, DDRB at $A002), per nippur72/apple1-sdcard
// kernal.lm:
//   bit 2 ($04) = ATN_OUT      (drive line LOW)
//   bit 3 ($08) = CLK_OUT      (drive line LOW)
//   bit 4 ($10) = DATA_OUT     (drive line LOW)
//   bit 5 ($20) = CLK_IN       (HIGH = released)
//   bit 6 ($40) = DATA_IN      (HIGH = released)
//   bits 0+7    = microSD CPU/MCU strobes — preserved untouched.
//
// SRQ is unwired on the P-LAB card.
//
// On output: VIA bit set + DDR=1 → SN7406 inverts → IEC line pulled LOW.
// On input : VIA reads bus level directly (high impedance through SN7406
// gate input), so set bit = line HIGH (released), clear bit = line LOW.

#include "IECBus.h"
#include "Drive1541.h"
#include "Peripheral.h"

#include <cstdint>
#include <string>

namespace pom1 {

class IECCard : public Peripheral {
public:
    std::string_view name() const override { return "IECCard"; }

    // VIA PORTB bit positions for the IEC card.
    static constexpr uint8_t kAtnOutBit  = 0x04;
    static constexpr uint8_t kClkOutBit  = 0x08;
    static constexpr uint8_t kDataOutBit = 0x10;
    static constexpr uint8_t kClkInBit   = 0x20;
    static constexpr uint8_t kDataInBit  = 0x40;
    // PORTB bits the IEC card ever touches (for safe merge with microSD strobes).
    static constexpr uint8_t kAllBits =
        kAtnOutBit | kClkOutBit | kDataOutBit | kClkInBit | kDataInBit;

    IECCard();

    // VIA hooks installed by MicroSD::attachIECCard.
    void onViaPortBWrite(uint8_t portB, uint8_t ddrB);
    void onViaDdrBWrite (uint8_t ddrB);
    uint8_t mergeViaPortBRead(uint8_t microsdValue) const;

    void advanceCycles(int cpuCycles);

    void busReset();
    void unmount();
    bool mountDisk(const std::string& d64Path);
    bool hasDisk() const { return drive_.hasDisk(); }
    const std::string& diskPath() const { return drive_.diskPath(); }

    Drive1541& drive() { return drive_; }
    const Drive1541& drive() const { return drive_; }
    const IECBus& bus() const { return bus_; }

    void serialize(SnapshotWriter& w) const override;
    void deserialize(SnapshotReader& r) override;

private:
    enum class Role { Idle, Listener, Talker };

    IECBus bus_;
    Drive1541 drive_;

    // Last observed VIA host state (so we can compute edges).
    uint8_t lastPortB_ = 0;
    uint8_t lastDdrB_  = 0;
    bool prevAtnLow_   = false;
    bool prevClkLow_   = false;
    bool prevDataLow_  = false;

    // Role / addressing.
    Role role_ = Role::Idle;
    uint8_t lastSecondary_ = 0xFF;
    bool wasOpenSecondary_ = false;

    // Listener byte-frame FSM.
    enum class RxPhase {
        Idle,                // waiting for talker activity
        WaitClkReleased,     // talker should release CLK to signal "have byte"
        ReadyAckPulled,      // we released DATA, awaiting CLK pull (start of byte)
        ReceivingBits,       // 8 CLK rising edges → 8 bits LSB first
        ByteAck,             // we pulled DATA to ack received byte
    };
    RxPhase rxPhase_ = RxPhase::Idle;
    uint8_t rxByte_ = 0;
    int rxBitCount_ = 0;
    bool rxEoi_ = false;
    int rxEoiTimerCycles_ = 0;

    // Talker byte-frame FSM (cycle-driven; uses a phase counter).
    enum class TxPhase {
        Idle,
        Initial,             // just became talker — pull CLK, release DATA
        ReadyToSend,         // CLK released, wait for listener to pull DATA
        StartByte,           // listener pulled DATA, pull CLK to begin
        SettleEoi,           // optional EOI hold
        BitSetup,            // emit bit on DATA, prepare CLK release
        BitValid,            // CLK released, listener samples
        BitEnd,              // pull CLK low + release DATA
        ByteEnd,             // wait for listener to pull DATA (ack)
        Hold,                // post-ack hold before next byte
        AllSent,             // no more bytes; release everything
    };
    TxPhase txPhase_ = TxPhase::Idle;
    uint8_t txByte_ = 0;
    int txBitIndex_ = 0;
    bool txEoi_ = false;
    int txDelayCycles_ = 0;

    // Edge-driven FSM helpers.
    void evaluateEdges(uint8_t portB, uint8_t ddrB);
    void onAtnAsserted();
    void onAtnReleased();
    void onClkRisingEdge();
    void onClkFallingEdge();
    void onDataRisingEdge();
    void onDataFallingEdge();

    void deliverByteToDrive(uint8_t b, bool eoi);
    void interpretCommandByte(uint8_t b);

    void startTransmitting();
    void enterIdle();
    void releaseAllDriveLines();
};

} // namespace pom1
