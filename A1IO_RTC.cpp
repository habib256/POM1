// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// P-LAB Apple-1 I/O Board & Real Time Clock emulation
// 65C22 VIA + ATMEGA32 MCU + DS3231 RTC
// https://p-l4b.github.io/A1-IO_RTC/
//
// Architecture:
//   CPU <-> VIA 65C22 ($2000-$200F) <-> ATMEGA32 <-> DS3231 RTC / ADC / GPIO
//
// The ATMEGA continuously broadcasts virtual registers (0-23) through the VIA.
// Each register is presented with a STROBE pulse on Port A bit 7.
// The CPU polls Port A for STROBE, reads the register index, and captures
// the data value from Port B.
//
// Port A layout during broadcast (ATMEGA -> CPU):
//   Bit 7: STROBE (data valid indicator)
//   Bits 0-4: Register index (0-23)
//   Bit 6: RW direction (CPU output, normally 0 during read)
//
// Port B: 8-bit data value for the current register
//
// Reading IRA clears the STROBE bit for that cycle, so:
//   BIT IRA  -> sees register | 0x80 (STROBE HIGH)
//   LDA IRA  -> sees register (STROBE consumed, bit 7 clear)
//   CMP $0280 -> matches the raw register number
//
// Writing (setting RTC):
//   CPU sets DDRB=$FF, DDRA=$FF
//   CPU writes register index to ORB ($2000)
//   CPU writes value | 0x40 to ORA ($2001) -- bit 6 = RW flag
//   ATMEGA detects RW, reads register from Port B and value from Port A
//   CPU clears RW by writing 0 to ORA
//
// Shift Register (16 digital outputs):
//   Two cascaded 74HC164 shift registers driven by VIA SR ($200A)
//   ACR mode $18 = shift out under phi2 control
//   Write high byte to SR, wait, write low byte to SR

#include "A1IO_RTC.h"
#include "SnapshotIO.h"
#include <chrono>
#include <ctime>

A1IO_RTC::A1IO_RTC()
{
    analogInputs.fill(0);
    digitalInputs.fill(1); // pull-ups: default HIGH
    reset();
}

void A1IO_RTC::reset()
{
    portB = 0;
    portA = 0;
    ddrB = 0;
    ddrA = 0;
    t1LatchLo = 0;
    t1LatchHi = 0;
    t2CounterLo = 0;
    t2CounterHi = 0;
    shiftReg = 0;
    acr = 0;
    pcr = 0;
    ifr = 0;
    ier = 0;
    t1Counter = 0;
    t1Running = false;

    broadcastRegister = 0;
    broadcastCycleCounter = 0;
    strobeActive = false;
    strobeConsumed = false;

    virtualRegisters.fill(0);
    rtcOffsetSeconds = 0;

    digitalOutputs = 0;
    shiftOutHigh = 0;
    shiftHighWritten = false;

    rwModeActive = false;

    updateVirtualRegisters();
}

// --- Host clock -> virtual RTC registers ---
void A1IO_RTC::updateVirtualRegisters()
{
    // Get current time adjusted by RTC offset
    auto now = std::chrono::system_clock::now();
    auto adjusted = now + std::chrono::seconds(rtcOffsetSeconds);
    std::time_t tt = std::chrono::system_clock::to_time_t(adjusted);
    std::tm* lt = std::localtime(&tt);

    if (lt) {
        virtualRegisters[0] = static_cast<uint8_t>(lt->tm_hour);
        virtualRegisters[1] = static_cast<uint8_t>(lt->tm_min);
        virtualRegisters[2] = static_cast<uint8_t>(lt->tm_sec);
        virtualRegisters[3] = static_cast<uint8_t>(lt->tm_mday);
        virtualRegisters[4] = static_cast<uint8_t>(lt->tm_mon + 1);  // tm_mon is 0-based
        virtualRegisters[5] = static_cast<uint8_t>((lt->tm_year + 1900 - 2000) & 0xFF); // year since 2000
    }

    // DS3231 built-in temperature sensor (~22°C room temp)
    virtualRegisters[6] = 22;

    // DS18B20 probe (disabled by default: returns 0)
    virtualRegisters[7] = 0;
    virtualRegisters[8] = 0;

    // Register 9: unused
    virtualRegisters[9] = 0;

    // Analog inputs (8 channels, registers 10-17)
    for (int i = 0; i < 8; ++i) {
        virtualRegisters[10 + i] = analogInputs[i];
    }

    // Registers 18-19: unused
    virtualRegisters[18] = 0;
    virtualRegisters[19] = 0;

    // Digital inputs (4 channels, registers 20-23)
    for (int i = 0; i < 4; ++i) {
        virtualRegisters[20 + i] = digitalInputs[i];
    }
}

