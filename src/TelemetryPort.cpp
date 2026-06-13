// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// TelemetryPort — dev-only telemetry side channel. See TelemetryPort.h and
// doc/TELEMETRY_SIDE_CHANNEL.md.

#include "TelemetryPort.h"
#include "CpuClock.h"
#include "Logger.h"

#include <cerrno>
#include <cstring>

// ─────────────────────────────────────────────────────────────
// Windows WinSock initialization (same pattern as TerminalCard / WiFiModem)
// ─────────────────────────────────────────────────────────────

#if !POM1_IS_WASM && defined(_WIN32)
  #pragma comment(lib, "ws2_32.lib")
  bool TelemetryPort::winsockInitialized = false;
  void TelemetryPort::initWinsock()
  {
      if (!winsockInitialized) {
          WSADATA wsaData;
          WSAStartup(MAKEWORD(2, 2), &wsaData);
          winsockInitialized = true;
      }
  }
#endif

// ─────────────────────────────────────────────────────────────
// Constructor / Destructor / Lifecycle
// ─────────────────────────────────────────────────────────────

TelemetryPort::TelemetryPort()
{
#if !POM1_IS_WASM && defined(_WIN32)
    initWinsock();
#endif
}

TelemetryPort::~TelemetryPort()
{
    stopServer();
}

void TelemetryPort::reset()
{
    std::lock_guard<std::mutex> lock(portMutex);
    disconnectClient();
    stopServer();

    frameBuf.clear();
    outBuf.clear();
    inBuf.clear();
    lockstep = false;
    awaitingAck = false;
    framesSentCount = 0;
    bytesSentCount = 0;
    bytesReceivedCount = 0;
    pollCycleAccum = 0;

    startServer();
}

void TelemetryPort::shutdown()
{
    std::lock_guard<std::mutex> lock(portMutex);
    disconnectClient();
    stopServer();
    frameBuf.clear();
    outBuf.clear();
    inBuf.clear();
}

void TelemetryPort::setKeyInjector(KeyInjector injector)
{
    std::lock_guard<std::mutex> lock(portMutex);
    keyInjector = std::move(injector);
}

void TelemetryPort::setListenPort(uint16_t port)
{
    std::lock_guard<std::mutex> lock(portMutex);
    listenPort = port;
}

// ─────────────────────────────────────────────────────────────
// MMIO register file ($C440-$C443)
// ─────────────────────────────────────────────────────────────

uint8_t TelemetryPort::readReg(uint16_t addr)
{
    std::lock_guard<std::mutex> lock(portMutex);
    switch (addr) {
    case kRegCtrl: {
        uint8_t status = 0;
        if (clientFd.valid()) status |= kStatConnected;
        if (!inBuf.empty())   status |= kStatInAvail;
        return status;
    }
    case kRegIn:
        if (inBuf.empty()) return 0;
        {
            uint8_t b = inBuf.front();
            inBuf.pop_front();
            return b;
        }
    case kRegInLen:
        return static_cast<uint8_t>(inBuf.size() > 255 ? 255 : inBuf.size());
    case kRegData:
    default:
        // kRegData is write-only; reads have no defined value.
        return 0;
    }
}

void TelemetryPort::writeReg(uint16_t addr, uint8_t value)
{
    std::lock_guard<std::mutex> lock(portMutex);
    switch (addr) {
    case kRegData:
        // Accumulate the current frame. Overflow is dropped (a single frame
        // larger than 64 KB is a caller bug, not a real telemetry packet).
        if (frameBuf.size() < kMaxFrameBytes) {
            frameBuf.push_back(value);
        }
        break;

    case kRegCtrl:
        switch (value) {
        case kCtrlEndFrame:
            endFrame();
            break;
        case kCtrlLockstepOn:
            lockstep = true;
            break;
        case kCtrlLockstepOff:
            lockstep = false;
            awaitingAck = false;
            break;
        default:
            // Unknown control opcode — ignore (forward-compatible).
            break;
        }
        break;

    case kRegIn:
    case kRegInLen:
    default:
        // Read-only registers — writes ignored.
        break;
    }
}

