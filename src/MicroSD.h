// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// P-LAB Apple-1 microSD Storage Card emulation
// 65C22 VIA + ATMEGA MCU protocol handler
// https://p-l4b.github.io/sdcard/

#ifndef MICROSD_H
#define MICROSD_H

#include "CpuClock.h"
#include "Peripheral.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <array>

namespace pom1 { class IECCard; }

class MicroSD : public pom1::Peripheral
{
public:
    std::string_view name() const override { return "microSD"; }

    static constexpr uint16_t kViaBase = 0xA000;
    static constexpr uint16_t kViaEnd  = 0xA00F;
    static constexpr uint16_t kRomBase = 0x8000;
    static constexpr uint16_t kRomEnd  = 0x9FFF;
    static constexpr size_t   kRomSize = 0x2000; // 8 KB EEPROM

    MicroSD();
    void reset();

    // VIA 65C22 register interface (called from Memory::memRead / memWrite)
    uint8_t readRegister(uint16_t address);
    void    writeRegister(uint16_t address, uint8_t value);

    // Timer 1 tick (called from Memory::advanceCycles)
    void advanceCycles(int cycles);

    // Set the host directory used as virtual SD card root
    void setSDCardPath(const std::string& path);
    const std::string& getSDCardPath() const { return sdCardRootPath; }

    // Debug logging (disabled by default for performance)
    bool debugEnabled = false;

    // /IRQ line state. 65C22 standard semantics: any IRQ source unmasked
    // in IER and active in IFR pulls /IRQ. Wired to the Memory-side
    // aggregator (cf. dev/Programming_TMS9918.md §18 Bug N°2). Most microSD shell
    // workflows poll PORTB STROBE bits rather than enable timer/SR
    // interrupts, so this is dormant in typical use — but a future SD
    // shell or assembler that uses Timer 1 for byte-time delays would
    // get a real /IRQ now.
    bool irqAsserted() const { return (ifr & ier & 0x7F) != 0; }

    // P-LAB IEC daughterboard hook. The IEC card is a passive sniffer on
    // unused VIA PORTB pins (bits 2-6). When attached, every PORTB / DDRB
    // write notifies it, and PORTB reads merge in the IEC bus IN bits.
    // Pointer is non-owning; ownership lives in Memory.
    void attachIECCard(pom1::IECCard* iec) { iecCard = iec; }
    pom1::IECCard* getIECCard() const { return iecCard; }

    // Snapshot round-trip: VIA 65C22 register file + handshake state +
    // MCU protocol FSM (phase + accumulators + response/write buffers) +
    // currentDirectory cursor. The on-disk virtual FAT32 (sdCardRootPath)
    // is set by Memory at construction; the snapshot only restores cursor
    // state, not the underlying filesystem.
    void serialize(pom1::SnapshotWriter& writer) const override;
    void deserialize(pom1::SnapshotReader& reader) override;

private:
    // --- MCU command IDs (match arduino.ino) ---
    static constexpr uint8_t CMD_READ   = 0;
    static constexpr uint8_t CMD_WRITE  = 1;
    static constexpr uint8_t CMD_DIR    = 2;
    static constexpr uint8_t CMD_LOAD   = 4;
    static constexpr uint8_t CMD_DEL    = 11;
    static constexpr uint8_t CMD_LS     = 12;
    static constexpr uint8_t CMD_CD     = 13;
    static constexpr uint8_t CMD_MKDIR  = 14;
    static constexpr uint8_t CMD_RMDIR  = 15;
    static constexpr uint8_t CMD_PWD    = 19;
    static constexpr uint8_t CMD_TEST   = 20;
    static constexpr uint8_t CMD_MOUNT  = 23;

    static constexpr uint8_t OK_RESPONSE  = 0x00;
    static constexpr uint8_t ERR_RESPONSE = 0xFF;

    static constexpr size_t  MAX_STRING_LEN  = 256;  // max filename/path length
    static constexpr size_t  MAX_WRITE_SIZE  = 0x8000; // max file write (32 KB)

    // --- VIA 65C22 registers ---
    uint8_t portB;          // $A000 - Port B (bit 0: CPU_STROBE out, bit 7: MCU_STROBE in)
    uint8_t portA;          // $A001 - Port A (8-bit data bus)
    uint8_t ddrB;           // $A002 - Data Direction Register B
    uint8_t ddrA;           // $A003 - Data Direction Register A
    uint8_t t1LatchLo;     // $A006 - Timer 1 Latch Low
    uint8_t t1LatchHi;     // $A007 - Timer 1 Latch High
    uint8_t t2CounterLo;   // $A008 - Timer 2 Counter Low
    uint8_t t2CounterHi;   // $A009 - Timer 2 Counter High
    uint8_t shiftReg;       // $A00A - Shift Register
    uint8_t acr;            // $A00B - Auxiliary Control Register
    uint8_t pcr;            // $A00C - Peripheral Control Register
    uint8_t ifr;            // $A00D - Interrupt Flag Register
    uint8_t ier;            // $A00E - Interrupt Enable Register

