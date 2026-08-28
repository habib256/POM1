# POM1 architecture — the human entry point

**Read this first, once.** It gives you the five ideas the whole tree is built
on, the one dependency rule, and the place your change belongs. It is short on
purpose and it does not repeat itself.

- Invariants, gotchas and hard-won details → [`CLAUDE.md`](CLAUDE.md) (dense, exhaustive, the reference).
- Every other document → [`doc/README.md`](doc/README.md).
- Open work → [`TODO.md`](TODO.md). Shipped work → [`CHANGELOG.md`](CHANGELOG.md).

POM1 is ~85 000 lines of C++ under `src/` (vendored code in `src/third_party/`,
Dear ImGui cloned locally into `imgui/`), plus 118 CTest targets. It emulates an
Apple 1 with 16 optional expansion cards, and it carries a development
environment for that machine (a cc65 IDE, graphics editors, two BASIC compilers).
The emulator proper is under a third of the code — knowing that up front explains
most of what you will find.

## 1. The shape

```
  UI panels (ImGui)                     src/MainWindow_*.cpp, editors
        │  commands down, immutable views up
  EmulationController                   the thread boundary + the state lock
        │
  MachineCoordinator + CardTopology     which cards may coexist, and the plan to get there
        │
  Memory ── PeripheralBus ── 16 cards   the address space and its MMIO
        │
  M6502                                 the CPU
```

Nothing below a line knows about anything above it. That is the whole rule; §3
says how it is held.

## 2. The five concepts

**1 — `Memory` is the address space *and* the owner of every card.**
`src/Memory.{h,cpp}`. 64 KB of RAM, PIA aliasing at `$D0xx`, ROM write-protect,
plus a `unique_ptr` and an enable flag per card. It is the biggest object in the
tree and the one you will meet everywhere; §6 is honest about that.

**2 — `PeripheralBus` is how MMIO is dispatched.**
`src/PeripheralBus.h`. A card registers `(name, range, priority, onRead,
onWrite)`; a 256-bit page bitmap makes the hot path O(1) and a stable sort by
priority resolves overlapping windows. A card never patches `memRead`/`memWrite`
— it registers a range. Address windows are tabulated in `CLAUDE.md`.

**3 — Cards are identified by `CardId`, and their legal combinations are policy.**
`src/CardTypes.h` holds the stable enum and the allocation-free `CardSet`;
`src/CardTopology.h` is pure policy over it — active conflicts, whether a
candidate may be plugged, and `planConfiguration()`, which returns a
deterministic detach/configure/attach plan. This exists because on real P-LAB
hardware exactly one card is plugged at a time (Parmigiani's rule) and many card
windows genuinely overlap. `src/MachineCoordinator.h` executes the plan;
`src/EmulationController.h` owns the lock and publishes once. **No card logic
compares display-name strings.**

**4 — The UI never reads the machine directly; it reads a published snapshot.**
The CPU runs on its own thread. `src/SnapshotPublisher.h` is a single-producer
single-consumer slot with page-level dirty copying, so an idle machine copies
nothing. The UI renders that snapshot and sends commands back through
`EmulationController`. `src/RewindBuffer.h` stores the same blobs on a timeline.

**5 — Decisions are extracted as pure functions; that is where the tests live.**
No test binary links the UI, and none needs an emulated machine to check a
parser. So every decision that can be a value-in/value-out function is one:
`src/MemoryImageLoader.h` (hex dumps), `src/PcmFile.h` (WAV/AIFF),
`src/SnapshotIO.h` (validate before applying), `src/ResourceLocator.h` (one
search order for data files), `src/StagedCardConfiguration.h` (the UI's
card-staging transaction), `src/Apple1KeyMap.h`, `src/WindowGeometry.h`.
**When you add logic, ask whether it can live on this side of the seam. Usually
it can, and then it gets a test.**

## 3. The dependency rule, and what actually enforces it

Direction: `pom1_ui → pom1_app → pom1_devices → pom1_core`.

Be clear about the mechanism: those are CMake `INTERFACE` targets that propagate
compiler flags and include paths. **They own no sources and enforce no
boundary.** The real guard is `tools/check_architecture.py` (CTest
`architecture_check`), which fails on any *new* include from the core/device set
toward ImGui or `MainWindow`, and holds ceilings recorded in
`tools/architecture_baseline.json`.

That baseline is a **ratchet, not a target**: lower a number when a refactor
earns it, never raise one to make CI green. A deliberate new edge needs an
explicit `allowed_reverse_dependencies` entry — i.e. a decision, written down.

## 4. Threads and locks

Three threads: the UI/render thread, one CPU thread (under WASM, a pump on the
main thread instead), and the OS audio callback thread.

Lock order is **`stateMutex > rewindMutex > keyMutex > snapshotMutex`**, and it
is not merely documented: `src/LockOrder.h` gives each lock a rank and asserts
that a thread only ever takes one strictly inside what it already holds
(compiled out under `NDEBUG`, live in every test binary). `lock_order_smoke`
forks a child per inversion and reaps the `SIGABRT`.

Two rules with no automated guard, so they are on you:
- **The audio callback must not allocate and must not wait on slow work.** Scratch
  buffers are sized once; a decoder is built outside the lock and swapped in.
- **A bus handler that writes `mem[]` itself must first ask
  `Memory::isRomWriteProtected(addr)`** — the bus answers before `memWrite`'s own
  guard runs.

## 5. Where your change goes

| You are adding | Start at | Also |
|---|---|---|
| An expansion card | a `.cpp/.h` pair in `src/`, a `CardId` row in `src/CardTypes.h` | declare its ranges/conflicts there; register on `PeripheralBus`; honour Parmigiani's rule |
| A machine preset | `src/MachinePresets.cpp` (pure data, **must stay UI-free**) | the README preset table is checked against it |
| A UI panel | a `MainWindow_*.cpp` TU | put every *decision* behind the §2.5 seam, not in the draw call |
| A file format | a pure parser next to `src/MemoryImageLoader.h` | bounded reads via `src/FileBytes.h`; a smoke test **and** a fuzz target |
| A CLI flag | `src/CliDispatcher.cpp` + [`doc/CLI.md`](doc/CLI.md) | this TU must never include a `MainWindow` header — `cli_dispatcher_smoke` fails to link if it does |
| A test | `tests/` + `tests/CMakeLists.txt` | pattern: `<cassert>` + `add_test`, no framework |

Then: `cd build && cmake .. && make && ctest`. The suite is ~55 s.

## 6. What is deliberately not clean (yet)

Stated plainly so you do not mistake it for a design you should imitate:

- **`Memory` is a god object.** ~190 public methods, 16 owned cards, included by
  ~59 translation units. The card-topology layer was built *around* it rather
  than by decomposing it. Do not grow it; see [`TODO.md`](TODO.md).
- **`EmulationController` is a wide façade**, not a thin one — a few hundred
  public methods across four TUs. New work should go through the structured
  commands (`applyCardConfiguration`, `setCardEnabled`), not new per-card wrappers.
- **The UI has ~17 000 lines and no direct test coverage.** This is where the
  known historical defects lived. Hence §2.5.
- **The development environment is a second product** sharing this process,
  build and release. Isolating it is the current priority in [`TODO.md`](TODO.md).