// --- Broadcast cycle management ---
void A1IO_RTC::advanceBroadcast(int cycles)
{
    broadcastCycleCounter += cycles;

    while (broadcastCycleCounter >= kRegisterPeriod) {
        broadcastCycleCounter -= kRegisterPeriod;
        broadcastRegister = (broadcastRegister + 1) % kNumRegisters;
        strobeConsumed = false;
    }

    // STROBE is active during the first kStrobeCycles of each register period
    strobeActive = (broadcastCycleCounter < kStrobeCycles);
}

uint8_t A1IO_RTC::getBroadcastPortA() const
{
    // Port A from ATMEGA during broadcast:
    // Bits 0-4: register index
    // Bit 7: STROBE (if active and not yet consumed by a read)
    uint8_t val = static_cast<uint8_t>(broadcastRegister & 0x1F);
    if (strobeActive && !strobeConsumed) {
        val |= 0x80; // STROBE on bit 7
    }
    return val;
}

uint8_t A1IO_RTC::getBroadcastPortB() const
{
    // Port B from ATMEGA: data value for current register
    if (broadcastRegister < 24) {
        return virtualRegisters[broadcastRegister];
    }
    return 0;
}

// --- VIA register read ---
uint8_t A1IO_RTC::readRegister(uint16_t address)
{
    uint8_t reg = address & 0x0F;

    switch (reg) {
    case 0x00: // ORB/IRB - Port B
        // If DDRB bits are 0 (input), return ATMEGA broadcast data
        // If DDRB bits are 1 (output), return what CPU wrote
        {
            uint8_t atmegaData = getBroadcastPortB();
            // Mix: input bits from ATMEGA, output bits from CPU's portB
            return (portB & ddrB) | (atmegaData & ~ddrB);
        }

    case 0x01: // ORA/IRA - Port A (with handshake)
        {
            uint8_t atmegaPA = getBroadcastPortA();
            uint8_t result = (portA & ddrA) | (atmegaPA & ~ddrA);
            // Reading IRA consumes the STROBE (so next read sees bit 7 clear)
            strobeConsumed = true;
            return result;
        }

    case 0x02: return ddrB;
    case 0x03: return ddrA;

    case 0x04: // T1C-L (Timer 1 Counter Low) - reading clears T1 interrupt flag
        ifr &= ~0x40;
        return static_cast<uint8_t>(t1Counter & 0xFF);
    case 0x05: // T1C-H (Timer 1 Counter High)
        return static_cast<uint8_t>((t1Counter >> 8) & 0xFF);
    case 0x06: return t1LatchLo;
    case 0x07: return t1LatchHi;

    case 0x08: // T2C-L
        ifr &= ~0x20; // clear T2 interrupt flag
        return t2CounterLo;
    case 0x09: return t2CounterHi;

    case 0x0A: return shiftReg;
    case 0x0B: return acr;
    case 0x0C: return pcr;
    case 0x0D:
        // 65C22 IFR bit 7 reads as the wire-OR of (IFR & IER & 0x7F),
        // i.e., "any unmasked source pending". Bits 0-6 are stored
        // directly. Real silicon recomputes bit 7 on the fly; the
        // emulator does the same so /IRQ status reads back correctly.
        return (ifr & 0x7F) | ((ifr & ier & 0x7F) ? 0x80 : 0);
    case 0x0E: return ier | 0x80; // bit 7 always reads as 1

    case 0x0F: // ORA (no handshake) - reads Port A without affecting STROBE
        {
            uint8_t atmegaPA = getBroadcastPortA();
            return (portA & ddrA) | (atmegaPA & ~ddrA);
        }

    default:
        return 0;
    }
}

