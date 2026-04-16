// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// P-LAB Apple-1 Terminal Card emulation
// Bidirectional serial bridge: TCP server on localhost
// https://p-l4b.github.io/terminal/

#ifndef TERMINALCARD_H
#define TERMINALCARD_H

#include "POM1Build.h"
#include "SocketHandle.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

// SocketHandle.h already pulls the platform socket headers; desktop-only extras
// (addrinfo, poll, fcntl) are added here.
#if !POM1_IS_WASM
  #ifdef _WIN32
    #include <ws2tcpip.h>
  #else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <fcntl.h>
    #include <poll.h>
  #endif
#endif

/// P-LAB Apple-1 Terminal Card — bidirectional serial bridge.
/// Passive device: eavesdrops on $D012 display writes, injects keys into $D010.
/// Exposes a TCP server on localhost for external terminal connection.
class TerminalCard
{
public:
    static constexpr uint16_t kDefaultPort = 6502;

    TerminalCard();
    ~TerminalCard();

    void reset();

    // Called from Memory::memWrite when address == 0xD012
    // Receives the RAW value (before & 0x7F) so 8-bit mode works
    void onDisplayWrite(uint8_t rawValue);

    // Called from Memory::advanceCycles to poll for incoming TCP data
    void advanceCycles(int cycles);

    // Callback to inject a key into the Apple 1 keyboard
    // bool raw: true = bypass forced uppercase conversion
    using KeyInjector = std::function<void(char key, bool raw)>;
    void setKeyInjector(KeyInjector injector);

    // Thread-safe pending flags for actions that can't run inside advanceCycles
    std::atomic<bool> resetPending{false};
    std::atomic<bool> hardResetPending{false};
    std::atomic<bool> clearScreenPending{false};
    bool consumeResetPending() { return resetPending.exchange(false); }
    bool consumeHardResetPending() { return hardResetPending.exchange(false); }
    bool consumeClearScreenPending() { return clearScreenPending.exchange(false); }

    // Snapshot for UI display (thread-safe)
    struct Snapshot {
        bool serverListening = false;
        bool clientConnected = false;
        std::string clientAddress;
        uint16_t listenPort = kDefaultPort;
        bool uppercaseOutgoing = true;
        bool uppercaseIncoming = false;
        bool eightBitMode = false;
        uint32_t bytesSent = 0;
        uint32_t bytesReceived = 0;
    };
    void copySnapshot(Snapshot& out) const;

private:
    // Mode state (faithful to real firmware defaults)
    bool uppercaseOutgoing = true;    // CTRL-O: ON by default (Apple 1 convention)
    bool uppercaseIncoming = false;   // CTRL-I: OFF by default
    bool eightBitMode = false;        // CTRL-T: OFF by default
    /// 8-bit display: saw CR — emit CRLF when LF arrives or on next real char.
    bool eightBitPendingCr = false;

    // Network — server socket + single client
    SocketHandle listenFd;
    SocketHandle clientFd;
    uint16_t listenPort = kDefaultPort;
    std::string clientAddress;

    // Statistics
    uint32_t bytesSentCount = 0;
    uint32_t bytesReceivedCount = 0;

    // Cycle accumulator for polling (~1ms at 1 MHz)
    int pollCycleAccum = 0;

    // TELNET IAC state machine
    enum class TelnetState { NORMAL, IAC, VERB };
    TelnetState telnetState = TelnetState::NORMAL;
    uint8_t telnetVerb = 0;

    // ESC-prefixed control alternates (workaround for macOS/BSD tty eating
    // Ctrl-T/Ctrl-O/Ctrl-R). True once an 0x1B has been received and we're
    // waiting for the next byte to decide intercept-vs-forward.
    bool escapePending = false;

    // Thread safety
    mutable std::mutex cardMutex;

    // Callback
    KeyInjector keyInjector;

    // Internal helpers
    void startServer();
    void stopServer();
    void acceptClient();
    void disconnectClient();
    void pollClient();
    void sendToClient(const uint8_t* data, size_t len);
    void sendToClient(uint8_t byte);
    void processIncomingByte(uint8_t byte);
    void handleControlCommand(uint8_t byte);

#if !POM1_IS_WASM && defined(_WIN32)
    static bool winsockInitialized;
    static void initWinsock();
#endif
};

#endif // TERMINALCARD_H
