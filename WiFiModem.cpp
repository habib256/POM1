// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// P-LAB Apple-1 Wi-Fi Modem emulation
// 65C51 ACIA + ESP8266 AT command interpreter + TCP/TELNET
// https://p-l4b.github.io/wifi/

#include "WiFiModem.h"
#include <cstring>
#include <iostream>

#if !POM1_IS_WASM && defined(_WIN32)
  #pragma comment(lib, "ws2_32.lib")
  bool WiFiModem::winsockInitialized = false;
  void WiFiModem::initWinsock()
  {
      if (!winsockInitialized) {
          WSADATA wsaData;
          WSAStartup(MAKEWORD(2, 2), &wsaData);
          winsockInitialized = true;
      }
  }
#endif

// ─────────────────────────────────────────────────────────────
// Construction / destruction
// ─────────────────────────────────────────────────────────────

WiFiModem::WiFiModem()
{
#if !POM1_IS_WASM && defined(_WIN32)
    initWinsock();
#endif
    reset();
}

WiFiModem::~WiFiModem()
{
    disconnect();
}

void WiFiModem::reset()
{
    std::lock_guard<std::mutex> lock(modemMutex);

    disconnect();

    dataRegRx  = 0;
    commandReg = 0;
    controlReg = 0x1E; // default: 9600 baud, 8N1
    rdrfFlag   = false;

    rxHead = rxTail = rxCount = 0;

    updateCyclesPerByte();
    rxCycleAccum  = 0;
    pollCycleAccum = 0;

    mode = ModemMode::COMMAND;
    atCmdBuffer.clear();
    echoEnabled = true;

    escapeCount = 0;
    escapeGuardCycles = 0;
    escapeArmed = false;

    connState = ConnState::IDLE;
    remoteHost.clear();
    remotePort = 0;
    bytesSentCount = 0;
    bytesReceivedCount = 0;

    telnetState = TelnetState::NORMAL;
    telnetVerb = 0;
    lastByteWasCR = false;
}

// ─────────────────────────────────────────────────────────────
// ACIA register interface
// ─────────────────────────────────────────────────────────────

uint8_t WiFiModem::readRegister(uint16_t address)
{
    std::lock_guard<std::mutex> lock(modemMutex);
    uint16_t reg = address - kAciaBase;

    switch (reg) {
    case REG_DATA: {
        // Reading data register returns received byte and clears RDRF
        uint8_t val = dataRegRx;
        rdrfFlag = false;
        // Load next byte from buffer if available
        loadNextRxByte();
        return val;
    }
    case REG_STATUS: {
        // Build status register dynamically
        uint8_t status = 0;
        // TDRE: always 1 (W65C51N hardware bug — transmitter always reports ready)
        status |= ST_TDRE;
        // RDRF: set when data register has a byte
        if (rdrfFlag) status |= ST_RDRF;
        // DCD: active low — bit 5 = 0 when connected, 1 when disconnected
        if (connState != ConnState::CONNECTED) status |= ST_DCD;
        // DSR: always active (low) — modem is always ready
        // (bit 6 = 0)
        return status;
    }
    case REG_COMMAND:
        return commandReg;
    case REG_CONTROL:
        return controlReg;
    default:
        return 0;
    }
}

void WiFiModem::writeRegister(uint16_t address, uint8_t value)
{
    std::lock_guard<std::mutex> lock(modemMutex);
    uint16_t reg = address - kAciaBase;

    switch (reg) {
    case REG_DATA:
        processTransmittedByte(value);
        break;
    case REG_STATUS:
        // Writing to status register is a programmed reset (W65C51N)
        commandReg = 0;
        break;
    case REG_COMMAND:
        commandReg = value;
        break;
    case REG_CONTROL:
        controlReg = value;
        updateCyclesPerByte();
        break;
    }
}

// ─────────────────────────────────────────────────────────────
// Cycle advancement — baud rate timing + socket polling
// ─────────────────────────────────────────────────────────────

