// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// command_port_smoke — the scripting control channel's VERB LAYER, with no
// socket in the binary.
//
// CommandPort::execute() is the whole behaviour of that class: the socket half
// is a line reader wrapped around it. Driving it directly is what makes the
// protocol testable at all — a test that had to open a listening port would
// fight any POM1 the developer has running, which is the exact defect
// TerminalCard::setEnabled was written to fix (see its header).
//
// Seven sections:
//   1. escape/unescape round-trip, including the control bytes a reply must
//      never emit raw.
//   2. trivia verbs and the shape of a reply (OK / ERR, one line, always).
//   3. peek / poke against a real Memory.
//   4. address parsing — the refusals matter more than the acceptances.
//   5. the display verbs: screen, screen-clear and the expect mark.
//   6. expect's timeout, and that a failed match consumes nothing.
//   7. key is LITERAL: the control bytes --paste drops must survive.

#include "CommandPort.h"
#include "EmulationController.h"
#include "EmulationSnapshot.h"

#include <cassert>
#include <cstdio>
#include <memory>
#include <string>

namespace {

using pom1::CommandPort;

std::string g_screen;

CommandPort::Hooks makeHooks(bool& quitRequested)
{
    CommandPort::Hooks h;
    h.screenText = [] { return g_screen; };
    h.requestQuit = [&quitRequested] { quitRequested = true; };
    return h;
}

bool isOk(const std::string& reply)  { return reply.rfind("OK", 0) == 0; }
bool isErr(const std::string& reply) { return reply.rfind("ERR", 0) == 0; }

/// Every reply must be exactly one line — the client's framing rule is
/// readline() and nothing else.
void assertSingleLine(const std::string& reply)
{
    assert(reply.find('\n') == std::string::npos);
    assert(reply.find('\r') == std::string::npos);
}

} // namespace

