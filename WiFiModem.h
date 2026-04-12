// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// P-LAB Apple-1 Wi-Fi Modem emulation
// 65C51 ACIA + ESP8266 AT command interpreter + TCP/TELNET
// https://p-l4b.github.io/wifi/

#ifndef WIFIMODEM_H
#define WIFIMODEM_H

#include "CpuClock.h"
#include "POM1Build.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <string>

// Networking on desktop only: WASM builds short-circuit to NO CARRIER because
// browsers cannot open raw TCP sockets (see WiFiModem.cpp for the rationale).
#if !POM1_IS_WASM
  #ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using SocketFd = SOCKET;
    static constexpr SocketFd kInvalidSocket = INVALID_SOCKET;
  #else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <poll.h>
    using SocketFd = int;
    static constexpr SocketFd kInvalidSocket = -1;
  #endif
#else
  using SocketFd = int;
  static constexpr SocketFd kInvalidSocket = -1;
#endif

/// P-LAB Apple-1 Wi-Fi Modem — 65C51 ACIA + ESP8266 modem emulation.
/// ACIA I/O at $B000-$B003 (4 registers).
/// AT command interpreter with TELNET client for BBS connections.
class WiFiModem
{
public:
    static constexpr uint16_t kAciaBase = 0xB000;
    static constexpr uint16_t kAciaEnd  = 0xB003;

    WiFiModem();
    ~WiFiModem();

    void reset();

    // ACIA register interface (called from Memory::memRead / memWrite)
    uint8_t readRegister(uint16_t address);
    void    writeRegister(uint16_t address, uint8_t value);

    // Cycle counting for baud rate simulation and socket polling
    void advanceCycles(int cycles);

    // UI-initiated disconnect — equivalent to the user typing +++ATH from the terminal.
    // Thread-safe: takes the modem mutex internally so it can be called from the UI thread.
    void requestDisconnect();

    // Snapshot for UI display
    struct Snapshot {
        uint8_t statusReg = 0;
        uint8_t commandReg = 0;
        uint8_t controlReg = 0;
        bool connected = false;
        bool echoEnabled = true;
        std::string remoteHost;
        uint16_t remotePort = 0;
        uint32_t bytesSent = 0;
        uint32_t bytesReceived = 0;
        int baudRate = 9600;
    };
    void copySnapshot(Snapshot& out) const;

private:
    // --- ACIA 65C51 register offsets ---
    static constexpr uint16_t REG_DATA    = 0; // +0 Data Register
    static constexpr uint16_t REG_STATUS  = 1; // +1 Status Register (read-only)
    static constexpr uint16_t REG_COMMAND = 2; // +2 Command Register (write-only)
    static constexpr uint16_t REG_CONTROL = 3; // +3 Control Register (write-only)

    // --- Status register bits (W65C51N datasheet) ---
    static constexpr uint8_t ST_PARITY_ERR  = 0x01; // bit 0
    static constexpr uint8_t ST_FRAMING_ERR = 0x02; // bit 1
    static constexpr uint8_t ST_OVERRUN     = 0x04; // bit 2
    static constexpr uint8_t ST_RDRF        = 0x08; // bit 3 — Receiver Data Register Full
    static constexpr uint8_t ST_TDRE        = 0x10; // bit 4 — Transmitter Data Register Empty
    static constexpr uint8_t ST_DCD         = 0x20; // bit 5 — Data Carrier Detect (active low)
    static constexpr uint8_t ST_DSR         = 0x40; // bit 6 — Data Set Ready (active low)
    static constexpr uint8_t ST_IRQ         = 0x80; // bit 7

    // --- Command register bits ---
    static constexpr uint8_t CMD_DTR        = 0x01; // bit 0 — Data Terminal Ready
    static constexpr uint8_t CMD_IRQ_DIS    = 0x02; // bit 1 — IRQ disable for receiver

    // --- ACIA registers ---
    uint8_t dataRegRx  = 0;   // last received byte (read by CPU)
    uint8_t commandReg = 0;   // command register
    uint8_t controlReg = 0;   // control register
    bool    rdrfFlag   = false; // Receiver Data Register Full

    // --- Receive circular buffer ---
    static constexpr size_t RX_BUFFER_SIZE = 4096;
    std::array<uint8_t, RX_BUFFER_SIZE> rxBuffer{};
    size_t rxHead = 0;
    size_t rxTail = 0;
    size_t rxCount = 0;

    // --- Baud rate emulation ---
    int  cyclesPerByte = 1065;  // overwritten in reset() — ~9600 baud @ POM1_CPU_CLOCK_HZ
    int  rxCycleAccum  = 0;     // cycles until next rx byte loads into data register
    int  pollCycleAccum = 0;    // cycles until next socket poll

    // --- Modem mode ---
    enum class ModemMode { COMMAND, DATA };
    ModemMode mode = ModemMode::COMMAND;

    // --- AT command parser ---
    std::string atCmdBuffer;
    bool echoEnabled = true;

    // --- Escape sequence (+++) detection ---
    int  escapeCount = 0;       // consecutive '+' chars received
    int  escapeGuardCycles = 0; // guard time counter (1 s = POM1_CPU_CLOCK_HZ cycles)
    bool escapeArmed = false;   // true after guard time before +++
    static constexpr int ESCAPE_GUARD_CYCLES = POM1_CPU_CLOCK_HZ;

    // --- Connection state ---
    enum class ConnState { IDLE, CONNECTING, CONNECTED };
    ConnState connState = ConnState::IDLE;
    std::string remoteHost;
    uint16_t    remotePort = 0;
    uint32_t    bytesSentCount = 0;
    uint32_t    bytesReceivedCount = 0;

    // --- Network socket ---
    SocketFd socketFd = kInvalidSocket;

    // --- TELNET IAC state ---
    enum class TelnetState { NORMAL, IAC, WILL_WONT_DO_DONT, SB, SB_IAC };
    TelnetState telnetState = TelnetState::NORMAL;
    uint8_t telnetVerb = 0;
    bool    lastByteWasCR = false; // CR+LF → CR stripping (Apple 1 uses CR only)

    // --- Thread safety ---
    mutable std::mutex modemMutex;

    // --- Internal helpers ---
    void enqueueRxByte(uint8_t byte);
    void enqueueRxString(const char* str);
    bool loadNextRxByte();

    void processTransmittedByte(uint8_t byte);
    void processCommandByte(uint8_t byte);
    void processDataByte(uint8_t byte);
    void executeATCommand(const std::string& cmd);

    void handleATDT(const std::string& args);
    void handleATH();

    // Network operations
    void sendToSocket(uint8_t byte);
    void connectToHost(const std::string& host, uint16_t port);
    void disconnect();
    void updateConnection();
    void processTelnetByte(uint8_t byte);

    // Baud rate
    int  decodeBaudRate() const;
    void updateCyclesPerByte();

#ifdef _WIN32
    static bool winsockInitialized;
    static void initWinsock();
#endif
};

#endif // WIFIMODEM_H
