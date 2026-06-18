# Telemetry Side Channel — Design Note

> **Status: shipped (POM1 1.9.2).** This dev-only virtual peripheral is
> implemented (`src/TelemetryPort.{h,cpp}`), wired into the *DevBench → Telemetry
> Side Channel* panel and the Bench Serial Monitor, and driven by the
> `--telemetry-port` / `--telemetry-log` / `--headless` CLI flags. The reusable
> kit (`dev/lib/telemetry/telemetry.inc`, `tools/pom1_telemetry.py`,
> `dev/projects/a1_telemetry_demo/`) ships too. Sections still tagged *proposed*
> below are the original design rationale, kept for context; the ✅ markers flag
> what landed.

**Goal.** Give an external test harness a way to *observe* a running Apple-1
program's state and *drive* its input, so games — especially real-time action
games — can be regression-tested automatically instead of replayed by hand
after every source change. This is the "side channel" Uncle Bernie described as
the missing piece of a 6502 SDK: the game feeds state out, a tailored test
program reads it and decides which keys/joystick inputs to send back.

**Audience.** Bernie (whose hardware dev rig — a parallel-port keyboard emulator
on dying vintage notebooks — is down) and anyone building a cc65/ca65 →
emulator → automated-test loop on POM1.

**Hardware truth.** None. This is a *virtual* development aid, not buildable
P-LAB hardware — same category as the "Multiplexing Fantasy" presets. It must be
clearly labelled as such so it never gets mistaken for a real card (Parmigiani's
one-board rule still applies to everything that *is* real).

---

## 1. The shape in one page

```
   Game 6502                POM1 (C++)                       Test harness (external)
 ┌──────────┐   STA $TELE   ┌────────────────┐   TCP :6503   ┌──────────────────────┐
 │ player_x │──────────────▶│ TelemetryPort  │──────────────▶│  read frame packet    │
 │ enemies[]│  (MMIO write) │  • out FIFO     │               │  decide input         │
 │  state   │               │  • in  FIFO     │◀──────────────│  send keys / commands │
 └──────────┘◀──────────────│  • SocketHandle │   commands    │  assert invariants    │
        ▲       LDA $TELE+2 └────────────────┘               └──────────────────────┘
        │       (read cmd)          │
        └── key injection ──────────┘   (reuses KeyInjector — same path as TerminalCard)
```

The whole mechanism is the Terminal Card pattern generalised: a passive bridge
that already does `$D012 → TCP → inject $D010`. We add a dedicated *binary,
bidirectional, frame-delimited* channel so the game can emit structured state
instead of just screen characters.

---

## 2. The virtual device — `TelemetryPort`

A new `class TelemetryPort : public pom1::Peripheral`, modelled line-for-line on
`src/TerminalCard.{h,cpp}`. It owns:

- **An MMIO register window** registered on the `PeripheralBus` exactly like
  every other card (`Memory.cpp` ctor, cf. the A1-IO RTC handle at
  `Memory.cpp:142`):
  ```cpp
  telemetryBusHandle = bus.registerHandle(
      "Telemetry", {0xC440, 0xC443}, /*priority*/ 30,  // >GEN2 (0): owns its 4 bytes in GEN2's broad $C200-$C7FF window
      [this](uint16_t a){ return telemetry->readReg(a); },
      [this](uint16_t a, uint8_t v){ telemetry->writeReg(a, v); });
  bus.setEnabled(telemetryBusHandle, telemetryEnabled);
  ```
  Dispatch is the usual O(1) page-mask lookup; zero cost when unplugged.

- **A TCP server on localhost**, reusing `SocketHandle` (the move-only RAII
  wrapper already shared by `WiFiModem` + `TerminalCard`) and the
  `startServer / acceptClient / pollClient / disconnectClient` shape from
  `TerminalCard.cpp`. Default port **6503** (Terminal Card owns 6502).

- **A polling hook** in `Memory::advanceCycles()`, added next to
  `terminalCard->advanceCycles(cycles)` (`Memory.cpp:1742`):
  ```cpp
  if (telemetryEnabled) telemetry->advanceCycles(cycles);
  ```
  This is where the outbound FIFO is flushed to the socket and inbound bytes are
  pulled off it.

- **A key-injection callback** — re-expose `setKeyInjector(KeyInjector)`
  identical to `TerminalCard`, so the harness can press *real* keys
  (`$D010/$D011`) for games that read the keyboard normally.

- **Threading.** A `telemetryMutex` ranked **below** `stateMutex` (same tier as
  `cardMutex` / `chipMutex` in the documented order
  `stateMutex > keyMutex > snapshotMutex`). Any UI-thread action uses
  `std::atomic` pending flags, mirroring `TerminalCard::resetPending`.

