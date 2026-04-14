// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// P-LAB Apple-1 Terminal Card emulation
// Bidirectional serial bridge: TCP server on localhost
// https://p-l4b.github.io/terminal/

#include "TerminalCard.h"
#include "CpuClock.h"
#include "Logger.h"
#include <cstring>

namespace {
// TELNET protocol constants (RFC 854 / RFC 857 / RFC 858).
constexpr uint8_t TEL_IAC      = 0xFF;
constexpr uint8_t TEL_WILL     = 0xFB;
constexpr uint8_t TEL_WONT     = 0xFC;
constexpr uint8_t TEL_DO       = 0xFD;
constexpr uint8_t TEL_DONT     = 0xFE;
constexpr uint8_t TEL_OPT_ECHO = 0x01;
constexpr uint8_t TEL_OPT_SGA  = 0x03;

// True if (verb, opt) is a legit client reply to the proactive negotiation we
// send on accept (WILL ECHO, WILL SGA, DO SGA). Accepted silently — anything
// else keeps the historical "refuse with DONT/WONT" behaviour so the server
// never ends up with an option it doesn't actually support.
constexpr bool isOptionAccepted(uint8_t verb, uint8_t opt)
{
    if (opt == TEL_OPT_ECHO) return verb == TEL_DO;
    if (opt == TEL_OPT_SGA)  return verb == TEL_WILL || verb == TEL_DO;
    return false;
}
} // namespace

// ─────────────────────────────────────────────────────────────
// Windows WinSock initialization (same pattern as WiFiModem)
// ─────────────────────────────────────────────────────────────

#if !POM1_IS_WASM && defined(_WIN32)
  #pragma comment(lib, "ws2_32.lib")
  bool TerminalCard::winsockInitialized = false;
  void TerminalCard::initWinsock()
  {
      if (!winsockInitialized) {
          WSADATA wsaData;
          WSAStartup(MAKEWORD(2, 2), &wsaData);
          winsockInitialized = true;
      }
  }
#endif

// ─────────────────────────────────────────────────────────────
// Constructor / Destructor / Reset
// ─────────────────────────────────────────────────────────────

TerminalCard::TerminalCard()
{
#if !POM1_IS_WASM && defined(_WIN32)
    initWinsock();
#endif
}

TerminalCard::~TerminalCard()
{
    stopServer();
}

void TerminalCard::reset()
{
    std::lock_guard<std::mutex> lock(cardMutex);
    disconnectClient();
    stopServer();

    // Reset modes to firmware defaults
    uppercaseOutgoing = true;
    uppercaseIncoming = false;
    eightBitMode = false;
    eightBitPendingCr = false;
    telnetState = TelnetState::NORMAL;

    // Clear statistics
    bytesSentCount = 0;
    bytesReceivedCount = 0;
    pollCycleAccum = 0;

    // Clear pending flags
    resetPending.store(false);
    clearScreenPending.store(false);

    startServer();
}

void TerminalCard::setKeyInjector(KeyInjector injector)
{
    std::lock_guard<std::mutex> lock(cardMutex);
    keyInjector = std::move(injector);
}

// ─────────────────────────────────────────────────────────────
// Display output: CPU writes to $D012 → send to terminal client
// ─────────────────────────────────────────────────────────────

