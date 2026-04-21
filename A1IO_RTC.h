// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// P-LAB Apple-1 I/O Board & Real Time Clock emulation
// 65C22 VIA + ATMEGA32 MCU + DS3231 RTC
// https://p-l4b.github.io/A1-IO_RTC/

#ifndef A1IO_RTC_H
#define A1IO_RTC_H

#include <cstdint>
#include <ctime>
#include <array>

class A1IO_RTC
{
public:
    static constexpr uint16_t kViaBase = 0x2000;
    static constexpr uint16_t kViaEnd  = 0x200F;

    A1IO_RTC();
    void reset();

    // VIA 65C22 register interface (called from Memory::memRead / memWrite)
    uint8_t readRegister(uint16_t address);
    void    writeRegister(uint16_t address, uint8_t value);

    // Broadcast cycle tick (called from Memory::advanceCycles)
    void advanceCycles(int cycles);

    // UI snapshot (thread-safe copy for status window)
    struct Snapshot {
        // RTC values
        int hour = 0;
        int minute = 0;
        int second = 0;
        int day = 1;
        int month = 1;
        int year = 26;
        int tempRTC = 22;       // DS3231 built-in temperature
        int tempDS18B20 = 0;    // DS18B20 probe integer part
        int tempDS18B20dec = 0; // DS18B20 probe decimal part
        // Analog inputs (8 channels, 0-255)
        std::array<uint8_t, 8> analogInputs = {};
        // Digital inputs (4 channels, 0 or 1)
        std::array<uint8_t, 4> digitalInputs = {1, 1, 1, 1}; // pull-ups default
        // Digital outputs (16 bits from shift register)
        uint16_t digitalOutputs = 0;
        // Broadcast state
        int currentRegister = 0;
        bool strobeActive = false;
    };
    void copySnapshot(Snapshot& out) const;

    // Configurable analog/digital inputs (for UI sliders / external control)
    void setAnalogInput(int channel, uint8_t value);
    void setDigitalInput(int channel, uint8_t value);

    // Freeze the RTC to a specific wall-clock instant (seconds since epoch).
    // Used by the CLI `--rtc-freeze` verb so scripted runs get a deterministic
    // clock. Internally sets rtcOffsetSeconds = target - host_now; the RTC
    // then continues ticking from that anchor at host-clock rate (it does not
    // actually stop — "freeze" is a misnomer inherited from the CLI verb, but
    // for short scripted runs the drift is under 1 s).
    void setOverrideTime(std::time_t target);

private:
    // --- VIA 65C22 registers ---
    uint8_t portB;          // $2000 - Port B (data bus to/from ATMEGA)
    uint8_t portA;          // $2001 - Port A (address/control bus)
    uint8_t ddrB;           // $2002 - Data Direction Register B
    uint8_t ddrA;           // $2003 - Data Direction Register A
    uint8_t t1LatchLo;     // $2006 - Timer 1 Latch Low
    uint8_t t1LatchHi;     // $2007 - Timer 1 Latch High
    uint8_t t2CounterLo;   // $2008 - Timer 2 Counter Low
    uint8_t t2CounterHi;   // $2009 - Timer 2 Counter High
    uint8_t shiftReg;       // $200A - Shift Register
    uint8_t acr;            // $200B - Auxiliary Control Register
    uint8_t pcr;            // $200C - Peripheral Control Register
    uint8_t ifr;            // $200D - Interrupt Flag Register
    uint8_t ier;            // $200E - Interrupt Enable Register

    // Timer 1 running state
    uint16_t t1Counter;
    bool     t1Running;

    // --- ATMEGA broadcast state ---
    // The ATMEGA continuously cycles through registers 0-23, broadcasting
    // each one with a STROBE pulse on Port A bit 7.
    static constexpr int kNumRegisters = 24;
    static constexpr int kStrobeCycles = 10;   // STROBE HIGH duration (cycles)
    static constexpr int kGapCycles = 90;      // gap between STROBEs (cycles)
    static constexpr int kRegisterPeriod = kStrobeCycles + kGapCycles; // 100 cycles per register

    int broadcastRegister;      // current register being broadcast (0-23)
    int broadcastCycleCounter;  // cycle counter within current register period
    bool strobeActive;          // true during STROBE HIGH phase
    bool strobeConsumed;        // true after first IRA read consumes STROBE

    // --- Virtual register file (ATMEGA) ---
    // Registers 0-5: RTC (hour, minute, second, day, month, year)
    // Register 6: DS3231 temperature
    // Register 7: DS18B20 integer part
    // Register 8: DS18B20 decimal part
    // Register 9: unused
    // Registers 10-17: analog inputs (8 channels)
    // Registers 18-19: unused
    // Registers 20-23: digital inputs (4 channels)
    std::array<uint8_t, 32> virtualRegisters;

    // RTC time offset: delta from host clock (allows setting RTC)
    int rtcOffsetSeconds;

    // Analog inputs (configurable)
    std::array<uint8_t, 8> analogInputs;

    // Digital inputs (configurable, default pull-up = 1)
    std::array<uint8_t, 4> digitalInputs;

    // --- Shift register output (16 digital outputs) ---
    uint16_t digitalOutputs;
    uint8_t shiftOutHigh;   // last high byte written to SR
    bool shiftHighWritten;  // true after first SR write (waiting for low byte)

    // --- RTC write protocol ---
    // When CPU sets Port A bit 6 (RW), ATMEGA enters write mode:
    // reads register index from Port B, value from Port A lower bits
    bool rwModeActive;

    // --- Internal helpers ---
    void updateVirtualRegisters();
    void advanceBroadcast(int cycles);
    uint8_t getBroadcastPortA() const;
    uint8_t getBroadcastPortB() const;
};

#endif // A1IO_RTC_H