// Wrap frameBuf as [0xAA][len-lo][len-hi][payload], tee to the golden-trace log
// and queue it for the socket. Caller holds portMutex.
void TelemetryPort::endFrame()
{
    const std::size_t len = frameBuf.size();
    const uint8_t hdr[3] = { kFrameSentinel,
                             static_cast<uint8_t>(len & 0xFF),
                             static_cast<uint8_t>((len >> 8) & 0xFF) };

    // Golden-trace tee — log every frame, before the socket backpressure check,
    // so a CI capture is complete + deterministic regardless of client timing.
    if (logFile_.is_open()) {
        logFile_.write(reinterpret_cast<const char*>(hdr), 3);
        if (len) logFile_.write(reinterpret_cast<const char*>(frameBuf.data()),
                                static_cast<std::streamsize>(len));
        logFile_.flush();   // survive a kill mid-run
    }
    ++framesSentCount;

    // Queue for the socket. Drop on backpressure (the log already has it) rather
    // than grow without bound; flag it once so the symptom is visible.
    if (outBuf.size() + len + 3 > kMaxOutBytes) {
        frameBuf.clear();
        pom1::log().warn("Telemetry", "outbound buffer full — frame dropped (socket)");
        // Do NOT arm the lock-step gate here: the harness will never receive
        // this frame, so parking on its ACK would just wedge the CPU until the
        // watchdog auto-resume fires. Drop the frame and keep running.
        return;
    }
    outBuf.insert(outBuf.end(), hdr, hdr + 3);
    outBuf.insert(outBuf.end(), frameBuf.begin(), frameBuf.end());
    frameBuf.clear();

    // Lock-step: park until the harness ACKs this (now-queued) frame. Memory
    // checks isAwaitingAck() right after this write and halts the CPU
    // (cycle-exact, right after the STA); EmulationController's slice loop then
    // pumps the socket via serviceStall() until kAckByte clears the gate. Armed
    // only once the frame is actually queued — never on the drop path above.
    if (lockstep) awaitingAck = true;
}

void TelemetryPort::setLogFile(const std::string& path)
{
    std::lock_guard<std::mutex> lock(portMutex);
    logFile_.close();
    logFile_.clear();
    logFile_.open(path, std::ios::binary | std::ios::trunc);
    if (logFile_.is_open())
        pom1::log().info("Telemetry", "golden-trace log -> " + path);
    else
        pom1::log().error("Telemetry", "failed to open log file " + path);
}

bool TelemetryPort::isAwaitingAck() const
{
    std::lock_guard<std::mutex> lock(portMutex);
    return awaitingAck;
}

void TelemetryPort::clearAwaitingAck()
{
    std::lock_guard<std::mutex> lock(portMutex);
    awaitingAck = false;
}

void TelemetryPort::serviceStall()
{
    std::lock_guard<std::mutex> lock(portMutex);
    acceptClient();   // a harness may (re)connect mid-stall
    pollClient();     // recv kAckByte → clears awaitingAck
    flushOutbound();  // make sure the parked frame actually reached the wire
}

// ─────────────────────────────────────────────────────────────
// Cycle-driven polling (called from Memory::advanceCycles)
// ─────────────────────────────────────────────────────────────

void TelemetryPort::advanceCycles(int cycles)
{
    std::lock_guard<std::mutex> lock(portMutex);
    pollCycleAccum += cycles;
    if (pollCycleAccum < POM1_CPU_CYCLES_PER_MILLISECOND) return;
    pollCycleAccum = 0;

    acceptClient();
    pollClient();
    flushOutbound();
}

void TelemetryPort::copySnapshot(Snapshot& out) const
{
    std::lock_guard<std::mutex> lock(portMutex);
    out.serverListening = listenFd.valid();
    out.clientConnected = clientFd.valid();
    out.clientAddress   = clientAddress;
    out.listenPort      = listenPort;
    out.lockstep        = lockstep;
    out.framesSent      = framesSentCount;
    out.bytesSent       = bytesSentCount;
    out.bytesReceived   = bytesReceivedCount;
}

// ─────────────────────────────────────────────────────────────
// Network operations — desktop only (mirrors TerminalCard)
// ─────────────────────────────────────────────────────────────

#if !POM1_IS_WASM

void TelemetryPort::startServer()
{
    if (listenFd) return; // already listening

    listenFd.reset(::socket(AF_INET, SOCK_STREAM, 0));
    if (!listenFd) {
        pom1::log().error("Telemetry", "failed to create socket");
        return;
    }

    int optval = 1;
#ifdef _WIN32
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&optval), sizeof(optval));
#else
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
#endif

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // localhost only
    addr.sin_port = htons(listenPort);

    if (bind(listenFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        pom1::log().warn("Telemetry", "failed to bind port " +
                         std::to_string(listenPort) + " (already in use?)");
        listenFd.reset();
        return;
    }

    if (listen(listenFd, 1) < 0) {
        pom1::log().error("Telemetry", "listen failed");
        listenFd.reset();
        return;
    }

#ifdef _WIN32
    u_long nbMode = 1;
    ioctlsocket(listenFd, FIONBIO, &nbMode);
#else
    int flags = fcntl(listenFd, F_GETFL, 0);
    fcntl(listenFd, F_SETFL, flags | O_NONBLOCK);
#endif

    pom1::log().info("Telemetry", "listening on localhost:" + std::to_string(listenPort));
}

void TelemetryPort::stopServer()
{
    disconnectClient();
    listenFd.reset();
}

