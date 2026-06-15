"""pom1_telemetry — harness library for POM1's telemetry side channel.

The reusable client side of the POM1 "dream SDK" automated-testing loop: a game
writes its per-frame state to $C440-$C443 and (in lock-step) parks on the
end-frame marker; this library reads each frame over TCP, lets the test decide
the input, and ACKs to advance exactly one frame — deterministically, with no
display and no human.

Two frame kinds share the channel (self-describing schema frames, v1):

  DATA frame    0xAA len_lo len_hi payload   — the field VALUES (unchanged).
  SCHEMA frame  0xA5 len_lo len_hi payload   — field DESCRIPTORS so the harness
                can decode DATA frames BY NAME. The payload is a run of
                [type:1 byte][field name: ASCII][0x00] descriptors. Type codes:
                1=U8 2=S8 3=U16(LE) 4=S16(LE) 5=BOOL 6=CHAR.

read_frame() transparently records any 0xA5 schema frame it sees (on .schema)
and keeps returning DATA-frame payloads, so existing callers are unaffected.
decode()/read_named() turn a DATA payload into a {name: value} dict using the
last schema seen.

Protocol + design: doc/TELEMETRY_SIDE_CHANNEL.md. 6502 side: dev/lib/telemetry/
telemetry.inc + telemetry.h. Worked examples: tools/test_telemetry_demo.py,
dev/projects/gen2_snake_telemetry/.

Typical use (self-launching, headless — CI-friendly):

    from pom1_telemetry import launch_headless
    with launch_headless("software/Telemetry/A1_TelemetryDemo.bin",
                         load_addr=0x0280, port=6601) as tc:
        frame = tc.read_frame()                 # bytes: the game's state packet
        while not frame[2]:                     # byte 2 = "won" in the demo
            move = b"\\x01" if frame[0] < frame[1] else b"\\x02"
            frame = tc.step(move)               # send input, ACK, get next frame

Self-describing (schema) use — decode by field name:

    from pom1_telemetry import launch_headless
    with launch_headless("software/Telemetry/GEN2Snake.bin",
                         load_addr=0x6000, port=6602, extra=["--preset", "12"]) as tc:
        st = tc.read_named()                    # {"head_x":.., "alive":True, ...}
        print(st["head_x"], st["length"], st["alive"])
"""

import contextlib
import os
import socket
import subprocess
import time

SENTINEL = 0xAA          # DATA frame  = SENTINEL,        len-lo, len-hi, payload[len]
SCHEMA_SENTINEL = 0xA5   # SCHEMA frame = SCHEMA_SENTINEL, len-lo, len-hi, payload[len]
ACK = b"\x06"            # lock-step release token (TELE_ACK; consumed by POM1)
DEFAULT_PORT = 6503

# Schema field type codes (mirror dev/lib/telemetry/telemetry.{inc,h}) ->
# (byte size, signed). BOOL/CHAR are 1-byte and handled specially in decode().
TELE_T_U8, TELE_T_S8, TELE_T_U16, TELE_T_S16, TELE_T_BOOL, TELE_T_CHAR = 1, 2, 3, 4, 5, 6
_TYPE_SIZE = {TELE_T_U8: 1, TELE_T_S8: 1, TELE_T_U16: 2, TELE_T_S16: 2,
              TELE_T_BOOL: 1, TELE_T_CHAR: 1}


class ProtocolError(Exception):
    """Bytes off the telemetry socket did not match the frame protocol."""


def parse_schema(payload):
    """Parse a SCHEMA-frame payload into a list of (type, name) descriptors.
    Each descriptor is [type:1 byte][name: ASCII][0x00]. Raises ProtocolError
    on a truncated descriptor."""
    fields = []
    i, n = 0, len(payload)
    while i < n:
        ftype = payload[i]
        i += 1
        end = payload.find(b"\x00", i)
        if end == -1:
            raise ProtocolError("schema descriptor missing NUL terminator")
        name = payload[i:end].decode("ascii", "replace")
        fields.append((ftype, name))
        i = end + 1
    return fields


def decode_with_schema(payload, schema):
    """Decode a DATA-frame `payload` into a {name: value} dict using `schema`
    (a list of (type, name)). Honours U8/S8/U16/S16/BOOL/CHAR sizing,
    signedness and little-endian word order. Returns {} if schema is empty."""
    out = {}
    off = 0
    for ftype, name in schema:
        size = _TYPE_SIZE.get(ftype, 1)
        raw = payload[off:off + size]
        if len(raw) < size:
            raise ProtocolError(f"DATA frame too short for field {name!r}")
        off += size
        if ftype == TELE_T_BOOL:
            out[name] = raw[0] != 0
        elif ftype == TELE_T_CHAR:
            out[name] = chr(raw[0])
        else:
            signed = ftype in (TELE_T_S8, TELE_T_S16)
            out[name] = int.from_bytes(raw, "little", signed=signed)
    return out


class TelemetryClient:
    """A TCP client for one POM1 telemetry port."""

    def __init__(self, port=DEFAULT_PORT, host="127.0.0.1"):
        self.host = host
        self.port = port
        self.sock = None
        self.schema = []        # last (type, name) descriptors seen (schema frame)

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
    def _read_raw_frame(self, timeout):
        """Read one wire frame: return (sentinel, payload), or None on timeout.
        Raises ProtocolError on an unknown sentinel, ConnectionError on close."""
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
        if hdr[0] not in (SENTINEL, SCHEMA_SENTINEL):
            raise ProtocolError(f"bad frame sentinel {hdr[0]:#04x}")
        n = hdr[1] | (hdr[2] << 8)
        payload = b""
        while len(payload) < n:
            chunk = self.sock.recv(n - len(payload))
            if not chunk:
                raise ConnectionError("telemetry socket closed mid-frame")
            payload += chunk
        return hdr[0], payload

    def read_frame(self, timeout=2.0):
        """Return the next DATA frame's payload (bytes), or None if none arrives
        within `timeout` (e.g. the CPU is parked on an un-ACKed lock-step
        frame). Any 0xA5 SCHEMA frames seen along the way are parsed and stored
        on self.schema (decode()/read_named() use them) and skipped — they never
        surface here, so existing callers see only DATA payloads. Raises
        ProtocolError on a bad sentinel, ConnectionError on a closed socket."""
        deadline = time.monotonic() + timeout
        while True:
            remaining = max(0.0, deadline - time.monotonic())
            frame = self._read_raw_frame(remaining)
            if frame is None:
                return None
            sentinel, payload = frame
            if sentinel == SCHEMA_SENTINEL:
                self.schema = parse_schema(payload)
                continue                    # record + skip; keep waiting for DATA
            return payload

    def decode(self, payload):
        """Decode a DATA-frame `payload` into a {name: value} dict using the
        last schema seen (self.schema). Returns {} if no schema has arrived."""
        return decode_with_schema(payload, self.schema)

    def read_named(self, timeout=2.0):
        """read_frame() + decode(): the next DATA frame as a {name: value} dict,
        or None on timeout. Falls back to {} if no schema has been seen."""
        payload = self.read_frame(timeout)
        if payload is None:
            return None
        return self.decode(payload)

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