int main()
{
    // ── 1. escaping ──────────────────────────────────────────────────────
    {
        const std::string raw = "a\\b\rc\nd\te\x01\x7f";
        const std::string esc = CommandPort::escape(raw);
        assert(esc.find('\r') == std::string::npos);
        assert(esc.find('\n') == std::string::npos);
        assert(CommandPort::unescape(esc) == raw);
        // A lone trailing backslash must not run off the end of the string.
        assert(CommandPort::unescape("abc\\") == "abc\\");
        // A malformed \x is kept literally rather than eating the next chars.
        assert(CommandPort::unescape("\\xZZ") == "\\xZZ");
        assert(CommandPort::unescape("\\x41") == "A");
        std::puts("  [PASS] 1. escape/unescape round-trip");
    }

    bool quitRequested = false;
    EmulationController emu(nullptr, /*initializeAudioHardware=*/false);
    CommandPort port(emu, makeHooks(quitRequested));

    // ── 2. trivia and reply shape ────────────────────────────────────────
    {
        assert(port.execute("ping") == "OK pong");
        assertSingleLine(port.execute("status"));
        assert(isOk(port.execute("status")));
        assert(isOk(port.execute("help")));
        // An empty request is a no-op, not an error: a client that sends a
        // stray newline must not be told it did something wrong.
        assert(isOk(port.execute("")));
        assert(isErr(port.execute("frobnicate")));
        assert(!quitRequested);
        assert(isOk(port.execute("quit")));
        assert(quitRequested);
        std::puts("  [PASS] 2. trivia verbs and reply shape");
    }

    // ── 3. peek / poke ───────────────────────────────────────────────────
    {
        assert(port.execute("poke 0300 DE AD BE EF") == "OK 4");
        assert(port.execute("peek 0300 4") == "OK DE AD BE EF");
        assert(port.execute("peek 0300") == "OK DE");        // length defaults to 1
        assert(port.execute("peek $0301 2") == "OK AD BE");  // $ prefix
        assert(port.execute("peek 0x0302 1") == "OK BE");    // 0x prefix
        // The cap is a refusal, never a silent clamp: a truncated read that
        // reports success is how a harness asserts against the wrong bytes.
        assert(isErr(port.execute("peek 0300 257")));
        assert(isErr(port.execute("peek 0300 0")));
        assert(isErr(port.execute("peek FFFF 2")));          // past $FFFF
        assert(isErr(port.execute("poke 0300 1FF")));        // not a byte
        assert(isErr(port.execute("poke 0300")));            // no data
        std::puts("  [PASS] 3. peek/poke against a real Memory");
    }

    // ── 4. address parsing ───────────────────────────────────────────────
    {
        assert(isErr(port.execute("peek ZZZZ")));
        assert(isErr(port.execute("peek 12345")));   // more than 4 hex digits
        assert(isErr(port.execute("peek")));         // missing entirely
        assert(isErr(port.execute("run")));
        assert(isErr(port.execute("break")));
        assert(isErr(port.execute("break GGGG")));
        assert(isOk(port.execute("break FF00")));
        std::puts("  [PASS] 4. address parsing refuses what it cannot represent");
    }

    // ── 5. the display verbs and the mark ────────────────────────────────
    {
        g_screen = "";
        port.execute("screen-clear");
        g_screen = "HELLO\rWORLD\r";
        // screen does NOT consume: two reads in a row see the same text.
        const std::string a = port.execute("screen");
        const std::string b = port.execute("screen");
        assert(a == b);
        assert(a == "OK HELLO\\rWORLD\\r");
        // expect consumes up to and including its match, so the next read
        // starts after it. That is what makes a sequence of expects read like
        // an expect script instead of re-matching stale output.
        assert(port.execute("expect 500 HELLO") == "OK HELLO");
        assert(port.execute("screen") == "OK \\rWORLD\\r");
        // Matching is case-insensitive: the Apple-1 answers in upper case.
        assert(isOk(port.execute("expect 500 world")));
        assert(port.execute("screen") == "OK \\r");
        port.execute("screen-clear");
        assert(port.execute("screen") == "OK ");
        // A shorter screen than the mark means the machine was reset; the mark
        // must fall back rather than index past the end.
        g_screen = "HELLO WORLD, and then some";
        port.execute("expect 500 WORLD");
        g_screen = "ab";
        assert(isOk(port.execute("screen")));
        std::puts("  [PASS] 5. screen, screen-clear and the expect mark");
    }

    // ── 6. expect's timeout ──────────────────────────────────────────────
    {
        g_screen = "PARTIAL";
        port.execute("screen-clear");
        g_screen = "PARTIALOUTPUT";   // the mark sits between the two words
        const std::string miss = port.execute("expect 100 NEVERAPPEARS");
        assert(isErr(miss));
        assertSingleLine(miss);
        // The failure names what it did see — a timeout with no context is the
        // hardest kind of red to act on.
        assert(miss.find("OUTPUT") != std::string::npos);
        // and it consumed nothing, so a retry sees the same text.
        assert(port.execute("screen") == "OK OUTPUT");
        assert(isErr(port.execute("expect")));          // no timeout given
        assert(isErr(port.execute("expect 100")));      // no text given
        assert(isErr(port.execute("expect abc TEXT"))); // timeout not a number
        std::puts("  [PASS] 6. expect times out, explains itself, consumes nothing");
    }

    // ── 7. key is literal ────────────────────────────────────────────────
    {
        // --paste keeps only CR and printable ASCII 32-126. This verb must not:
        // ESC dismisses the CFFA1 paginator and Ctrl-R is the Terminal Card's
        // reset, and a harness that cannot send them is pushed back onto the
        // telnet wire this channel replaces.
        assert(port.execute("key \\x1b") == "OK 1");
        assert(port.execute("key \\x12") == "OK 1");
        assert(port.execute("key ABC\\r") == "OK 4");
        // \n is folded to CR — no Apple-1 program wants a line feed.
        assert(port.execute("key \\n") == "OK 1");
        assert(isErr(port.execute("key")));
        std::puts("  [PASS] 7. key is literal, unlike --paste");
    }

    std::puts("command_port_smoke: all sections passed");
    return 0;
}
