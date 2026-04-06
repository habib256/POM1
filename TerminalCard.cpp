// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// P-LAB Apple-1 Terminal Card emulation
// Bidirectional serial bridge: TCP server on localhost
// https://p-l4b.github.io/terminal/

#include "TerminalCard.h"
#include <cstring>
#include <iostream>

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
    if (clientFd == kTermInvalidSocket) return;

    if (eightBitMode) {
        // 8-bit raw pass-through — no filtering
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
    if (pollCycleAccum < 1000) return;  // ~1ms at 1 MHz
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
    out.serverListening = (listenFd != kTermInvalidSocket);
    out.clientConnected = (clientFd != kTermInvalidSocket);
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
        // Respond: WILL→DONT, DO→WONT
        uint8_t response[3] = { 255, 0, byte };
        if (telnetVerb == 251 || telnetVerb == 253) {
            // WILL or DO → respond with DONT or WONT
            response[1] = (telnetVerb == 251) ? 254 : 252; // DONT or WONT
        } else {
            // WONT or DONT → just acknowledge, no response needed
            telnetState = TelnetState::NORMAL;
            return;
        }
        sendToClient(response, 3);
        telnetState = TelnetState::NORMAL;
        return;
    } else if (byte == 255) {
        // IAC start
        telnetState = TelnetState::IAC;
        return;
    }

    // Control commands (only in 7-bit mode)
    if (!eightBitMode) {
        switch (byte) {
        case 9:   // CTRL-I: Toggle incoming uppercase
        case 12:  // CTRL-L: Clear screen
        case 15:  // CTRL-O: Toggle outgoing uppercase
        case 18:  // CTRL-R: Reset Apple 1
        case 20:  // CTRL-T: Toggle 8-bit mode
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
    if (listenFd != kTermInvalidSocket) return;  // already listening

    listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd == kTermInvalidSocket) {
        std::cout << "Terminal Card: failed to create socket" << std::endl;
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
        std::cout << "Terminal Card: failed to bind port " << listenPort
                  << " (already in use?)" << std::endl;
#ifdef _WIN32
        closesocket(listenFd);
#else
        close(listenFd);
#endif
        listenFd = kTermInvalidSocket;
        return;
    }

    if (listen(listenFd, 1) < 0) {
        std::cout << "Terminal Card: listen failed" << std::endl;
#ifdef _WIN32
        closesocket(listenFd);
#else
        close(listenFd);
#endif
        listenFd = kTermInvalidSocket;
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

    std::cout << "Terminal Card: listening on localhost:" << listenPort << std::endl;
}

void TerminalCard::stopServer()
{
    disconnectClient();
    if (listenFd != kTermInvalidSocket) {
#ifdef _WIN32
        closesocket(listenFd);
#else
        close(listenFd);
#endif
        listenFd = kTermInvalidSocket;
    }
}

void TerminalCard::acceptClient()
{
    if (listenFd == kTermInvalidSocket) return;

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
    TermSocketFd newClient = accept(listenFd,
        reinterpret_cast<struct sockaddr*>(&clientAddr), &addrLen);

    if (newClient == kTermInvalidSocket) return;

    // Drop existing client if any
    if (clientFd != kTermInvalidSocket) {
        disconnectClient();
    }

    clientFd = newClient;
    telnetState = TelnetState::NORMAL;

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

    // Send welcome message
    const char* welcome = "\r\nPOM1 - P-LAB Terminal Card\r\n\r\n";
    sendToClient(reinterpret_cast<const uint8_t*>(welcome), strlen(welcome));

    std::cout << "Terminal Card: client connected from " << clientAddress << std::endl;
}

void TerminalCard::disconnectClient()
{
    if (clientFd != kTermInvalidSocket) {
#ifdef _WIN32
        closesocket(clientFd);
#else
        close(clientFd);
#endif
        clientFd = kTermInvalidSocket;
        clientAddress.clear();
        std::cout << "Terminal Card: client disconnected" << std::endl;
    }
}

void TerminalCard::pollClient()
{
    if (clientFd == kTermInvalidSocket) return;

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
    if (clientFd == kTermInvalidSocket || len == 0) return;

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
void TerminalCard::disconnectClient() { clientFd = kTermInvalidSocket; clientAddress.clear(); }
void TerminalCard::pollClient() {}
void TerminalCard::sendToClient(uint8_t) {}
void TerminalCard::sendToClient(const uint8_t*, size_t) {}

#endif // POM1_IS_WASM
