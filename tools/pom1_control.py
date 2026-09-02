#!/usr/bin/env python3
"""pom1_control -- drive a headless POM1 over the scripting control channel.

WHY THIS EXISTS
    The seven `tools/test_*_telnet.py` harnesses were good tests that never
    ran. Each one hardcoded the Terminal Card's port 6502 (so it fought any
    POM1 the developer had open, and every sibling under a parallel ctest),
    none passed --headless (so none could run on a display-less CI box), three
    of them assumed a POM1 the operator had already launched by hand, and
    between them they carried forty `time.sleep()` calls standing in for "has
    the machine got there yet?".

    This module replaces all four of those with the control channel POM1 grew
    for the purpose (`--cmd-port`, src/CommandPort.h):

      * the port is picked free at launch, so nothing collides;
      * --headless is the default here, not an afterthought;
      * the harness launches and reaps its own emulator;
      * `expect()` blocks on the emulated display until the text appears,
        which is what the sleeps were approximating.

USAGE
    from pom1_control import Pom1, Checks, skip

    with Pom1(preset=8, enable=["iec"]) as m:
        m.reset()
        m.type_line("8000R")
        m.expect("/>")

CONVENTIONS
    Addresses are hex, with or without `$`. `expect` matching is
    case-insensitive (the Apple-1 answers in upper case). Exit code 77 is
    ctest's "skipped" -- use skip() when a fixture or the binary is missing,
    never to paper over a failure.
"""
from __future__ import annotations

import os
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path
from typing import Iterable, Sequence

REPO_ROOT = Path(__file__).resolve().parent.parent
SKIP_EXIT = 77


class Pom1Error(RuntimeError):
    """A control-channel verb answered ERR, or the channel went away."""


def skip(reason: str) -> "NoReturn":  # type: ignore[name-defined]
    """Leave with ctest's skip code. For a missing prerequisite only."""
    print(f"SKIP: {reason}")
    sys.exit(SKIP_EXIT)


def pom1_binary() -> Path:
    """The built emulator, or a skip.

    Resolution order: `--pom1 PATH` on the harness's own command line (what
    ctest passes, as `$<TARGET_FILE:pom1_imgui>`, so an out-of-tree or
    multi-config build works), then $POM1_BINARY, then the usual build/ layouts.
    """
    argv_path = None
    if "--pom1" in sys.argv:
        i = sys.argv.index("--pom1")
        if i + 1 < len(sys.argv):
            argv_path = Path(sys.argv[i + 1])
    env = os.environ.get("POM1_BINARY")
    candidates = [p for p in (argv_path, Path(env) if env else None) if p]
    candidates += [
        REPO_ROOT / "build" / "POM1",
        REPO_ROOT / "build" / "POM1.app" / "Contents" / "MacOS" / "POM1",
        REPO_ROOT / "build" / "POM1.exe",
    ]
    for c in candidates:
        if c.is_file() and os.access(c, os.X_OK):
            return c
    skip("build/POM1 not found — build the emulator first (cd build && make)")


def _free_port() -> int:
    """A port nothing holds right now.

    Closed before POM1 binds it, so a foreign process could in principle take
    it in between. That window is microseconds and the alternative — letting
    POM1 pick and report one back — would need a second channel to report it
    on. A collision surfaces as a clean "cannot bind" failure at launch, not as
    a mysterious hang, which is the property that matters.
    """
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


def _unescape(payload: str) -> str:
    out, i = [], 0
    while i < len(payload):
        c = payload[i]
        if c != "\\" or i + 1 >= len(payload):
            out.append(c)
            i += 1
            continue
        n = payload[i + 1]
        if n == "r":    out.append("\r"); i += 2
        elif n == "n":  out.append("\n"); i += 2
        elif n == "t":  out.append("\t"); i += 2
        elif n == "\\": out.append("\\"); i += 2
        elif n == "x" and i + 3 < len(payload):
            try:
                out.append(chr(int(payload[i + 2:i + 4], 16))); i += 4
            except ValueError:
                out.append(c); i += 1
        else:
            out.append(c); i += 1
    return "".join(out)


def _escape(text: str) -> str:
    out = []
    for ch in text:
        if ch == "\\":   out.append("\\\\")
        elif ch == "\r": out.append("\\r")
        elif ch == "\n": out.append("\\n")
        elif ch == "\t": out.append("\\t")
        elif ord(ch) < 0x20 or ord(ch) == 0x7F:
            out.append("\\x%02X" % ord(ch))
        else:
            out.append(ch)
    return "".join(out)