void TerminalCard::onDisplayWrite(uint8_t rawValue)
{
    std::lock_guard<std::mutex> lock(cardMutex);
    if (!clientFd) return;

    if (eightBitMode) {
        // NUL padding after CR (common on serial/telnet paths) — never draw it.
        if (rawValue == 0) return;

        // Defer CR: one CRLF for \r\n, \r\r\n, or \r + next char; avoids an extra
        // blank line (\r\n\r) that leaves stray box chars (e.g. '_') on macOS.
        const uint8_t low = rawValue & 0x7F;
        if (low == 10) {
            if (eightBitPendingCr) {
                uint8_t crlf[2] = { 13, 10 };
                sendToClient(crlf, 2);
                eightBitPendingCr = false;
            }
            return;
        }
        if (low == 13) {
            eightBitPendingCr = true;
            return;
        }
        if (eightBitPendingCr) {
            eightBitPendingCr = false;
            uint8_t crlf[2] = { 13, 10 };
            sendToClient(crlf, 2);
        }
        sendToClient(rawValue);
        return;
    }

    // 7-bit mode
    uint8_t ch = rawValue & 0x7F;

    // Filter non-printable except CR (13) and ESC (27)
    if (ch < 32 && ch != 13 && ch != 27) return;

    // Optional incoming uppercase conversion (display → terminal)
    if (uppercaseIncoming && ch >= 'a' && ch <= 'z') {
        ch = ch - 'a' + 'A';
    }

    // CR → CR+LF (terminal convention)
    if (ch == 13) {
        uint8_t crlf[2] = { 13, 10 };
        sendToClient(crlf, 2);
        return;
    }

    sendToClient(ch);
}

// ─────────────────────────────────────────────────────────────
// Cycle-driven polling (called from Memory::advanceCycles)
// ─────────────────────────────────────────────────────────────

void TerminalCard::advanceCycles(int cycles)
{
    std::lock_guard<std::mutex> lock(cardMutex);
    pollCycleAccum += cycles;
    if (pollCycleAccum < POM1_CPU_CYCLES_PER_MILLISECOND) return;
    pollCycleAccum = 0;

    acceptClient();
    pollClient();
}

// ─────────────────────────────────────────────────────────────
// Snapshot for UI
// ─────────────────────────────────────────────────────────────

void TerminalCard::copySnapshot(Snapshot& out) const
{
    std::lock_guard<std::mutex> lock(cardMutex);
    out.serverListening = listenFd.valid();
    out.clientConnected = clientFd.valid();
    out.clientAddress = clientAddress;
    out.listenPort = listenPort;
    out.uppercaseOutgoing = uppercaseOutgoing;
    out.uppercaseIncoming = uppercaseIncoming;
    out.eightBitMode = eightBitMode;
    out.bytesSent = bytesSentCount;
    out.bytesReceived = bytesReceivedCount;
}

// ─────────────────────────────────────────────────────────────
// Incoming byte processing: terminal → Apple 1
// ─────────────────────────────────────────────────────────────