- **Snapshot.** `serialize` / `deserialize` are **no-ops** — the FIFOs and the
  socket are transient I/O, like a network connection (cf. the open snapshot gap
  for WiFiModem/TerminalCard in `TODO.md`). Document it; don't try to freeze a
  live socket into a rewind frame.

---

## 3. Register interface (4 bytes)

**Window: `$C440–$C443`** (dev-only). This is the one sub-block of the Apple-1
expansion-I/O space that is collision-free across every card:

- **GEN2 is structurally blind to it.** GEN2's soft-switch decoder is
  `SEL = $Cxxx & !A11 & A9 & A4`, which requires A9=1; `$C4xx`/`$C5xx` have A9=0,
  so the card can *never* address them — even though its broad `$C200-$C7FF` bus
  handle spans the page (there it just falls through to RAM). This matters
  because Bernie's graphics games run *with* GEN2 plugged.
- **No other card touches `$C4xx`/`$C5xx`:** ACI is `$C0xx`/`$C1xx`, SID is
  `$C8xx+`, Juke-Box ends at `$BFFF`, the PIA aliases only `$Dxxx`.
- Register the handle at **priority > GEN2's (0)** so it owns its four bytes over
  GEN2's pass-through handler — the same priority mechanism TMS9918 uses to win
  over SID at `$CC00`. Enabled only by `--telemetry-port`.

Equates: `TELE = $C440` → `TELE_DATA = $C440`, `TELE_CTRL = $C441`,
`TELE_IN = $C442`, `TELE_INLEN = $C443`.

| Offset | Name | Access | Meaning |
|--------|------|--------|---------|
| `+0` | `TELE_DATA` | W | push one byte into the outbound FIFO → socket |
| `+1` | `TELE_CTRL` / `TELE_STAT` | W / R | **W:** `$01` = end-of-frame (delimit + flush), `$02` = arm lock-step, `$00` = disarm. **R:** bit7 = harness connected, bit0 = inbound byte available |
| `+2` | `TELE_IN` | R | pop one byte from the inbound FIFO (harness → game) |
| `+3` | `TELE_INLEN` | R | number of inbound bytes pending |

Game side — emit a state packet once per logical frame:

```asm
        LDA player_x
        STA TELE_DATA
        LDA player_y
        STA TELE_DATA
        LDA enemy_count
        STA TELE_DATA
        ; ... whatever the test needs to see ...
        LDA #$01            ; end-of-frame marker
        STA TELE_CTRL       ; POM1 delimits the packet and flushes it
```

Poll for a harness command (non-blocking):

```asm
        LDA TELE_STAT
        AND #$01
        BEQ no_cmd
        LDA TELE_IN         ; consume one command byte
        ; dispatch ...
no_cmd:
```

The **end-of-frame marker** is what answers Bernie's hard question ("how does
the test *see* moving objects and decide inputs at the right moment"): the game
publishes one structured packet per frame, and the harness reacts in lock-step
with the game's own notion of a frame — not with wallclock.

---

## 4. Wire protocol

Keep it dead simple and self-describing so a Python/C harness needs no POM1
headers:

- Outbound stream = a sequence of **frames**. A frame is the bytes written to
  `TELE_DATA` since the last `TELE_CTRL=$01`, wrapped as
  `0xAA <len:u16-le> <payload…>`. The `0xAA` sentinel + length lets the harness
  resync if it attaches mid-run.
- Inbound stream = raw command bytes; the game defines their meaning. The harness
  may also inject keystrokes out-of-band via the key-injection path (§5).
- The *layout of the payload* (which byte is `player_x`, etc.) is a contract
  between one game and its one test program — POM1 stays agnostic. Convention:
  ship the layout as a comment block / struct in the game's test file, **or**
  declare it on the wire with a schema frame (§4a) so the harness/UI can decode
  it by name.

---

## 4a. Self-describing schema frames (v1) ✅

The plain DATA frame (§4) is a bag of bytes whose meaning lives only in a
comment. A **schema frame** lets a game *declare its fields on the wire* once, so
the harness — and the POM1 Telemetry window — can decode every subsequent DATA
frame **by name** with no per-game code. The DATA frame format is **unchanged**;
the schema is purely additive.

Two frame kinds now share the channel, distinguished by their sentinel:

| Kind | On the wire | Closed by `TELE_CTRL` opcode | Payload |
|------|-------------|------------------------------|---------|
| **DATA** | `0xAA len_lo len_hi payload` | `0x01` (`TELE_END`) | field **values**, in schema order, each sized by its type |
| **SCHEMA** | `0xA5 len_lo len_hi payload` | `0x03` (`TELE_SCHEMA`) | a run of field **descriptors** |

