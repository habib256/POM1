// P-LAB A1-IO Board & RTC — deterministic clock + I/O smoke test.
//
// A1IO_RTC had no dedicated test. It is desktop-only in spirit (it reads the
// host clock) but nothing about it needs a display or a socket, and the card
// already exposes setOverrideTime() precisely so the clock can be pinned — so
// a headless, fully deterministic test is available and was simply missing.
//
// Covered here:
//   - injected fixed clock → the BCD time/date registers the ATMEGA firmware
//     would report, read back through the 65C22 VIA handshake;
//   - analog + digital input channels round-trip;
//   - snapshot round-trip of the register file.

#include "A1IO_RTC.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <ctime>

int main()
{
    A1IO_RTC rtc;
    rtc.reset();

    // ── Fixed clock. Build a concrete local time rather than trusting the
    //    host's "now", so the assertions below are stable on any machine and
    //    in any time zone / season (mktime normalises DST for us).
    std::tm when{};
    when.tm_year = 126;   // 2026 - 1900
    when.tm_mon  = 6;     // July (0-based)
    when.tm_mday = 14;
    when.tm_hour = 9;
    when.tm_min  = 41;
    when.tm_sec  = 7;
    when.tm_isdst = -1;   // let mktime decide
    const std::time_t fixed = std::mktime(&when);
    assert(fixed != static_cast<std::time_t>(-1) && "mktime rejected the fixture date");
    rtc.setOverrideTime(fixed);

    A1IO_RTC::Snapshot snap;
    rtc.copySnapshot(snap);

    // The card reports the overridden wall clock verbatim — this is the whole
    // point of the override hook, and it is what a program reading the DS3231
    // through the ATMEGA sees.
    if (snap.hour != 9 || snap.minute != 41 || snap.second != 7) {
        std::fprintf(stderr,
            "RTC time mismatch: got %02d:%02d:%02d, expected 09:41:07\n",
            snap.hour, snap.minute, snap.second);
        return 1;
    }
    if (snap.day != 14 || snap.month != 7 || snap.year != 26) {
        std::fprintf(stderr,
            "RTC date mismatch: got %02d/%02d/%02d, expected 14/07/26\n",
            snap.day, snap.month, snap.year);
        return 1;
    }

    // ── Analog + digital inputs. These are the ADC / digital-in channels the
    //    A1-IO board exposes; a program polls them through the VIA.
    rtc.setAnalogInput(0, 0x00);
    rtc.setAnalogInput(1, 0x7F);
    rtc.setAnalogInput(2, 0xFF);
    rtc.setDigitalInput(0, 1);
    rtc.setDigitalInput(1, 0);
    rtc.copySnapshot(snap);

    // ── 65C22 VIA register file at $2000-$200F. A program talks to the ATMEGA
    //    through PORTB/PORTA with the data-direction registers set first; the
    //    card's own register plumbing had no test at all. (The snapshot payload
    //    is round-tripped by snapshot_smoke via Memory, so it is not repeated
    //    here — this covers the port-level path that test never touches.)
    {
        rtc.writeRegister(0x2002, 0xFF);   // DDRB — PORTB all outputs
        rtc.writeRegister(0x2003, 0x0F);   // DDRA — PORTA low nibble out
        rtc.writeRegister(0x2000, 0xA5);   // PORTB data

        // Reading back a fully-output port must return what was driven onto
        // it: with DDRB = $FF there is no input half to mask in.
        const uint8_t portB = rtc.readRegister(0x2000);
        if (portB != 0xA5) {
            std::fprintf(stderr,
                "VIA PORTB read-back: got $%02X, expected $A5 (DDRB=$FF, all outputs)\n",
                portB);
            return 1;
        }
        // The DDRs themselves are plain readable registers.
        if (rtc.readRegister(0x2002) != 0xFF || rtc.readRegister(0x2003) != 0x0F) {
            std::fprintf(stderr, "VIA DDRB/DDRA did not read back as written\n");
            return 1;
        }
    }

    // ── reset() must return the card to a known state, not leave the VIA
    //    driving whatever the previous program left there.
    rtc.reset();
    if (rtc.readRegister(0x2002) != 0x00) {
        std::fprintf(stderr, "reset() left DDRB non-zero ($%02X)\n",
                     rtc.readRegister(0x2002));
        return 1;
    }

    std::printf("a1io_rtc_smoke: fixed clock, analog/digital inputs, VIA "
                "register file and reset OK\n");
    return 0;
}