void TerminalCard::processIncomingByte(uint8_t byte)
{
    bytesReceivedCount++;

    // TELNET IAC handling: refuse all negotiations
    if (telnetState == TelnetState::IAC) {
        // WILL(251), WONT(252), DO(253), DONT(254) → need one more byte
        if (byte >= 251 && byte <= 254) {
            telnetVerb = byte;
            telnetState = TelnetState::VERB;
            return;
        }
        // IAC IAC = literal 255
        if (byte == 255) {
            telnetState = TelnetState::NORMAL;
            // Fall through to process 255 as data
        } else {
            // Unknown IAC sub-command, ignore
            telnetState = TelnetState::NORMAL;
            return;
        }
    } else if (telnetState == TelnetState::VERB) {
        // Accept silently the legit replies to our proactive negotiation;
        // without this the server would cancel its own offer with DONT/WONT.
        if (isOptionAccepted(telnetVerb, byte)) {
            telnetState = TelnetState::NORMAL;
            return;
        }
        uint8_t response[3] = { TEL_IAC, 0, byte };
        if (telnetVerb == TEL_WILL || telnetVerb == TEL_DO) {
            // Refuse every other option: WILL→DONT, DO→WONT.
            response[1] = (telnetVerb == TEL_WILL) ? TEL_DONT : TEL_WONT;
        } else {
            // WONT or DONT — nothing more to say.
            telnetState = TelnetState::NORMAL;
            return;
        }
        sendToClient(response, 3);
        telnetState = TelnetState::NORMAL;
        return;
    } else if (byte == TEL_IAC) {
        // IAC start
        telnetState = TelnetState::IAC;
        return;
    }

    // ESC-prefixed alternates. Reason: macOS/BSD tty eats Ctrl-T (status),
    // Ctrl-O (discard) and Ctrl-R (rprnt) before telnet/nc can send them; ESC
    // passes through any tty unharmed. Mapping mirrors the Ctrl set:
    //   ESC T → toggle 8-bit     (escape hatch, works even in 8-bit mode)
    //   ESC O → toggle UC OUT
    //   ESC L → clear screen
    //   ESC R → reset Apple 1
    //   ESC I → toggle UC IN
    // ESC followed by anything else is forwarded verbatim (ESC then the byte)
    // so ANSI escape sequences intended for the Apple 1 still pass through.
    auto mapEscToCtrl = [](uint8_t b) -> uint8_t {
        switch (b) {
        case 'T': case 't': return 20; // Ctrl-T
        case 'O': case 'o': return 15; // Ctrl-O
        case 'L': case 'l': return 12; // Ctrl-L
        case 'R': case 'r': return 18; // Ctrl-R
        case 'I': case 'i': return 9;  // Ctrl-I
        default:            return 0;
        }
    };

    if (escapePending) {
        escapePending = false;
        uint8_t ctrl = mapEscToCtrl(byte);
        // Mapped ESC + letter is always a deliberate Terminal Card command (unlike
        // raw Ctrl-* in 8-bit mode, which must reach the Apple 1). Apply in both
        // modes so e.g. ESC I toggles UC IN when Ctrl-I is passed through raw.
        const bool applicable = (ctrl != 0);
        if (applicable) {
            handleControlCommand(ctrl);
            return;
        }
        // Unrecognised ESC-sequence: inject the swallowed ESC to the Apple 1
        // and let `byte` fall through to be processed normally just after.
        if (keyInjector) keyInjector(0x1B, eightBitMode);
    } else if (byte == 0x1B) {
        // Hold ESC until we see the next byte (command or passthrough).
        escapePending = true;
        return;
    }

    // Ctrl-T is the escape hatch: it must work in 8-bit mode too, otherwise the
    // user has no way back to 7-bit short of dropping the TCP connection.
    if (byte == 20) {
        handleControlCommand(20);
        return;
    }

    // Other control commands only bite in 7-bit mode — in 8-bit raw mode we
    // want every remaining byte (including Ctrl-L/R/O/I) to reach the Apple 1.
    if (!eightBitMode) {
        switch (byte) {
        case 9:   // CTRL-I: Toggle incoming uppercase
        case 12:  // CTRL-L: Clear screen
        case 15:  // CTRL-O: Toggle outgoing uppercase
        case 18:  // CTRL-R: Reset Apple 1
            handleControlCommand(byte);
            return;
        }
    }

    // Ignore bare LF — Apple 1 uses CR only
    if (byte == 10) return;

    char key = static_cast<char>(byte);

    if (eightBitMode) {
        // Raw injection, bypass uppercase conversion
        if (keyInjector) keyInjector(key, true);
    } else {
        // 7-bit mode
        key = static_cast<char>(byte & 0x7F);
        // Optional outgoing uppercase conversion (terminal → Apple 1)
        if (uppercaseOutgoing && key >= 'a' && key <= 'z') {
            key = key - 'a' + 'A';
        }
        if (keyInjector) keyInjector(key, false);
    }
}