A SCHEMA-frame payload is a sequence of **descriptors**, each:

```
[type : 1 byte] [field name : ASCII bytes] [0x00 terminator]
```

Field **type codes**:

| Code | Type | Size | Notes |
|------|------|------|-------|
| `1` | `U8`   | 1 | unsigned |
| `2` | `S8`   | 1 | signed |
| `3` | `U16`  | 2 | little-endian, unsigned |
| `4` | `S16`  | 2 | little-endian, signed |
| `5` | `BOOL` | 1 | `0` = false, else true |
| `6` | `CHAR` | 1 | ASCII |

**Semantics.** Consumers keep the **last** schema seen and decode subsequent
DATA frames field-by-field against it; with **no** schema seen they fall back to
raw bytes (so old games keep working). Emit the schema **once at startup**, then
one DATA frame per tick. Schema frames **never park lock-step** — they flush
immediately regardless of mode. The UI shows a **decoded named table** (field
name → value) once it has a schema.

It stays **fully generalizable**: any game declares its own fields. For example,
[`dev/projects/gen2_snake_telemetry`](../dev/projects/gen2_snake_telemetry)
(Snake on the GEN2 HGR card) declares exactly four fields —
`head_x:U8`, `head_y:U8`, `length:U8`, `alive:BOOL` — and runs free-run so it
plays live while the Telemetry window shows the decoded state.

**Emit it (C — `dev/lib/telemetry/telemetry.h`):**

```c
#include "telemetry.h"
tele_field(TELE_T_U8,   "head_x");      /* declare the schema, once */
tele_field(TELE_T_U8,   "head_y");
tele_field(TELE_T_U8,   "length");
tele_field(TELE_T_BOOL, "alive");
tele_schema_close();                     /* -> 0xA5 schema frame */
tele_freerun();                          /* live play, no lock-step */
/* per tick: */
tele_put(head_x); tele_put(head_y); tele_put(length); tele_put(alive);
tele_frame();                            /* -> 0xAA data frame */
```

**Emit it (asm — `dev/lib/telemetry/telemetry.inc`):**

```asm
.include "telemetry.inc"
        TELE_FIELD TELE_T_U8,   "head_x"
        TELE_FIELD TELE_T_U8,   "head_y"
        TELE_FIELD TELE_T_U8,   "length"
        TELE_FIELD TELE_T_BOOL, "alive"
        TELE_SCHEMA_FRAME                  ; -> 0xA5 schema frame
```

**Decode it (Python — `tools/pom1_telemetry.py`):** `read_frame()` transparently
records any 0xA5 schema frame on `client.schema` and keeps returning DATA
payloads; `decode(payload)` / `read_named()` turn a DATA payload into a
`{name: value}` dict using the last schema.

---

## 5. Deterministic lock-step mode — the real unlock ✅ implemented

Flaky action-game tests come from the test racing the game in real time.
Lock-step fixes it with a synchronous, **game-transparent** handshake on
`TELE_CTRL`:

- Arm with `TELE_CTRL=$02` (`kCtrlLockstepOn`). Each subsequent end-frame write
  (`TELE_CTRL=$01`) **parks the CPU at that instruction** until the harness sends
  one ACK byte (`0x06` = `kAckByte`, consumed — not delivered to `TELE_IN`).
- Mechanism (no game-side polling, no deadlock): the end-frame write sets the ACK
  gate; `Memory` immediately calls `M6502::stop()`, so `cpu->run()` returns **right
  after the `STA`** — cycle-exact, zero over-run. The slice loop
  (`EmulationController::runEmulationSlice`) then sees `isAwaitingAck()` and, in
  place of running the CPU, pumps the socket via `serviceStall()` **between slices
  with `stateMutex` released** — so the ACK can actually arrive and the UI stays
  responsive. On ACK the gate clears and the next slice resumes the CPU exactly
  where it stopped.
- Safety: a 5 s wall-clock timeout (`kTelemetryStallTimeoutSec`) auto-resumes +
  logs if the harness never ACKs, so a dead/missing harness can't wedge the
  emulator.
- Result: every frame replays identically regardless of host load, MAX speed, or
  a slow CI box — a real-time action test becomes a reproducible regression test.
  Verified end-to-end (exactly one frame per ACK; CPU provably parked between).

Without arming it, the channel is a fire-hose telemetry tap — enough for "did it
crash / did the score go backwards" smoke tests.

> **Why not just block inside `writeReg`?** It runs on the emulation thread while
> `cpu->run` holds `stateMutex`, and the socket poll that would receive the ACK
> *also* runs on that thread (inside `cpu->run` → `advanceCycles`). Blocking there
> could never see the ACK → guaranteed deadlock. Hence stop-then-park-between-slices.

