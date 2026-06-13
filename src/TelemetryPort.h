// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// TelemetryPort — dev-only virtual "side channel" for automated game testing.
//
// A binary, bidirectional, frame-delimited bridge between an Apple-1 program
// (which writes its state out through a 4-byte MMIO window) and an external
// test harness (which reads the state over TCP and drives synthetic input).
// This is the generalisation of the Terminal Card's $D012→TCP→$D010 bridge:
// where the Terminal Card carries screen characters, this carries structured
// state + a frame marker so a harness can react in lock-step with the game.
//
// NOT real hardware — a development aid, in the same "fantasy" category as the
// Multiplexing Fantasy presets. See doc/TELEMETRY_SIDE_CHANNEL.md for the full
// design (protocol, lock-step mode, CLI, the test loop) and TODO.md ›
// "Automated game testing".
//
// Window: $C440-$C443. This is the $C4xx A9=0 dead zone where Uncle Bernie's
// GEN2 soft-switch decoder (SEL = $Cxxx & !A11 & A9 & A4, needs A9=1) is
// structurally blind, and which no other card claims (ACI $C0/$C1xx, SID
// $C8xx+, Juke-Box ≤ $BFFF, PIA = $Dxxx). Registered on the PeripheralBus at a
// priority above GEN2's (0) so it owns its four bytes over GEN2's broad
// $C200-$C7FF pass-through handler — the same mechanism TMS9918 uses to win
// over SID at $CC00.

#ifndef TELEMETRYPORT_H
#define TELEMETRYPORT_H

#include "POM1Build.h"
#include "Peripheral.h"
#include "SocketHandle.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

// SocketHandle.h already pulls the platform socket headers; desktop-only extras
// (addrinfo, poll, fcntl) match TerminalCard.h.
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

class TelemetryPort : public pom1::Peripheral
{
public:
    std::string_view name() const override { return "Telemetry"; }
    std::string_view mutexLabel() const override { return "TelemetryPort::portMutex"; }

    // MMIO window ($C440-$C443).
    static constexpr uint16_t kBaseAddr = 0xC440;
    static constexpr uint16_t kEndAddr  = 0xC443;
    static constexpr uint16_t kRegData  = 0xC440; // W: push a byte to the outbound frame
    static constexpr uint16_t kRegCtrl  = 0xC441; // W: control opcode / R: status byte
    static constexpr uint16_t kRegIn    = 0xC442; // R: pop one inbound byte (harness → game)
    static constexpr uint16_t kRegInLen = 0xC443; // R: inbound bytes pending (saturates at 255)

    // Control opcodes written to kRegCtrl.
    static constexpr uint8_t kCtrlLockstepOff = 0x00;
    static constexpr uint8_t kCtrlEndFrame    = 0x01; // delimit + queue the accumulated frame
    static constexpr uint8_t kCtrlLockstepOn  = 0x02; // arm deterministic handshake (see TODO below)

    // Status bits returned by reading kRegCtrl.
    static constexpr uint8_t kStatConnected = 0x80; // bit7: a harness is connected
    static constexpr uint8_t kStatInAvail   = 0x01; // bit0: at least one inbound byte waiting

    // Lock-step release token. While the CPU is parked on an end-frame marker,
    // this byte from the harness advances exactly one frame; it is consumed
    // (not delivered to TELE_IN). Any other inbound byte is normal game input.
    static constexpr uint8_t kAckByte = 0x06;       // ASCII ACK

    // Outbound frame wire format: kFrameSentinel, len-lo, len-hi, payload[len].
    static constexpr uint8_t  kFrameSentinel = 0xAA;
    static constexpr uint16_t kDefaultPort   = 6503; // Terminal Card owns 6502

    TelemetryPort();
    ~TelemetryPort() override;

    // Lifecycle. reset() (re)starts the TCP server + clears state; shutdown()
    // stops the server and drops the client. Memory::setTelemetryEnabled()
    // calls reset() on enable and shutdown() on disable — the server is NOT
    // opened unless the port is explicitly enabled (--telemetry-port).
    void reset();
    void shutdown();

    // MMIO — registered on the PeripheralBus. Both run on the emulation thread
    // under stateMutex (the bus dispatch is inside Memory::memRead/memWrite).
    uint8_t readReg(uint16_t addr);
    void    writeReg(uint16_t addr, uint8_t value);

    // Polled from Memory::advanceCycles(): accept/poll the client and flush the
    // outbound queue. Gated by Memory's telemetryEnabled flag.
    void advanceCycles(int cycles);

    // Lock-step. When armed (kCtrlLockstepOn) an end-frame write parks the CPU
    // (Memory halts it the instant the marker is written) until the harness
    // sends kAckByte. EmulationController's slice loop polls isAwaitingAck() and
    // pumps the socket via serviceStall() (the normal poll runs inside cpu->run,
    // which is skipped while parked). clearAwaitingAck() is the timeout/disarm
    // escape hatch so a dead harness can't wedge the emulator.
    bool isAwaitingAck() const;
    void clearAwaitingAck();
    void serviceStall();

    // Same key-injection contract as the Terminal Card: synthetic input for
    // games that read the keyboard ($D010/$D011) the normal way. raw == true
    // bypasses forced-uppercase (setKeyPressedRaw).
    using KeyInjector = std::function<void(char key, bool raw)>;
    void setKeyInjector(KeyInjector injector);

