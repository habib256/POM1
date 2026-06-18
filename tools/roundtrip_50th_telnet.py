#!/usr/bin/env python3
"""
roundtrip_50th_telnet.py — End-to-end ACI round-trip of the 50th-birthday
demo driven entirely through the Terminal Card.

Flow:
  1. Launch POM1 with `--preset 4 --terminal --cpu-max --save-tape
     /tmp/50th.wav`.
  2. Telnet in, hard-reset, paste `software/demos/50th.apl.txt` (minus
     its trailing `000280R` auto-run line) so the program sits in RAM.
  3. Drive the ACI monitor: `C100R` then `0280.0FFFW` to record the
     program onto tape. Wait for the Wozmon `\\` prompt to return.
  4. SIGTERM POM1 so `~MainWindow_ImGui` flushes `--save-tape` to
     `/tmp/50th.wav`.
  5. Transcode `/tmp/50th.wav` to `cassettes/50TH.ogg` with
     `ffmpeg -c:a libvorbis -q:a 4`.
  6. Launch a fresh POM1 with `--tape cassettes/50TH.ogg`
     (auto-arms PLAY, the new ARMED banner is visible until step 8).
  7. Telnet in, hard-reset, drive `C100R` then `0280.0FFFR` to load the
     program back into RAM.
  8. `280R` to run the demo. The Apple 1 screen starts the animation;
     leave POM1 running.

Usage:
  python3 tools/roundtrip_50th_telnet.py [--verbose] [--keep-wav]
                                          [--skip-run]

Requires:
  - build/POM1 built
  - ffmpeg with libvorbis in PATH
  - software/demos/50th.apl.txt
  - TCP port 6502 free
"""
from __future__ import annotations

import argparse
import os
import re
import select
import shutil
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

HOST = "127.0.0.1"
PORT = 6502
CTRL_R = 18
ACI_PRESET = 4                    # "Apple-1 with ACI & Integer BASIC"
PROG_FROM = 0x0280
PROG_TO   = 0x0FFF                # covers the full 50th demo (3456 bytes spanning $0280-$0FFF — running past $0BFF is what made the earlier roundtrips crash mid-animation after the Macintosh frame)
WOZMON_PROMPT = "\\"              # Woz Monitor prompt character
VERBOSE = False


# ---------------------------------------------------------------------------
# Paths & process helpers
# ---------------------------------------------------------------------------

def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def pom1_bin() -> Path:
    p = repo_root() / "build" / "POM1"
    if not p.exists():
        sys.exit(f"ERROR: {p} not found — build POM1 first "
                 "(cd build && make POM1)")
    return p


def launch_pom1(extra: list[str], log_path: Path) -> subprocess.Popen:
    cmd = [str(pom1_bin()), "--preset", str(ACI_PRESET), "--terminal",
           "--cpu-max", *extra]
    if VERBOSE:
        print(f"  [launch] {' '.join(cmd)}")
    log = open(log_path, "w")
    proc = subprocess.Popen(cmd, cwd=str(repo_root()),
                            stdout=log, stderr=log,
                            start_new_session=True)
    deadline = time.time() + 10.0
    while time.time() < deadline:
        if proc.poll() is not None:
            sys.exit(f"ERROR: POM1 exited early (code {proc.returncode}); "
                     f"see {log_path}")
        try:
            with socket.create_connection((HOST, PORT), timeout=0.3):
                time.sleep(0.5)
                return proc
        except OSError:
            time.sleep(0.15)
    proc.kill()
    sys.exit(f"ERROR: Terminal Card never came up on {HOST}:{PORT}; "
             f"see {log_path}")


def shutdown_pom1(proc: subprocess.Popen, log_path: Path, *,
                  sig=signal.SIGTERM, wait_s: float = 5.0) -> None:
    if proc.poll() is not None:
        return
    proc.send_signal(sig)
    try:
        proc.wait(timeout=wait_s)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    if VERBOSE:
        print(f"  [shutdown] exit={proc.returncode}")


# ---------------------------------------------------------------------------
# Telnet helpers
# ---------------------------------------------------------------------------