void WiFiModem::advanceCycles(int cycles)
{
    std::lock_guard<std::mutex> lock(modemMutex);

    // Advance rx timing — load next byte from buffer to data register
    if (!rdrfFlag && rxCount > 0) {
        rxCycleAccum -= cycles;
        if (rxCycleAccum <= 0) {
            loadNextRxByte();
        }
    }

    // Poll socket periodically (~1 ms at CPU clock)
    pollCycleAccum += cycles;
    if (pollCycleAccum >= POM1_CPU_CYCLES_PER_MILLISECOND) {
        pollCycleAccum = 0;
        if (connState == ConnState::CONNECTED || connState == ConnState::CONNECTING) {
            updateConnection();
        }
    }

    // Escape sequence guard time
    if (escapeCount > 0 && escapeCount < 3) {
        escapeGuardCycles += cycles;
        if (escapeGuardCycles > ESCAPE_GUARD_CYCLES) {
            // Timeout — partial escape sequence, send buffered '+' chars as data
            escapeCount = 0;
            escapeGuardCycles = 0;
            escapeArmed = false;
        }
    }
    if (escapeCount >= 3) {
        escapeGuardCycles += cycles;
        if (escapeGuardCycles >= ESCAPE_GUARD_CYCLES) {
            // +++ with guard time after — return to command mode
            escapeCount = 0;
            escapeGuardCycles = 0;
            escapeArmed = false;
            mode = ModemMode::COMMAND;
            atCmdBuffer.clear();
            enqueueRxString("\r\nOK\r\n");
        }
    }
}

// ─────────────────────────────────────────────────────────────
// Receive buffer management
// ─────────────────────────────────────────────────────────────

void WiFiModem::enqueueRxByte(uint8_t byte)
{
    if (rxCount >= RX_BUFFER_SIZE) return; // drop if full
    rxBuffer[rxHead] = byte;
    rxHead = (rxHead + 1) % RX_BUFFER_SIZE;
    rxCount++;
}

void WiFiModem::enqueueRxString(const char* str)
{
    while (*str) {
        enqueueRxByte(static_cast<uint8_t>(*str++));
    }
}

bool WiFiModem::loadNextRxByte()
{
    if (rxCount == 0) return false;
    if (rdrfFlag) return false; // data register already full

    dataRegRx = rxBuffer[rxTail];
    rxTail = (rxTail + 1) % RX_BUFFER_SIZE;
    rxCount--;
    rdrfFlag = true;
    rxCycleAccum = cyclesPerByte; // enforce baud rate delay for next byte
    return true;
}

// ─────────────────────────────────────────────────────────────
// Transmit path
// ─────────────────────────────────────────────────────────────

void WiFiModem::processTransmittedByte(uint8_t byte)
{
    if (mode == ModemMode::COMMAND) {
        processCommandByte(byte);
    } else {
        processDataByte(byte);
    }
}

void WiFiModem::processCommandByte(uint8_t byte)
{
    char ch = static_cast<char>(byte & 0x7F); // strip bit 7

    // Echo if enabled
    if (echoEnabled) {
        enqueueRxByte(byte & 0x7F);
    }

    if (ch == '\r' || ch == '\n') {
        // Execute accumulated command
        if (!atCmdBuffer.empty()) {
            executeATCommand(atCmdBuffer);
            atCmdBuffer.clear();
        }
    } else if (ch == '\b' || ch == 0x7F) {
        // Backspace
        if (!atCmdBuffer.empty()) {
            atCmdBuffer.pop_back();
        }
    } else {
        atCmdBuffer += ch;
    }
}

void WiFiModem::processDataByte(uint8_t byte)
{
    // Check for +++ escape sequence
    char ch = static_cast<char>(byte & 0x7F);
    if (ch == '+') {
        if (escapeCount == 0) {
            escapeArmed = true;
        }
        escapeCount++;
        escapeGuardCycles = 0;
        if (escapeCount >= 3) {
            // Don't send the '+' chars — wait for guard time
            return;
        }
        return; // buffer '+', don't send yet
    } else {
        // Not a '+' — send any buffered '+' chars
        if (escapeCount > 0) {
            for (int i = 0; i < escapeCount && i < 3; i++) {
                sendToSocket('+');
            }
            escapeCount = 0;
            escapeGuardCycles = 0;
            escapeArmed = false;
        }
    }

    sendToSocket(byte & 0x7F);
}