    // Timer 1 running state
    uint16_t t1Counter;
    bool     t1Running;

    // Timer 2 running state. Used by SD OS 1.3's IEC byte-frame 1 ms
    // timeout (writes T2H to start, polls IFR bit 5 for underflow).
    bool     t2Running = false;

    // P-LAB IEC daughterboard (non-owning).
    pom1::IECCard* iecCard = nullptr;

    // --- Handshake state ---
    bool cpuStrobeHigh;     // Current CPU_STROBE (PORTB bit 0)
    bool mcuStrobeHigh;     // Current MCU_STROBE (PORTB bit 7, set by emulated MCU)

    // --- MCU protocol state machine ---
    enum class McuPhase {
        IDLE,               // Waiting for command byte from CPU
        RECEIVING_STRING,   // Collecting null-terminated filename/path
        SENDING_RESPONSE,   // Sending multi-byte response to CPU
        DIR_WAIT_REQUEST,   // DIR: waiting for CPU to request next entry
        WRITE_RECV_LEN,     // WRITE: receiving 2-byte file length
        WRITE_RECV_DATA,    // WRITE: receiving file data bytes
        TEST_ECHO,          // TEST: waiting for one byte to echo back
    };

    McuPhase mcuPhase;
    McuPhase nextPhaseAfterResponse; // where to go after SENDING_RESPONSE completes
    uint8_t  currentCommand;

    // String accumulator (filename/path)
    std::string stringBuffer;

    // Response buffer (bytes to send to CPU)
    std::vector<uint8_t> responseBuffer;
    size_t responseIndex;

    // DIR/LS listing state
    struct DirEntry {
        std::string name;
        uintmax_t size;
        bool isDirectory;
    };
    std::vector<DirEntry> dirEntries;
    size_t dirEntryIndex;
    bool dirIsLs; // true for LS (short format), false for DIR (long format)

    // WRITE state
    std::string writeFilename;
    uint16_t writeExpectedLen;
    int writeLenBytesReceived;
    std::vector<uint8_t> writeDataBuffer;

    // --- DIR / TEST timeout ---
    int dirIdleCycles;              // cycles since last CPU interaction in DIR_WAIT_REQUEST
    int testIdleCycles;             // cycles idle since last TEST_ECHO byte
    static constexpr int DIR_TIMEOUT_CYCLES  = POM1_CPU_CLOCK_HZ / 2;
    static constexpr int TEST_TIMEOUT_CYCLES = POM1_CPU_CLOCK_HZ / 2;

    // --- Host filesystem ---
    std::string sdCardRootPath;
    std::string currentDirectory; // relative to sdCardRootPath (e.g. "" or "BASIC" or "BASIC/SUB")

    // --- MCU protocol handlers ---
    void handleByteFromCPU(uint8_t byte);
    void processCommand();
    void prepareNextResponseByte();

    // Command implementations
    void cmdRead(const std::string& filename, bool fuzzyMatch);
    void cmdWrite(const std::string& filename);
    void cmdDir(const std::string& path, bool isLs);
    void cmdDel(const std::string& filename);
    void cmdCd(const std::string& path);
    void cmdMkdir(const std::string& path);
    void cmdRmdir(const std::string& path);
    void cmdPwd();
    void cmdMount();
    void cmdWriteFinish();

    // Response helpers
    void beginResponse(const std::vector<uint8_t>& data, McuPhase nextPhase);
    void sendOK(McuPhase nextPhase = McuPhase::IDLE);
    void sendError(const std::string& message);

    // Filesystem helpers
    std::string resolveHostPath(const std::string& name) const;
    std::string fuzzyMatchFilename(const std::string& name) const;
    std::string getCurrentDirDisplay() const;
    void prepareDirEntry(size_t index);

    // Extract tag info from filename: "GAME#F10300" -> type=0xF1, addr=0x0300
    static bool parseTag(const std::string& filename, uint8_t& type, uint16_t& addr);
    static std::string getDisplayName(const std::string& filename);
    static std::string getFileExtension(uint8_t type);
};

#endif // MICROSD_H