void TerminalCard::handleControlCommand(uint8_t byte)
{
    switch (byte) {
    case 12: // CTRL-L: Clear screen
    {
        // Send ANSI clear sequence to terminal client
        const uint8_t clearSeq[] = { 0x1B, '[', '2', 'J', 0x1B, '[', 'H' };
        sendToClient(clearSeq, sizeof(clearSeq));
        // Request native screen clear (consumed by EmulationController)
        clearScreenPending.store(true);
        break;
    }
    case 18: // CTRL-R: Reset Apple 1
        // Request reset (consumed by EmulationController outside stateMutex)
        resetPending.store(true);
        break;

    case 15: // CTRL-O: Toggle outgoing uppercase (terminal → Apple 1)
        uppercaseOutgoing = !uppercaseOutgoing;
        {
            const char* msg = uppercaseOutgoing ?
                "\r\n[UC OUT: ON]\r\n" : "\r\n[UC OUT: OFF]\r\n";
            sendToClient(reinterpret_cast<const uint8_t*>(msg), strlen(msg));
        }
        break;

    case 9: // CTRL-I: Toggle incoming uppercase (Apple 1 → terminal)
        uppercaseIncoming = !uppercaseIncoming;
        {
            const char* msg = uppercaseIncoming ?
                "\r\n[UC IN: ON]\r\n" : "\r\n[UC IN: OFF]\r\n";
            sendToClient(reinterpret_cast<const uint8_t*>(msg), strlen(msg));
        }
        break;

    case 20: // CTRL-T: Toggle 8-bit mode
        eightBitMode = !eightBitMode;
        eightBitPendingCr = false;
        {
            const char* msg = eightBitMode ?
                "\r\n[8-BIT MODE]\r\n" : "\r\n[7-BIT MODE]\r\n";
            sendToClient(reinterpret_cast<const uint8_t*>(msg), strlen(msg));
        }
        break;
    }
}

// ─────────────────────────────────────────────────────────────
// Network operations — desktop only
// ─────────────────────────────────────────────────────────────

#if !POM1_IS_WASM

void TerminalCard::startServer()
{
    if (listenFd) return;  // already listening

    listenFd.reset(::socket(AF_INET, SOCK_STREAM, 0));
    if (!listenFd) {
        pom1::log().error("Term", "failed to create socket");
        return;
    }

    // Allow port reuse
    int optval = 1;
#ifdef _WIN32
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&optval), sizeof(optval));
#else
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
#endif

    // Bind to localhost only
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(listenPort);

    if (bind(listenFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        pom1::log().warn("Term", "failed to bind port " + std::to_string(listenPort) +
                         " (already in use?)");
        listenFd.reset();
        return;
    }

    if (listen(listenFd, 1) < 0) {
        pom1::log().error("Term", "listen failed");
        listenFd.reset();
        return;
    }

    // Set non-blocking
#ifdef _WIN32
    u_long nbMode = 1;
    ioctlsocket(listenFd, FIONBIO, &nbMode);
#else
    int flags = fcntl(listenFd, F_GETFL, 0);
    fcntl(listenFd, F_SETFL, flags | O_NONBLOCK);
#endif

    pom1::log().info("Term", "listening on localhost:" + std::to_string(listenPort));
}

void TerminalCard::stopServer()
{
    disconnectClient();
    listenFd.reset();
}

void TerminalCard::acceptClient()
{
    if (!listenFd) return;

    // Non-blocking check for pending connections
#ifdef _WIN32
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(listenFd, &readSet);
    struct timeval tv{0, 0};
    int sel = select(static_cast<int>(listenFd + 1), &readSet, nullptr, nullptr, &tv);
    if (sel <= 0) return;
#else
    struct pollfd pfd{};
    pfd.fd = listenFd;
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, 0);
    if (ret <= 0 || !(pfd.revents & POLLIN)) return;
#endif

    struct sockaddr_in clientAddr{};
    socklen_t addrLen = sizeof(clientAddr);
    NativeSocket newClient = ::accept(listenFd,
        reinterpret_cast<struct sockaddr*>(&clientAddr), &addrLen);

    if (newClient == kInvalidNativeSocket) return;

    // Drop existing client if any (closes the old FD via SocketHandle).
    clientFd.reset(newClient);
    telnetState = TelnetState::NORMAL;
    escapePending = false;
    eightBitPendingCr = false;

    // Set non-blocking
#ifdef _WIN32
    u_long nbMode = 1;
    ioctlsocket(clientFd, FIONBIO, &nbMode);
