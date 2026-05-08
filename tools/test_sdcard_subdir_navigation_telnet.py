#!/usr/bin/env python3
"""
test_sdcard_subdir_navigation_telnet.py -- Pin the SD CARD OS
"commands only search the current directory" invariant that confused
a user typing `LOAD YUM` at `/PLAB>` while YUM actually lived in
`/PLAB/MCODE`.

Scenario ("CD before LOAD/DEL"):
  1. Boot with --preset 5 (P-LAB microSD + Applesoft), --terminal, --cpu-max.
  2. Drop a throwaway tagged file at sdcard/testdir/HELLO#040300 (harmless
     NOP bytes that load at $0300 without bricking anything).
  3. 8000R -> SD CARD OS prompt at /.
  4. LOAD HELLO at /                -> FILE NOT FOUND.
  5. DEL  HELLO at /                -> FILE NOT FOUND.
  6. CD testdir                     -> ok.
  7. LOAD HELLO in /testdir         -> no FILE NOT FOUND.
  8. DEL  HELLO in /testdir         -> no FILE NOT FOUND, host file gone.
  9. CD ..                          -> back to /.
 10. LOAD HELLO at / again          -> FILE NOT FOUND (doubles as a
     negative-control for the fuzzy matcher — once the file is deleted
     the root is genuinely empty, not just invisibly shadowed).

Audit -- the SD CARD OS commands that resolve names against the
CURRENT DIRECTORY ONLY, with no recursive search. Verified in
MicroSD.cpp on 2026-04-20:

  CMD_READ  (0)  / cmdRead   -- strict name match in currentDirectory.
  CMD_LOAD  (4)  / cmdRead   -- same path, fuzzy prefix match.
  CMD_WRITE (1)  / cmdWrite  -- writes into currentDirectory.
  CMD_DIR   (2)  / cmdDir    -- lists currentDirectory (or the arg).
  CMD_LS    (12) / cmdDir    -- same, short format.
  CMD_DEL   (11) / cmdDel    -- deletes from currentDirectory (fuzzy).
  CMD_MKDIR (14) / cmdMkdir  -- creates sub-dir inside currentDirectory.
  CMD_RMDIR (15) / cmdRmdir  -- removes sub-dir inside currentDirectory.

CMD_CD is the ONLY navigation primitive -- it accepts `..`, an
absolute `/PATH`, relative names, and a fuzzy leaf match. Always call
it before any of the eight file-ops above on a file/dir that lives
deeper in the tree.

Pre-requisites:
  - build/POM1 built (desktop, not WASM).
  - Nothing else listening on 127.0.0.1:6502.

The script launches and kills POM1 itself -- no manual setup.
Run from the repo root:

  python3 tools/test_sdcard_subdir_navigation_telnet.py [-v]
"""
from __future__ import annotations

import select
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

HOST = "127.0.0.1"
PORT = 6502
CTRL_R = 18
VERBOSE = "--verbose" in sys.argv or "-v" in sys.argv

REPO_ROOT = Path(__file__).resolve().parent.parent
SDCARD_DIR = REPO_ROOT / "sdcard"
TEST_SUBDIR = SDCARD_DIR / "testdir"
# NAME#TTAAAA -- type 04 (BIN), load address $0300. Four EA bytes = NOPs,
# completely inert if the LOAD command decides to transfer them to RAM.
TEST_FILE_NAME = "HELLO#040300"
TEST_FILE_PATH = TEST_SUBDIR / TEST_FILE_NAME
TEST_FILE_CONTENT = b"\xEA\xEA\xEA\xEA"


def vprint(label: str, data: str) -> None:
    if VERBOSE:
        print(f"    [{label}] {data[-500:]!r}")


def recv_avail(sock: socket.socket, total: float = 4.0, idle: float = 0.4) -> str:
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


def send_line(sock: socket.socket, cmd: str, wait: float = 0.3, read_t: float = 4.0) -> str:
    sock.sendall((cmd + "\r").encode("ascii"))
    time.sleep(wait)
    return recv_avail(sock, total=read_t, idle=0.4)


def send_ctrl(sock: socket.socket, byte: int) -> None:
    sock.sendall(bytes([byte & 0xFF]))


passed = 0
failed = 0


def check(name: str, output: str, expected: str) -> bool:
    global passed, failed
    vprint(name, output)
    if expected.upper() in output.upper():
        passed += 1
        print(f"  [PASS] {name}")
        return True
    failed += 1
    print(f"  [FAIL] {name} -- expected '{expected}'")
    print(f"         got: {output[-300:]!r}")
    return False


def check_not(name: str, output: str, unexpected: str) -> bool:
    global passed, failed
    vprint(name, output)
    if unexpected.upper() not in output.upper():
        passed += 1
        print(f"  [PASS] {name}")
        return True
    failed += 1
    print(f"  [FAIL] {name} -- unexpected '{unexpected}'")
    print(f"         got: {output[-300:]!r}")
    return False


def check_bool(name: str, condition: bool, detail: str = "") -> bool:
    global passed, failed
    if condition:
        passed += 1
        print(f"  [PASS] {name}")
        return True
    failed += 1
    print(f"  [FAIL] {name}{' -- ' + detail if detail else ''}")
    return False