// ─────────────────────────────────────────────────────────────
// AT command parser
// ─────────────────────────────────────────────────────────────

void WiFiModem::executeATCommand(const std::string& cmd)
{
    // Convert to uppercase for matching
    std::string upper;
    upper.reserve(cmd.size());
    for (char c : cmd) upper += static_cast<char>(toupper(c));

    // Must start with "AT"
    if (upper.size() < 2 || upper.substr(0, 2) != "AT") {
        enqueueRxString("\r\nERROR\r\n");
        return;
    }

    // "AT" alone
    if (upper.size() == 2) {
        enqueueRxString("\r\nOK\r\n");
        return;
    }

    std::string rest = upper.substr(2);

    if (rest == "E0") {
        echoEnabled = false;
        enqueueRxString("\r\nOK\r\n");
    } else if (rest == "E1") {
        echoEnabled = true;
        enqueueRxString("\r\nOK\r\n");
    } else if (rest == "I" || rest == "I0") {
        enqueueRxString("\r\nP-LAB APPLE-1 WI-FI MODEM\r\n");
        enqueueRxString("POM1 EMULATION V1.0\r\n");
        enqueueRxString("OK\r\n");
    } else if (rest == "H" || rest == "H0") {
        handleATH();
    } else if (rest.substr(0, 2) == "DT") {
        // ATDT host:port — use original case for hostname
        std::string args = cmd.substr(4); // skip "ATDT" in original case
        handleATDT(args);
    } else if (rest[0] == 'S') {
        // ATS registers — accept and acknowledge (not functionally used)
        enqueueRxString("\r\nOK\r\n");
    } else if (rest == "Z") {
        // ATZ — reset modem
        handleATH();
        echoEnabled = true;
        controlReg = 0x1E;
        updateCyclesPerByte();
        enqueueRxString("\r\nOK\r\n");
    } else {
        enqueueRxString("\r\nERROR\r\n");
    }
}

void WiFiModem::handleATDT(const std::string& args)
{
    // Parse host:port
    std::string host;
    uint16_t port = 23; // default TELNET port

    // Trim whitespace
    std::string trimmed = args;
    while (!trimmed.empty() && trimmed[0] == ' ') trimmed.erase(0, 1);
    while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();

    if (trimmed.empty()) {
        enqueueRxString("\r\nERROR\r\n");
        return;
    }

    size_t colonPos = trimmed.rfind(':');
    if (colonPos != std::string::npos && colonPos < trimmed.size() - 1) {
        host = trimmed.substr(0, colonPos);
        try {
            port = static_cast<uint16_t>(std::stoi(trimmed.substr(colonPos + 1)));
        } catch (...) {
            enqueueRxString("\r\nERROR\r\n");
            return;
        }
    } else {
        host = trimmed;
    }

    if (host.empty()) {
        enqueueRxString("\r\nERROR\r\n");
        return;
    }

    remoteHost = host;
    remotePort = port;
    bytesSentCount = 0;
    bytesReceivedCount = 0;

    connectToHost(host, port);
}

void WiFiModem::handleATH()
{
    if (connState != ConnState::IDLE) {
        disconnect();
        enqueueRxString("\r\nNO CARRIER\r\n");
    } else {
        enqueueRxString("\r\nOK\r\n");
    }
    mode = ModemMode::COMMAND;
}

void WiFiModem::requestDisconnect()
{
    std::lock_guard<std::mutex> lock(modemMutex);
    handleATH();
}

// ─────────────────────────────────────────────────────────────
// Network operations
//
// Desktop builds use real BSD sockets (POSIX or Winsock).
// WASM builds short-circuit to NO CARRIER: browsers cannot open raw TCP
// sockets — Emscripten can only proxy through WebSockets, which BBSes don't
// speak natively. Reaching a real BBS from the browser requires running a
// WebSocket-to-TCP bridge such as `websockify` next to the user.
// ─────────────────────────────────────────────────────────────

