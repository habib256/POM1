# Scripting Control Channel — `--cmd-port`

> **Status: shipped.** Implementation `src/CommandPort.{h,cpp}`, CLI flag
> `--cmd-port N` ([`CLI.md`](CLI.md)), Python client + launcher
> [`tools/pom1_control.py`](../tools/pom1_control.py), pinned by ctest
> `command_port_smoke` and by the seven card harnesses it exists to run.

**Dev-only.** Off unless asked, `--headless` only, bound to the loopback
interface exclusively, and carrying no version negotiation. Same category as
the telemetry side channel ([`TELEMETRY_SIDE_CHANNEL.md`](TELEMETRY_SIDE_CHANNEL.md)):
test plumbing, not a public API, and not real hardware.

---

## 1. Why it exists

POM1 had exactly one way to be steered while it ran: the Terminal Card's telnet
socket on `127.0.0.1:6502`. That socket carries **two** things down one wire —
the keystrokes an external harness types, and the `$D012` display echo it must
parse to learn what happened. Everything awkward about driving POM1 from
outside followed from that:

| symptom | cause |
|---|---|
| `time.sleep(3)` after launch | nothing says "the machine is ready" |
| `time.sleep(0.3)` after every command | nothing says "that command finished" |
| a hardcoded port 6502 | one card, one port, no way to ask for another |
| tests that fight a running POM1 | same |
| memory checked by typing Monitor pokes and parsing the dump | no other way in |

The measurable consequence: **2 977 lines of working Python test code sat
outside `ctest`** across seven harnesses — the SD CARD OS command set, the IEC
bus, the Juke-Box Program Manager, CFFA1, LOGO and the ACI tape round-trip.
Between them they carried forty `time.sleep()` calls, none passed `--headless`,
and three expected the operator to have launched POM1 by hand.

All seven now run in `ctest -L emulator`, in 48 seconds, asserting 198 things.

## 2. The protocol in one paragraph

