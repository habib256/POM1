// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// CommandPort — the scripting control channel (`--cmd-port N`).
//
// WHY THIS EXISTS
//     POM1 had exactly one way to be driven from outside while it ran: the
//     Terminal Card's telnet socket on 127.0.0.1:6502. That socket carries
//     TWO things down one wire — the keystrokes an external harness types, and
//     the $D012 display echo it must parse to know what happened. A test
//     harness therefore had to guess when the machine was ready by sleeping,
//     and guess when a command had finished by watching characters trickle
//     back. The seven telnet harnesses under tools/ are built that way; they
//     hardcode port 6502 (so they fight any POM1 the developer has running,
//     and each other under a parallel ctest), they never pass --headless, and
//     between them they carry 40 `time.sleep()` calls. That is why 2 977 lines
//     of working test code sit outside `ctest`.
//
//     This channel separates control from the emulated machine's own I/O:
//     one line in, one line out, no keyboard, no display echo, no telnet
//     negotiation. `expect` blocks on the emulated display until the text it
//     is waiting for appears — which is what a `sleep()` was approximating,
//     and the reason a converted harness is deterministic rather than merely
//     faster.
//
// THE PROTOCOL, in one paragraph
//     A request is ONE line: a verb, then space-separated arguments. A reply
//     is ONE line: `OK` optionally followed by a payload, or `ERR <message>`.
//     Payloads escape `\` `\r` `\n` so a reply is always exactly one line and
//     a client never needs a framing rule beyond readline(). Verbs mirror the
//     CLI's (doc/CLI.md) — `load`, `run`, `step`, `break`, `snapshot-save`,
//     `sd-put` — because that vocabulary already exists, is already
//     documented, and is already decoupled from the UI (`cli_dispatcher_smoke`
//     pins that the dispatcher links without ImGui). The verbs this channel
//     adds on top are the ones a one-shot command line cannot express: `key`,
//     `expect`, `screen`, `peek`, `reset`.
//
// SCOPE AND SAFETY
//     Dev-only, off unless `--cmd-port` is given, `--headless` only, and bound
//     to the loopback interface exclusively — the same posture as
//     TelemetryPort next door (doc/TELEMETRY_SIDE_CHANNEL.md). It is NOT a
//     stable public API and carries no versioning: it is test plumbing.
//
// THREADING
//     One thread owns the listener and the single connected client, and calls
//     into EmulationController exactly as the UI thread would. It takes no
//     lock of its own, so it introduces no new edge in the rank order of
//     src/LockOrder.h: every call it makes acquires `stateMutex` at the top,
//     which is the outermost rank. `expect` polls the screen hook rather than
//     holding anything across the wait.
//
//     Deliberately NOT a new EmulationController method: this file is a
//     CONSUMER of the existing facade (`controller_public_methods` is a frozen
//     ratchet, and a debug channel is exactly the kind of caller that must
//     live outside it). Everything below is reachable through API that already
//     had another caller.

#ifndef POM1_COMMAND_PORT_H
#define POM1_COMMAND_PORT_H

#include "POM1Build.h"
#include "SocketHandle.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

class EmulationController;

namespace pom1 {

class CommandPort
{
public:
    /// Host-side seams the channel needs and cannot reach through
    /// EmulationController: the accumulated emulated-display text (the headless
    /// driver owns the DisplayDevice that captures it) and the process's own
    /// shutdown request. Both are supplied by main_imgui.cpp.
    struct Hooks {
        /// Every character the machine has written to $D012 this session,
        /// bit 7 already stripped. Must be safe to call from another thread.
        std::function<std::string()> screenText;
        /// Ask the headless driver to leave its idle loop and exit 0.
        std::function<void()>        requestQuit;
    };

    CommandPort(EmulationController& emu, Hooks hooks);
    ~CommandPort();

    CommandPort(const CommandPort&)            = delete;
    CommandPort& operator=(const CommandPort&) = delete;

    /// Bind 127.0.0.1:`port` and start serving. Returns false with `error` set
    /// when the port cannot be taken — a harness that asked for a specific port
    /// must fail loudly rather than run against a machine it cannot steer.
    bool start(uint16_t port, std::string& error);

    /// Stop serving and join the thread. Idempotent; also called by ~CommandPort.
    void stop();

    bool listening() const { return listening_.load(); }

    /// Execute one request line and return the reply line (no trailing newline).
    /// Public because it is the whole behaviour of this class and the only part
    /// worth testing without a socket — `command_port_smoke` drives it directly.
    std::string execute(const std::string& line);

    /// Escape `\` `\r` `\n` so a payload can ride in a single reply line.
    static std::string escape(const std::string& raw);
    /// Inverse of escape(), applied to `key` / `expect` arguments.
    static std::string unescape(const std::string& text);

private:
    void serve();                       ///< thread body: accept, then serve one client
    void serveClient(SocketHandle client);

    EmulationController& emu_;
    Hooks                hooks_;

    SocketHandle       listenFd_;
    std::thread        thread_;
    std::atomic<bool>  stopping_{false};
    std::atomic<bool>  listening_{false};

    /// How far into screenText() the client has already consumed. `expect`
    /// searches from here and advances past its match, so successive expects
    /// read like an expect script instead of re-matching stale output.
    size_t screenMark_ = 0;
};

} // namespace pom1

#endif // POM1_COMMAND_PORT_H