#if !POM1_IS_WASM

void WiFiModem::sendToSocket(uint8_t byte)
{
    if (socketFd == kInvalidSocket) return;

#ifdef _WIN32
    char buf = static_cast<char>(byte);
    ::send(socketFd, &buf, 1, 0);
#else
    uint8_t buf = byte;
    ::send(socketFd, &buf, 1, MSG_NOSIGNAL);
#endif
    bytesSentCount++;
}

void WiFiModem::connectToHost(const std::string& host, uint16_t port)
{
    // Resolve hostname
    struct addrinfo hints{}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string portStr = std::to_string(port);
    int rc = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result);
    if (rc != 0 || !result) {
        if (result) freeaddrinfo(result);
        enqueueRxString("\r\nNO CARRIER\r\n");
        return;
    }

    // Create socket
    socketFd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (socketFd == kInvalidSocket) {
        freeaddrinfo(result);
        enqueueRxString("\r\nNO CARRIER\r\n");
        return;
    }

    // Set non-blocking
#ifdef _WIN32
    u_long nbMode = 1;
    ioctlsocket(socketFd, FIONBIO, &nbMode);
#else
    int flags = fcntl(socketFd, F_GETFL, 0);
    fcntl(socketFd, F_SETFL, flags | O_NONBLOCK);
#endif

    // Initiate connection
    rc = ::connect(socketFd, result->ai_addr, static_cast<int>(result->ai_addrlen));
    freeaddrinfo(result);

#ifdef _WIN32
    if (rc == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
        closesocket(socketFd);
        socketFd = kInvalidSocket;
        enqueueRxString("\r\nNO CARRIER\r\n");
        return;
    }
#else
    if (rc < 0 && errno != EINPROGRESS) {
        close(socketFd);
        socketFd = kInvalidSocket;
        enqueueRxString("\r\nNO CARRIER\r\n");
        return;
    }
#endif

    connState = ConnState::CONNECTING;
    telnetState = TelnetState::NORMAL;

    std::cout << "WiFi Modem: connecting to " << host << ":" << port << std::endl;
}

void WiFiModem::disconnect()
{
    if (socketFd != kInvalidSocket) {
#ifdef _WIN32
        closesocket(socketFd);
#else
        close(socketFd);
#endif
        socketFd = kInvalidSocket;
    }
    connState = ConnState::IDLE;
    telnetState = TelnetState::NORMAL;
}

