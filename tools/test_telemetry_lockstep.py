#!/usr/bin/env python3
# Manual test for the dev telemetry side channel's deterministic lock-step mode.
# See doc/TELEMETRY_SIDE_CHANNEL.md.
#
# Self-contained: assembles a tiny 6502 program that arms lock-step and emits one
# frame per loop (parking on the end-frame marker), launches POM1 with the program
# loaded + --telemetry-port, then verifies over TCP that:
#   1. the parked frame is flushed on connect,
#   2. NO further frame arrives without an ACK (the CPU is provably parked),
#   3. each ACK byte (0x06) releases exactly one more frame.
#
# Usage (from repo root, after building build/POM1):
#     python3 tools/test_telemetry_lockstep.py [path/to/POM1]
# Needs a display (POM1 is a GLFW/ImGui app); exits 77 (skip) if it can't boot.

import os, socket, subprocess, sys, tempfile, time

PORT = 6503
BIN  = sys.argv[1] if len(sys.argv) > 1 else os.path.join("build", "POM1")

# $0300: arm lock-step, then per-frame write $AA to TELE_DATA + end-frame marker
# (parks here until ACK), JMP loop.
PROG = bytes([
    0xA9, 0x02,        # LDA #$02
    0x8D, 0x41, 0xC4,  # STA $C441   ; kCtrlLockstepOn
    # loop = $0305
    0xA9, 0xAA,        # LDA #$AA
    0x8D, 0x40, 0xC4,  # STA $C440   ; TELE_DATA = $AA
    0xA9, 0x01,        # LDA #$01
    0x8D, 0x41, 0xC4,  # STA $C441   ; kCtrlEndFrame -> parks
    0x4C, 0x05, 0x03,  # JMP $0305
])

def read_frame(sock, timeout):
    sock.settimeout(timeout)
    buf = b''
    try:
        while len(buf) < 3:
            d = sock.recv(3 - len(buf))
            if not d: return 'EOF'
            buf += d
        if buf[0] != 0xAA:
            return ('BADSENTINEL', buf)
        ln = buf[1] | (buf[2] << 8)
        payload = b''
        while len(payload) < ln:
            d = sock.recv(ln - len(payload))
            if not d: return 'EOF'
            payload += d
        return payload
    except socket.timeout:
        return 'TIMEOUT'

def main():
    if not os.path.exists(BIN):
        print(f"SKIP: {BIN} not found (build first)"); return 77
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        f.write(PROG); prog_path = f.name

    proc = subprocess.Popen(
        [BIN, "--telemetry-port", str(PORT),
         "--load", f"0300:{prog_path}", "--run", "0300"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        # Give POM1 time to boot, plug the port, and run the program.
        sock = None
        for _ in range(50):
            time.sleep(0.2)
            if proc.poll() is not None:
                print("SKIP: POM1 exited early (no display?)"); return 77
            try:
                sock = socket.create_connection(("127.0.0.1", PORT), 1); break
            except OSError:
                continue
        if sock is None:
            print("FAIL: could not connect to telemetry port"); return 1

        f1 = read_frame(sock, 4.0)
        assert f1 == b'\xaa', f"frame1: {f1!r}"
        assert read_frame(sock, 0.8) == 'TIMEOUT', "expected park (no frame without ACK)"
        for n in (1, 2, 3):
            sock.sendall(b'\x06')
            fn = read_frame(sock, 2.0)
            assert fn == b'\xaa', f"frame after ACK #{n}: {fn!r}"
            assert read_frame(sock, 0.8) == 'TIMEOUT', f"expected re-park after ACK #{n}"
        sock.close()
        print("PASS: lock-step delivers exactly one frame per ACK; CPU parks between frames")
        return 0
    finally:
        proc.terminate()
        try: proc.wait(timeout=3)
        except subprocess.TimeoutExpired: proc.kill()
        os.unlink(prog_path)

if __name__ == "__main__":
    sys.exit(main())
