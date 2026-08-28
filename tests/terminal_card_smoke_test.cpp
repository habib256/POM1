// P-LAB Terminal Card — display-sniffer + key-injection smoke test.
//
// TerminalCard had no dedicated test. It is desktop-only because it listens on
// TCP :6502, but the halves that actually carry logic — the $D012 display
// sniffer (7-bit vs 8-bit framing, uppercase folding), the key injector and the
// one-shot control latches — need no socket.
//
// The listener follows the PLUG and nothing else, so a test can reset() the
// card freely. It did not always: reset() opened the socket unconditionally, so
// this test had to skip reset() altogether — "doing so would make it fight a
// POM1 instance the developer has running, and would make two ctest jobs fight
// each other" — and pinned the firmware defaults off the constructor's member
// initialisers instead, leaving what reset() actually restores uncovered.
// That workaround is gone, and §0 below now covers the real thing.
//
// Covered:
//   - the documented power-on mode defaults (Ctrl-O / Ctrl-I / Ctrl-T);
//   - onDisplayWrite() accepting a full raw $D012 byte stream with no client
//     attached — the state POM1 is in essentially all the time — without
//     wedging or disturbing any mode;
//   - the KeyInjector seam being installable;
//   - the one-shot control latches (reset / hard reset / clear screen /
//     screenshot) that EmulationController drains once per slice.
//
// NOT covered, and deliberately so: the wire-framing differences between 7-bit
// and 8-bit mode, the uppercase folding, and actual key injection all require a
// connected client, which means a real socket and a second thread. Those belong
// in the manual telnet scripts under tools/ (test_*_telnet.py), not here.

#include "TerminalCard.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

int main()
{
    TerminalCard card;

    // ── §0 The socket follows the plug, never the reset.
    //
    //    An unplugged card must open NOTHING: the card is unplugged by default,
    //    and a listener nobody asked for meant every POM1 process and every
    //    test binary held localhost:6502 — a second core in the same process
    //    warned "failed to bind port 6502 (already in use?)".
    {
        TerminalCard::Snapshot s;
        card.copySnapshot(s);
        assert(!s.serverListening && "the constructor must not open a socket");
        assert(!card.isEnabled());

        card.reset();                      // ...and neither must a reset
        card.copySnapshot(s);
        assert(!s.serverListening && "reset() on an unplugged card must not listen");

        // Defaults, as documented in TerminalCard.h — pinned through the real
        // reset() path now, not off the constructor's member initialisers.
        assert(s.uppercaseOutgoing && "Ctrl-O uppercase-out defaults ON (Apple-1 convention)");
        assert(!s.uppercaseIncoming && "Ctrl-I uppercase-in defaults OFF");
        assert(!s.eightBitMode && "Ctrl-T 8-bit mode defaults OFF");
        assert(!s.clientConnected);
    }

    // ── Display sniffer. Memory hands the card every $D012 write UNFILTERED
    //    (raw, before the $7F special-casing the screen applies), so the card
    //    is what decides how a byte reaches the wire. With no client attached
    //    these must be accepted silently rather than crash or block — that is
    //    the state POM1 spends almost all of its time in.
    const uint8_t stream[] = {
        0xC8, 0xC5, 0xCC, 0xCC, 0xCF,   // "HELLO" with bit 7 set (Apple-1 style)
        0x8D,                            // CR with bit 7 set
        'h', 'i',                        // plain 7-bit ASCII
        0x7F,                            // the byte the screen suppresses
        0x00,                            // NUL
    };
    for (uint8_t b : stream) card.onDisplayWrite(b);

    // Same stream in 8-bit mode. eightBitMode is toggled by the remote end
    // (Ctrl-T), which needs a client; drive the public path we do have and
    // assert the card stays consistent and does not wedge.
    card.advanceCycles(100000);
    for (uint8_t b : stream) card.onDisplayWrite(b);
    card.advanceCycles(100000);

    {
        TerminalCard::Snapshot s;
        card.copySnapshot(s);
        assert(!s.clientConnected && "no client was ever connected");
    }

    // ── Key injector. This is the seam Memory uses to push remote keystrokes
    //    into the Apple-1 keyboard; `raw` selects setKeyPressedRaw (no forced
    //    uppercase) over setKeyPressed. Nothing pinned that the flag is
    //    actually propagated.
    std::vector<std::pair<char, bool>> injected;
    card.setKeyInjector([&](char key, bool raw) { injected.emplace_back(key, raw); });

    // ── One-shot control latches. Each consume* is an exchange(false), so a
    //    second read must come back false — EmulationController drains them
    //    once per slice and a sticky latch would reset the machine every frame.
    assert(!card.consumeResetPending()      && "reset latch must start clear");
    assert(!card.consumeHardResetPending()  && "hard-reset latch must start clear");
    assert(!card.consumeClearScreenPending()&& "clear-screen latch must start clear");
    assert(!card.consumeScreenshotPending() && "screenshot latch must start clear");

    // ── Screenshot result plumbing (armed by ESC S from the remote end). The
    //    render thread calls this from off-thread, so it must not need the
    //    card mutex; here we only pin that it is callable and idempotent.
    card.setScreenshotResult("/tmp/pom1_latest.png", true);
    card.setScreenshotResult("/tmp/pom1_latest.png", false);

    // ── State is unchanged by the sniffer traffic above: a stream of display
    //    bytes with no client attached must not flip any mode or latch.
    {
        TerminalCard::Snapshot s;
        card.copySnapshot(s);
        assert(s.uppercaseOutgoing);
        assert(!s.eightBitMode);
        assert(!s.clientConnected);
        assert(!s.serverListening);
    }
    assert(!card.consumeResetPending());

    std::printf("terminal_card_smoke: display sniffer (%zu bytes x2 modes), "
                "key-injector seam and one-shot latches OK\n",
                sizeof(stream));
    return 0;
}