def recv_avail(sock: socket.socket, total: float = 4.0,
               idle: float = 0.3) -> str:
    end = time.time() + total
    buf = b""
    while time.time() < end:
        r, _, _ = select.select([sock], [], [], idle)
        if r:
            chunk = sock.recv(65536)
            if not chunk:
                break
            buf += chunk
        elif buf:
            break
    return buf.decode("latin-1", errors="replace")


def send_line(sock: socket.socket, cmd: str, *, wait: float = 0.1,
              read_t: float = 1.0) -> str:
    sock.sendall((cmd + "\r").encode("ascii"))
    time.sleep(wait)
    out = recv_avail(sock, total=read_t, idle=0.25)
    if VERBOSE:
        print(f"    > {cmd!r}  <- {out!r}")
    return out


def send_ctrl(sock: socket.socket, byte: int) -> None:
    sock.sendall(bytes([byte & 0xFF]))


def wait_for_prompt(sock: socket.socket, deadline_s: float,
                    marker: str = WOZMON_PROMPT,
                    poke_interval: float = 3.0) -> bool:
    """Poll the socket until `marker` shows up in the Apple-1 echo stream.
    We periodically send a bare CR to keep Wozmon tapping out its prompt —
    during an ACI READ/WRITE the CPU is busy and our CR queues in the PIA
    buffer, then Wozmon consumes it and echoes `\\` once it jumps back to
    $FF1A. The marker acts as "ACI handed control back to Wozmon"."""
    end = time.time() + deadline_s
    last_poke = 0.0
    while time.time() < end:
        out = recv_avail(sock, total=0.6, idle=0.2)
        if out and marker in out:
            return True
        if time.time() - last_poke > poke_interval:
            sock.sendall(b"\r")
            last_poke = time.time()
    return False


# ---------------------------------------------------------------------------
# Hex dump parsing
# ---------------------------------------------------------------------------

HEX_BYTE = re.compile(r"\b([0-9A-F]{2})\b")
HEX_ADDR = re.compile(r"\b([0-9A-F]{4})\s*:")


def parse_wozmon_hex(text: str) -> dict[int, int]:
    """Parse a Woz-Monitor-style hex dump into a {address: byte} dict.
    Handles the 50th.apl.txt format where a single physical line carries
    multiple `AAAA: BB BB BB` or `: BB BB BB` groups separated by more
    colons (continuation without a new address)."""
    text = text.upper()
    # Strip the trailing `000280R` auto-run and any other `...R` tokens that
    # ask Wozmon to run; we only want the load.
    text = re.sub(r"\b[0-9A-F]{1,4}R\b", " ", text)

    result: dict[int, int] = {}
    cursor = None  # next address to store into
    # Tokenise: colons separate segments; within a segment we look for
    # an optional leading `AAAA` address followed by hex bytes.
    segments = text.split(":")
    for seg in segments:
        # Trim
        seg = seg.strip()
        if not seg:
            continue
        addr_match = HEX_ADDR.match(seg + ":")
        data_start = 0
        if addr_match:
            cursor = int(addr_match.group(1), 16)
            data_start = addr_match.end() - 1  # drop the trailing colon we added
        elif cursor is None:
            # No address established yet — skip leading noise.
            continue
        # Hex bytes from the rest of the segment. Any 2-digit hex token is
        # a byte; the address regex already consumed a leading 4-hex if
        # present, so we won't re-eat it as a byte.
        for m in HEX_BYTE.finditer(seg[data_start:]):
            val = int(m.group(1), 16)
            result[cursor] = val
            cursor += 1
    return result


def poke_bytes(sock: socket.socket, addr: int, data: bytes,
               chunk: int = 16, wait: float = 0.05) -> None:
    """Write data into Apple-1 RAM via Wozmon store syntax. 16 bytes/line
    is a comfortable fit for the keyboard buffer/echo loop."""
    for i in range(0, len(data), chunk):
        blob = data[i:i + chunk]
        hex_bytes = " ".join(f"{b:02X}" for b in blob)
        send_line(sock, f"{addr + i:04X}: {hex_bytes}",
                  wait=wait, read_t=0.5)