void TelemetryPort::acceptClient()
{
    if (!listenFd) return;

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

    // Drop any existing client (closes the old FD via SocketHandle). A fresh
    // harness gets a clean inbound queue; queued outbound frames are kept.
    clientFd.reset(newClient);
    inBuf.clear();

#ifdef _WIN32
    u_long nbMode = 1;
    ioctlsocket(clientFd, FIONBIO, &nbMode);
#else
    int flags = fcntl(clientFd, F_GETFL, 0);
    fcntl(clientFd, F_SETFL, flags | O_NONBLOCK);
#endif

    clientAddress = inet_ntoa(clientAddr.sin_addr);
    clientAddress += ":" + std::to_string(ntohs(clientAddr.sin_port));
    pom1::log().info("Telemetry", "harness connected from " + clientAddress);
}

void TelemetryPort::disconnectClient()
{
    if (clientFd) {
        clientFd.reset();
        clientAddress.clear();
        pom1::log().info("Telemetry", "harness disconnected");
    }
}

void TelemetryPort::pollClient()
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
    if (FD_ISSET(clientFd, &errorSet)) { disconnectClient(); return; }
    if (FD_ISSET(clientFd, &readSet)) {
        char buf[256];
        int n = recv(clientFd, buf, sizeof(buf), 0);
        if (n <= 0) { disconnectClient(); return; }
        for (int i = 0; i < n; ++i) {
            const uint8_t b = static_cast<uint8_t>(buf[i]);
            if (awaitingAck && b == kAckByte) { awaitingAck = false; continue; } // lock-step release (consumed)
            if (inBuf.size() >= kMaxInBytes) inBuf.pop_front(); // drop-oldest
            inBuf.push_back(b);
        }
        bytesReceivedCount += static_cast<uint32_t>(n);
    }
#else // POSIX
    struct pollfd pfd{};
    pfd.fd = clientFd;
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, 0);
    if (ret <= 0) return;
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) { disconnectClient(); return; }
    if (pfd.revents & POLLIN) {
        uint8_t buf[256];
        ssize_t n = recv(clientFd, buf, sizeof(buf), 0);
        if (n <= 0) { disconnectClient(); return; }
        for (ssize_t i = 0; i < n; ++i) {
            const uint8_t b = buf[i];
            if (awaitingAck && b == kAckByte) { awaitingAck = false; continue; } // lock-step release (consumed)
            if (inBuf.size() >= kMaxInBytes) inBuf.pop_front(); // drop-oldest
            inBuf.push_back(b);
        }
        bytesReceivedCount += static_cast<uint32_t>(n);
    }
#endif
}

void TelemetryPort::flushOutbound()
{
    if (!clientFd || outBuf.empty()) return;
    const std::size_t sent = sendRaw(outBuf.data(), outBuf.size());
    if (!clientFd) {
        // Hard disconnect mid-flush: whatever is left is a partial frame that
        // would desync the next harness — drop the whole queue for a clean start.
        outBuf.clear();
        return;
    }
    if (sent >= outBuf.size())
        outBuf.clear();
    else if (sent > 0)
        // Short write (kernel send buffer full): keep the unsent tail and retry
        // next tick. The SAME client receives the remainder in order — clearing
        // it here would drop bytes mid-frame and desync the harness's parser.
        outBuf.erase(outBuf.begin(),
                     outBuf.begin() + static_cast<std::ptrdiff_t>(sent));
    // sent == 0 with the client still up = EAGAIN: keep the full queue.
}

std::size_t TelemetryPort::sendRaw(const uint8_t* data, std::size_t len)
{
    if (!clientFd || len == 0) return 0;
    std::size_t total = 0;
    while (total < len) {
#ifdef _WIN32
        const int sent = ::send(clientFd, reinterpret_cast<const char*>(data + total),
                                static_cast<int>(len - total), 0);
#else
        const ssize_t sent = ::send(clientFd, data + total, len - total, MSG_NOSIGNAL);
#endif
        if (sent > 0) { total += static_cast<std::size_t>(sent); continue; }

        // sent <= 0: distinguish transient backpressure (non-blocking socket,
        // kernel buffer full) from a dead peer. The client FD is O_NONBLOCK
        // (set in acceptClient), so EAGAIN/EWOULDBLOCK is normal flow control —
        // keep the tail and retry, do NOT tear down a healthy connection.
#ifdef _WIN32
        const bool wouldBlock = (sent < 0 && WSAGetLastError() == WSAEWOULDBLOCK);
#else
        const bool wouldBlock = (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
#endif
        if (wouldBlock) break;
        disconnectClient();   // sent == 0 (peer closed) or a real error / EPIPE
        break;
    }
    bytesSentCount += static_cast<uint32_t>(total);
    return total;
}

#else // POM1_IS_WASM — no networking (channel degrades to a no-op tap)

void TelemetryPort::startServer() {}
void TelemetryPort::stopServer() {}
void TelemetryPort::acceptClient() {}
void TelemetryPort::disconnectClient() { clientFd.reset(); clientAddress.clear(); }
void TelemetryPort::pollClient() {}
void TelemetryPort::flushOutbound() { outBuf.clear(); }
std::size_t TelemetryPort::sendRaw(const uint8_t*, std::size_t) { return 0; }

#endif // POM1_IS_WASM
