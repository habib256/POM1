// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// CommandPort — implementation. Rationale, protocol and threading rules are in
// CommandPort.h; the verb reference is doc/COMMAND_PORT.md.

#include "CommandPort.h"

#include "CliDispatcher.h"
#include "EmulationController.h"
#include "EmulationSnapshot.h"
#include "HexDumpFile.h"
#include "Logger.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <memory>
#include <cstdio>
#include <sstream>
#include <thread>
#include <vector>

#if !POM1_IS_WASM
  #ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
  #else
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <netinet/in.h>
    #include <sys/select.h>
    #include <sys/socket.h>
  #endif
#endif

namespace pom1 {
namespace {

/// The channel's own vocabulary, printed by `help`. Kept next to the dispatch
/// switch so the two cannot drift the way doc/CLI.md and kCliFlagHelp[] once did.
/// Cap on one `key` request. Same order as --paste's 4096: a control channel
/// is not a file loader, and an unbounded key burst would sit in the queue
/// unread while the CPU drains it one $D010 strobe at a time.
constexpr size_t kMaxKeyBytes = 4096;

constexpr const char* kVerbs =
    "ping status help quit | key expect screen screen-clear | "
    "reset hardreset start stop step cycles break | "
    "peek poke load run snapshot-save snapshot-load";

std::vector<std::string> splitWords(const std::string& s)
{
    std::vector<std::string> out;
    std::istringstream is(s);
    std::string w;
    while (is >> w) out.push_back(w);
    return out;
}

/// Everything after the first `n` whitespace-separated tokens, verbatim —
/// `key` and `expect` take free text that must keep its inner spaces.
std::string tailAfter(const std::string& s, int n)
{
    size_t i = 0;
    for (int k = 0; k < n; ++k) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    }
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

/// Parse an address. `$FFEF`, `0xFFEF` and a bare `FFEF` are all hex — this is
/// a 6502 tool and every address a harness types comes off a Monitor listing.
/// Returns false rather than clamping: a silently truncated address is how the
/// memory-image loader once wrote a program into page zero.
bool parseAddress(const std::string& tok, uint32_t& out)
{
    std::string t = tok;
    if (!t.empty() && t[0] == '$') t.erase(0, 1);
    else if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t.erase(0, 2);
    if (t.empty() || t.size() > 4) return false;
    uint32_t v = 0;
    for (char c : t) {
        int d;
        if (c >= '0' && c <= '9')      d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = v * 16 + static_cast<uint32_t>(d);
    }
    out = v;
    return true;
}

bool parseCount(const std::string& tok, uint64_t& out)
{
    if (tok.empty()) return false;
    uint64_t v = 0;
    for (char c : tok) {
        if (c == '_') continue;                       // 5_000_000 reads better
        if (c < '0' || c > '9') return false;
        if (v > (UINT64_MAX - 9) / 10) return false;  // refuse, never wrap
        v = v * 10 + static_cast<uint64_t>(c - '0');
    }
    out = v;
    return true;
}

std::string toUpper(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string hexByte(uint8_t v)
{
    char b[3];
    std::snprintf(b, sizeof(b), "%02X", static_cast<unsigned>(v));
    return b;
}

std::string hexWord(uint16_t v)
{
    char b[5];
    std::snprintf(b, sizeof(b), "%04X", static_cast<unsigned>(v));
    return b;
}

/// Snapshots live on the HEAP, never on this thread's stack.
///
/// `sizeof(EmulationSnapshot)` is 260 KB and a std::thread gets 512 KB on
/// macOS (Windows reserves 1 MB, Linux 8). The compiler reserves the frame for
/// EVERY branch of execute() at entry, so the four verbs that wanted a
/// snapshot summed to more than the whole stack and the first request of any
/// kind — `ping` included — died in ___chkstk_darwin with SIGBUS, before a
/// single byte of reply was sent. CLAUDE.md's testing section names this exact
/// trap and the tool that finds it from a machine that is not the one crashing:
/// `clang -Wframe-larger-than=32768`.
std::unique_ptr<EmulationSnapshot> takeSnapshot(EmulationController& emu)
{
    auto snap = std::make_unique<EmulationSnapshot>();
    emu.copySnapshot(*snap);
    return snap;
}

} // namespace

// ─────────────────────────────────────────────────────────────
// Escaping — a reply is always exactly one line
// ─────────────────────────────────────────────────────────────

std::string CommandPort::escape(const std::string& raw)
{
    std::string out;
    out.reserve(raw.size() + 8);
    for (unsigned char c : raw) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\r': out += "\\r";  break;
            case '\n': out += "\\n";  break;
            default:
                if (c < 0x20 || c == 0x7f) {
                    char b[5];
                    std::snprintf(b, sizeof(b), "\\x%02X", static_cast<unsigned>(c));
                    out += b;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

std::string CommandPort::unescape(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 >= text.size()) { out.push_back(text[i]); continue; }
        char n = text[++i];
        switch (n) {
            case 'r':  out.push_back('\r'); break;
            case 'n':  out.push_back('\n'); break;
            case 't':  out.push_back('\t'); break;
            case '\\': out.push_back('\\'); break;
            case 'x': {
                if (i + 2 < text.size()) {
                    auto hexDigit = [](char c) -> int {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                        return -1;
                    };
                    int hi = hexDigit(text[i + 1]), lo = hexDigit(text[i + 2]);
                    if (hi >= 0 && lo >= 0) {
                        out.push_back(static_cast<char>(hi * 16 + lo));
                        i += 2;
                        break;
                    }
                }
                out.push_back('\\');
                out.push_back('x');
                break;
            }
            default: out.push_back('\\'); out.push_back(n); break;
        }
    }
    return out;
}

// ─────────────────────────────────────────────────────────────
// Verb dispatch
// ─────────────────────────────────────────────────────────────

CommandPort::CommandPort(EmulationController& emu, Hooks hooks)
    : emu_(emu), hooks_(std::move(hooks))
{
#if !POM1_IS_WASM && defined(_WIN32)
    static bool winsockReady = false;
    if (!winsockReady) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        winsockReady = true;
    }
#endif
}

CommandPort::~CommandPort() { stop(); }

std::string CommandPort::execute(const std::string& line)
{
    const auto words = splitWords(line);
    if (words.empty()) return "OK";
    const std::string verb = words[0];

    // ---- trivia -----------------------------------------------------------
    if (verb == "ping") return "OK pong";
    if (verb == "help") return std::string("OK ") + kVerbs;
    if (verb == "quit") {
        if (hooks_.requestQuit) hooks_.requestQuit();
        return "OK bye";
    }

    if (verb == "status") {
        auto snap = takeSnapshot(emu_);
        std::string s = "OK pc=$" + hexWord(snap->programCounter);
        s += " a=$" + hexByte(snap->accumulator);
        s += " x=$" + hexByte(snap->xRegister);
        s += " y=$" + hexByte(snap->yRegister);
        s += " sp=$" + hexByte(snap->stackPointer);
        s += " p=$" + hexByte(snap->statusRegister);
        s += std::string(" running=") + (snap->cpuRunning ? "1" : "0");
        s += " ram=" + std::to_string(snap->ramSizeKB);
        return s;
    }

    // ---- the emulated display --------------------------------------------
    if (verb == "screen" || verb == "screen-clear") {
        if (!hooks_.screenText) return "ERR no display capture on this build";
        const std::string all = hooks_.screenText();
        if (screenMark_ > all.size()) screenMark_ = all.size();   // machine was reset
        if (verb == "screen-clear") { screenMark_ = all.size(); return "OK"; }
        return "OK " + escape(all.substr(screenMark_));
    }

    if (verb == "expect") {
        if (!hooks_.screenText) return "ERR no display capture on this build";
        if (words.size() < 2)   return "ERR usage: expect <timeout_ms> <text>";
        uint64_t timeoutMs = 0;
        if (!parseCount(words[1], timeoutMs)) return "ERR expect: bad timeout";
        const std::string wanted = toUpper(unescape(tailAfter(line, 2)));
        if (wanted.empty()) return "ERR usage: expect <timeout_ms> <text>";

        // Poll rather than hold anything: the emulation thread is what makes the
        // text appear, and blocking it would guarantee the timeout we are here to
        // avoid. Matching is case-insensitive because the Apple-1 answers in
        // upper case and every harness compared .upper() to .upper().
        const auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::milliseconds(timeoutMs);
        for (;;) {
            const std::string all = hooks_.screenText();
            if (screenMark_ > all.size()) screenMark_ = all.size();
            const std::string tail = all.substr(screenMark_);
            const size_t hit = toUpper(tail).find(wanted);
            if (hit != std::string::npos) {
                const std::string consumed = tail.substr(0, hit + wanted.size());
                screenMark_ += consumed.size();
                return "OK " + escape(consumed);
            }
            if (std::chrono::steady_clock::now() >= deadline)
                return "ERR timeout after " + std::to_string(timeoutMs) +
                       "ms; unmatched tail: " + escape(tail.substr(tail.size() > 400
                                                                  ? tail.size() - 400 : 0));
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    // ---- keyboard ---------------------------------------------------------
    if (verb == "key") {
        const std::string text = unescape(tailAfter(line, 1));
        if (text.empty()) return "ERR usage: key <text>   (\\r for RETURN)";
        if (text.size() > kMaxKeyBytes)
            return "ERR key: more than " + std::to_string(kMaxKeyBytes) + " bytes";
        // LITERAL, unlike --paste. `queueKeystrokes` keeps only CR and
        // printable ASCII 32-126, which is right for pasting a text file and
        // wrong for a control channel: the CFFA1 paginator is dismissed with
        // ESC ($1B), the Terminal Card's own reset is Ctrl-R ($12), and a
        // harness that cannot send those has to fall back to the very telnet
        // wire this channel exists to replace. `\n` still becomes CR, because
        // no Apple-1 program has ever wanted a line feed.
        int n = 0;
        for (char ch : text) {
            emu_.queueKey(ch == '\n' ? '\r' : ch);
            ++n;
        }
        return "OK " + std::to_string(n);
    }

    // ---- CPU transport ----------------------------------------------------
    if (verb == "reset")     { emu_.warmResetToMonitor();          return "OK"; }
    if (verb == "hardreset") { emu_.hardReset(/*animateBoot=*/false); return "OK"; }
    if (verb == "start")     { emu_.startCpu();                    return "OK"; }
    if (verb == "stop")      { emu_.stopCpu();                     return "OK"; }

    if (verb == "step") {
        uint64_t n = 1;
        if (words.size() > 1 && !parseCount(words[1], n)) return "ERR step: bad count";
        if (n > 1000000) return "ERR step: count > 1000000";
        // Stop first: stepping while the async loop runs makes "exactly N
        // instructions" a wall-clock race — the same determinism bug that made
        // three TMS9918 micro-tests flap under TSan (see runDeferredActions).
        emu_.stopCpu();
        for (uint64_t k = 0; k < n; ++k) emu_.stepCpu();
        return "OK pc=$" + hexWord(takeSnapshot(emu_)->programCounter);
    }

    if (verb == "cycles") {
        uint64_t n = 0;
        if (words.size() < 2 || !parseCount(words[1], n)) return "ERR usage: cycles <n>";
        const bool wasRunning = takeSnapshot(emu_)->cpuRunning;
        emu_.runCyclesSync(n);          // leaves the CPU stopped, by contract
        if (wasRunning) emu_.startCpu();
        return "OK pc=$" + hexWord(takeSnapshot(emu_)->programCounter);
    }

    if (verb == "break") {
        uint32_t addr = 0;
        if (words.size() < 2 || !parseAddress(words[1], addr))
            return "ERR usage: break <addr>";
        emu_.setCpuBreakpoint(static_cast<uint16_t>(addr));
        return "OK";
    }

    // ---- memory -----------------------------------------------------------
    if (verb == "peek") {
        uint32_t addr = 0;
        uint64_t len  = 1;
        if (words.size() < 2 || !parseAddress(words[1], addr))
            return "ERR usage: peek <addr> [len]";
        if (words.size() > 2 && !parseCount(words[2], len)) return "ERR peek: bad length";
        if (len == 0 || len > 256) return "ERR peek: length must be 1..256";
        if (addr + len > 0x10000)  return "ERR peek: range past $FFFF";
        auto snap = takeSnapshot(emu_);
        std::string s = "OK";
        for (uint64_t k = 0; k < len; ++k)
            s += " " + hexByte(snap->memory[addr + k]);
        return s;
    }

    if (verb == "poke") {
        uint32_t addr = 0;
        if (words.size() < 3 || !parseAddress(words[1], addr))
            return "ERR usage: poke <addr> <hex> [hex...]";
        std::vector<std::pair<uint16_t, uint8_t>> writes;
        for (size_t k = 2; k < words.size(); ++k) {
            uint32_t v = 0;
            if (!parseAddress(words[k], v) || v > 0xFF)
                return "ERR poke: '" + words[k] + "' is not a byte";
            const uint32_t target = addr + static_cast<uint32_t>(writes.size());
            if (target > 0xFFFF) return "ERR poke: range past $FFFF";
            writes.emplace_back(static_cast<uint16_t>(target), static_cast<uint8_t>(v));
        }
        emu_.writeMemoryBatch(writes);
        return "OK " + std::to_string(writes.size());
    }

    // ---- files ------------------------------------------------------------
    if (verb == "load") {
        uint32_t addr = 0;
        if (words.size() < 3 || !parseAddress(words[1], addr))
            return "ERR usage: load <addr> <path>";
        const std::string path = tailAfter(line, 2);   // may contain spaces
        std::string err;
        int bytes = 0;
        bool ok;
        if (isHexDumpPath(path)) {
            // The file's own AAAA: prefixes and R suffix win over <addr>, exactly
            // as --load documents; <addr> is the fallback for a raw binary.
            uint16_t start = static_cast<uint16_t>(addr);
            ok = emu_.loadHexDump(path, start, err, &bytes, nullptr, /*startCpu=*/true);
        } else {
            ok = emu_.loadBinary(path, static_cast<uint16_t>(addr), err, &bytes,
                                 /*startCpu=*/true);
        }
        if (!ok) return "ERR load: " + (err.empty() ? std::string("failed") : err);
        return "OK " + std::to_string(bytes);
    }

    if (verb == "run") {
        uint32_t addr = 0;
        if (words.size() < 2 || !parseAddress(words[1], addr))
            return "ERR usage: run <addr>";
        emu_.jumpTo(static_cast<uint16_t>(addr));
        return "OK";
    }

    if (verb == "snapshot-save" || verb == "snapshot-load") {
        const std::string path = tailAfter(line, 1);
        if (path.empty()) return "ERR usage: " + verb + " <path>";
        std::string err;
        const bool ok = (verb == "snapshot-save") ? emu_.saveSnapshot(path, err)
                                                  : emu_.loadSnapshot(path, err);
        if (!ok) return "ERR " + verb + ": " + (err.empty() ? std::string("failed") : err);
        return "OK";
    }

    return "ERR unknown verb '" + verb + "' — try: help";
}

// ─────────────────────────────────────────────────────────────
// Socket plumbing — desktop only
// ─────────────────────────────────────────────────────────────

#if !POM1_IS_WASM

bool CommandPort::start(uint16_t port, std::string& error)
{
    if (listenFd_) { error = "already listening"; return false; }

    listenFd_.reset(::socket(AF_INET, SOCK_STREAM, 0));
    if (!listenFd_) { error = "socket() failed"; return false; }

    int optval = 1;
#ifdef _WIN32
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&optval), sizeof(optval));
#else
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
#endif

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // loopback ONLY, never 0.0.0.0
    addr.sin_port        = htons(port);

    if (::bind(listenFd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        error = "cannot bind 127.0.0.1:" + std::to_string(port) + " (already in use?)";
        listenFd_.reset();
        return false;
    }
    if (::listen(listenFd_, 1) < 0) {
        error = "listen() failed";
        listenFd_.reset();
        return false;
    }

    listening_.store(true);
    thread_ = std::thread([this] { serve(); });
    pom1::log().info("Cmd", "control channel listening on localhost:" +
                            std::to_string(port));
    return true;
}

void CommandPort::stop()
{
    stopping_.store(true);
    if (thread_.joinable()) thread_.join();
    listenFd_.reset();
    listening_.store(false);
}

void CommandPort::serve()
{
    while (!stopping_.load()) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(listenFd_.get(), &rd);
        struct timeval tv{0, 200000};   // 200 ms, so stop() is responsive
        const int r = ::select(static_cast<int>(listenFd_.get()) + 1, &rd, nullptr, nullptr, &tv);
        if (r <= 0) continue;

        struct sockaddr_in peer{};
#ifdef _WIN32
        int peerLen = sizeof(peer);
#else
        socklen_t peerLen = sizeof(peer);
#endif
        SocketHandle client(::accept(listenFd_,
                                     reinterpret_cast<struct sockaddr*>(&peer), &peerLen));
        if (!client) continue;
        pom1::log().info("Cmd", "control client connected");
        serveClient(std::move(client));
        pom1::log().info("Cmd", "control client disconnected");
    }
}

void CommandPort::serveClient(SocketHandle client)
{
    std::string pending;
    char buf[4096];

    while (!stopping_.load()) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(client.get(), &rd);
        struct timeval tv{0, 200000};
        const int r = ::select(static_cast<int>(client.get()) + 1, &rd, nullptr, nullptr, &tv);
        if (r < 0) return;
        if (r == 0) continue;

#ifdef _WIN32
        const int n = ::recv(client, buf, static_cast<int>(sizeof(buf)), 0);
#else
        const ssize_t n = ::recv(client, buf, sizeof(buf), 0);
#endif
        if (n <= 0) return;                      // peer closed
        pending.append(buf, static_cast<size_t>(n));
        if (pending.size() > 1u << 20) return;   // a client that never sends \n

        size_t nl;
        while ((nl = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();

            const std::string reply = execute(line) + "\n";
            size_t sent = 0;
            while (sent < reply.size()) {
#ifdef _WIN32
                const int w = ::send(client, reply.data() + sent,
                                     static_cast<int>(reply.size() - sent), 0);
#else
                const ssize_t w = ::send(client, reply.data() + sent,
                                         reply.size() - sent, 0);
#endif
                if (w <= 0) return;
                sent += static_cast<size_t>(w);
            }
            // `quit` is answered before the socket goes away, so the client
            // reads "OK bye" rather than an unexplained EOF.
            if (line.rfind("quit", 0) == 0) return;
        }
    }
}

#else   // POM1_IS_WASM — no sockets in the browser

bool CommandPort::start(uint16_t, std::string& error)
{
    error = "control channel unavailable in the WASM build";
    return false;
}
void CommandPort::stop() {}
void CommandPort::serve() {}
void CommandPort::serveClient(SocketHandle) {}

#endif

} // namespace pom1