# ---------------------------------------------------------------------------
# Phase 1 — capture to .wav
# ---------------------------------------------------------------------------

def phase_capture(wav_path: Path) -> None:
    print("\n=== Phase 1: record 50th demo to .wav via ACI ===")
    src = repo_root() / "software" / "demos" / "50th.apl.txt"
    if not src.exists():
        sys.exit(f"ERROR: {src} missing")
    mem = parse_wozmon_hex(src.read_text())
    if not mem:
        sys.exit("ERROR: 50th.apl.txt parsed to zero bytes")
    lo, hi = min(mem), max(mem)
    print(f"  parsed {len(mem)} bytes spanning ${lo:04X}-${hi:04X}")
    if lo != PROG_FROM:
        print(f"  WARNING: lowest address ${lo:04X} != PROG_FROM ${PROG_FROM:04X}")

    # Flatten to a contiguous byte buffer ($0280 upward), filling holes
    # with 0x00 so Wozmon sees one clean poke per line.
    length = PROG_TO - PROG_FROM + 1
    blob = bytearray(length)
    for addr, val in mem.items():
        if PROG_FROM <= addr <= PROG_TO:
            blob[addr - PROG_FROM] = val

    log = repo_root() / "build" / "pom1_50th_capture.log"
    if wav_path.exists():
        wav_path.unlink()
    proc = launch_pom1(["--save-tape", str(wav_path)], log)
    try:
        sock = socket.create_connection((HOST, PORT), timeout=5)
        sock.settimeout(20)
        recv_avail(sock, total=1.2)  # banner

        send_ctrl(sock, CTRL_R)
        time.sleep(0.8)
        recv_avail(sock, total=1.5)

        print(f"  poking {length} bytes into ${PROG_FROM:04X}-${PROG_TO:04X}…")
        t0 = time.time()
        poke_bytes(sock, PROG_FROM, bytes(blob))
        print(f"  poke done in {time.time() - t0:.1f} s")

        # Confirm at least the first few bytes round-trip through Wozmon.
        send_line(sock, f"{PROG_FROM:04X}.{PROG_FROM+7:04X}",
                  wait=0.2, read_t=3.0)

        # Drive ACI WRITE.
        print(f"  starting ACI WRITE ${PROG_FROM:04X}.${PROG_TO:04X}W…")
        send_line(sock, "C100R", wait=0.4, read_t=2.0)
        send_line(sock, f"{PROG_FROM:04X}.{PROG_TO:04X}W",
                  wait=0.3, read_t=1.0)

        # Wait for Wozmon prompt to come back. A ~2.4 KB write takes a
        # while even at --cpu-max (ACI writes are tens of seconds of
        # emulated time). Generous budget; quits early on prompt.
        t0 = time.time()
        got_prompt = wait_for_prompt(sock, deadline_s=180.0)
        dt = time.time() - t0
        if got_prompt:
            print(f"  ACI WRITE finished (prompt back in {dt:.1f} s)")
        else:
            print(f"  WARNING: no Wozmon prompt after {dt:.0f} s — "
                  "saving whatever we got")

        sock.close()
    finally:
        shutdown_pom1(proc, log)

    if not wav_path.exists():
        sys.exit(f"ERROR: --save-tape produced nothing at {wav_path}")
    sz = wav_path.stat().st_size
    print(f"  WAV captured: {wav_path} ({sz/1024:.1f} KB)")


# ---------------------------------------------------------------------------
# Phase 2 — transcode wav -> ogg
# ---------------------------------------------------------------------------