// --- VIA register write ---
void A1IO_RTC::writeRegister(uint16_t address, uint8_t value)
{
    uint8_t reg = address & 0x0F;

    switch (reg) {
    case 0x00: // ORB - Port B
        portB = value;
        break;

    case 0x01: // ORA - Port A (with handshake)
        portA = value;
        // Check RW flag (bit 6): when set, ATMEGA enters write mode
        if (value & 0x40) {
            if (!rwModeActive && ddrB == 0xFF && ddrA == 0xFF) {
                // CPU is writing to ATMEGA: register index on Port B, value on Port A
                rwModeActive = true;
                uint8_t regIndex = portB;
                uint8_t dataValue = value & 0x3F; // lower 6 bits = data (bit 6 = RW flag)

                // Update RTC based on register
                if (regIndex <= 5 && regIndex != 0) {
                    // Registers 1-5: min, sec, day, month, year
                    // Compute current time and adjust offset
                    auto now = std::chrono::system_clock::now();
                    auto adjusted = now + std::chrono::seconds(rtcOffsetSeconds);
                    std::time_t tt = std::chrono::system_clock::to_time_t(adjusted);
                    std::tm ltCopy = *std::localtime(&tt);

                    switch (regIndex) {
                    case 1: ltCopy.tm_min = dataValue; break;
                    case 2: ltCopy.tm_sec = dataValue; break;
                    case 3: ltCopy.tm_mday = dataValue; break;
                    case 4: ltCopy.tm_mon = dataValue - 1; break; // 0-based
                    case 5: ltCopy.tm_year = dataValue + 2000 - 1900; break;
                    }

                    std::time_t newTime = std::mktime(&ltCopy);
                    std::time_t hostTime = std::chrono::system_clock::to_time_t(now);
                    rtcOffsetSeconds = static_cast<int>(newTime - hostTime);
                    updateVirtualRegisters();
                } else if (regIndex == 0 && dataValue > 0) {
                    // Hour: excluded when 0 to prevent false writes at VIA power-up
                    auto now = std::chrono::system_clock::now();
                    auto adjusted = now + std::chrono::seconds(rtcOffsetSeconds);
                    std::time_t tt = std::chrono::system_clock::to_time_t(adjusted);
                    std::tm ltCopy = *std::localtime(&tt);
                    ltCopy.tm_hour = dataValue;
                    std::time_t newTime = std::mktime(&ltCopy);
                    std::time_t hostTime = std::chrono::system_clock::to_time_t(now);
                    rtcOffsetSeconds = static_cast<int>(newTime - hostTime);
                    updateVirtualRegisters();
                }
            }
        } else {
            // RW cleared: exit write mode
            rwModeActive = false;
        }
        break;

    case 0x02: ddrB = value; break;
    case 0x03: ddrA = value; break;

    case 0x04: // T1C-L (writes to latch low)
        t1LatchLo = value;
        break;
    case 0x05: // T1C-H (writes to counter high, copies latch low to counter low, starts timer)
        t1LatchHi = value;
        t1Counter = (static_cast<uint16_t>(value) << 8) | t1LatchLo;
        t1Running = true;
        ifr &= ~0x40; // clear T1 interrupt flag
        break;
    case 0x06: t1LatchLo = value; break;
    case 0x07: t1LatchHi = value; ifr &= ~0x40; break;

    case 0x08: t2CounterLo = value; break;
    case 0x09: t2CounterHi = value; ifr &= ~0x20; break;

    case 0x0A: // SR - Shift Register
        shiftReg = value;
        // Track 16-bit shift register output (two cascaded 74HC164)
        if ((acr & 0x1C) == 0x18) { // shift-out under phi2 mode
            if (!shiftHighWritten) {
                // First write = high byte (outputs 9-16)
                shiftOutHigh = value;
                shiftHighWritten = true;
            } else {
                // Second write = low byte (outputs 1-8)
                digitalOutputs = (static_cast<uint16_t>(shiftOutHigh) << 8) | value;
                shiftHighWritten = false;
            }
        }
        break;

    case 0x0B: // ACR
        acr = value;
        // Reset shift register tracking when ACR changes
        if ((value & 0x1C) != 0x18) {
            shiftHighWritten = false;
        }
        break;

    case 0x0C: pcr = value; break;

    case 0x0D: // IFR - writing 1 to a bit clears that flag
        ifr &= ~(value & 0x7F);
        break;

    case 0x0E: // IER - bit 7: 1=set, 0=clear the masked bits
        if (value & 0x80)
            ier |= (value & 0x7F);
        else
            ier &= ~(value & 0x7F);
        break;

    case 0x0F: // ORA (no handshake)
        portA = value;
        break;
    }
}

// --- Cycle-accurate tick ---
void A1IO_RTC::advanceCycles(int cycles)
{
    if (cycles <= 0) return;

    // Advance broadcast cycle
    advanceBroadcast(cycles);

    // Refresh RTC values periodically (every full broadcast cycle = 2400 cycles)
    // We update on register 0 start to keep it simple
    if (broadcastRegister == 0 && broadcastCycleCounter < cycles) {
        updateVirtualRegisters();
    }

    // Timer 1
    if (t1Running) {
        int remaining = static_cast<int>(t1Counter) - cycles;
        if (remaining <= 0) {
            ifr |= 0x40; // set T1 interrupt flag
            if (acr & 0x40) {
                // Free-running mode: reload from latch
                t1Counter = (static_cast<uint16_t>(t1LatchHi) << 8) | t1LatchLo;
            } else {
                // One-shot: stop
                t1Running = false;
                t1Counter = 0;
            }
        } else {
            t1Counter = static_cast<uint16_t>(remaining);
        }
    }
}

