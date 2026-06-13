"""pom1_telemetry — harness library for POM1's telemetry side channel.

The reusable client side of the POM1 "dream SDK" automated-testing loop: a game
writes its per-frame state to $C440-$C443 and (in lock-step) parks on the
end-frame marker; this library reads each frame over TCP, lets the test decide
the input, and ACKs to advance exactly one frame — deterministically, with no
display and no human.

Protocol + design: doc/TELEMETRY_SIDE_CHANNEL.md. 6502 side: dev/lib/telemetry/
telemetry.inc. Worked example: tools/test_telemetry_demo.py.

Typical use (self-launching, headless — CI-friendly):

    from pom1_telemetry import launch_headless
    with launch_headless("software/Telemetry/A1_TelemetryDemo.bin",
                         load_addr=0x0280, port=6601) as tc:
        frame = tc.read_frame()                 # bytes: the game's state packet
        while not frame[2]:                     # byte 2 = "won" in the demo
            move = b"\\x01" if frame[0] < frame[1] else b"\\x02"
            frame = tc.step(move)               # send input, ACK, get next frame
"""

import contextlib
import os
import socket
import subprocess
import time

SENTINEL = 0xAA          # frame = SENTINEL, len-lo, len-hi, payload[len]
ACK = b"\x06"            # lock-step release token (TELE_ACK; consumed by POM1)
DEFAULT_PORT = 6503


class ProtocolError(Exception):
    """Bytes off the telemetry socket did not match the frame protocol."""


class TelemetryClient:
    """A TCP client for one POM1 telemetry port."""

    def __init__(self, port=DEFAULT_PORT, host="127.0.0.1"):
        self.host = host
        self.port = port
        self.sock = None

    # -- connection -------------------------------------------------------
    def connect(self, timeout=10.0, interval=0.2):
        """Connect, retrying until `timeout` (POM1 needs a moment to boot)."""
        deadline = time.monotonic() + timeout
        while True:
            try:
                self.sock = socket.create_connection((self.host, self.port), 2.0)
                return self
            except OSError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(interval)

    def close(self):
        if self.sock is not None:
            try:
                self.sock.close()
            finally:
                self.sock = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # -- frames -----------------------------------------------------------
    def read_frame(self, timeout=2.0):
        """Return the next frame's payload (bytes), or None if none arrives
        within `timeout` (e.g. the CPU is parked on an un-ACKed lock-step
        frame). Raises ProtocolError on a bad sentinel, ConnectionError on a
        closed socket."""
        self.sock.settimeout(timeout)
        hdr = b""
        try:
            while len(hdr) < 3:
                chunk = self.sock.recv(3 - len(hdr))
                if not chunk:
                    raise ConnectionError("telemetry socket closed")
                hdr += chunk
        except socket.timeout:
            return None
        if hdr[0] != SENTINEL:
            raise ProtocolError(f"bad frame sentinel {hdr[0]:#04x}")
        n = hdr[1] | (hdr[2] << 8)
        payload = b""
        while len(payload) < n:
            chunk = self.sock.recv(n - len(payload))
            if not chunk:
                raise ConnectionError("telemetry socket closed mid-frame")
            payload += chunk
        return payload

    # -- input ------------------------------------------------------------
    def send(self, data):
        """Send raw bytes to the game's inbound FIFO (read via TELE_IN)."""
        if data:
            self.sock.sendall(bytes(data))

    def ack(self):
        """Release exactly one lock-step frame (advance the CPU)."""
        self.sock.sendall(ACK)

    def step(self, inp=b"", timeout=2.0):
        """One lock-step transaction: deliver `inp` to TELE_IN, ACK to advance
        one frame, and return the next frame's payload. The canonical loop body
        for testing a lock-step game."""
        self.send(inp)
        self.ack()
        return self.read_frame(timeout)


@contextlib.contextmanager
def launch_headless(program, *, load_addr=0x0280, port=DEFAULT_PORT,
                    pom1=None, extra=(), connect_timeout=10.0):
    """Launch POM1 --headless with `program` loaded + running, yield a connected
    TelemetryClient, and tear the process down on exit. No display required.

    `program` is a path to a raw binary; `load_addr` is its origin (matches the
    program's link config — e.g. 0x0280 for apple1_4k.cfg). `extra` are extra
    POM1 CLI args. `pom1` defaults to $POM1 or "build/POM1"."""
    pom1 = pom1 or os.environ.get("POM1", "build/POM1")
    args = [pom1, "--headless",
            "--telemetry-port", str(port),
            "--load", f"{load_addr:04X}:{program}",
            "--run", f"{load_addr:04X}",
            *extra]
    proc = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    client = TelemetryClient(port=port)
    try:
        deadline = time.monotonic() + connect_timeout
        while True:
            if proc.poll() is not None:
                raise RuntimeError(
                    f"POM1 exited early (code {proc.returncode}) before telemetry was ready")
            try:
                client.sock = socket.create_connection((client.host, port), 1.0)
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise TimeoutError(f"could not reach telemetry port {port}")
                time.sleep(0.2)
        yield client
    finally:
        client.close()
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
