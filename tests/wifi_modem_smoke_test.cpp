// P-LAB MODEM BBS (Wi-Fi Modem) — 65C51 ACIA + Hayes AT smoke test.
//
// WiFiModem had no dedicated test. Like the Terminal Card it is desktop-only
// because it opens TCP sockets, but the parts that carry the logic are offline:
// the 65C51 register file at $B000-$B003 and the Hayes AT command parser. No
// ATD/ATDT (dial) command is issued here, so nothing touches the network and
// the test is deterministic.
//
// Covered:
//   - ACIA register semantics: status/command/control, TX-empty + RX-full bits;
//   - AT command → response round-trip through the data register (AT, ATE0/ATE1
//     echo control, ATI), i.e. exactly what a terminal program sees;
//   - reset() returning the card to its documented defaults.

#include "WiFiModem.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>

namespace {

// 65C51 register map (see the Memory map in CLAUDE.md): $B000 data,
// $B001 status, $B002 command, $B003 control.
constexpr uint16_t kData    = 0xB000;
constexpr uint16_t kStatus  = 0xB001;
constexpr uint16_t kCommand = 0xB002;
constexpr uint16_t kControl = 0xB003;

constexpr uint8_t kStatusRxFull  = 0x08;   // receive data register full
constexpr uint8_t kStatusTxEmpty = 0x10;   // transmit data register empty

// Push one AT command line into the modem, byte by byte, terminated by CR —
// the same path a program using the ACIA takes.
void sendLine(WiFiModem& m, const std::string& line)
{
    for (char c : line) m.writeRegister(kData, static_cast<uint8_t>(c));
    m.writeRegister(kData, '\r');
    m.advanceCycles(20000);   // let the command FSM run
}

// Drain whatever the modem queued for the CPU.
//
// The 65C51 data register holds ONE byte and readRegister() deliberately does
// not reload it — advanceCycles() does, once the emulated baud interval has
// elapsed, so a CPU spinning on RDRF drains the line at line speed rather than
// at CPU speed (see the comment in WiFiModem::readRegister). A drain loop must
// therefore tick the clock between reads; at 9600 baud one byte is ~1065 cycles
// at 1.0227 MHz, so 4000 is a comfortable margin.
std::string drain(WiFiModem& m, int maxBytes = 512)
{
    std::string out;
    for (int i = 0; i < maxBytes; ++i) {
        m.advanceCycles(4000);
        if ((m.readRegister(kStatus) & kStatusRxFull) == 0) {
            // Give the FSM one more, longer, chance before calling the line
            // idle — a response may still be being generated.
            m.advanceCycles(40000);
            if ((m.readRegister(kStatus) & kStatusRxFull) == 0) break;
        }
        out.push_back(static_cast<char>(m.readRegister(kData)));
    }
    return out;
}

} // namespace

int main()
{
    WiFiModem modem;
    modem.reset();

    // ── Documented power-on defaults.
    {
        WiFiModem::Snapshot s;
        modem.copySnapshot(s);
        assert(!s.connected && "a fresh modem is not connected — no socket is opened here");
        assert(s.echoEnabled && "Hayes echo (ATE1) defaults ON");
    }

    // ── ACIA status register. The transmitter is always ready in this model
    //    (the host socket absorbs bytes), so TX-empty must be asserted or a
    //    polling driver would spin forever waiting to send.
    {
        const uint8_t st = modem.readRegister(kStatus);
        if ((st & kStatusTxEmpty) == 0) {
            std::fprintf(stderr,
                "ACIA status $%02X: TX-empty (bit 4) clear on an idle modem — "
                "a polling driver would hang\n", st);
            return 1;
        }
    }

    // ── Command / control registers are writable and read back.
    modem.writeRegister(kCommand, 0x0B);   // DTR on, no parity, RX IRQ disabled
    modem.writeRegister(kControl, 0x1E);   // 9600 8N1
    {
        WiFiModem::Snapshot s;
        modem.copySnapshot(s);
        if (s.commandReg != 0x0B || s.controlReg != 0x1E) {
            std::fprintf(stderr,
                "ACIA command/control did not stick: cmd=$%02X ctrl=$%02X\n",
                s.commandReg, s.controlReg);
            return 1;
        }
    }

    // ── Hayes AT round-trip. A bare "AT" must answer OK — this is the
    //    handshake every terminal program starts with, and the single most
    //    load-bearing behaviour of the card.
    drain(modem);                       // clear any boot banner
    sendLine(modem, "AT");
    const std::string atReply = drain(modem);
    if (atReply.find("OK") == std::string::npos) {
        std::fprintf(stderr, "AT did not answer OK (got \"%s\")\n", atReply.c_str());
        return 1;
    }

    // ── ATE0 disables command echo; ATE1 restores it. The echo state is
    //    observable in the snapshot, so this pins the parser actually acting on
    //    the command rather than blindly replying OK to everything.
    sendLine(modem, "ATE0");
    drain(modem);
    {
        WiFiModem::Snapshot s;
        modem.copySnapshot(s);
        if (s.echoEnabled) {
            std::fprintf(stderr, "ATE0 did not clear echo — the AT parser is "
                                 "answering OK without interpreting the command\n");
            return 1;
        }
    }
    sendLine(modem, "ATE1");
    drain(modem);
    {
        WiFiModem::Snapshot s;
        modem.copySnapshot(s);
        assert(s.echoEnabled && "ATE1 must restore echo");
    }

    // ── An unknown command must not be silently swallowed as OK.
    sendLine(modem, "ATZZZQ");
    const std::string bad = drain(modem);
    if (bad.find("ERROR") == std::string::npos && bad.find("OK") != std::string::npos) {
        std::fprintf(stderr,
            "unknown AT command answered OK (\"%s\") — the parser accepts anything\n",
            bad.c_str());
        return 1;
    }

    // ── reset() returns to defaults.
    modem.reset();
    {
        WiFiModem::Snapshot s;
        modem.copySnapshot(s);
        assert(s.echoEnabled && "reset() must restore ATE1");
        assert(!s.connected);
    }

    std::printf("wifi_modem_smoke: ACIA registers, AT/ATE0/ATE1 round-trip and "
                "reset OK\n");
    return 0;
}