def phase_transcode(wav_path: Path, ogg_path: Path) -> None:
    print("\n=== Phase 2: transcode .wav -> .ogg (libvorbis) ===")
    if not shutil.which("ffmpeg"):
        sys.exit("ERROR: ffmpeg not in PATH — install it "
                 "(brew install ffmpeg) or keep the .wav")
    if ogg_path.exists():
        ogg_path.unlink()
    # Prefer libvorbis when present; fall back to ffmpeg's native `vorbis`
    # encoder (marked experimental → needs -strict -2). Either produces an
    # OGG-Vorbis file that miniaudio decodes natively.
    probe = subprocess.run(["ffmpeg", "-hide_banner", "-encoders"],
                           capture_output=True, text=True, check=True)
    if "libvorbis" in probe.stdout:
        codec_args = ["-c:a", "libvorbis", "-q:a", "4"]
    else:
        # ffmpeg's native vorbis encoder is experimental and stereo-only,
        # so upmix mono to 2 channels with -ac 2.
        codec_args = ["-ac", "2", "-c:a", "vorbis", "-strict", "-2",
                      "-q:a", "4"]
    cmd = ["ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
           "-i", str(wav_path), *codec_args, str(ogg_path)]
    if VERBOSE:
        print(f"  [ffmpeg] {' '.join(cmd)}")
    subprocess.run(cmd, check=True)
    sz = ogg_path.stat().st_size
    print(f"  OGG written: {ogg_path} ({sz/1024:.1f} KB)")


# ---------------------------------------------------------------------------
# Phase 3 — reload and run
# ---------------------------------------------------------------------------

def phase_play(ogg_path: Path, *, auto_run: bool = True) -> subprocess.Popen:
    print("\n=== Phase 3: reload .ogg and run 280R ===")
    log = repo_root() / "build" / "pom1_50th_play.log"
    proc = launch_pom1(["--tape", str(ogg_path)], log)
    sock = socket.create_connection((HOST, PORT), timeout=5)
    sock.settimeout(20)
    recv_avail(sock, total=1.2)

    # Hard reset so we're sitting at a clean Wozmon prompt. The cassette
    # deck stays armed (PLAY latched via --tape); the new ARMED banner is
    # visible in the UI until C100R pulls the first $C081 poll.
    send_ctrl(sock, CTRL_R)
    time.sleep(0.8)
    recv_avail(sock, total=1.5)

    # Clear target range so a failed load shows up as zeros.
    print(f"  clearing ${PROG_FROM:04X}-${PROG_TO:04X}…")
    poke_bytes(sock, PROG_FROM, bytes(PROG_TO - PROG_FROM + 1),
               chunk=32, wait=0.03)

    print(f"  starting ACI READ ${PROG_FROM:04X}.${PROG_TO:04X}R…")
    send_line(sock, "C100R", wait=0.4, read_t=2.0)
    send_line(sock, f"{PROG_FROM:04X}.{PROG_TO:04X}R",
              wait=0.3, read_t=1.0)

    t0 = time.time()
    got_prompt = wait_for_prompt(sock, deadline_s=180.0)
    dt = time.time() - t0
    if got_prompt:
        print(f"  ACI READ finished (prompt back in {dt:.1f} s)")
    else:
        print(f"  WARNING: no Wozmon prompt after {dt:.0f} s — "
              "try running manually")

    if auto_run:
        print("  launching with 280R…")
        send_line(sock, "280R", wait=0.2, read_t=1.0)
        print("  50th demo running — switch to the POM1 window to watch it.")

    sock.close()
    return proc  # caller keeps the emulator alive


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> int:
    global VERBOSE
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--keep-wav", action="store_true",
                    help="do not delete /tmp/50th.wav after transcode")
    ap.add_argument("--skip-run", action="store_true",
                    help="stop after Phase 3 ACI READ (do not send 280R)")
    args = ap.parse_args()
    VERBOSE = args.verbose

    print("=" * 62)
    print("POM1 50th-demo cassette round-trip via Terminal Card")
    print("=" * 62)

    wav = Path("/tmp/50th.wav")
    ogg = repo_root() / "cassettes" / "50TH.ogg"

    phase_capture(wav)
    phase_transcode(wav, ogg)
    proc = phase_play(ogg, auto_run=not args.skip_run)

    if not args.keep_wav and wav.exists():
        wav.unlink()

    print("\nDone. POM1 (PID {}) is still running — close its window or "
          "SIGTERM it to quit.".format(proc.pid))
    print(f"Tape: {ogg}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