void WiFiModem::updateConnection()
{
    if (socketFd == kInvalidSocket) return;

#ifdef _WIN32
    fd_set readSet, writeSet, errorSet;
    FD_ZERO(&readSet); FD_ZERO(&writeSet); FD_ZERO(&errorSet);
    FD_SET(socketFd, &readSet);
    FD_SET(socketFd, &errorSet);
    if (connState == ConnState::CONNECTING)
        FD_SET(socketFd, &writeSet);

    struct timeval tv{0, 0}; // non-blocking
    int sel = select(static_cast<int>(socketFd + 1), &readSet, &writeSet, &errorSet, &tv);
    if (sel <= 0) return;

    if (FD_ISSET(socketFd, &errorSet)) {
        disconnect();
        enqueueRxString("\r\nNO CARRIER\r\n");
        mode = ModemMode::COMMAND;
        return;
    }

    // Check connection completion
    if (connState == ConnState::CONNECTING && FD_ISSET(socketFd, &writeSet)) {
        int err = 0;
        int errLen = sizeof(err);
        getsockopt(socketFd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &errLen);
        if (err != 0) {
            disconnect();
            enqueueRxString("\r\nNO CARRIER\r\n");
            mode = ModemMode::COMMAND;
            return;
        }
        connState = ConnState::CONNECTED;
        mode = ModemMode::DATA;
        int baud = decodeBaudRate();
        std::string connectMsg = "\r\nCONNECT " + std::to_string(baud) + "\r\n";
        enqueueRxString(connectMsg.c_str());
        std::cout << "WiFi Modem: connected to " << remoteHost << ":" << remotePort << std::endl;
    }

    // Read incoming data
    if (connState == ConnState::CONNECTED && FD_ISSET(socketFd, &readSet)) {
        char buf[256];
        int n = recv(socketFd, buf, sizeof(buf), 0);
        if (n <= 0) {
            disconnect();
            enqueueRxString("\r\nNO CARRIER\r\n");
            mode = ModemMode::COMMAND;
            return;
        }
        for (int i = 0; i < n; i++) {
            processTelnetByte(static_cast<uint8_t>(buf[i]));
        }
    }

#else // POSIX
    struct pollfd pfd{};
    pfd.fd = socketFd;
    pfd.events = POLLIN;
    if (connState == ConnState::CONNECTING)
        pfd.events |= POLLOUT;

    int ret = poll(&pfd, 1, 0); // non-blocking
    if (ret <= 0) return;

    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        disconnect();
        enqueueRxString("\r\nNO CARRIER\r\n");
        mode = ModemMode::COMMAND;
        return;
    }

    // Check connection completion
    if (connState == ConnState::CONNECTING && (pfd.revents & POLLOUT)) {
        int err = 0;
        socklen_t errLen = sizeof(err);
        getsockopt(socketFd, SOL_SOCKET, SO_ERROR, &err, &errLen);
        if (err != 0) {
            disconnect();
            enqueueRxString("\r\nNO CARRIER\r\n");
            mode = ModemMode::COMMAND;
            return;
        }
        connState = ConnState::CONNECTED;
        mode = ModemMode::DATA;
        int baud = decodeBaudRate();
        std::string connectMsg = "\r\nCONNECT " + std::to_string(baud) + "\r\n";
        enqueueRxString(connectMsg.c_str());
        std::cout << "WiFi Modem: connected to " << remoteHost << ":" << remotePort << std::endl;
    }

    // Read incoming data
    if (connState == ConnState::CONNECTED && (pfd.revents & POLLIN)) {
        uint8_t buf[256];
        ssize_t n = recv(socketFd, buf, sizeof(buf), 0);
        if (n <= 0) {
            disconnect();
            enqueueRxString("\r\nNO CARRIER\r\n");
            mode = ModemMode::COMMAND;
            return;
        }
        for (ssize_t i = 0; i < n; i++) {
            processTelnetByte(buf[i]);
        }
    }
#endif
}

#else // POM1_IS_WASM — browsers cannot open raw TCP sockets

void WiFiModem::sendToSocket(uint8_t) {}

void WiFiModem::connectToHost(const std::string&, uint16_t)
{
    enqueueRxString("\r\nNO CARRIER\r\n");
    enqueueRxString("(WEB BUILD: BBS DIALING REQUIRES A LOCAL\r\n");
    enqueueRxString(" WEBSOCKET-TO-TCP BRIDGE - SEE ABOUT BOX)\r\n");
}

void WiFiModem::disconnect()
{
    connState = ConnState::IDLE;
    socketFd = kInvalidSocket;
}

void WiFiModem::updateConnection() {}

#endif // POM1_IS_WASM

// ─────────────────────────────────────────────────────────────
// TELNET protocol — filter IAC sequences
// ─────────────────────────────────────────────────────────────

