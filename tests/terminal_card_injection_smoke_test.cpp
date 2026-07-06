// terminal_card_injection_smoke_test.cpp -- pin the Terminal Card Ctrl-K hand-over.
//
// Ctrl-K (0x0B) toggles injectionSuspended: while suspended, incoming DATA bytes
// are DROPPED (so the local keyboard drives the Apple 1) but control commands —
// including Ctrl-K itself — still bite, so a second Ctrl-K re-attaches. Mirrors
// the Ctrl-T escape hatch. This drives the private incoming-byte parser directly
// (via a friend seam) with no live TCP socket, so it runs headless in ctest.
#include "TerminalCard.h"

#include <cassert>
#include <cstdio>
#include <string>

// Friend seam declared in TerminalCard.h — reaches the private parser + state.
struct TerminalCardTestAccess {
    static void feed(TerminalCard& c, uint8_t b) { c.processIncomingByte(b); }
    static bool suspended(const TerminalCard& c) { return c.injectionSuspended; }
    static bool eightBit(const TerminalCard& c)  { return c.eightBitMode; }
};

namespace {
constexpr uint8_t CTRL_K = 11;   // suspend/resume injection
constexpr uint8_t CTRL_T = 20;   // toggle 8-bit
constexpr uint8_t CTRL_L = 12;   // clear screen (a control cmd that still works)
constexpr uint8_t ESC    = 0x1B;

#define CHECK(cond, msg) do { if (!(cond)) { \
    std::fprintf(stderr, "FAIL: %s (line %d)\n", (msg), __LINE__); return 1; } } while (0)
}

int main()
{
    TerminalCard card;
    std::string injected;
    card.setKeyInjector([&](char key, bool /*raw*/) { injected.push_back(key); });
    using T = TerminalCardTestAccess;

    // Attached by default: a data byte is injected.
    T::feed(card, 'A');
    CHECK(!T::suspended(card), "starts attached");
    CHECK(injected == "A", "'A' injected while attached");

    // Ctrl-K suspends. It is a command, not data → never injected itself.
    T::feed(card, CTRL_K);
    CHECK(T::suspended(card), "Ctrl-K suspends injection");
    CHECK(injected == "A", "Ctrl-K not injected as data");

    // Suspended: data bytes are dropped (local keyboard has the machine).
    T::feed(card, 'B');
    CHECK(injected == "A", "data dropped while suspended");

    // Suspended: control commands still bite (Ctrl-L → clear-screen pending).
    card.clearScreenPending.store(false);
    T::feed(card, CTRL_L);
    CHECK(card.clearScreenPending.load(), "control cmd still works while suspended");
    CHECK(injected == "A", "Ctrl-L not injected as data");

    // A second Ctrl-K re-attaches (works BECAUSE it is handled before the gate).
    T::feed(card, CTRL_K);
    CHECK(!T::suspended(card), "second Ctrl-K re-attaches");
    T::feed(card, 'C');
    CHECK(injected == "AC", "'C' injected after resume");

    // ESC K is the tty-safe alias for Ctrl-K — same toggle.
    T::feed(card, ESC); T::feed(card, 'K');
    CHECK(T::suspended(card), "ESC K suspends");
    T::feed(card, 'D');
    CHECK(injected == "AC", "data dropped after ESC K");
    T::feed(card, ESC); T::feed(card, 'K');
    CHECK(!T::suspended(card), "ESC K again resumes");

    // Ctrl-K is an escape hatch: it must also work in 8-bit raw mode.
    T::feed(card, CTRL_T);                 // enter 8-bit
    CHECK(T::eightBit(card), "Ctrl-T enters 8-bit");
    T::feed(card, 'E');
    CHECK(injected == "ACE", "raw byte injected in 8-bit while attached");
    T::feed(card, CTRL_K);                 // suspend from within 8-bit mode
    CHECK(T::suspended(card), "Ctrl-K suspends in 8-bit mode");
    T::feed(card, 'F');
    CHECK(injected == "ACE", "raw byte dropped while suspended in 8-bit");
    T::feed(card, CTRL_K);                 // resume from within 8-bit mode
    CHECK(!T::suspended(card), "Ctrl-K resumes in 8-bit mode");

    std::printf("terminal_card_injection_smoke: OK\n");
    return 0;
}