---

## 6. Input injection back to the game

Two complementary mechanisms, both already proven by the Terminal Card:

1. **Inbound FIFO** (`TELE_IN`) — for game-defined commands the game explicitly
   polls. Best for "set difficulty 3", "spawn boss now", joystick deltas, etc.
2. **Real keystroke injection** — reuse the `KeyInjector` /
   `Memory::setKeyPressedRaw()` path so the harness can press keys at
   `$D010/$D011` for games that read the keyboard the normal way, with no game
   changes at all.

---

## 7. The SDK in practice — compile → load → test ✅ implemented

The CLI flags exist (`--telemetry-port`, `--telemetry-log`) plus `--headless`
(no display). The reusable **kit** on top of them:

- **6502 side** — `dev/lib/telemetry/telemetry.inc` (equates + `TELE_ARM` /
  `TELE_PUT*` / `TELE_FRAME` macros).
- **Harness side** — `tools/pom1_telemetry.py` (`TelemetryClient` +
  `launch_headless`).
- **Worked example** — `dev/projects/a1_telemetry_demo/` (a homing game) +
  `tools/test_telemetry_demo.py`.

Bernie's "dream SDK" loop, end to end — no display, no human:

```bash
make -C dev/projects/a1_telemetry_demo      # cc65 -> software/Telemetry/A1_TelemetryDemo.bin
python3 tools/test_telemetry_demo.py        # boots POM1 --headless, drives it, asserts
```

A test is a few lines — read the frame, decide, advance one lock-step frame:

```python
from pom1_telemetry import launch_headless
with launch_headless("software/Telemetry/A1_TelemetryDemo.bin",
                     load_addr=0x0280, port=6601) as tc:
    frame = tc.read_frame()                        # bytes: [player, target, won]
    while not frame[2]:
        move = b"\x01" if frame[0] < frame[1] else b"\x02"
        frame = tc.step(move)                      # send input + ACK + next frame
```

…and the matching game emits its state with the macros:

```asm
.include "telemetry.inc"
        TELE_ARM                  ; arm lock-step (once at startup)
loop:   TELE_PUT player          ; per frame: push the state bytes
        TELE_PUT target
        TELE_FRAME                ; flush + park until the harness ACKs
        lda TELE_IN               ; resume: read the harness's input byte
        ; ... apply input, loop ...
```

> **Note:** `--headless --preset N` applies the preset's machine config (RAM +
> cards + BASIC ROM) with no display — e.g. `--preset 11` plugs Uncle Bernie's
> GEN2 for HGR game tests (`tools/test_headless_preset.py` verifies it).

---

## 8. Reused vs new

| Reused as-is | New code |
|--------------|----------|
| `PeripheralBus::registerHandle` (MMIO dispatch) | `TelemetryPort.{h,cpp}` (FIFOs + register file) |
| `SocketHandle` + `TerminalCard` server pattern | FIFO ↔ socket forwarding + frame delimiting |
| `advanceCycles()` per-card hook (`Memory.cpp:1742`) | lock-step: `cpu->stop()` on end-frame + between-slice ACK park + timeout |
| `KeyInjector` / `setKeyPressedRaw` (key injection) | `--telemetry-*` flags in `CliDispatcher` |
| `Peripheral::serialize` (no-op here) | — |

---

## 9. Dependencies & open questions

- **Headless mode (out of scope here, needed for CI).** POM1 is a GLFW/ImGui
  app; truly unattended CI wants an offscreen run. Options: an offscreen flag
  reusing the WASM-style `pumpEmulationMainThread` loop without a window, or a
  virtual display. The side channel works *today in windowed mode*; headless is
  the complementary piece that unlocks "test on every commit." Related:
  `TODO.md` › *CI GitHub Actions*.
- **Relation to "Scriptable runtime IPC"** (`TODO.md`, `--cmd-fd`): that channel
  carries *control* verbs (load/run/step) and is host→emulator. This one carries
  *observation* (game→host) + synchronised input. They're complementary; the
  socket-poll plumbing can likely be shared.
- **Window address** — chosen: `$C440-$C443`, the GEN2 A9=0 blind zone (§3). If
  GEN2's broad `$C200-$C7FF` registration is ever tightened to its decoded pages,
  the page-mask overlap disappears entirely (optional cleanup, not required).
- **Joystick model** — POM1 has no analog paddle/joystick MMIO yet; for now the
  inbound FIFO + key injection cover digital input. A paddle channel is a
  separate feature if a game needs `$C064`-style analog reads.