A **request** is one line: a verb, then space-separated arguments. A **reply**
is one line: `OK` optionally followed by a payload, or `ERR <message>`.
Payloads escape `\` `\r` `\n` and other control bytes (`\xHH`), so a reply is
*always* exactly one line and a client needs no framing rule beyond
`readline()`. Addresses are hex, with or without `$` / `0x`. `expect` matching
is case-insensitive — the Apple-1 answers in upper case.

```
$ POM1 --headless --preset 8 --cmd-port 6510 --cpu-max &
$ nc 127.0.0.1 6510
ping
OK pong
status
OK pc=$FF2C a=$00 x=$00 y=$00 sp=$FF p=$63 running=1 ram=64
peek FF00 8
OK D8 58 A0 7F 8C 12 D0 A9
key 8000R\r
OK 6
expect 8000 />
OK \r*** SD CARD OS 1.3\r/>
quit
OK bye
```

## 3. The verbs

| verb | reply | notes |
|---|---|---|
| `ping` | `OK pong` | |
| `status` | `OK pc=$… a=$… x=$… y=$… sp=$… p=$… running=0\|1 ram=N` | one snapshot |
| `help` | `OK <verb list>` | |
| `quit` | `OK bye` | answered before the socket closes, then the process exits 0 |
| `key <text>` | `OK <n>` | **literal** — see §5 |
| `screen` | `OK <text since the mark>` | does **not** consume |
| `screen-clear` | `OK` | moves the mark to the end |
| `expect <ms> <text>` | `OK <text consumed>` / `ERR timeout …` | blocks; consumes through the match |
| `reset` | `OK` | warm reset to the Monitor (the red RESET key) |
| `hardreset` | `OK` | power cycle, RAM cleared, no boot animation |
| `start` / `stop` | `OK` | run / pause the CPU |
| `step [n]` | `OK pc=$…` | stops the CPU first, then steps; leaves it stopped |
| `cycles <n>` | `OK pc=$…` | runs exactly n cycles synchronously, then restores the run state |
| `break <addr>` | `OK` | arms the single PC-matched halt |
| `peek <addr> [len]` | `OK HH HH …` | len 1..256 |
| `poke <addr> <hex>…` | `OK <n>` | one locked batch |
| `load <addr> <path>` | `OK <bytes>` | extension-routed exactly like `--load` |
| `run <addr>` | `OK` | |
| `snapshot-save/-load <path>` | `OK` | |
| `tape <path>` | `OK` | inserts a tape and does **not** press PLAY — see §5b |
| `tape-play` / `tape-stop` | `OK` | the deck's transport |
| `tape-rewind` / `tape-eject` | `OK` | |

Every refusal is an `ERR` with a reason. Nothing clamps silently: `peek 0300
257` is refused rather than truncated, because a short read reported as success
is how a harness ends up asserting against the wrong bytes.

## 4. `expect` and the mark

`screen` and `expect` read the emulated display — every byte written to
`$D012`, bit 7 already stripped. The channel keeps a **mark**: `expect`
searches from it and, on a match, advances past what it consumed. Successive
expects therefore read like an expect script rather than re-matching stale
output, and `screen` shows only what the current command produced.

Two rules a harness has to know:

* **Anchor on what FOLLOWS the answer, not on the answer.** `expect("DEVICE")`
  against `DEVICE: 8` consumes through `DEVICE` and leaves `: 8` unread — and
  leaves it in front of the next command's expect. Anchoring on the firmware's
  prompt captures the whole reply and lands the mark at a known place.
* **A prompt character is not always a prompt.** The SD CARD OS prompt ends in
  `>`, and so does every `<DIR>` line of a listing. Where a prompt is only
  identifiable by position, wait for the output to go quiet *and* end in it —
  `tools/test_sdcard_os_telnet.py` shows the pattern.

A failed `expect` consumes nothing and reports the unmatched tail, so a timeout
says what the machine was actually printing.

## 5b. `tape` loads and does NOT play — on purpose

There are two ways a tape reaches the deck and they differ in one button. The
CLI's `--tape` presses PLAY; the GUI's *File ▸ Load Tape* does not — it loads,
says *"Tape loaded"*, opens the deck and stops. That difference is invisible
until a program needs pulses: `C500R` then `RX RX` on Uncle Bernie's Extended
ACI spins in the ACI ROM at `$C1xx` forever, printing nothing, because the tape
is in but not rolling.

`tape <path>` reproduces the GUI's state, which is the only reason it exists —
a verb that quietly pressed PLAY could not express the bug. `status` reports
both halves (`tape=in|out`, `play=0|1`) because neither is visible from RAM.
`tools/test_aci_telnet.py` scenario D pins the failure and the one-button fix.

## 5. `key` is literal, unlike `--paste`

`--paste` keeps only CR and printable ASCII 32-126, which is right for pasting
a text file and wrong here: ESC (`$1B`) dismisses the CFFA1 paginator, Ctrl-R
(`$12`) is the Terminal Card's reset, and a harness that cannot send them is
pushed back onto the telnet wire this channel replaces. `key` queues every byte
after unescaping, folding `\n` to CR because no Apple-1 program has ever wanted
a line feed. Cap: 4096 bytes per request.

## 6. Threading, and what it deliberately is not

One thread owns the listener and the single connected client, and calls into
`EmulationController` exactly as the UI thread would. It takes no lock of its
own, so it adds no edge to the rank order in [`src/LockOrder.h`](../src/LockOrder.h):
every call it makes acquires `stateMutex` at the top, which is the outermost
rank. `expect` polls rather than holding anything across the wait — the
emulation thread is what makes the text appear, and blocking it would guarantee
the timeout.

**It is not a new `EmulationController` method.** `controller_public_methods` is
a frozen ratchet ([`../CLAUDE.md`](../CLAUDE.md)) and it did not move: every verb
above is reachable through facade API that already had another caller, and the
file-shaped ones reuse the CLI's own vocabulary. What the channel cost the
architecture is two ceilings, both named in CLAUDE.md: one new source outside
`pom1_test_devices`, and two new consumers of `EmulationController.h` (the
channel and its test).

## 7. Client

[`tools/pom1_control.py`](../tools/pom1_control.py) launches a headless POM1 on
a free port, waits for the channel — **the connection succeeding IS the
readiness handshake**, because the channel is opened last, after the preset,
the card overrides and the deferred verbs — and exposes the verbs as methods
plus a `Checks` helper for pass/fail bookkeeping.

```python
from pom1_control import Pom1, Checks

c = Checks("my card")
with Pom1(preset=8, enable=["iec"]) as m:
    m.monitor()                                   # reset, wait for the Monitor
    out = m.command("8000R", expect="/>")         # type, then wait
    c.contains("firmware came up", out, "SD CARD OS")
    m.poke(0x0300, [0xA9, 0xC1])
    assert m.peek(0x0300, 2) == bytes([0xA9, 0xC1])
raise SystemExit(c.summary())
```

Exit code 77 is ctest's *skipped* — use `skip()` for a missing fixture, never
to paper over a failure.