class Pom1:
    """A headless POM1 process plus its control connection."""

    def __init__(
        self,
        *,
        preset: int | None = None,
        enable: Sequence[str] = (),
        disable: Sequence[str] = (),
        extra_args: Sequence[str] = (),
        cpu_max: bool = True,
        log_path: str | os.PathLike | None = None,
        boot_timeout: float = 30.0,
        verbose: bool = False,
    ) -> None:
        self.exe = pom1_binary()
        self.port = _free_port()
        self.verbose = verbose
        self.log_path = Path(log_path) if log_path else (
            REPO_ROOT / "build" / f"pom1_control_{os.getpid()}_{self.port}.log")
        self.log_path.parent.mkdir(parents=True, exist_ok=True)

        argv = [str(self.exe), "--headless", "--cmd-port", str(self.port)]
        if preset is not None:
            argv += ["--preset", str(preset)]
        if enable:
            argv += ["--enable", ",".join(enable)]
        if disable:
            argv += ["--disable", ",".join(disable)]
        if cpu_max:
            argv.append("--cpu-max")
        argv += list(extra_args)
        self.argv = argv

        self._log = open(self.log_path, "w")
        self.proc = subprocess.Popen(
            argv, stdout=self._log, stderr=subprocess.STDOUT,
            cwd=str(REPO_ROOT), start_new_session=True)
        self._sock: socket.socket | None = None
        self._file = None
        try:
            self._connect(boot_timeout)
        except Exception:
            self.close()
            raise

    # -- lifecycle ---------------------------------------------------------

    def _connect(self, timeout: float) -> None:
        """Wait for the control channel.

        POM1 opens it only after the preset, the card overrides and the
        deferred verbs have all been applied, so a successful connect IS the
        readiness handshake. No sleep, and no guessing.
        """
        deadline = time.time() + timeout
        last = None
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise Pom1Error(
                    f"POM1 exited with code {self.proc.returncode} during boot\n"
                    f"argv: {' '.join(self.argv)}\n{self._log_tail()}")
            try:
                self._sock = socket.create_connection(("127.0.0.1", self.port), timeout=2)
                self._sock.settimeout(120)
                self._file = self._sock.makefile("rwb")
                return
            except OSError as e:
                last = e
                time.sleep(0.05)
        raise Pom1Error(
            f"control channel never opened on 127.0.0.1:{self.port} ({last})\n"
            f"argv: {' '.join(self.argv)}\n{self._log_tail()}")

    def _log_tail(self, lines: int = 25) -> str:
        try:
            self._log.flush()
            text = self.log_path.read_text(errors="replace").splitlines()
            return "--- POM1 log tail ---\n" + "\n".join(text[-lines:])
        except OSError:
            return ""

    def close(self) -> None:
        if self._file is not None:
            try:
                self._file.close()
            except OSError:
                pass
            self._file = None
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None
        if self.proc.poll() is None:
            try:
                self.proc.send_signal(signal.SIGTERM)
                self.proc.wait(timeout=5)
            except Exception:
                self.proc.kill()
                try:
                    self.proc.wait(timeout=5)
                except Exception:
                    pass
        try:
            self._log.close()
        except Exception:
            pass

    def __enter__(self) -> "Pom1":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    # -- the channel -------------------------------------------------------

    def try_cmd(self, line: str) -> tuple[bool, str]:
        """Send one request; return (ok, payload). Never raises on ERR."""
        if self._file is None:
            raise Pom1Error("control channel is closed")
        if self.verbose:
            print(f"    > {line}")
        self._file.write((line + "\n").encode("latin-1"))
        self._file.flush()
        raw = self._file.readline()
        if not raw:
            raise Pom1Error(
                f"control channel closed while answering '{line}'\n{self._log_tail()}")
        reply = raw.decode("latin-1").rstrip("\r\n")
        if self.verbose:
            print(f"    < {reply[:200]}")
        if reply.startswith("OK"):
            return True, reply[3:] if len(reply) > 2 else ""
        if reply.startswith("ERR"):
            return False, reply[4:] if len(reply) > 3 else ""
        raise Pom1Error(f"malformed reply to '{line}': {reply!r}")

    def cmd(self, line: str) -> str:
        ok, payload = self.try_cmd(line)
        if not ok:
            raise Pom1Error(f"'{line}' -> ERR {payload}")
        return payload

    # -- verbs -------------------------------------------------------------

    def ping(self) -> str:
        return self.cmd("ping")

    def status(self) -> dict[str, str]:
        return dict(
            kv.split("=", 1) for kv in self.cmd("status").split() if "=" in kv)

    def pc(self) -> int:
        return int(self.status()["pc"].lstrip("$"), 16)

    def key(self, text: str) -> int:
        """Type raw text. Use \\r for RETURN; nothing is appended."""
        return int(self.cmd("key " + _escape(text)))

    def type_line(self, text: str) -> int:
        return self.key(text + "\r")

    def screen(self) -> str:
        """Display output since the last screen_clear()/expect() match."""
        return _unescape(self.cmd("screen"))

    def screen_clear(self) -> None:
        self.cmd("screen-clear")

    def expect(self, text: str, timeout_ms: int = 5000) -> str:
        """Block until `text` appears on the emulated display; return the text
        consumed up to and including the match. Raises Pom1Error on timeout."""
        ok, payload = self.try_cmd(f"expect {timeout_ms} {_escape(text)}")
        if not ok:
            raise Pom1Error(f"expect({text!r}) failed: {_unescape(payload)}")
        return _unescape(payload)

    def try_expect(self, text: str, timeout_ms: int = 5000) -> str | None:
        """expect() that returns None on timeout instead of raising."""
        ok, payload = self.try_cmd(f"expect {timeout_ms} {_escape(text)}")
        return _unescape(payload) if ok else None

    def reset(self) -> None:
        """Warm reset to the Woz Monitor (the red RESET key)."""
        self.cmd("reset")

    def hard_reset(self) -> None:
        self.cmd("hardreset")

    def peek(self, addr: int | str, length: int = 1) -> bytes:
        a = addr if isinstance(addr, str) else f"{addr:04X}"
        return bytes(int(b, 16) for b in self.cmd(f"peek {a} {length}").split())

    def poke(self, addr: int | str, data: Iterable[int]) -> int:
        a = addr if isinstance(addr, str) else f"{addr:04X}"
        blob = " ".join(f"{b:02X}" for b in data)
        return int(self.cmd(f"poke {a} {blob}"))

    def load(self, addr: int | str, path: str | os.PathLike) -> int:
        a = addr if isinstance(addr, str) else f"{addr:04X}"
        return int(self.cmd(f"load {a} {path}"))

    def run(self, addr: int | str) -> None:
        a = addr if isinstance(addr, str) else f"{addr:04X}"
        self.cmd(f"run {a}")

    def cycles(self, n: int) -> None:
        self.cmd(f"cycles {n}")

    # -- convenience -------------------------------------------------------

    def monitor(self, settle_ms: int = 4000) -> None:
        """Reset and wait for the Woz Monitor's `\\` prompt."""
        self.screen_clear()
        self.reset()
        self.expect("\\", timeout_ms=settle_ms)

    def command(self, line: str, expect: str | None = None,
                timeout_ms: int = 5000) -> str:
        """Type a line, then wait for `expect` (default: nothing). Returns the
        display text produced. The screen mark makes this sequential — each
        call reads only what its own command produced."""
        self.type_line(line)
        if expect is None:
            time.sleep(0.2)
            return self.screen()
        return self.expect(expect, timeout_ms=timeout_ms)


class Checks:
    """The pass/fail bookkeeping all seven harnesses had a private copy of."""

    def __init__(self, title: str = "") -> None:
        self.passed = 0
        self.failed = 0
        if title:
            print("=" * 62)
            print(title)
            print("=" * 62)

    def ok(self, name: str, condition: bool, detail: str = "") -> bool:
        if condition:
            self.passed += 1
            print(f"  [PASS] {name}")
            return True
        self.failed += 1
        print(f"  [FAIL] {name}")
        if detail:
            print(f"         {detail[-400:]}")
        return False

    def contains(self, name: str, haystack: str, needle: str) -> bool:
        return self.ok(name, needle.upper() in haystack.upper(),
                       f"expected {needle!r} in: {haystack[-300:]!r}")

    def excludes(self, name: str, haystack: str, needle: str) -> bool:
        return self.ok(name, needle.upper() not in haystack.upper(),
                       f"unexpected {needle!r} in: {haystack[-300:]!r}")

    def summary(self) -> int:
        print("-" * 62)
        print(f"Results: {self.passed} passed, {self.failed} failed")
        return 0 if self.failed == 0 else 1