#else
    int flags = fcntl(clientFd, F_GETFL, 0);
    fcntl(clientFd, F_SETFL, flags | O_NONBLOCK);
#endif

    // Store client address
    clientAddress = inet_ntoa(clientAddr.sin_addr);
    clientAddress += ":" + std::to_string(ntohs(clientAddr.sin_port));

    // Push the client into character-at-a-time mode. Without this negotiation a
    // standard Linux/BSD telnet client stays in line mode, so control keys
    // (Ctrl-T, Ctrl-O, Ctrl-I, Ctrl-L, Ctrl-R) are buffered by the local tty
    // and never reach processIncomingByte().
    static constexpr uint8_t kInitialNegotiation[] = {
        TEL_IAC, TEL_WILL, TEL_OPT_ECHO,  // we echo — client disables local echo
        TEL_IAC, TEL_WILL, TEL_OPT_SGA,   // suppress GA from our side
        TEL_IAC, TEL_DO,   TEL_OPT_SGA,   // ask the client to suppress GA too
    };
    sendToClient(kInitialNegotiation, sizeof(kInitialNegotiation));

    // Send welcome message
    const char* welcome = "\r\nPOM1 - P-LAB Terminal Card\r\n\r\n";
    sendToClient(reinterpret_cast<const uint8_t*>(welcome), strlen(welcome));

    pom1::log().info("Term", "client connected from " + clientAddress);
}

void TerminalCard::disconnectClient()
{
    if (clientFd) {
        clientFd.reset();
        clientAddress.clear();
        pom1::log().info("Term", "client disconnected");
    }
}

void TerminalCard::pollClient()
{
    if (!clientFd) return;

#ifdef _WIN32
    fd_set readSet, errorSet;
    FD_ZERO(&readSet); FD_ZERO(&errorSet);
    FD_SET(clientFd, &readSet);
    FD_SET(clientFd, &errorSet);
    struct timeval tv{0, 0};
    int sel = select(static_cast<int>(clientFd + 1), &readSet, nullptr, &errorSet, &tv);
    if (sel <= 0) return;

    if (FD_ISSET(clientFd, &errorSet)) {
        disconnectClient();
        return;
    }

    if (FD_ISSET(clientFd, &readSet)) {
        char buf[256];
        int n = recv(clientFd, buf, sizeof(buf), 0);
        if (n <= 0) {
            disconnectClient();
            return;
        }
        for (int i = 0; i < n; i++) {
            processIncomingByte(static_cast<uint8_t>(buf[i]));
        }
    }

#else // POSIX
    struct pollfd pfd{};
    pfd.fd = clientFd;
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, 0);
    if (ret <= 0) return;

    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        disconnectClient();
        return;
    }

    if (pfd.revents & POLLIN) {
        uint8_t buf[256];
        ssize_t n = recv(clientFd, buf, sizeof(buf), 0);
        if (n <= 0) {
            disconnectClient();
            return;
        }
        for (ssize_t i = 0; i < n; i++) {
            processIncomingByte(buf[i]);
        }
    }
#endif
}

void TerminalCard::sendToClient(uint8_t byte)
{
    sendToClient(&byte, 1);
}

void TerminalCard::sendToClient(const uint8_t* data, size_t len)
{
    if (!clientFd || len == 0) return;

#ifdef _WIN32
    ::send(clientFd, reinterpret_cast<const char*>(data), static_cast<int>(len), 0);
#else
    ::send(clientFd, data, len, MSG_NOSIGNAL);
#endif
    bytesSentCount += static_cast<uint32_t>(len);
}

#else // POM1_IS_WASM — no networking

void TerminalCard::startServer() {}
void TerminalCard::stopServer() {}
void TerminalCard::acceptClient() {}
void TerminalCard::disconnectClient() { clientFd.reset(); clientAddress.clear(); }
void TerminalCard::pollClient() {}
void TerminalCard::sendToClient(uint8_t) {}
void TerminalCard::sendToClient(const uint8_t*, size_t) {}

#endif // POM1_IS_WASM