// --- UI snapshot ---
void A1IO_RTC::copySnapshot(Snapshot& out) const
{
    out.hour = virtualRegisters[0];
    out.minute = virtualRegisters[1];
    out.second = virtualRegisters[2];
    out.day = virtualRegisters[3];
    out.month = virtualRegisters[4];
    out.year = virtualRegisters[5];
    out.tempRTC = virtualRegisters[6];
    out.tempDS18B20 = virtualRegisters[7];
    out.tempDS18B20dec = virtualRegisters[8];

    for (int i = 0; i < 8; ++i)
        out.analogInputs[i] = analogInputs[i];
    for (int i = 0; i < 4; ++i)
        out.digitalInputs[i] = digitalInputs[i];

    out.digitalOutputs = digitalOutputs;
    out.currentRegister = broadcastRegister;
    out.strobeActive = strobeActive && !strobeConsumed;
}

// --- Configurable inputs ---
void A1IO_RTC::setAnalogInput(int channel, uint8_t value)
{
    if (channel >= 0 && channel < 8) {
        analogInputs[channel] = value;
    }
}

void A1IO_RTC::setDigitalInput(int channel, uint8_t value)
{
    if (channel >= 0 && channel < 4) {
        digitalInputs[channel] = value ? 1 : 0;
    }
}

void A1IO_RTC::setOverrideTime(std::time_t target)
{
    const auto nowSys = std::chrono::system_clock::now();
    const std::time_t hostNow = std::chrono::system_clock::to_time_t(nowSys);
    rtcOffsetSeconds = static_cast<int>(target - hostNow);
    updateVirtualRegisters();
}

void A1IO_RTC::serialize(pom1::SnapshotWriter& w) const
{
    w.writeU8(portB); w.writeU8(portA); w.writeU8(ddrB); w.writeU8(ddrA);
    w.writeU8(t1LatchLo); w.writeU8(t1LatchHi);
    w.writeU8(t2CounterLo); w.writeU8(t2CounterHi);
    w.writeU8(shiftReg); w.writeU8(acr); w.writeU8(pcr);
    w.writeU8(ifr); w.writeU8(ier);
    w.writeU16(t1Counter);
    w.writeU8 (t1Running ? 1 : 0);
    w.writeU32(static_cast<uint32_t>(broadcastRegister));
    w.writeU32(static_cast<uint32_t>(broadcastCycleCounter));
    w.writeU8 (strobeActive ? 1 : 0);
    w.writeU8 (strobeConsumed ? 1 : 0);
    w.writeBytes(virtualRegisters.data(), virtualRegisters.size());
    w.writeU32(static_cast<uint32_t>(rtcOffsetSeconds));
    w.writeBytes(analogInputs.data(),  analogInputs.size());
    w.writeBytes(digitalInputs.data(), digitalInputs.size());
    w.writeU16(digitalOutputs);
    w.writeU8 (shiftOutHigh);
    w.writeU8 (shiftHighWritten ? 1 : 0);
    w.writeU8 (rwModeActive ? 1 : 0);
}

void A1IO_RTC::deserialize(pom1::SnapshotReader& r)
{
    portB = r.readU8(); portA = r.readU8(); ddrB = r.readU8(); ddrA = r.readU8();
    t1LatchLo = r.readU8(); t1LatchHi = r.readU8();
    t2CounterLo = r.readU8(); t2CounterHi = r.readU8();
    shiftReg = r.readU8(); acr = r.readU8(); pcr = r.readU8();
    ifr = r.readU8(); ier = r.readU8();
    t1Counter = r.readU16();
    t1Running = r.readU8() != 0;
    broadcastRegister     = static_cast<int>(r.readU32());
    broadcastCycleCounter = static_cast<int>(r.readU32());
    strobeActive   = r.readU8() != 0;
    strobeConsumed = r.readU8() != 0;
    r.readBytes(virtualRegisters.data(), virtualRegisters.size());
    rtcOffsetSeconds = static_cast<int>(r.readU32());
    r.readBytes(analogInputs.data(),  analogInputs.size());
    r.readBytes(digitalInputs.data(), digitalInputs.size());
    digitalOutputs   = r.readU16();
    shiftOutHigh     = r.readU8();
    shiftHighWritten = r.readU8() != 0;
    rwModeActive     = r.readU8() != 0;
}