void WiFiModem::processTelnetByte(uint8_t byte)
{
    switch (telnetState) {
    case TelnetState::NORMAL:
        if (byte == 0xFF) { // IAC
            lastByteWasCR = false;
            telnetState = TelnetState::IAC;
        } else if (byte == 0x00 && lastByteWasCR) {
            // TELNET CR-NUL → literal CR already sent, drop the NUL
            lastByteWasCR = false;
        } else if (byte == 0x0A && lastByteWasCR) {
            // TELNET CR-LF → Apple 1 only uses CR; drop the spurious LF
            lastByteWasCR = false;
        } else {
            lastByteWasCR = (byte == 0x0D);
            enqueueRxByte(byte);
            bytesReceivedCount++;
        }
        break;

    case TelnetState::IAC:
        if (byte == 0xFF) {
            // Escaped 0xFF — pass through as data
            enqueueRxByte(0xFF);
            bytesReceivedCount++;
            telnetState = TelnetState::NORMAL;
        } else if (byte == 0xFB || byte == 0xFC || byte == 0xFD || byte == 0xFE) {
            // WILL (FB), WONT (FC), DO (FD), DONT (FE) — need one more byte
            telnetVerb = byte;
            telnetState = TelnetState::WILL_WONT_DO_DONT;
        } else if (byte == 0xFA) {
            // SB (subnegotiation begin)
            telnetState = TelnetState::SB;
        } else {
            // Other IAC commands (GA, NOP, etc.) — ignore
            telnetState = TelnetState::NORMAL;
        }
        break;

    case TelnetState::WILL_WONT_DO_DONT: {
        // Respond: WILL/DO → reply DONT/WONT (refuse everything)
        uint8_t response[3] = {0xFF, 0, byte};
        if (telnetVerb == 0xFB || telnetVerb == 0xFD) {
            // Server says WILL or DO — we say DONT or WONT
            response[1] = (telnetVerb == 0xFB) ? 0xFE : 0xFC; // DONT or WONT
#if !POM1_IS_WASM
            if (socketFd != kInvalidSocket) {
#ifdef _WIN32
                ::send(socketFd, reinterpret_cast<char*>(response), 3, 0);
#else
                ::send(socketFd, response, 3, MSG_NOSIGNAL);
#endif
            }
#endif
        }
        telnetState = TelnetState::NORMAL;
        break;
    }

    case TelnetState::SB:
        // Skip subnegotiation content until IAC SE
        if (byte == 0xFF) {
            telnetState = TelnetState::SB_IAC;
        }
        break;

    case TelnetState::SB_IAC:
        if (byte == 0xF0) { // SE — end of subnegotiation
            telnetState = TelnetState::NORMAL;
        } else if (byte == 0xFF) {
            // Escaped 0xFF inside SB
            telnetState = TelnetState::SB;
        } else {
            telnetState = TelnetState::NORMAL;
        }
        break;
    }
}

// ─────────────────────────────────────────────────────────────
// Baud rate decoding
// ─────────────────────────────────────────────────────────────

int WiFiModem::decodeBaudRate() const
{
    // Control register bits 0-3 select baud rate (W65C51N table)
    static const int baudTable[16] = {
        // 0: 16x external clock — treat as 9600
        9600,
        // 1-15: standard rates
        50, 75, 109, 134, 150, 300, 600, 1200,
        1800, 2400, 3600, 4800, 7200, 9600, 19200
    };
    return baudTable[controlReg & 0x0F];
}

void WiFiModem::updateCyclesPerByte()
{
    int baud = decodeBaudRate();
    // 8N1 = 10 bits per character (1 start + 8 data + 1 stop)
    int charsPerSec = baud / 10;
    if (charsPerSec <= 0) charsPerSec = 1;
    cyclesPerByte = POM1_CPU_CLOCK_HZ / charsPerSec;
}

// ─────────────────────────────────────────────────────────────
// Snapshot for UI
// ─────────────────────────────────────────────────────────────

void WiFiModem::copySnapshot(Snapshot& out) const
{
    std::lock_guard<std::mutex> lock(modemMutex);
    out.statusReg = 0;
    if (rdrfFlag) out.statusReg |= ST_RDRF;
    out.statusReg |= ST_TDRE; // always set (W65C51N bug)
    if (connState != ConnState::CONNECTED) out.statusReg |= ST_DCD;
    out.commandReg = commandReg;
    out.controlReg = controlReg;
    out.connected = (connState == ConnState::CONNECTED);
    out.echoEnabled = echoEnabled;
    out.remoteHost = remoteHost;
    out.remotePort = remotePort;
    out.bytesSent = bytesSentCount;
    out.bytesReceived = bytesReceivedCount;
    out.baudRate = decodeBaudRate();
}