    // Push synthetic inbound bytes (harness → game) from the in-app Serial
    // Monitor, exactly as if a TCP harness had sent them: same TELE_IN FIFO,
    // same drop-oldest cap. Thread-safe (portMutex). The lock-step ACK ($06) is
    // NOT special-cased here — that release path is socket-only; the UI advances
    // a parked frame via EmulationController instead. See doc/TELEMETRY…md.
    void injectInbound(const uint8_t* data, std::size_t len);

    void setListenPort(uint16_t port);

    // Golden-trace tee (--telemetry-log): mirror the outbound frame stream to a
    // file (binary, same `0xAA <len16> <payload>` framing as the socket). Lets
    // CI diff a run against an expected capture with no live harness. Captures
    // every frame, even when the socket side drops on backpressure.
    void setLogFile(const std::string& path);

    // Thread-safe snapshot for the UI panel / Serial Monitor.
    struct Snapshot {
        bool        serverListening = false;
        bool        clientConnected = false;
        std::string clientAddress;
        uint16_t    listenPort  = kDefaultPort;
        bool        lockstep    = false;
        bool        awaitingAck = false;   // CPU parked on a lock-step frame
        uint32_t    framesSent  = 0;
        uint32_t    bytesSent   = 0;
        uint32_t    bytesReceived = 0;
        // Serial Monitor tap: a bounded copy of the most-recent outbound *wire*
        // bytes (0xAA <len16> payload) plus a monotonic total-ever counter. The
        // UI keeps its own running total and appends only `txTotal - lastSeen`
        // bytes from the tail of `txTap`, so it survives the SPSC slot being
        // overwritten between UI reads (it only loses data if it falls more than
        // kMonitorRingBytes behind — acceptable for a scrolling monitor).
        std::vector<uint8_t> txTap;
        uint64_t             txTotal = 0;
    };
    void copySnapshot(Snapshot& out) const;

    // Snapshot hooks — the FIFOs and socket are transient I/O (like a network
    // connection), intentionally not serialised. Mirrors the open WiFiModem /
    // TerminalCard snapshot gap. Default Peripheral hooks are no-ops; overridden
    // here only to document the choice.
    void serialize(pom1::SnapshotWriter&) const override {}
    void deserialize(pom1::SnapshotReader&) override {}

private:
    // ---- Caps (drop-oldest / drop-overflow, logged once) ----
    static constexpr std::size_t kMaxFrameBytes = 65535;   // one frame's payload
    static constexpr std::size_t kMaxOutBytes   = 1u << 20; // queued-but-unsent backpressure
    static constexpr std::size_t kMaxInBytes    = 1u << 16; // inbound ring
    static constexpr std::size_t kMonitorRingBytes = 8192; // Serial Monitor TX tap window

    // ---- State (guarded by portMutex) ----
    std::vector<uint8_t> frameBuf;   // bytes written to kRegData since last end-frame
    std::vector<uint8_t> outBuf;     // framed bytes awaiting socket flush
    std::deque<uint8_t>  inBuf;      // inbound bytes (harness → game)
    bool                 lockstep = false;

    // Serial Monitor TX tap — last kMonitorRingBytes of the outbound wire stream
    // (tapped in endFrame, even on socket backpressure drop, so the monitor sees
    // every frame the game emitted) + a monotonic total of bytes ever tapped.
    std::deque<uint8_t>  txMonitorRing;
    uint64_t             txMonitorTotal = 0;

    // Lock-step ACK gate. Set by an end-frame write while `lockstep`; cleared
    // when the harness sends kAckByte (pollClient) or on timeout/disarm. While
    // set, Memory halts the CPU and EmulationController parks the slice loop —
    // the wait happens between slices with stateMutex released, never inside
    // writeReg (that would hold stateMutex and stop the socket from being
    // polled → deadlock).
    bool awaitingAck = false;

    // Network — server socket + single client.
    SocketHandle listenFd;
    SocketHandle clientFd;
    uint16_t     listenPort = kDefaultPort;
    std::string  clientAddress;

    // Statistics.
    uint32_t framesSentCount    = 0;
    uint32_t bytesSentCount     = 0;
    uint32_t bytesReceivedCount = 0;

    // Cycle accumulator (~1 ms at 1 MHz), same cadence as TerminalCard.
    int pollCycleAccum = 0;

    KeyInjector keyInjector;

    // Optional golden-trace tee of the outbound frame stream (--telemetry-log).
    std::ofstream logFile_;

    mutable std::mutex portMutex;

    // Internal helpers (network ops are desktop-only; WASM gets no-op stubs).
    void endFrame();
    void flushOutbound();
    void startServer();
    void stopServer();
    void acceptClient();
    void disconnectClient();
    void pollClient();
    // Sends as much of [data,len) as the non-blocking socket accepts and returns
    // the byte count actually handed to the kernel (0 on EAGAIN / disconnect).
    // The caller keeps any unsent tail for the next flush.
    std::size_t sendRaw(const uint8_t* data, std::size_t len);

#if !POM1_IS_WASM && defined(_WIN32)
    static bool winsockInitialized;
    static void initWinsock();
#endif
};

#endif // TELEMETRYPORT_H