def ensure_pom1_binary() -> Path:
    p = REPO_ROOT / "build" / "POM1"
    if not p.is_file():
        sys.exit(f"ERROR: {p} not found - build POM1 first (cd build && make)")
    return p


def launch_pom1(log_path: str):
    exe = ensure_pom1_binary()
    log = open(log_path, "w")
    proc = subprocess.Popen(
        [str(exe), "--preset", "5", "--terminal", "--cpu-max"],
        stdout=log, stderr=subprocess.STDOUT, start_new_session=True,
    )
    # Boot + applyMachineConfig + 15-frame card-enable defer + first CPU slice.
    time.sleep(3.0)
    if proc.poll() is not None:
        sys.exit(f"ERROR: POM1 exited early (code {proc.returncode}); see {log_path}")
    return proc, log


def teardown_pom1(proc, log) -> None:
    try:
        proc.send_signal(signal.SIGTERM)
        proc.wait(timeout=5)
    except Exception:
        proc.kill()
    log.close()


def setup_test_file() -> None:
    TEST_SUBDIR.mkdir(parents=True, exist_ok=True)
    TEST_FILE_PATH.write_bytes(TEST_FILE_CONTENT)


def cleanup_test_file() -> None:
    try:
        if TEST_FILE_PATH.exists():
            TEST_FILE_PATH.unlink()
        if TEST_SUBDIR.exists() and not any(TEST_SUBDIR.iterdir()):
            TEST_SUBDIR.rmdir()
    except Exception:
        pass


def main() -> int:
    print("=" * 60)
    print("SD CARD OS -- 'LOAD/DEL need CD first' invariant")
    print("=" * 60)

    cleanup_test_file()  # from a previous failed run
    setup_test_file()

    log_path = "/tmp/pom1_sdcard_subdir_test.log"
    proc, log = launch_pom1(log_path)

    try:
        try:
            sock = socket.create_connection((HOST, PORT), timeout=5)
        except (ConnectionRefusedError, OSError) as e:
            sys.exit(f"ERROR: cannot connect to {HOST}:{PORT}: {e}")
        sock.settimeout(10)

        recv_avail(sock, total=2.0)  # drain the Terminal Card banner
        send_ctrl(sock, CTRL_R)       # soft reset -> Woz Monitor
        time.sleep(0.9)
        recv_avail(sock, total=1.5)

        print("\nStep 1: launch SD CARD OS (8000R)")
        # The OS takes ~1-2 s to print its banner + initialize cwd to '/'.
        # If the next command arrives before init completes, the firmware
        # responds with a stale-cwd '?I/O ERROR' instead of 'FILE NOT FOUND'
        # and the prompt comes back as '>' instead of '/>'. Block until we
        # see '/>' (or 5 s timeout) so subsequent commands are deterministic.
        sock.sendall(b"8000R\r")
        deadline = time.time() + 5.0
        out = ""
        while time.time() < deadline and "/>" not in out:
            out += recv_avail(sock, total=0.5, idle=0.2)
        vprint("1.1 banner", out)

        print("\nStep 2: LOAD / DEL at / (file lives in /testdir/)")
        out = send_line(sock, "LOAD HELLO", wait=0.5, read_t=3.0)
        check("2.1 LOAD HELLO at / -> FILE NOT FOUND", out, "FILE NOT FOUND")

        out = send_line(sock, "DEL HELLO", wait=0.5, read_t=3.0)
        check("2.2 DEL HELLO at / -> FILE NOT FOUND", out, "FILE NOT FOUND")

        print("\nStep 3: CD testdir")
        out = send_line(sock, "CD TESTDIR", wait=0.5, read_t=3.0)
        check_not("3.1 CD TESTDIR -> no error", out, "NOT FOUND")

        print("\nStep 4: LOAD from /testdir (should succeed)")
        out = send_line(sock, "LOAD HELLO", wait=0.8, read_t=3.0)
        check_not("4.1 LOAD HELLO from /testdir -> no FILE NOT FOUND", out, "FILE NOT FOUND")

        print("\nStep 5: DEL from /testdir removes the host file")
        out = send_line(sock, "DEL HELLO", wait=0.5, read_t=3.0)
        check_not("5.1 DEL HELLO from /testdir -> no FILE NOT FOUND", out, "FILE NOT FOUND")
        time.sleep(0.3)
        check_bool("5.2 Host file gone from disk", not TEST_FILE_PATH.exists(),
                   detail=f"still present: {TEST_FILE_PATH}")

        print("\nStep 6: CD .. and re-verify from the empty root")
        out = send_line(sock, "CD ..", wait=0.5, read_t=3.0)
        out = send_line(sock, "LOAD HELLO", wait=0.5, read_t=3.0)
        check("6.1 LOAD HELLO at / -> FILE NOT FOUND (post-delete)", out, "FILE NOT FOUND")

        sock.close()

    finally:
        teardown_pom1(proc, log)
        cleanup_test_file()

    print("\n" + "=" * 60)
    print(f"Results: {passed} passed, {failed} failed")
    print("=" * 60)
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
